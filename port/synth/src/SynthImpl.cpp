/* TrackerXXL open polysynth layer: polysynth::Synth, Instrument, Mixer, PatchStore, PatchPool and the instrument type
 * registry -- the classes Polyend ships in the proprietary libPolySynth.a, written against the SDK headers
 * (synth/include/PolySynth, MIT, see LICENSE Part 1). libPolySynth.a / libPolyDSP.a are never linked.
 *
 * Not provided (the Tracker does not use them): Grid, GridMapper(Store), SampleStore, Sequencer (createX returns null),
 * MIDI sinks, the effect instruments (kDelay / kReverb), the MIDI-out instrument. */
#include "PolySynth/Instrument.h"
#include "PolySynth/Mixer.h"
#include "PolySynth/PatchPool.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/Synth.h"
#include "PolySynth/FileSystem.h"
#include "PolySynth/Grid.h"
#include "PolySynth/GridMapper.h"
#include "PolySynth/GridMapperStore.h"
#include "PolySynth/SampleStore.h"
#include "PolySynth/MidiSink.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include "PolySynth/sdk/InstrumentVoice.h"

#include "PatchImpl.h"
#include "xxl_hub.h"
#include "xxl_types.h"

#include "interruptsControl.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>

namespace polysynth
{
/* ---------------------------------------------------------------- registry */
std::shared_ptr<const InstrumentType> InstrumentTypeRegistry::getInstrumentType(int typeId) const
{
    auto it = instrumentTypes.find(typeId);
    return it == instrumentTypes.end() ? nullptr : it->second;
}

void InstrumentTypeRegistry::addInstrumentType(int typeId, std::unique_ptr<InstrumentType> instrumentType)
{
    if (instrumentType)
    {
        instrumentTypes[typeId] = std::shared_ptr<InstrumentType>(std::move(instrumentType));
    }
}
} // namespace polysynth

namespace xxl
{
using namespace polysynth;

class SynthImpl;

/* ---------------------------------------------------------------- instrument */
class XInstrument final : public Instrument, public std::enable_shared_from_this<XInstrument>
{
  public:
    XInstrument(int instrumentId, SynthImpl& owner);
    ~XInstrument() override;

    void assignPatch(std::shared_ptr<Patch> newPatch) override;
    void connect(MidiSource& midiSource) override;
    void setMidiChannel(int channel) override { midiChannel = channel; }
    [[nodiscard]] int getMidiChannel() const override { return midiChannel; }
    void setLocalMode(LocalMode mode) override { localMode = mode; }
    [[nodiscard]] LocalMode getLocalMode() const override { return localMode; }
    [[nodiscard]] int getVoiceCount() const override { return voiceCount; }
    void setVoiceCount(int count) override;

    [[nodiscard]] int id() const { return instrumentId; }
    void render(AudioBufferStereoRef main, float tempo);

  private:
    class Listener final : public Patch::ParamChangeListener
    {
      public:
        explicit Listener(XInstrument& i)
          : owner(i)
        {
        }
        void onParamChange(int paramId,
                           Patch::optional_paramvalue_t newValue,
                           Patch::ParamChangeSource,
                           tiny_optional<int> voiceId) override
        {
            owner.paramChanged(paramId, newValue, voiceId);
        }
        [[nodiscard]] int getVoiceCount() const override { return owner.voiceCount; }

      private:
        XInstrument& owner;
    };

    int instrumentId;
    SynthImpl& synthOwner;
    int midiChannel = 0;
    LocalMode localMode = LocalMode::kOn;
    int voiceCount = 0;
    std::shared_ptr<PatchImpl> patch;
    std::vector<std::unique_ptr<InstrumentVoice>> voices;
    std::array<uint32_t, kMaxVoicesPerPatch> started = {};
    uint32_t serial = 0;
    std::shared_ptr<Listener> listener;
    std::shared_ptr<MidiSource::MidiEventCallback> callback;
    float tempo = 120.f;

    void rebuildVoices();
    void pushAllParams(InstrumentVoice& voice, int index);
    void paramChanged(int paramId, Patch::optional_paramvalue_t value, tiny_optional<int> voiceId);
    void onMidi(const MidiEvent& e);
    int pickVoice(int note);
    void allNotesOff();
    [[nodiscard]] InstrumentVoice::InstrumentVoiceContext context() const { return { tempo }; }
};

/* ---------------------------------------------------------------- synth */
class PatchStoreImpl;

class SynthImpl final : public Synth
{
  public:
    explicit SynthImpl(Allocator alloc);

