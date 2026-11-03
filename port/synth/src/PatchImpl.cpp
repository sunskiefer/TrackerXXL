#include "PatchImpl.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xxl
{
using polysynth::Patch;

namespace
{
constexpr char kMagic[4] = { 'X', 'P', 'A', 'T' };
constexpr uint16_t kFileVersion = 1;
constexpr int kMaxModifiers = 16;

void put16(std::vector<uint8_t>& o, uint16_t v)
{
    o.push_back(static_cast<uint8_t>(v & 0xff));
    o.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& o, uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        o.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }
}
void putF(std::vector<uint8_t>& o, float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(o, u);
}

struct Reader
{
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
    bool bad = false;
    uint8_t u8()
    {
        if (pos + 1 > n)
        {
            bad = true;
            return 0;
        }
        return p[pos++];
    }
    uint16_t u16()
    {
        const uint16_t a = u8();
        const uint16_t b = u8();
        return static_cast<uint16_t>(a | (b << 8));
    }
    uint32_t u32()
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++)
        {
            v |= static_cast<uint32_t>(u8()) << (8 * i);
        }
        return v;
    }
    float f()
    {
        const uint32_t u = u32();
        float r;
        std::memcpy(&r, &u, 4);
        return r;
    }
};
} // namespace

PatchImpl::PatchImpl(std::shared_ptr<const polysynth::InstrumentType> type)
  : instrumentType(std::move(type))
{
    instrumentType->createMetadata(*this, metadata);
    values.resize(metadata.params.size(), 0.f);
    for (size_t i = 0; i < metadata.params.size(); i++)
    {
        const auto& d = metadata.params[i].defaultValue;
        values[i] = d.has_value() ? d.value() : 0.f;
    }
    if (metadata.initDefaultMacros)
    {
        metadata.initDefaultMacros(metadata.macros);
    }
    for (auto& m : metadata.macros)
    {
        if (m.displayName.size() > static_cast<size_t>(Metadata::Macro::kMaxDisplayNameLen))
        {
            m.displayName.resize(Metadata::Macro::kMaxDisplayNameLen);
        }
    }
    for (int m = 0; m < polysynth::kMacroCount; m++)
    {
        macroValues[m] = 0.f;
        for (auto& v : voiceMacroValues)
        {
            v[m] = 0.f;
        }
    }
}

float PatchImpl::rangeOf(int paramId) const
{
    const auto& p = metadata.params[paramId];
    return static_cast<float>(p.maxValue) - static_cast<float>(p.minValue);
}

float PatchImpl::clampToParam(int paramId, float v) const
{
    const auto& p = metadata.params[paramId];
    const float lo = static_cast<float>(p.minValue);
    const float hi = static_cast<float>(p.maxValue);
    return std::clamp(v, std::min(lo, hi), std::max(lo, hi));
}

Patch::optional_paramvalue_t PatchImpl::getParamValue(int paramId) const
{
    if (paramId < 0 || paramId >= static_cast<int>(values.size()))
    {
        return std::nullopt;
    }
    return values[paramId];
}

float PatchImpl::curveForward(Metadata::Param::Curve c, float x, float lo, float hi)
{
    x = std::clamp(x, 0.f, 1.f);
    switch (c)
    {
        case Metadata::Param::Curve::kLinear: return lo + x * (hi - lo);
        case Metadata::Param::Curve::kExponential:
            if (lo > 0.f && hi > lo)
            {
                return lo * std::pow(hi / lo, x);
            }
            return lo + (hi - lo) * (std::pow(100.f, x) - 1.f) / 99.f;
        case Metadata::Param::Curve::kExponentialSteep:
            if (lo > 0.f && hi > lo)
            {
                return lo * std::pow(hi / lo, x * x);
            }
            return lo + (hi - lo) * (std::pow(1000.f, x) - 1.f) / 999.f;
        case Metadata::Param::Curve::kPower: return lo + (hi - lo) * x * x;
    }
    return lo + x * (hi - lo);
}

