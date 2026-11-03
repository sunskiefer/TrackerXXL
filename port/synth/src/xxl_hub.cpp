#include "xxl_hub.h"

#include <cstring>

namespace xxl
{
Hub& Hub::get()
{
    static Hub hub;
    return hub;
}

Hub::Hub()
{
    TvConfig cfg;
    tv_config_init(&cfg);
    cfg.synth_slots = kHubSynthSlots;
    cfg.drum_voices = TV_NUM_DRUM_VOICES;
    cfg.sample_rate = 44100.f;
    tv = tv_create(&cfg);
}

int Hub::acquireSynthSlot(int voiceId)
{
    if (!tv)
    {
        return kNoSlot;
    }
    for (int i = 0; i < kHubSynthSlots; i++)
    {
        if (!slotUsed[i])
        {
            slotUsed[i] = true;
            tv_slot_kill(tv, i);
            tv_slot_set_voice(tv, i, voiceId);
            tv_set_level(tv, i, 0.8f); // the sound's default LEVEL; gain and pan belong to the Tracker's mixer
            tv_set_pan(tv, i, 0.5f);
            tv_set_send(tv, i, 0.f);
            return i;
        }
    }
    return kNoSlot;
}

void Hub::releaseSynthSlot(int slot)
{
    if (tv && slot >= 0 && slot < kHubSynthSlots && slotUsed[slot])
    {
        tv_slot_kill(tv, slot);
        slotUsed[slot] = false;
    }
}

void Hub::recomputeFxOwner()
{
    fxOwner = -1;
    for (int p = 0; p < kParts; p++)
    {
        if (drumVoices[p] > 0)
        {
            fxOwner = p;
            return;
        }
    }
}

void Hub::drumVoiceCreated(int part)
{
    if (part >= 0 && part < kParts)
    {
        drumVoices[part]++;
        drumTotal++;
        recomputeFxOwner();
    }
}

void Hub::drumVoiceDestroyed(int part)
{
    if (part >= 0 && part < kParts && drumVoices[part] > 0)
    {
        drumVoices[part]--;
        drumTotal--;
        recomputeFxOwner();
    }
}

TvFxUnit* Hub::acquireFx(int part)
{
    if (part < 0 || part >= kParts)
    {
        return nullptr;
    }
    FxEntry& e = fx[part];
    if (!e.unit)
    {
        e.unit = tv_fxunit_create();
    }
    if (e.unit)
    {
        e.users++;
    }
    return e.unit;
}

void Hub::releaseFx(int part)
{
    if (part < 0 || part >= kParts)
    {
        return;
    }
    FxEntry& e = fx[part];
    if (e.users > 0 && --e.users == 0 && e.unit)
    {
        tv_fxunit_destroy(e.unit);
        e.unit = nullptr;
        e.duckSource = 0;
    }
}

void Hub::setDuckSource(int part, int source)
{
    if (part >= 0 && part < kParts)
    {
        fx[part].duckSource = source;
    }
}

void Hub::notePlayed(int part)
{
    for (int p = 0; p < kParts; p++)
    {
        FxEntry& e = fx[p];
        if (!e.unit)
        {
            continue;
        }
        const int src = e.duckSource == 0 ? p : e.duckSource - 1;
        if (src == part)
        {
            tv_fxunit_duck_trigger(e.unit);
        }
    }
}

void Hub::beginPart(int part, float tempo)
{
    if (!tv)
    {
        return;
    }
    const bool newBlock = part <= lastPart;
    lastPart = part;
    if (!newBlock)
    {
        return;
    }
    if (tempo > 0.f && tempo != bpm)
    {
        bpm = tempo;
        tv_set_tempo(tv, bpm);
        for (auto& e : fx)
        {
            if (e.unit)
            {
                tv_fxunit_set_tempo(e.unit, bpm);
            }
        }
    }
    if (drumTotal > 0)
    {
        // every drum voice is consumed each block (the engine renders all six together; unread rings would go stale)
        for (int v = 0; v < TV_NUM_DRUM_VOICES; v++)
        {
            std::memset(cacheL[v].data(), 0, sizeof(float) * kBlock);
            std::memset(cacheR[v].data(), 0, sizeof(float) * kBlock);
            tv_render_slot(tv, kHubSynthSlots + v, cacheL[v].data(), cacheR[v].data(), kBlock);
        }
        std::memset(fxCacheL.data(), 0, sizeof(float) * kBlock);
        std::memset(fxCacheR.data(), 0, sizeof(float) * kBlock);
        tv_render_drum_fx(tv, fxCacheL.data(), fxCacheR.data(), kBlock);
    }
}

TvInstance* Hub::scratchInstance()
{
    if (!scratch)
    {
        TvConfig cfg;
        tv_config_init(&cfg);
        cfg.synth_slots = 1;
        cfg.drum_voices = TV_NUM_DRUM_VOICES;
        cfg.sample_rate = 44100.f;
        scratch = tv_create(&cfg);
    }
    return scratch;
}
} // namespace xxl