    std::unique_ptr<PatchStore> createPatchStore(FileSystemId id, bool instrumentTypeFolderStructure) override;
    std::unique_ptr<PatchPool> createPatchPool(
      std::shared_ptr<PatchStore> store,
      int slotCount,
      std::function<tiny::optional<int>(const std::string&)> nameToIndex) override;
    std::shared_ptr<Instrument> createInstrument(int instrumentId) override;
    std::unique_ptr<Mixer> createMixer(int channels, int sends) override;
    std::unique_ptr<Grid> createGrid() override { return nullptr; }
    std::unique_ptr<GridMapperStore> createGridMapperStore(FileSystemId) override { return nullptr; }
    std::unique_ptr<GridMapper> createGridMapper() override { return nullptr; }
    std::unique_ptr<SampleStore> createSampleStore(FileSystemId) override { return nullptr; }
    std::shared_ptr<Sequencer> createSequencer() override { return nullptr; }

    void addMidiSource(const std::string&, std::shared_ptr<MidiSource>) override {}
    void addMidiSink(const std::string&, std::unique_ptr<MidiSink>) override {}
    void addFileSystem(FileSystemId id, std::unique_ptr<FileSystem> fileSystem) override
    {
        fileSystems[id] = std::move(fileSystem);
    }
    void initSamplePool(size_t, std::shared_ptr<SampleStore>) override {}

    std::string getInstrumentName(int instrumentTypeId) override
    {
        auto t = registry.getInstrumentType(instrumentTypeId);
        return t ? t->name : std::string("?");
    }
    std::vector<std::string_view> getInstrumentTypeNames() override
    {
        std::vector<std::string_view> r;
        for (const auto& kv : registry.getInstrumentTypes())
        {
            r.emplace_back(kv.second->name);
        }
        return r;
    }
    std::vector<int> getInstrumentTypeIds() override
    {
        std::vector<int> r;
        for (const auto& kv : registry.getInstrumentTypes())
        {
            r.push_back(kv.first);
        }
        std::sort(r.begin(), r.end());
        return r;
    }

    void setup(std::span<std::shared_ptr<Instrument>> newInstruments,
               std::span<std::shared_ptr<Instrument>> newEffects,
               std::shared_ptr<Mixer> newMixer,
               TempoProvider provider) override
    {
        instruments.assign(newInstruments.begin(), newInstruments.end());
        (void)newEffects;
        mixer = std::move(newMixer);
        tempoProvider = std::move(provider);
    }
    void disconnectInstrument(int instrumentId) override
    {
        for (auto& i : instruments)
        {
            if (i && i->getMidiChannel() >= 0 && static_cast<XInstrument*>(i.get())->id() == instrumentId)
            {
                i.reset();
            }
        }
    }
    void renderAudioBuffer(AudioBufferStereoRef buffer) override;
    void renderAudioBuffer(AudioBufferStereoRef bufferMain,
                           std::span<AudioBufferRef> bufferAux,
                           int instrumentId) override;
    void updateAudio() override {}
    void updateMain() override {}

    InstrumentTypeRegistry registry;
    std::map<FileSystemId, std::unique_ptr<FileSystem>> fileSystems;
    std::vector<std::shared_ptr<Instrument>> instruments;
    std::shared_ptr<Mixer> mixer;
    TempoProvider tempoProvider;