float PatchImpl::curveInverse(Metadata::Param::Curve c, float v, float lo, float hi)
{
    if (hi == lo)
    {
        return 0.f;
    }
    float x;
    switch (c)
    {
        case Metadata::Param::Curve::kLinear: x = (v - lo) / (hi - lo); break;
        case Metadata::Param::Curve::kExponential:
            if (lo > 0.f && hi > lo)
            {
                x = std::log(std::max(v, lo) / lo) / std::log(hi / lo);
            }
            else
            {
                x = std::log(1.f + 99.f * (v - lo) / (hi - lo)) / std::log(100.f);
            }
            break;
        case Metadata::Param::Curve::kExponentialSteep:
            if (lo > 0.f && hi > lo)
            {
                x = std::sqrt(std::log(std::max(v, lo) / lo) / std::log(hi / lo));
            }
            else
            {
                x = std::log(1.f + 999.f * (v - lo) / (hi - lo)) / std::log(1000.f);
            }
            break;
        case Metadata::Param::Curve::kPower: x = std::sqrt(std::max(0.f, (v - lo) / (hi - lo))); break;
        default: x = (v - lo) / (hi - lo); break;
    }
    return std::clamp(x, 0.f, 1.f);
}

tiny_optional<float> PatchImpl::getParamValueEditMapped(int paramId) const
{
    if (paramId < 0 || paramId >= static_cast<int>(values.size()))
    {
        return std::nullopt;
    }
    const auto& p = metadata.params[paramId];
    return curveInverse(p.editCurve,
                        values[paramId],
                        static_cast<float>(p.minValue),
                        static_cast<float>(p.maxValue));
}

void PatchImpl::setParamValueEditMapped(int paramId,
                                        tiny_optional<float> value,
                                        ParamChangeSource source)
{
    if (paramId < 0 || paramId >= static_cast<int>(values.size()))
    {
        return;
    }
    if (!value.has_value())
    {
        setParamValue(paramId, std::nullopt, source);
        return;
    }
    const auto& p = metadata.params[paramId];
    float v = curveForward(
      p.editCurve, value.value(), static_cast<float>(p.minValue), static_cast<float>(p.maxValue));
    if (p.isEnum)
    {
        v = std::round(v);
    }
    setParamValue(paramId, v, source);
}

float PatchImpl::macroNormalized(int macroId, tiny_optional<int> voiceId) const
{
    const auto& m = metadata.macros[macroId];
    float v;
    if (m.perVoice)
    {
        if (!voiceId.has_value() || voiceId.value() < 0 || voiceId.value() >= kMaxVoicesPerPatch)
        {
            return 0.f; // per-voice macros do not enter a voice-less final value
        }
        v = voiceMacroValues[voiceId.value()][macroId];
    }
    else
    {
        v = macroValues[macroId];
    }
    return v / Patch::kMaxMacroValue;
}

float PatchImpl::finalValue(int paramId, tiny_optional<int> voiceId) const
{
    const auto& p = metadata.params[paramId];
    const float base = values[paramId];
    float add = 0.f;
    float factor = 1.f;
    const float lo = static_cast<float>(p.minValue);
    const float hi = static_cast<float>(p.maxValue);
    bool touched = false;
    for (int mi = 0; mi < static_cast<int>(metadata.macros.size()); mi++)
    {
        for (const auto& mod : metadata.macros[mi].modifiers)
        {
            if (!mod.paramId.has_value() || mod.paramId.value() != paramId)
            {
                continue;
            }
            const float mv = macroNormalized(mi, voiceId);
            if (mv == 0.f)
            {
                continue;
            }
            touched = true;
            if (p.macroOperation == Metadata::Param::MacroOperation::kAdd)
            {
                add += mv * mod.amount * (hi - lo);
            }
            else if (lo > 0.f && hi > lo)
            {
                factor *= std::pow(hi / lo, mv * mod.amount);
            }
            else
            {
                factor *= std::max(0.f, 1.f + mv * mod.amount);
            }
        }
    }
    if (!touched)
    {
        return base;
    }
    float v = clampToParam(paramId, base * factor + add);
    if (p.isEnum)
    {
        v = std::round(v);
    }
    return v;
}

