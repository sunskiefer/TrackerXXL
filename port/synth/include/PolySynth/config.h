#pragma once

/*
 * Config file contains various definitions related to PolySynth API
 * that can vary between products using the module.
 * Product-specific definitions of these items are guarded by PRODUCT_XYZ
 * defines.
 */

#include <cstdint>
#include <span>

namespace polysynth
{
#if PRODUCT == WAVE

using sample_t = float;              // all of internal processing
using sample_store_t = int16_t;      // storage of sample file contents in memory
using sample_store_long_t = int32_t; // in calculations of sample_store_t that may overflow

static constexpr int kAudioBufferSize = 128;
static constexpr float kDefaultTempo = 120.f;

using AudioBufferRef = std::span<sample_t, kAudioBufferSize>;
using AudioBufferStereoRef = std::array<AudioBufferRef, 2>;

static constexpr int kMaxSequenceLength = 64;

static constexpr size_t kUserMacroCount = 6;
static constexpr size_t kMacroCount = kUserMacroCount + 2; // user + velocity + pressure

enum class InstrumentTypeId
{
    kMidiOut,
    kSimpleAnalogSynth,
    kFatAnalogSynth,
    kWaveTableSynth,
    kVASynth,
    kFMSynth,
    kDrumSynth,
    kShapingSynth,
    kChiptuneSynth,
    // TrackerXXL: the voice library's 25 voices. kXxlVoiceBase + 0..18 are the ZYNTH synth voices, + 19..24 the LXR drum
    // voices (xxl_types.h). Polyend's factory engines above (ACD, FAT, ...) are not available in this build.
    kXxlVoiceBase = 100,
    kLastSynth = kXxlVoiceBase + 24,
    kDelay = 1000,
    kReverb,
};

static constexpr int kMaxNumberOfInstruments = 3;

#else
#error "No product configuration defined"
#endif
}