    FileSystem* fileSystem(FileSystemId id)
    {
        auto it = fileSystems.find(id);
        return it == fileSystems.end() ? nullptr : it->second.get();
    }
    float currentTempo() const
    {
        if (tempoProvider)
        {
            auto t = tempoProvider();
            if (t.has_value() && t.value() > 0.f)
            {
                return t.value();
            }
        }
        return 120.f;
    }
};

/* ---- XInstrument ---- */
XInstrument::XInstrument(int instrumentId, SynthImpl& owner)
  : instrumentId(instrumentId)
  , synthOwner(owner)
{
    listener = std::make_shared<Listener>(*this);
    callback = std::make_shared<MidiSource::MidiEventCallback>([this](const MidiEvent& e, int) { onMidi(e); });
}

XInstrument::~XInstrument()
{
    if (patch)
    {
        patch->unsubscribeParamUpdates(*listener);
    }
    voices.clear();
}

void XInstrument::connect(MidiSource& midiSource)
{
    midiSource.subscribeMidiEvents(instrumentId, callback);
}

void XInstrument::assignPatch(std::shared_ptr<Patch> newPatch)
{
    auto impl = std::static_pointer_cast<PatchImpl>(newPatch);
    if (patch)
    {
        patch->unsubscribeParamUpdates(*listener);
    }
    const bool sameType = patch && impl && patch->typeId() == impl->typeId();
    patch = std::move(impl);
    if (!patch)
    {
        voices.clear();
        return;
    }
    patch->subscribeParamUpdates(listener);
    if (sameType && static_cast<int>(voices.size()) == std::min(voiceCount, patch->getMetadata().maxVoices))
    {
        for (size_t i = 0; i < voices.size(); i++)
        {
            voices[i]->setPatch(patch);
            pushAllParams(*voices[i], static_cast<int>(i));
        }
    }
    else
    {
        rebuildVoices();
    }
}

void XInstrument::rebuildVoices()
{
    voices.clear();
    if (!patch)
    {
        return;
    }
    const int n = std::min(voiceCount, patch->getMetadata().maxVoices);
    Hub::get().creatingPart = instrumentId;
    for (int i = 0; i < n; i++)
    {
        auto v = patch->type().createVoice(i);
        if (!v)
        {
            break;
        }
        v->setPatch(patch);
        pushAllParams(*v, i);
        voices.push_back(std::move(v));
    }
}

void XInstrument::pushAllParams(InstrumentVoice& voice, int index)
{
    const int n = static_cast<int>(patch->getMetadata().params.size());
    for (int p = 0; p < n; p++)
    {
        voice.onParamChange(p, patch->finalValue(p, index));
    }
}

void XInstrument::setVoiceCount(int count)
{
    count = std::max(0, count);
    voiceCount = count;
    if (!patch)
    {
        return;
    }
    const int want = std::min(count, patch->getMetadata().maxVoices);
    if (want < static_cast<int>(voices.size()))
    {
        voices.resize(want);
    }
    else
    {
        Hub::get().creatingPart = instrumentId;
        while (static_cast<int>(voices.size()) < want)
        {
            const int i = static_cast<int>(voices.size());
            auto v = patch->type().createVoice(i);
            if (!v)
            {
                break;
            }
            v->setPatch(patch);
            pushAllParams(*v, i);
            voices.push_back(std::move(v));
        }
    }
}

void XInstrument::paramChanged(int paramId, Patch::optional_paramvalue_t value, tiny_optional<int> voiceId)
{
    if (voiceId.has_value())
    {
        const int v = voiceId.value();
        if (v >= 0 && v < static_cast<int>(voices.size()))
        {
            voices[v]->onParamChange(paramId, value);
        }
        return;
    }
    for (auto& v : voices)
    {
        v->onParamChange(paramId, value);
    }
}

int XInstrument::pickVoice(int note)
{
    const int n = static_cast<int>(voices.size());
    for (int i = 0; i < n; i++)
    {
        if (voices[i]->handlesNote(note))
        {
            return i; // the same note again retriggers its voice
        }
    }
    int best = -1;
    for (int i = 0; i < n; i++) // a voice that is not held and has stopped sounding
    {
        if (!voices[i]->handlesAnyNote() && !voices[i]->isActive())
        {
            return i;
        }
    }
    uint32_t oldest = 0xffffffffu;
    for (int i = 0; i < n; i++) // else the oldest voice that was released (still in its tail)
    {
        if (!voices[i]->handlesAnyNote() && started[i] < oldest)
        {
            oldest = started[i];
            best = i;
        }
    }
    if (best >= 0)
    {
        return best;
    }
    oldest = 0xffffffffu; // else steal the oldest held voice
    for (int i = 0; i < n; i++)
    {
        if (started[i] < oldest)
        {
            oldest = started[i];
            best = i;
        }
    }
    return best;
}

void XInstrument::allNotesOff()
{
    MidiEvent off{};
    off.type = MidiEvent::Type::kNoteOff;
    off.channel = static_cast<uint8_t>(midiChannel);
    for (auto& v : voices)
    {
        for (int guard = 0; guard < 16 && v->handlesAnyNote(); guard++)
        {
            auto last = v->getLastNote();
            if (!last.has_value())
            {
                break;
            }
            off.data.note.note = static_cast<uint8_t>(last.value());
            off.data.note.velocity = 0;
            v->onMidiEvent(context(), off);
            if (v->handlesNote(last.value()))
            {
                break;
            }
        }
    }
}

void XInstrument::onMidi(const MidiEvent& e)
{
    using T = MidiEvent::Type;
    if (e.type == T::kClock || e.type == T::kStart || e.type == T::kStop || e.type == T::kContinue)
    {
        return; // the XXL voices are not clocked; their tempo comes from the project
    }
    if (voices.empty() || !patch)
    {
        return;
    }
    if (e.channel != midiChannel && e.channel != MidiEvent::kChannelOmni)
    {
        return;
    }
    switch (e.type)
    {
        case T::kNoteOn:
        {
            if (e.data.note.velocity == 0)
            {
                for (auto& v : voices)
                {
                    if (v->handlesNote(e.data.note.note))
                    {
                        v->onMidiEvent(context(), e);
                        break;
                    }
                }
                break;
            }
            const int vi = pickVoice(e.data.note.note);
            if (vi < 0)
            {
                break;
            }
            started[vi] = ++serial;
            patch->setMacroValue(*Patch::SystemMacroId::kVelocity,
                                 static_cast<float>(e.data.note.velocity) * Patch::kMaxMacroValue /
                                   static_cast<float>(MidiEvent::Data::Note::kMaxVelocity),
                                 vi);
            voices[vi]->onMidiEvent(context(), e);
            break;
        }
        case T::kNoteOff:
            for (auto& v : voices)
            {
                if (v->handlesNote(e.data.note.note))
                {
                    v->onMidiEvent(context(), e);
                    break;
                }
            }
            break;
        case T::kControlChange:
        {
            const int id = e.data.controlChange.id;
            if (id == MidiEvent::Data::ControlChange::kAllNotesOffCC || id == 120)
            {
                allNotesOff();
            }
            else if (id >= Patch::Metadata::Macro::kMacroCCStart &&
                     id < Patch::Metadata::Macro::kMacroCCStart + static_cast<int>(kUserMacroCount))
            {
                const int macro = id - Patch::Metadata::Macro::kMacroCCStart;
                const auto& m = patch->getMetadata().macros[macro];
                const float cc = static_cast<float>(e.data.controlChange.value);
                const float mv = m.bipolar ? (cc - 63.5f) / 63.5f * Patch::kMaxMacroValue
                                           : cc / static_cast<float>(MidiEvent::Data::ControlChange::kMaxValue) *
                                               Patch::kMaxMacroValue;
                patch->setMacroValue(macro, mv, std::nullopt);
            }
            break;
        }
        case T::kChannelAftertouch:
            for (int i = 0; i < static_cast<int>(voices.size()); i++)
            {
                if (voices[i]->handlesAnyNote())
                {
                    patch->setMacroValue(*Patch::SystemMacroId::kPressure,
                                         static_cast<float>(e.data.channelAftertouch.velocity) * Patch::kMaxMacroValue /
                                           127.f,
                                         i);
                }
            }
            break;
        default: break; // pitch bend, program change, poly pressure: not used by the XXL voices
    }
}

void XInstrument::render(AudioBufferStereoRef main, float bpm)
{
    tempo = bpm;
    std::fill(main[0].begin(), main[0].end(), 0.f);
    std::fill(main[1].begin(), main[1].end(), 0.f);
    if (voices.empty())
    {
        return;
    }
    Hub::get().beginPart(instrumentId, bpm);
    std::array<float, kAudioBufferSize> bl, br;
    AudioBufferStereoRef tmp = { AudioBufferRef(bl), AudioBufferRef(br) };
    std::span<AudioBufferRef> noAux;
    const auto ctx = context();
    for (auto& v : voices)
    {
        v->renderVoiceAudio(ctx, std::nullopt, tmp, noAux);
        for (int i = 0; i < kAudioBufferSize; i++)
        {
            main[0][i] += bl[i];
            main[1][i] += br[i];
        }
    }
    voices.front()->renderPostProcessAudio(ctx, main);
}

/* ---- mixer ---- */
class MixerImpl final : public Mixer
{
  public:
    MixerImpl(int channels, int sends)
      : gain(channels, 0.f)
      , pan(channels, 0.f)
      , send(channels * std::max(1, sends), -96.f)
      , sendCount(std::max(1, sends))
    {
    }
    float getChannelGain(int c) override { return at(gain, c); }
    void setChannelGain(int c, float g) override { set(gain, c, g); }
    float getChannelPanning(int c) override { return at(pan, c); }
    void setChannelPanning(int c, float p) override { set(pan, c, p); }
    float getChannelSend(int c, int s) override { return at(send, c * sendCount + s); }
    void setChannelSend(int c, int s, float g) override { set(send, c * sendCount + s, g); }
    float getReturnChannelSend(int, int) override { return 0.f; }
    void setReturnChannelSend(int, int, float) override {}
    void connectInput(int, std::shared_ptr<Instrument>) override {}
    void disconnectInput(int) override {}
    void connectSend(int, std::shared_ptr<Instrument>) override {}