Patch::optional_paramvalue_t PatchImpl::getParamFinalValue(int paramId) const
{
    if (paramId < 0 || paramId >= static_cast<int>(values.size()))
    {
        return std::nullopt;
    }
    return finalValue(paramId, std::nullopt);
}

void PatchImpl::pruneListeners()
{
    listeners.erase(std::remove_if(listeners.begin(),
                                   listeners.end(),
                                   [](const std::weak_ptr<ParamChangeListener>& l)
                                   { return l.expired(); }),
                    listeners.end());
}

void PatchImpl::notify(int paramId, ParamChangeSource source, tiny_optional<int> voiceId)
{
    if (batch)
    {
        return;
    }
    // per-voice values are only sent to the voice they belong to; shared ones go to everybody with each voice's own
    // per-voice macros applied (the listener resolves them)
    for (auto& w : listeners)
    {
        if (auto l = w.lock())
        {
            if (voiceId.has_value())
            {
                l->onParamChange(paramId, finalValue(paramId, voiceId), source, voiceId);
            }
            else
            {
                const int n = std::min(l->getVoiceCount(), kMaxVoicesPerPatch);
                if (n <= 0)
                {
                    l->onParamChange(paramId, finalValue(paramId, std::nullopt), source, std::nullopt);
                }
                for (int v = 0; v < n; v++)
                {
                    l->onParamChange(paramId, finalValue(paramId, v), source, v);
                }
            }
        }
    }
}

void PatchImpl::setParamValue(int paramId, optional_paramvalue_t value, ParamChangeSource source)
{
    if (paramId < 0 || paramId >= static_cast<int>(values.size()))
    {
        return;
    }
    const auto& p = metadata.params[paramId];
    float v;
    if (value.has_value())
    {
        v = value.value();
    }
    else if (p.defaultValue.has_value())
    {
        v = p.defaultValue.value();
    }
    else
    {
        return;
    }
    v = clampToParam(paramId, v);
    if (p.isEnum)
    {
        v = std::round(v);
    }
    values[paramId] = v;
    notify(paramId, source, std::nullopt);
}

void PatchImpl::subscribeParamUpdates(std::weak_ptr<ParamChangeListener> listener)
{
    pruneListeners();
    listeners.push_back(std::move(listener));
}

void PatchImpl::unsubscribeParamUpdates(const ParamChangeListener& listener)
{
    listeners.erase(std::remove_if(listeners.begin(),
                                   listeners.end(),
                                   [&](const std::weak_ptr<ParamChangeListener>& w)
                                   {
                                       auto l = w.lock();
                                       return !l || l.get() == &listener;
                                   }),
                    listeners.end());
}

void PatchImpl::startBatchUpdate()
{
    batch = true;
}

void PatchImpl::finishBatchUpdate()
{
    batch = false;
    for (int p = 0; p < static_cast<int>(values.size()); p++)
    {
        notify(p, ParamChangeSource::kPatchAssign, std::nullopt);
    }
}

void PatchImpl::notifyAll(ParamChangeListener& listener, ParamChangeSource source) const
{
    const int n = std::max(1, std::min(listener.getVoiceCount(), kMaxVoicesPerPatch));
    for (int p = 0; p < static_cast<int>(values.size()); p++)
    {
        for (int v = 0; v < n; v++)
        {
            listener.onParamChange(p, finalValue(p, v), source, v);
        }
    }
}

Patch::optional_paramvalue_t PatchImpl::getMacroValue(int macroId) const
{
    if (macroId < 0 || macroId >= static_cast<int>(metadata.macros.size()))
    {
        return std::nullopt;
    }
    return macroValues[macroId];
}

