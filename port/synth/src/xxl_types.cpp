/* TrackerXXL open polysynth layer: instrument types and voices on the voice library (port/voices).
 *
 * Every voice of the library is an instrument type with its 20 controls (names, ranges and readouts from the library's
 * tables), the shared chain (filter, envelopes, FM, SUB, NOISE, swarm, drive, LFO, accent: the synth voices' sound beyond
 * V1..V20; LEVEL and PAN are left to the Tracker's mixer), and a delay + gate/duck insert per part. TIMBRE and MORPH are
 * the first two macro knobs. A voice object is one mono slot of the shared TvInstance. */
#include "xxl_types.h"
#include "xxl_hub.h"

#include "PolySynth/sdk/InstrumentVoice.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace xxl
{
using polysynth::Patch;

namespace
{
constexpr int kMaxVoicesPerType = 8; // kSynthMaxVoices

std::string sanitize(const char* s)
{
    std::string r = s ? s : "";
    for (auto& c : r)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            c = '-';
        }
    }
    return r;
}

const char* const kDuckSrcNames[4] = { "OWN", "SYNTH 1", "SYNTH 2", "SYNTH 3" };

std::string percent(float v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%d%%", static_cast<int>(std::lround(v * 100.f)));
    return b;
}

std::string formatValue(int voice, const ParamDesc& d, float v)
{
    char buf[40] = {};
    switch (d.kind)
    {
        case ParamDesc::kCtl:
        {
            const float tvv = d.opts > 0 ? (std::round(v) + 0.5f) / static_cast<float>(d.opts) : v;
            tv_control_text(voice, d.idx, std::clamp(tvv, 0.f, 1.f), buf, sizeof buf, 1);
            return buf[0] ? std::string(buf) : percent(v);
        }
        case ParamDesc::kChain:
            if (d.opts > 0)
            {
                return tv_chain_option_name(d.idx, std::clamp(static_cast<int>(std::lround(v)), 0, d.opts - 1));
            }
            return percent(v);
        case ParamDesc::kFx:
            if (d.opts > 0)
            {
                return tv_fx_param_option_name(d.idx, std::clamp(static_cast<int>(std::lround(v)), 0, d.opts - 1));
            }
            return percent(v);
        case ParamDesc::kFxSync: return v >= 0.5f ? "TEMPO" : "FREE";
        case ParamDesc::kGateOn: return v >= 0.5f ? "ON" : "OFF";
        case ParamDesc::kDuckSrc: return kDuckSrcNames[std::clamp(static_cast<int>(std::lround(v)), 0, 3)];
    }
    return {};
}

std::string paramName(int voice, const ParamDesc& d)
{
    switch (d.kind)
    {
        case ParamDesc::kCtl: return tv_control_name(voice, d.idx);
        case ParamDesc::kChain: return tv_chain_name(d.idx);
        case ParamDesc::kFx:
        {
            std::string n = tv_fx_param_name(d.idx);
            if (d.idx == 5)
            {
                return "GD MODE";
            }
            if (d.idx >= 6)
            {
                return "GD " + n;
            }
            return n;
        }
        case ParamDesc::kFxSync: return "DELAY SYNC";
        case ParamDesc::kGateOn: return "GD ON";
        case ParamDesc::kDuckSrc: return "GD TRIGGER";
    }
    return {};
}

float enumDefault(float x, int opts)
{
    return static_cast<float>(std::clamp(static_cast<int>(x * static_cast<float>(opts)), 0, opts - 1));
}