  private:
    std::vector<float> gain, pan, send;
    int sendCount;
    static float at(const std::vector<float>& v, int i) { return i >= 0 && i < static_cast<int>(v.size()) ? v[i] : 0.f; }
    static void set(std::vector<float>& v, int i, float x)
    {
        if (i >= 0 && i < static_cast<int>(v.size()))
        {
            v[i] = x;
        }
    }
};

/* ---- patch store ---- */
class PatchStoreImpl final : public PatchStore
{
  public:
    PatchStoreImpl(SynthImpl& owner, FileSystem* fs, bool typed)
      : owner(owner)
      , fs(fs)
      , typed(typed)
    {
    }

    std::unique_ptr<Patch> createPatch(int instrumentTypeId) override
    {
        auto t = owner.registry.getInstrumentType(instrumentTypeId);
        if (!t)
        {
            return nullptr;
        }
        return std::make_unique<PatchImpl>(t);
    }

    std::vector<std::pair<std::string_view, int>> listPatchesFilter(std::function<bool(InstrumentTypeId)> filter) override
    {
        pairCache.clear();
        pairNames.clear();
        for (int id : owner.getInstrumentTypeIds())
        {
            if (filter && !filter(static_cast<InstrumentTypeId>(id)))
            {
                continue;
            }
            for (auto& n : names(id))
            {
                pairNames.push_back({ std::move(n), id });
            }
        }
        for (const auto& pn : pairNames)
        {
            pairCache.emplace_back(std::string_view(pn.first), pn.second);
        }
        return pairCache;
    }