void PatchImpl::notifyMacroTargets(int macroId, ParamChangeSource source, tiny_optional<int> voiceId)
{
    for (const auto& mod : metadata.macros[macroId].modifiers)
    {
        if (mod.paramId.has_value() && mod.paramId.value() >= 0 &&
            mod.paramId.value() < static_cast<int>(values.size()))
        {
            notify(mod.paramId.value(), source, voiceId);
        }
    }
}

void PatchImpl::setMacroValue(int macroId, optional_paramvalue_t value, tiny_optional<int> voiceId)
{
    if (macroId < 0 || macroId >= static_cast<int>(metadata.macros.size()))
    {
        return;
    }
    const auto& m = metadata.macros[macroId];
    const float lo = m.bipolar ? -Patch::kMaxMacroValue : 0.f;
    const float v = std::clamp(value.has_value() ? value.value() : 0.f, lo, Patch::kMaxMacroValue);
    if (m.perVoice)
    {
        if (!voiceId.has_value() || voiceId.value() < 0 || voiceId.value() >= kMaxVoicesPerPatch)
        {
            return;
        }
        voiceMacroValues[voiceId.value()][macroId] = v;
        notifyMacroTargets(macroId, ParamChangeSource::kMacro, voiceId);
    }
    else
    {
        macroValues[macroId] = v;
        notifyMacroTargets(macroId, ParamChangeSource::kMacro, std::nullopt);
    }
}

void PatchImpl::setMacroModifier(int macroId, int modifierIdx, tiny_optional<int> paramId)
{
    if (macroId < 0 || macroId >= static_cast<int>(metadata.macros.size()) || modifierIdx < 0)
    {
        return;
    }
    auto& mods = metadata.macros[macroId].modifiers;
    int oldParam = -1;
    if (modifierIdx < static_cast<int>(mods.size()) && mods[modifierIdx].paramId.has_value())
    {
        oldParam = mods[modifierIdx].paramId.value();
    }
    if (!paramId.has_value())
    {
        if (modifierIdx < static_cast<int>(mods.size()))
        {
            mods.erase(mods.begin() + modifierIdx);
        }
    }
    else if (paramId.value() >= 0 && paramId.value() < static_cast<int>(values.size()))
    {
        if (modifierIdx < static_cast<int>(mods.size()))
        {
            mods[modifierIdx].paramId = paramId.value();
        }
        else if (modifierIdx == static_cast<int>(mods.size()) &&
                 static_cast<int>(mods.size()) < kMaxModifiers)
        {
            mods.push_back({ paramId.value(), 0.5f });
        }
    }
    if (oldParam >= 0)
    {
        notify(oldParam, ParamChangeSource::kMacro, std::nullopt);
    }
    if (paramId.has_value() && paramId.value() >= 0 && paramId.value() < static_cast<int>(values.size()))
    {
        notify(paramId.value(), ParamChangeSource::kMacro, std::nullopt);
    }
}

void PatchImpl::setMacroModifierAmount(int macroId, int modifierIdx, float amount)
{
    if (macroId < 0 || macroId >= static_cast<int>(metadata.macros.size()))
    {
        return;
    }
    auto& mods = metadata.macros[macroId].modifiers;
    if (modifierIdx < 0 || modifierIdx >= static_cast<int>(mods.size()))
    {
        return;
    }
    mods[modifierIdx].amount = std::clamp(amount,
                                          -Metadata::Macro::ParamModifier::kMaxAmount,
                                          Metadata::Macro::ParamModifier::kMaxAmount);
    if (mods[modifierIdx].paramId.has_value() && mods[modifierIdx].paramId.value() < static_cast<int>(values.size()))
    {
        notify(mods[modifierIdx].paramId.value(), ParamChangeSource::kMacro, std::nullopt);
    }
}

