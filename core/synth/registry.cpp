#include "config.h"
#ifdef FEATURE_SYNTH

#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include "engines/ChiptuneSynthVoice.h"
#include "trackerxxl_voices.h"

namespace xxl
{
// TrackerXXL open instrument types (port/synth/src/xxl_types.cpp): one per voice of the voice library.
std::unique_ptr<polysynth::InstrumentType> createXxlVoiceType(int voice);
} // namespace xxl

namespace polysynth
{
void InstrumentTypeRegistry::registerInstrumentTypes(
  InstrumentTypeRegistry& registry,
  [[maybe_unused]] const Synth::Allocator& largeBuffersAlloc)
{
    // TrackerXXL: Polyend's factory engines (libPolySynth.a / libPolyDSP.a) are proprietary and never linked. The 25 voices of
    // the open voice library take their place: 19 synth voices and 6 drum voices.
    for (int v = 0; v < TV_NUM_VOICES; v++)
    {
        registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kXxlVoiceBase) + v, xxl::createXxlVoiceType(v));
    }

    // Open-Source Custom Synth Engines
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kChiptuneSynth),
                               createChiptuneSynth());
}

} // namespace polysynth

#endif