    std::vector<std::string_view> listPatchesByType(int typeId) override
    {
        cache = names(typeId);
        std::vector<std::string_view> r;
        r.reserve(cache.size());
        for (const auto& s : cache)
        {
            r.emplace_back(s);
        }
        return r;
    }

    bool loadPatch(const std::string& name, Patch& patch, bool synchronizeAudio) override
    {
        auto& p = static_cast<PatchImpl&>(patch);
        const int typeId = p.typeId();
        std::vector<uint8_t> bytes;
        if (readFile(typeId, name, bytes))
        {
            p.startBatchUpdate();
            bool ok = false;
            if (synchronizeAudio)
            {
                DISABLE_AUDIO_INTERRUPTS();
            }
            ok = p.deserialize(bytes.data(), bytes.size());
            p.finishBatchUpdate();
            if (synchronizeAudio)
            {
                ENABLE_AUDIO_INTERRUPTS();
            }
            return ok;
        }
        if (typed)
        {
            const int voice = voiceOfTypeId(typeId);
            for (int i = 0; i < factoryPatchCount(voice); i++)
            {
                if (factoryPatchName(voice, i) == name)
                {
                    std::vector<float> vals;
                    if (!factoryPatchValues(voice, i, vals))
                    {
                        return false;
                    }
                    p.startBatchUpdate();
                    if (synchronizeAudio)
                    {
                        DISABLE_AUDIO_INTERRUPTS();
                    }
                    p.assignValues(vals);
                    p.finishBatchUpdate();
                    if (synchronizeAudio)
                    {
                        ENABLE_AUDIO_INTERRUPTS();
                    }
                    return true;
                }
            }
        }
        return false;
    }