tiny_optional<float> PatchImpl::getMacroModifierAmount(int macroId, int modifierIdx)
{
    if (macroId < 0 || macroId >= static_cast<int>(metadata.macros.size()))
    {
        return std::nullopt;
    }
    const auto& mods = metadata.macros[macroId].modifiers;
    if (modifierIdx < 0 || modifierIdx >= static_cast<int>(mods.size()))
    {
        return std::nullopt;
    }
    return mods[modifierIdx].amount;
}

void PatchImpl::clearMacroModifiers()
{
    for (auto& m : metadata.macros)
    {
        m.modifiers.clear();
    }
    for (int p = 0; p < static_cast<int>(values.size()); p++)
    {
        notify(p, ParamChangeSource::kMacro, std::nullopt);
    }
}

void PatchImpl::changeMacroModifierAmount(int macroId, int modifierIdx, paramvalue_t value)
{
    if (macroId < 0 || macroId >= static_cast<int>(metadata.macros.size()))
    {
        return;
    }
    const auto& mods = metadata.macros[macroId].modifiers;
    if (modifierIdx < 0 || modifierIdx >= static_cast<int>(mods.size()) || !mods[modifierIdx].paramId.has_value())
    {
        return;
    }
    const float range = rangeOf(mods[modifierIdx].paramId.value());
    if (range == 0.f)
    {
        return;
    }
    setMacroModifierAmount(macroId, modifierIdx, mods[modifierIdx].amount + value / range);
}

void PatchImpl::setMacroBipolar(int macroId, bool bipolar)
{
    if (macroId < 0 || macroId >= static_cast<int>(metadata.macros.size()))
    {
        return;
    }
    auto& m = metadata.macros[macroId];
    if (m.bipolar == bipolar)
    {
        return;
    }
    m.bipolar = bipolar;
    macroValues[macroId] = 0.f;
    for (auto& v : voiceMacroValues)
    {
        v[macroId] = 0.f;
    }
    notifyMacroTargets(macroId, ParamChangeSource::kMacro, std::nullopt);
}

void PatchImpl::setMacroDisplayName(int macroId, std::string name)
{
    if (macroId < 0 || macroId >= static_cast<int>(metadata.macros.size()))
    {
        return;
    }
    if (name.size() > static_cast<size_t>(Metadata::Macro::kMaxDisplayNameLen))
    {
        name.resize(Metadata::Macro::kMaxDisplayNameLen);
    }
    metadata.macros[macroId].displayName = std::move(name);
}

void PatchImpl::assignValues(const std::vector<float>& v)
{
    for (size_t i = 0; i < values.size() && i < v.size(); i++)
    {
        values[i] = clampToParam(static_cast<int>(i), v[i]);
        if (metadata.params[i].isEnum)
        {
            values[i] = std::round(values[i]);
        }
    }
}

const polysynth::Sample* PatchImpl::getSample(int) const
{
    return nullptr; // the XXL voices have no sample slots
}

polysynth::LoadStatus PatchImpl::loadSample(int, const std::string&)
{
    return polysynth::LoadStatus::kFileError;
}

void PatchImpl::setSequence(const Sequencer::Sequence& steps, int idx)
{
    if (idx >= 0 && idx < static_cast<int>(sequencer.sequences.size()))
    {
        sequencer.sequences[idx] = steps;
    }
}

void PatchImpl::setSequencerConfig(const Sequencer::Config& config)
{
    sequencer.config = config;
}

/* ---------------------------------------------------------------- file image
 *   "XPAT" u16 version u16 0 | i32 typeId | u32 nParams | f32 params[nParams] | u32 nMacros |
 *   per macro: u8 bipolar | u8 nameLen | name | u8 nMods | per modifier: i16 paramId, f32 amount
 * Macro values are not stored (they live in the project's synth settings). */
