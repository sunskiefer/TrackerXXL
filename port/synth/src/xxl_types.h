/* TrackerXXL open polysynth layer: the instrument types (one per voice of the voice library) and their factory patches. */
#pragma once

#include "PatchImpl.h"
#include "PolySynth/config.h"
#include "PolySynth/sdk/InstrumentType.h"
#include "trackerxxl_voices.h"

#include <memory>
#include <string>
#include <vector>

namespace xxl
{
/* InstrumentTypeId of voice v (0..18 ZYNTH synth voices, 19..24 LXR drum voices) is kXxlVoiceBase + v */
inline constexpr int typeIdOfVoice(int voice)
{
    return static_cast<int>(polysynth::InstrumentTypeId::kXxlVoiceBase) + voice;
}
inline constexpr int voiceOfTypeId(int typeId)
{
    const int v = typeId - static_cast<int>(polysynth::InstrumentTypeId::kXxlVoiceBase);
    return (v >= 0 && v < TV_NUM_VOICES) ? v : -1;
}

struct ParamDesc
{
    enum Kind : uint8_t
    {
        kCtl,     // one of the voice's 20 controls          idx = control
        kChain,   // the shared chain (filter, envelopes ...) idx = chain index
        kFx,      // the part's delay / gate-duck (tv_fx_*)   idx = fx parameter
        kFxSync,  // delay time free / tempo
        kGateOn,  // gate/duck on
        kDuckSrc, // gate/duck trigger: 0 own notes, 1..3 synth part
    };
    Kind kind;
    int idx;
    int opts; // > 0: an enum parameter with that many options (value = option index)
};

struct TypeInfo
{
    int voice = -1;
    bool drum = false;
    std::string name;
    std::string folder; // Patches/<folder>/
    std::vector<ParamDesc> params;
    int macroCtl[2] = { -1, -1 };

    static const TypeInfo* get(int voice);
};

std::unique_ptr<polysynth::InstrumentType> createXxlVoiceType(int voice);

/* ---- factory patches: the voice's starter sounds / kits (and BAZZLINE's bass bank on BAZZ) ---- */
int factoryPatchCount(int voice);
std::string factoryPatchName(int voice, int index);
/* the base values of factory patch `index` of `voice`, one per parameter of the type; false if unknown */
bool factoryPatchValues(int voice, int index, std::vector<float>& out);
} // namespace xxl