    bool savePatch(const std::string& name, const Patch& patch) override
    {
        if (!fs || name.empty())
        {
            return false;
        }
        const auto& p = static_cast<const PatchImpl&>(patch);
        std::vector<uint8_t> bytes;
        p.serialize(bytes);
        if (typed)
        {
            fs->createFolder(folderOf(p.typeId()));
        }
        auto h = fs->open(pathOf(p.typeId(), name), FileSystem::OpenOptions::kWrite);
        if (!h)
        {
            return false;
        }
        const bool ok = fs->write(*h, std::span<const uint8_t>(bytes.data(), bytes.size()));
        fs->close(*h);
        return ok;
    }

    std::optional<InstrumentTypeId> loadPatchInstrumentType(const std::string& name) override
    {
        for (int id : owner.getInstrumentTypeIds())
        {
            std::vector<uint8_t> bytes;
            if (readFile(id, name, bytes))
            {
                int t = 0;
                if (PatchImpl::readTypeId(bytes.data(), bytes.size(), t))
                {
                    return static_cast<InstrumentTypeId>(t);
                }
            }
            if (typed)
            {
                const int voice = voiceOfTypeId(id);
                for (int i = 0; i < factoryPatchCount(voice); i++)
                {
                    if (factoryPatchName(voice, i) == name)
                    {
                        return static_cast<InstrumentTypeId>(id);
                    }
                }
            }
            if (!typed)
            {
                break; // a flat store has one folder: the first read decided
            }
        }
        return std::nullopt;
    }

  private:
    SynthImpl& owner;
    FileSystem* fs;
    bool typed;
    std::vector<std::string> cache;
    std::vector<std::pair<std::string, int>> pairNames;
    std::vector<std::pair<std::string_view, int>> pairCache;

    std::string folderOf(int typeId) const
    {
        auto t = owner.registry.getInstrumentType(typeId);
        std::string n = t ? t->name : std::string("Unknown");
        for (auto& c : n)
        {
            if (c == '/' || c == '\\')
            {
                c = '-';
            }
        }
        return n;
    }
    std::string pathOf(int typeId, const std::string& name) const
    {
        return (typed ? folderOf(typeId) + "/" : std::string()) + name + PatchStore::kFileExtension;
    }

    bool readFile(int typeId, const std::string& name, std::vector<uint8_t>& out)
    {
        if (!fs)
        {
            return false;
        }
        auto h = fs->open(pathOf(typeId, name), FileSystem::OpenOptions::kNone);
        if (!h)
        {
            return false;
        }
        out.clear();
        uint8_t chunk[256];
        for (;;)
        {
            const int32_t n = fs->read(*h, std::span<uint8_t>(chunk, sizeof chunk));
            if (n <= 0)
            {
                break;
            }
            out.insert(out.end(), chunk, chunk + n);
            if (out.size() > 65536)
            {
                break;
            }
        }
        fs->close(*h);
        return !out.empty();
    }

    /* file names (without extension) in this type's folder, then the factory patches not shadowed by a file */
    std::vector<std::string> names(int typeId)
    {
        std::vector<std::string> r;
        if (fs)
        {
            const std::string ext = PatchStore::kFileExtension;
            auto files = fs->listFiles(typed ? folderOf(typeId) : std::string(),
                                       [&ext](const std::string& n)
                                       { return n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0; },
                                       std::nullopt);
            for (auto f : files)
            {
                if (f.size() > ext.size())
                {
                    r.emplace_back(f.substr(0, f.size() - ext.size()));
                }
            }
        }
        if (typed)
        {
            const int voice = voiceOfTypeId(typeId);
            const int n = factoryPatchCount(voice);
            for (int i = 0; i < n; i++)
            {
                std::string fn = factoryPatchName(voice, i);
                if (!fn.empty() && std::find(r.begin(), r.end(), fn) == r.end())
                {
                    r.push_back(std::move(fn));
                }
            }
        }
        return r;
    }
};

/* ---- patch pool: patches by index, loaded on demand from a store ---- */
class PatchPoolImpl final : public PatchPool
{
  public:
    PatchPoolImpl(std::shared_ptr<PatchStore> store,
                  int slots,
                  std::function<tiny::optional<int>(const std::string&)> nameToIndex)
      : store(std::move(store))
      , nameToIndex(std::move(nameToIndex))
      , slotNames(std::max(0, slots))
    {
    }