int findParam(const TypeInfo& t, ParamDesc::Kind k, int idx)
{
    for (size_t i = 0; i < t.params.size(); i++)
    {
        if (t.params[i].kind == k && t.params[i].idx == idx)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int findCtlByName(int voice, const char* name)
{
    for (int c = 0; c < TV_NUM_CONTROLS; c++)
    {
        if (std::strcmp(tv_control_name(voice, c), name) == 0)
        {
            return c;
        }
    }
    return -1;
}

int findChainByKey(const char* key)
{
    for (int c = 0; c < tv_chain_count(); c++)
    {
        if (std::strcmp(tv_chain_key(c), key) == 0)
        {
            return c;
        }
    }
    return -1;
}

TypeInfo build(int v)
{
    TypeInfo t;
    t.voice = v;
    t.drum = tv_voice_is_drum(v) != 0;
    t.name = tv_voice_name(v);
    t.folder = sanitize(tv_voice_name(v));
    for (int c = 0; c < TV_NUM_CONTROLS; c++)
    {
        t.params.push_back({ ParamDesc::kCtl, c, tv_control_steps(v, c) });
    }
    if (!t.drum)
    {
        for (int c = 0; c < tv_chain_count(); c++)
        {
            const char* key = tv_chain_key(c);
            if (!std::strcmp(key, "level") || !std::strcmp(key, "pan"))
            {
                continue;
            }
            t.params.push_back({ ParamDesc::kChain, c, tv_chain_options(c) });
        }
        t.macroCtl[0] = tv_voice_macro_control(v, 0);
        t.macroCtl[1] = tv_voice_macro_control(v, 1);
    }
    else
    {
        // the drum voices have no TIMBRE / MORPH table: the control that changes the voice most / the pitch
        static const char* const kTimbre[6] = { "FM AMOUNT", "FM AMOUNT", "FM AMOUNT", "NOISE MIX", "MOD OSC GAIN 1", "MOD OSC GAIN 1" };
        t.macroCtl[0] = findCtlByName(v, kTimbre[v - TV_NUM_SYNTH_VOICES]);
        t.macroCtl[1] = findCtlByName(v, "TUNE");
    }
    for (int i = 0; i < tv_fx_param_count(); i++)
    {
        t.params.push_back({ ParamDesc::kFx, i, tv_fx_param_options(i) });
    }
    t.params.push_back({ ParamDesc::kFxSync, 0, 2 });
    t.params.push_back({ ParamDesc::kGateOn, 0, 2 });
    t.params.push_back({ ParamDesc::kDuckSrc, 0, 4 });
    return t;
}

/* ---------------------------------------------------------------- the editor's sections (names pick the icons) */
int synthSection(const char* key)
{
    struct Row
    {
        const char* key;
        int sec;
    };
    // 0 Mixer, 1 Shaper, 2 Filter, 3 Amp Envelope, 4 LFO, 5 Voice
    static const Row rows[] = {
        { "detune", 0 },   { "fm_lvl", 0 },   { "fm_ratio", 0 }, { "sub_lvl", 0 },   { "wave", 0 },
        { "noise_lvl", 0 }, { "sw_mode", 0 }, { "sw_rate", 0 },  { "sw_mix", 0 },
        { "drive", 1 },    { "drive_pos", 1 }, { "curve", 1 },
        { "fmode", 2 },    { "fmodel", 2 },  { "cutoff", 2 },    { "resonance", 2 }, { "keytrack", 2 },
        { "vcf_env", 2 },  { "vcf_atk", 2 }, { "vcf_dec", 2 },   { "vcf_sus", 2 },   { "vcf_rel", 2 },
        { "vca_atk", 3 },  { "vca_env", 3 }, { "sustain", 3 },   { "vca_rel", 3 },
        { "lfo_rate", 4 }, { "lfo_lvl", 4 }, { "mod_wave", 4 },  { "mod_dest", 4 },  { "mod_sync", 4 },
    };
    for (const auto& r : rows)
    {
        if (!std::strcmp(r.key, key))
        {
            return r.sec;
        }
    }
    return 5;
}

void layout(const TypeInfo& t, Patch::Metadata& md)
{
    using Section = Patch::Metadata::EditorLayout::EditorSection;
    using Element = Patch::Metadata::EditorLayout::EditorElement;
    auto el = [](int id) { return Element{ Element::Type::kParam, id }; };
    std::vector<Section> secs;
    if (!t.drum)
    {
        Section osc{ "Oscillator", "Osc", {}, {} }, deep{ "Oscillator Deep", "Osc 2", {}, {} };
        Section mixer{ "Mixer", "Mixer", {}, {} }, shaper{ "Shaper", "Shaper", {}, {} };
        Section filter{ "Filter", "Filter", {}, {} }, amp{ "Amp Envelope", "Amp Env", {}, {} };
        Section lfo{ "LFO", "LFO", {}, {} }, voice{ "Voice", "Voice", {}, {} };
        Section* bySec[6] = { &mixer, &shaper, &filter, &amp, &lfo, &voice };
        for (size_t i = 0; i < t.params.size(); i++)
        {
            const auto& d = t.params[i];
            if (d.kind == ParamDesc::kCtl)
            {
                (d.idx < 6 ? osc : d.idx < 12 ? deep : voice).elements.push_back(el(static_cast<int>(i)));
            }
            else if (d.kind == ParamDesc::kChain)
            {
                bySec[synthSection(tv_chain_key(d.idx))]->elements.push_back(el(static_cast<int>(i)));
            }
        }
        secs = { osc, deep, voice, mixer, shaper, filter, amp, lfo };
    }
    else
    {
        static const char* const kVoiceSection[6] = { "Kick", "Tom", "Tom 2", "Snare", "Cymbal", "Hihat" };
        Section main{ kVoiceSection[t.voice - TV_NUM_SYNTH_VOICES], t.name, {}, {} };
        Section filter{ "Filter", "Filter", {}, {} }, shaper{ "Shaper", "Shaper", {}, {} }, mixer{ "Mixer", "Mixer", {}, {} };
        for (size_t i = 0; i < t.params.size(); i++)
        {
            const auto& d = t.params[i];
            if (d.kind != ParamDesc::kCtl)
            {
                continue;
            }
            const std::string n = tv_control_name(t.voice, d.idx);
            Section* s = &main;
            if (n == "FILTER TYPE" || n == "CUTOFF" || n == "RESONANCE" || n == "FILTER DRIVE")
            {
                s = &filter;
            }
            else if (n == "DRIVE" || n == "DECIMATE")
            {
                s = &shaper;
            }
            else if (n == "VOLUME" || n == "PAN")
            {
                s = &mixer;
            }
            s->elements.push_back(el(static_cast<int>(i)));
        }
        secs = { main, filter, shaper, mixer };
    }
    Section fx{ "FX", "FX", {}, {} };
    for (size_t i = 0; i < t.params.size(); i++)
    {
        if (t.params[i].kind >= ParamDesc::kFx)
        {
            fx.elements.push_back(el(static_cast<int>(i)));
        }
    }
    secs.push_back(fx);
    md.layoutFlat.sections = secs;
    md.layout23 = md.layoutFlat;
}

void createMetadata(const TypeInfo& t, Patch::Metadata& md)
{
    const int v = t.voice;
    md.instrumentTypeId = typeIdOfVoice(v);
    md.outputChannels = t.drum ? Patch::Metadata::OutputChannels::kStereo : Patch::Metadata::OutputChannels::kMono;
    md.rendersAuxSends = false;
    md.maxVoices = t.drum ? 1 : kMaxVoicesPerType;

    // the voice's home sound (the Init patch); the drum voices start from the init kit
    TvInstance* sc = t.drum ? nullptr : Hub::get().scratchInstance();
    const bool home = sc && tv_load_starter(sc, 0, v, 0) == 0;

    md.params.resize(t.params.size());
    for (size_t i = 0; i < t.params.size(); i++)
    {
        const ParamDesc d = t.params[i];
        auto& p = md.params[i];
        p.displayName = paramName(v, d);
        p.editCurve = Patch::Metadata::Param::Curve::kLinear;
        p.macroOperation = Patch::Metadata::Param::MacroOperation::kAdd;
        p.isEnum = d.opts > 0;
        p.minValue = 0;
        p.maxValue = d.opts > 0 ? static_cast<float>(d.opts - 1) : 1.f;
        float def = 0.f;
        switch (d.kind)
        {
            case ParamDesc::kCtl:
            {
                const float x = home ? tv_get_param(sc, 0, d.idx) : tv_control_default(v, d.idx);
                def = d.opts > 0 ? enumDefault(x, d.opts) : x;
                break;
            }
            case ParamDesc::kChain:
                def = home ? tv_get_chain(sc, 0, d.idx) : tv_chain_default(d.idx);
                break;
            case ParamDesc::kFx: def = tv_fx_param_default(d.idx); break;
            default: def = 0.f; break;
        }
        p.defaultValue = def;
        p.valueFormatter = [v, d](const Patch&, Patch::paramvalue_t value) { return formatValue(v, d, value); };
    }

    md.macros[*Patch::SystemMacroId::kVelocity].displayName = "Velocity";
    md.macros[*Patch::SystemMacroId::kVelocity].system = true;
    md.macros[*Patch::SystemMacroId::kVelocity].perVoice = true;
    md.macros[*Patch::SystemMacroId::kPressure].displayName = "Pressure";
    md.macros[*Patch::SystemMacroId::kPressure].system = true;
    md.macros[*Patch::SystemMacroId::kPressure].perVoice = true;

    md.initDefaultMacros = [v](std::vector<Patch::Metadata::Macro>& macros)
    {
        const TypeInfo& ti = *TypeInfo::get(v);
        auto mod = [&](int macro, const char* name, int paramId, float amount)
        {
            macros[macro].displayName = name;
            macros[macro].modifiers.clear();
            if (paramId >= 0)
            {
                macros[macro].modifiers.push_back({ paramId, amount });
            }
        };
        mod(0, "TIMBRE", ti.macroCtl[0] >= 0 ? findParam(ti, ParamDesc::kCtl, ti.macroCtl[0]) : -1, 1.0f);
        mod(1, "MORPH", ti.macroCtl[1] >= 0 ? findParam(ti, ParamDesc::kCtl, ti.macroCtl[1]) : -1, 1.0f);
        if (!ti.drum)
        {
            mod(2, "CUTOFF", findParam(ti, ParamDesc::kChain, findChainByKey("cutoff")), 0.5f);
            mod(3, "RESONANCE", findParam(ti, ParamDesc::kChain, findChainByKey("resonance")), 0.5f);
            mod(4, "DRIVE", findParam(ti, ParamDesc::kChain, findChainByKey("drive")), 0.5f);
            mod(5, "DECAY", findParam(ti, ParamDesc::kChain, findChainByKey("vca_env")), 0.5f);
        }
        else
        {
            mod(2, "CUTOFF", findParam(ti, ParamDesc::kCtl, findCtlByName(ti.voice, "CUTOFF")), 0.5f);
            mod(3, "RESONANCE", findParam(ti, ParamDesc::kCtl, findCtlByName(ti.voice, "RESONANCE")), 0.5f);
            mod(4, "DRIVE", findParam(ti, ParamDesc::kCtl, findCtlByName(ti.voice, "DRIVE")), 0.5f);
            int dec = findCtlByName(ti.voice, "DECAY");
            if (dec < 0)
            {
                dec = findCtlByName(ti.voice, "CLOSED DECAY");
            }
            mod(5, "DECAY", findParam(ti, ParamDesc::kCtl, dec), 0.5f);
        }
    };
    layout(t, md);
}

/* ---------------------------------------------------------------- the voice */
class XxlVoice final : public polysynth::InstrumentVoice
{
  public:
    XxlVoice(const TypeInfo& type, int voiceIdx)
      : info(type)
      , hub(Hub::get())
      , voiceIdx(voiceIdx)
      , part(hub.creatingPart)
    {
        tv = hub.instance();
        if (info.drum)
        {
            slot = hub.drumSlot(info.voice);
            hub.drumVoiceCreated(part);
        }
        else
        {
            slot = hub.acquireSynthSlot(info.voice);
        }
        if (voiceIdx == 0)
        {
            fxu = hub.acquireFx(part);
        }
    }

    ~XxlVoice() override
    {
        if (info.drum)
        {
            hub.drumVoiceDestroyed(part);
        }
        else
        {
            hub.releaseSynthSlot(slot);
        }
        if (fxu)
        {
            hub.releaseFx(part);
        }
    }

    void onMidiEvent(const InstrumentVoiceContext&, const polysynth::MidiEvent& e) override
    {
        using T = polysynth::MidiEvent::Type;
        switch (e.type)
        {
            case T::kNoteOn:
                if (e.data.note.velocity == 0)
                {
                    noteOff(e.data.note.note);
                }
                else
                {
                    noteOn(e.data.note.note, e.data.note.velocity);
                }
                break;
            case T::kNoteOff: noteOff(e.data.note.note); break;
            default: break;
        }
    }

    void onParamChange(int paramId, Patch::optional_paramvalue_t newValue) override
    {
        if (!newValue.has_value() || paramId < 0 || paramId >= static_cast<int>(info.params.size()))
        {
            return;
        }
        const float v = newValue.value();
        const ParamDesc& d = info.params[paramId];
        switch (d.kind)
        {
            case ParamDesc::kCtl:
                if (tv && slot >= 0)
                {
                    const float tvv = d.opts > 0 ? (std::round(v) + 0.5f) / static_cast<float>(d.opts) : v;
                    tv_set_param(tv, slot, d.idx, std::clamp(tvv, 0.f, 1.f));
                }
                break;
            case ParamDesc::kChain:
                if (tv && slot >= 0)
                {
                    tv_set_chain(tv, slot, d.idx, d.opts > 0 ? std::round(v) : std::clamp(v, 0.f, 1.f));
                }
                break;
            case ParamDesc::kFx:
                if (d.idx == 3)
                {
                    wet = v;
                }
                if (fxu)
                {
                    tv_fxunit_set_param(fxu, d.idx, d.opts > 0 ? std::round(v) : v);
                }
                break;
            case ParamDesc::kFxSync:
                if (fxu)
                {
                    tv_fxunit_set_sync(fxu, v >= 0.5f ? 1 : 0);
                }
                break;
            case ParamDesc::kGateOn:
                gate = v >= 0.5f;
                if (fxu)
                {
                    tv_fxunit_set_gate_duck_on(fxu, gate ? 1 : 0);
                }
                break;
            case ParamDesc::kDuckSrc:
                if (voiceIdx == 0)
                {
                    hub.setDuckSource(part, std::clamp(static_cast<int>(std::lround(v)), 0, 3));
                }
                break;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext&,
                          std::optional<polysynth::AudioBufferStereoRef>,
                          polysynth::AudioBufferStereoRef out,
                          std::span<polysynth::AudioBufferRef>) override
    {
        float* l = out[0].data();
        float* r = out[1].data();
        std::fill(l, l + kBlock, 0.f);
        std::fill(r, r + kBlock, 0.f);
        if (!tv || slot < 0)
        {
            return;
        }
        if (info.drum)
        {
            const float* cl = hub.drumL(slot);
            const float* cr = hub.drumR(slot);
            for (int i = 0; i < kBlock; i++)
            {
                l[i] = cl[i];
                r[i] = cr[i];
            }
            if (hub.isDrumFxOwner(part))
            {
                const float* fl = hub.drumFxL();
                const float* fr = hub.drumFxR();
                for (int i = 0; i < kBlock; i++)
                {
                    l[i] += fl[i];
                    r[i] += fr[i];
                }
            }
        }
        else
        {
            float scratchR[kBlock];
            std::fill(scratchR, scratchR + kBlock, 0.f);
            tv_render_slot(tv, slot, l, scratchR, kBlock); // pan is centred: L == R
        }
    }

    void renderPostProcessAudio(const InstrumentVoiceContext&, polysynth::AudioBufferStereoRef out) override
    {
        if (!fxu || (!gate && wet < 0.001f))
        {
            return;
        }
        if (info.drum)
        {
            tv_fxunit_process(fxu, out[0].data(), out[1].data(), kBlock);
        }
        else
        {
            float mirror[kBlock];
            std::copy(out[0].begin(), out[0].end(), mirror);
            tv_fxunit_process(fxu, out[0].data(), mirror, kBlock);
        }
    }

    bool isActive() override
    {
        return heldCount > 0 || (tv && slot >= 0 && tv_slot_active(tv, slot));
    }
    bool handlesNote(polysynth::note_t note) override
    {
        for (int i = 0; i < heldCount; i++)
        {
            if (held[i].note == note)
            {
                return true;
            }
        }
        return false;
    }
    bool handlesAnyNote() override { return heldCount > 0; }
    tiny_optional<polysynth::note_t> getLastNote() override
    {
        return lastNote >= 0 ? tiny_optional<polysynth::note_t>(lastNote) : tiny_optional<polysynth::note_t>(std::nullopt);
    }
    tiny_optional<float> getCurrentNoteVelocity() override
    {
        return heldCount > 0 ? tiny_optional<float>(held[heldCount - 1].velocity) : tiny_optional<float>(std::nullopt);
    }
    void clearPreviousNotes() override
    {
        if (heldCount > 1)
        {
            held[0] = held[heldCount - 1];
            heldCount = 1;
        }
    }

  private:
    struct Held
    {
        int note;
        float velocity;
        int vel127;
    };
    const TypeInfo& info;
    Hub& hub;
    TvInstance* tv = nullptr;
    int voiceIdx;
    int part;
    int slot = kNoSlot;
    TvFxUnit* fxu = nullptr;
    bool gate = false;
    float wet = 0.f;
    std::array<Held, 8> held = {};
    int heldCount = 0;
    int lastNote = -1;

    void start(int note, int vel127, bool slide)
    {
        if (!tv || slot < 0)
        {
            return;
        }
        if (info.drum)
        {
            // note 63 plays a drum voice at its own pitch (LXR convention): the Tracker's C-4 (60) is that pitch
            int n = note + 3;
            unsigned flags = 0;
            if (info.voice == TV_NUM_SYNTH_VOICES + 5 && note >= 72) // HIHAT: C-5 and above is the open hat
            {
                flags |= TV_NOTE_HAT_OPEN;
                n -= 12;
            }
            tv_note_on(tv, slot, std::clamp(n, 0, 127), vel127, flags);
        }
        else
        {
            tv_note_on(tv, slot, note, vel127, TV_NOTE_VEL_ACCENT | (slide ? TV_NOTE_SLIDE : 0u));
        }
        hub.notePlayed(part);
    }

    void noteOn(int note, int vel127)
    {
        const bool slide = heldCount > 0;
        for (int i = 0; i < heldCount; i++)
        {
            if (held[i].note == note)
            {
                for (int j = i; j + 1 < heldCount; j++)
                {
                    held[j] = held[j + 1];
                }
                heldCount--;
                break;
            }
        }
        if (heldCount == static_cast<int>(held.size()))
        {
            for (int j = 0; j + 1 < heldCount; j++)
            {
                held[j] = held[j + 1];
            }
            heldCount--;
        }
        held[heldCount++] = { note, static_cast<float>(vel127) / 127.f, vel127 };
        lastNote = note;
        start(note, vel127, slide);
    }

    void noteOff(int note)
    {
        int at = -1;
        for (int i = 0; i < heldCount; i++)
        {
            if (held[i].note == note)
            {
                at = i;
            }
        }
        if (at < 0)
        {
            return;
        }
        const bool wasTop = at == heldCount - 1;
        for (int j = at; j + 1 < heldCount; j++)
        {
            held[j] = held[j + 1];
        }
        heldCount--;
        if (heldCount > 0)
        {
            if (wasTop && !info.drum)
            {
                lastNote = held[heldCount - 1].note;
                start(held[heldCount - 1].note, held[heldCount - 1].vel127, true); // back to the note still held
            }
        }
        else if (tv && slot >= 0 && !info.drum)
        {
            tv_note_off(tv, slot);
        }
    }
};

std::array<TypeInfo, TV_NUM_VOICES>& infos()
{
    static std::array<TypeInfo, TV_NUM_VOICES> arr;
    static bool built = false;
    if (!built)
    {
        for (int v = 0; v < TV_NUM_VOICES; v++)
        {
            arr[v] = build(v);
        }
        built = true;
    }
    return arr;
}
} // namespace

const TypeInfo* TypeInfo::get(int voice)
{
    return (voice >= 0 && voice < TV_NUM_VOICES) ? &infos()[voice] : nullptr;
}

std::unique_ptr<polysynth::InstrumentType> createXxlVoiceType(int voice)
{
    const TypeInfo* t = TypeInfo::get(voice);
    if (!t)
    {
        return nullptr;
    }
    auto type = std::make_unique<polysynth::InstrumentType>();
    type->name = t->name;
    type->createMetadata = [t](Patch&, Patch::Metadata& md) { createMetadata(*t, md); };
    type->createVoice = [t](int voiceIdx) -> std::unique_ptr<polysynth::InstrumentVoice>
    { return std::make_unique<XxlVoice>(*t, voiceIdx); };
    return type;
}

/* ---------------------------------------------------------------- factory patches */
int factoryPatchCount(int voice)
{
    if (voice < 0 || voice >= TV_NUM_VOICES)
    {
        return 0;
    }
    if (tv_voice_is_drum(voice))
    {
        return tv_kit_count();
    }
    return tv_starter_count(voice) + (voice == 15 ? tv_bass_bank_count() : 0); // 15 = BAZZ
}

std::string factoryPatchName(int voice, int index)
{
    if (voice < 0 || voice >= TV_NUM_VOICES || index < 0 || index >= factoryPatchCount(voice))
    {
        return {};
    }
    if (tv_voice_is_drum(voice))
    {
        return sanitize(tv_kit_name(index));
    }
    if (index < tv_starter_count(voice))
    {
        return sanitize(tv_starter_name(voice, index));
    }
    return "BASS " + sanitize(tv_bass_bank_name(index - tv_starter_count(voice)));
}

bool factoryPatchValues(int voice, int index, std::vector<float>& out)
{
    const TypeInfo* t = TypeInfo::get(voice);
    TvInstance* sc = Hub::get().scratchInstance();
    if (!t || !sc || index < 0 || index >= factoryPatchCount(voice))
    {
        return false;
    }
    int slot = 0;
    if (t->drum)
    {
        slot = tv_synth_slot_count(sc) + (voice - TV_NUM_SYNTH_VOICES);
        if (tv_load_starter(sc, slot, voice, index) != 0)
        {
            return false;
        }
    }
    else if (index < tv_starter_count(voice))
    {
        if (tv_load_starter(sc, 0, voice, index) != 0)
        {
            return false;
        }
    }
    else if (tv_load_bass_bank(sc, 0, index - tv_starter_count(voice)) != 0)
    {
        return false;
    }
    out.assign(t->params.size(), 0.f);
    for (size_t i = 0; i < t->params.size(); i++)
    {
        const ParamDesc& d = t->params[i];
        switch (d.kind)
        {
            case ParamDesc::kCtl:
            {
                const float x = tv_get_param(sc, slot, d.idx);
                out[i] = d.opts > 0 ? enumDefault(x, d.opts) : x;
                break;
            }
            case ParamDesc::kChain: out[i] = tv_get_chain(sc, slot, d.idx); break;
            case ParamDesc::kFx: out[i] = tv_fx_param_default(d.idx); break;
            default: out[i] = 0.f; break;
        }
    }
    return true;
}
} // namespace xxl
