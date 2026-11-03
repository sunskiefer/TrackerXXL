/* TrackerXXL open polysynth layer: the shared state behind all instrument types.
 *
 * One TvInstance (port/voices) renders every synth part: kHubSynthSlots mono synth slots (a part with N voices owns N of
 * them, handed out when the voice objects are created, so the audio thread never allocates) and the six LXR drum voices
 * (one fixed slot each). A second, lazily created instance (the "scratch") only produces factory patches (starters / kits)
 * on the UI thread and is never rendered.
 *
 * Threading follows the firmware: everything that touches the render instance is either on the audio thread (sequencer
 * events, rendering) or on the UI thread inside DISABLE_AUDIO_INTERRUPTS() (the platform's audio gate). */
#pragma once

#include "trackerxxl_voices.h"

#include <array>
#include <cstdint>

namespace xxl
{
constexpr int kBlock = 128;          // frames per render call (polysynth::kAudioBufferSize)
constexpr int kParts = 3;            // kSynthParts of the Tracker+ configuration
constexpr int kHubSynthSlots = 16;   // 8 voices are allowed at once (kSynthMaxVoices); the rest absorbs transient states
constexpr int kNoSlot = -1;

class Hub
{
  public:
    static Hub& get();

    bool ok() const { return tv != nullptr; }
    TvInstance* instance() const { return tv; }

    /* ---- slots (UI thread, under the audio gate) ---- */
    int acquireSynthSlot(int voiceId);          // kNoSlot when the pool is empty
    void releaseSynthSlot(int slot);
    int drumSlot(int voiceId) const { return kHubSynthSlots + (voiceId - TV_NUM_SYNTH_VOICES); }
    void drumVoiceCreated(int part);
    void drumVoiceDestroyed(int part);

    /* ---- the part being created / its fx unit registry (UI thread) ---- */
    int creatingPart = 0;                       // set by the instrument before it creates voices
    TvFxUnit* acquireFx(int part);              // the part's delay / gate-duck insert (created on first use)
    void releaseFx(int part);
    void setDuckSource(int part, int source);   // 0 = own notes, 1..kParts = notes of that synth part
    void notePlayed(int part);                  // audio thread: a note started on `part` -> duck triggers

    /* ---- audio thread ---- */
    void beginPart(int part, float tempo);      // call before rendering a part; starts a new block when part <= last
    const float* drumL(int slot) const { return cacheL[slot - kHubSynthSlots].data(); }
    const float* drumR(int slot) const { return cacheR[slot - kHubSynthSlots].data(); }
    const float* drumFxL() const { return fxCacheL.data(); }
    const float* drumFxR() const { return fxCacheR.data(); }
    bool isDrumFxOwner(int part) const { return part == fxOwner; }
    float tempo() const { return bpm; }

    /* ---- factory patches (UI thread; own instance, never rendered) ---- */
    TvInstance* scratchInstance();

  private:
    Hub();
    TvInstance* tv = nullptr;
    TvInstance* scratch = nullptr;
    bool slotUsed[kHubSynthSlots] = {};
    int drumVoices[kParts] = {};
    int drumTotal = 0;
    int fxOwner = -1;
    struct FxEntry
    {
        TvFxUnit* unit = nullptr;
        int users = 0;
        int duckSource = 0;
    };
    std::array<FxEntry, kParts> fx = {};
    int lastPart = kParts;
    float bpm = 120.f;
    std::array<std::array<float, kBlock>, TV_NUM_DRUM_VOICES> cacheL = {}, cacheR = {};
    std::array<float, kBlock> fxCacheL = {}, fxCacheR = {};

    friend class XxlVoice;
    TvFxUnit* fxUnit(int part) const { return fx[part].unit; }
    void recomputeFxOwner();
};
} // namespace xxl