void PatchImpl::serialize(std::vector<uint8_t>& out) const
{
    out.clear();
    out.insert(out.end(), kMagic, kMagic + 4);
    put16(out, kFileVersion);
    put16(out, 0);
    put32(out, static_cast<uint32_t>(metadata.instrumentTypeId));
    put32(out, static_cast<uint32_t>(values.size()));
    for (float v : values)
    {
        putF(out, v);
    }
    put32(out, static_cast<uint32_t>(metadata.macros.size()));
    for (const auto& m : metadata.macros)
    {
        out.push_back(m.bipolar ? 1 : 0);
        const size_t len = std::min<size_t>(m.displayName.size(), 255);
        out.push_back(static_cast<uint8_t>(len));
        out.insert(out.end(), m.displayName.begin(), m.displayName.begin() + len);
        const size_t nm = std::min<size_t>(m.modifiers.size(), kMaxModifiers);
        out.push_back(static_cast<uint8_t>(nm));
        for (size_t i = 0; i < nm; i++)
        {
            put16(out, static_cast<uint16_t>(m.modifiers[i].paramId.has_value() ? m.modifiers[i].paramId.value() : -1));
            putF(out, m.modifiers[i].amount);
        }
    }
}

bool PatchImpl::readTypeId(const uint8_t* data, size_t size, int& typeId)
{
    if (size < 12 || std::memcmp(data, kMagic, 4) != 0)
    {
        return false;
    }
    Reader r{ data, size, 8 };
    typeId = static_cast<int>(r.u32());
    return !r.bad;
}

bool PatchImpl::deserialize(const uint8_t* data, size_t size)
{
    int fileType = 0;
    if (!readTypeId(data, size, fileType) || fileType != metadata.instrumentTypeId)
    {
        return false;
    }
    Reader r{ data, size, 12 };
    const uint32_t np = r.u32();
    if (r.bad || np > 4096)
    {
        return false;
    }
    std::vector<float> nv(values.size());
    for (size_t i = 0; i < nv.size(); i++)
    {
        const auto& d = metadata.params[i].defaultValue;
        nv[i] = d.has_value() ? d.value() : 0.f;
    }
    for (uint32_t i = 0; i < np; i++)
    {
        const float f = r.f();
        if (i < nv.size() && std::isfinite(f))
        {
            nv[i] = clampToParam(static_cast<int>(i), f);
            if (metadata.params[i].isEnum)
            {
                nv[i] = std::round(nv[i]);
            }
        }
    }
    const uint32_t nm = r.u32();
    if (r.bad || nm > 64)
    {
        return false;
    }
    std::vector<Metadata::Macro> macros = metadata.macros;
    for (auto& m : macros)
    {
        m.modifiers.clear();
    }
    for (uint32_t mi = 0; mi < nm; mi++)
    {
        const bool bip = r.u8() != 0;
        const uint8_t len = r.u8();
        std::string name;
        for (uint8_t i = 0; i < len; i++)
        {
            name.push_back(static_cast<char>(r.u8()));
        }
        const uint8_t nmod = r.u8();
        std::vector<Metadata::Macro::ParamModifier> mods;
        for (uint8_t i = 0; i < nmod && i < kMaxModifiers; i++)
        {
            const int16_t pid = static_cast<int16_t>(r.u16());
            const float amount = r.f();
            if (pid >= 0 && pid < static_cast<int>(values.size()) && std::isfinite(amount))
            {
                mods.push_back({ static_cast<int>(pid),
                                 std::clamp(amount,
                                            -Metadata::Macro::ParamModifier::kMaxAmount,
                                            Metadata::Macro::ParamModifier::kMaxAmount) });
            }
        }
        if (r.bad)
        {
            return false;
        }
        if (mi < macros.size())
        {
            macros[mi].bipolar = bip;
            if (name.size() > static_cast<size_t>(Metadata::Macro::kMaxDisplayNameLen))
            {
                name.resize(Metadata::Macro::kMaxDisplayNameLen);
            }
            macros[mi].displayName = std::move(name);
            macros[mi].modifiers = std::move(mods);
        }
    }
    // commit
    values = std::move(nv);
    metadata.macros = std::move(macros);
    return true;
}
} // namespace xxl