    bool getPatch(int idx, Patch* patch, bool loadFromStore) override
    {
        if (idx < 0 || idx >= static_cast<int>(slotNames.size()))
        {
            return false;
        }
        if (!slotNames[idx].has_value())
        {
            if (!loadFromStore || !patch || !index())
            {
                return false;
            }
        }
        if (!slotNames[idx].has_value())
        {
            return false;
        }
        return patch ? store->loadPatch(*slotNames[idx], *patch, true) : true;
    }

    std::optional<std::string> getPatchName(int idx) override
    {
        if (idx < 0 || idx >= static_cast<int>(slotNames.size()))
        {
            return std::nullopt;
        }
        if (!slotNames[idx].has_value())
        {
            index();
        }
        return slotNames[idx];
    }

  private:
    std::shared_ptr<PatchStore> store;
    std::function<tiny::optional<int>(const std::string&)> nameToIndex;
    std::vector<std::optional<std::string>> slotNames;

    bool index()
    {
        bool any = false;
        auto list = store->listPatchesFilter([](InstrumentTypeId) { return true; });
        for (const auto& item : list)
        {
            std::string n(item.first);
            auto i = nameToIndex ? nameToIndex(n) : tiny::optional<int>();
            if (i.has_value() && i.value() >= 0 && i.value() < static_cast<int>(slotNames.size()))
            {
                slotNames[i.value()] = n;
                any = true;
            }
        }
        return any;
    }
};

/* ---- SynthImpl ---- */
SynthImpl::SynthImpl(Allocator alloc)
{
    InstrumentTypeRegistry::registerInstrumentTypes(registry, alloc);
}

std::unique_ptr<PatchStore> SynthImpl::createPatchStore(FileSystemId id, bool instrumentTypeFolderStructure)
{
    return std::make_unique<PatchStoreImpl>(*this, fileSystem(id), instrumentTypeFolderStructure);
}

std::unique_ptr<PatchPool> SynthImpl::createPatchPool(
  std::shared_ptr<PatchStore> store,
  int slotCount,
  std::function<tiny::optional<int>(const std::string&)> nameToIndex)
{
    return std::make_unique<PatchPoolImpl>(std::move(store), slotCount, std::move(nameToIndex));
}

std::shared_ptr<Instrument> SynthImpl::createInstrument(int instrumentId)
{
    return std::make_shared<XInstrument>(instrumentId, *this);
}

std::unique_ptr<Mixer> SynthImpl::createMixer(int channels, int sends)
{
    return std::make_unique<MixerImpl>(channels, sends);
}

void SynthImpl::renderAudioBuffer(AudioBufferStereoRef buffer)
{
    std::fill(buffer[0].begin(), buffer[0].end(), 0.f);
    std::fill(buffer[1].begin(), buffer[1].end(), 0.f);
    std::array<float, kAudioBufferSize> l, r;
    const float bpm = currentTempo();
    for (auto& i : instruments)
    {
        if (!i)
        {
            continue;
        }
        AudioBufferStereoRef tmp = { AudioBufferRef(l), AudioBufferRef(r) };
        static_cast<XInstrument*>(i.get())->render(tmp, bpm);
        for (int k = 0; k < kAudioBufferSize; k++)
        {
            buffer[0][k] += l[k];
            buffer[1][k] += r[k];
        }
    }
}

void SynthImpl::renderAudioBuffer(AudioBufferStereoRef bufferMain, std::span<AudioBufferRef>, int instrumentId)
{
    for (auto& i : instruments)
    {
        if (i && static_cast<XInstrument*>(i.get())->id() == instrumentId)
        {
            static_cast<XInstrument*>(i.get())->render(bufferMain, currentTempo());
            return;
        }
    }
    std::fill(bufferMain[0].begin(), bufferMain[0].end(), 0.f);
    std::fill(bufferMain[1].begin(), bufferMain[1].end(), 0.f);
}
} // namespace xxl

namespace polysynth
{
std::shared_ptr<Synth> Synth::createSynth(Allocator largeBuffersAlloc)
{
    return std::make_shared<xxl::SynthImpl>(std::move(largeBuffersAlloc));
}
} // namespace polysynth
