/* TrackerXXL open polysynth layer: polysynth::Patch (parameters, macros, listeners, file image).
 *
 * Semantics follow Polyend's SDK documentation (docs/custom_synth_sdk.md, PolySynth/Patch.h): a parameter's final value is
 * its base value plus every macro modifier on it (kAdd: macro * amount * range; kMult: a ratio), clamped to its range; the
 * per-voice macros (velocity, pressure) only enter the final value of the voice they were set for. */
#pragma once

#include "PolySynth/Patch.h"
#include "PolySynth/sdk/InstrumentType.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace xxl
{
constexpr int kMaxVoicesPerPatch = 8; // kSynthMaxVoices

class PatchImpl final : public polysynth::Patch
{
  public:
    explicit PatchImpl(std::shared_ptr<const polysynth::InstrumentType> type);

    [[nodiscard]] optional_paramvalue_t getParamValue(int paramId) const override;
    [[nodiscard]] tiny_optional<float> getParamValueEditMapped(int paramId) const override;
    void setParamValueEditMapped(int paramId,
                                 tiny_optional<float> value,
                                 ParamChangeSource source) override;
    [[nodiscard]] optional_paramvalue_t getParamFinalValue(int paramId) const override;
    void setParamValue(int paramId, optional_paramvalue_t value, ParamChangeSource source) override;

    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener> listener) override;
    void unsubscribeParamUpdates(const ParamChangeListener& listener) override;
    void startBatchUpdate() override;
    void finishBatchUpdate() override;

    [[nodiscard]] optional_paramvalue_t getMacroValue(int macroId) const override;
    void setMacroValue(int macroId, optional_paramvalue_t value, tiny_optional<int> voiceId) override;
    void setMacroModifier(int macroId, int modifierIdx, tiny_optional<int> paramId) override;
    void setMacroModifierAmount(int macroId, int modifierIdx, float amount) override;
    [[nodiscard]] tiny_optional<float> getMacroModifierAmount(int macroId, int modifierIdx) override;
    void clearMacroModifiers() override;
    void changeMacroModifierAmount(int macroId, int modifierIdx, paramvalue_t value) override;
    void setMacroBipolar(int macroId, bool bipolar) override;
    void setMacroDisplayName(int macroId, std::string name) override;

    [[nodiscard]] const polysynth::Sample* getSample(int sampleSlotId) const override;
    [[nodiscard]] polysynth::LoadStatus loadSample(int sampleSlotId, const std::string& filePath) override;

    [[nodiscard]] const Metadata& getMetadata() const override { return metadata; }
    [[nodiscard]] const Sequencer& getSequencer() const override { return sequencer; }
    void setSequence(const Sequencer::Sequence& steps, int idx) override;
    void setSequencerConfig(const Sequencer::Config& config) override;

    /* the value the voices should use for one parameter (voiceId: the voice whose per-voice macros apply, or none) */
    [[nodiscard]] float finalValue(int paramId, tiny_optional<int> voiceId) const;
    /* all parameters to `listener` (used when a patch is assigned to an instrument) */
    void notifyAll(ParamChangeListener& listener, ParamChangeSource source) const;

    /* replace every base value at once (no notification: wrap in startBatchUpdate / finishBatchUpdate) */
    void assignValues(const std::vector<float>& v);

    /* file image (see PatchStoreImpl); false = not a patch of this type / damaged */
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t* data, size_t size);
    static bool readTypeId(const uint8_t* data, size_t size, int& typeId);

    [[nodiscard]] int typeId() const { return metadata.instrumentTypeId; }
    [[nodiscard]] const polysynth::InstrumentType& type() const { return *instrumentType; }
    [[nodiscard]] float rangeOf(int paramId) const;

  private:
    std::shared_ptr<const polysynth::InstrumentType> instrumentType;
    Metadata metadata;
    Sequencer sequencer;
    std::vector<float> values;
    std::array<float, polysynth::kMacroCount> macroValues = {};
    std::array<std::array<float, polysynth::kMacroCount>, kMaxVoicesPerPatch> voiceMacroValues = {};
    std::vector<std::weak_ptr<ParamChangeListener>> listeners;
    bool batch = false;

    [[nodiscard]] float macroNormalized(int macroId, tiny_optional<int> voiceId) const;
    [[nodiscard]] float clampToParam(int paramId, float v) const;
    void notify(int paramId, ParamChangeSource source, tiny_optional<int> voiceId);
    void notifyMacroTargets(int macroId, ParamChangeSource source, tiny_optional<int> voiceId);
    void pruneListeners();
    [[nodiscard]] static float curveForward(Metadata::Param::Curve c, float x, float lo, float hi);
    [[nodiscard]] static float curveInverse(Metadata::Param::Curve c, float v, float lo, float hi);
};
} // namespace xxl
