/* TrackerXXL runtime: the public (non-firmware) API of the Linux platform + execution model. */
#pragma once
#include <stdint.h>

namespace xxl
{
/* Input state, readable from any thread. */
bool buttonDown(int fwButtonIndex);        /* interfaceButton* index (0..127) */
bool padDown(int fwPadIndex);              /* 0..47: row*12 + column, row 0 = top */
uint8_t ledLevel(int ledIndex);            /* what the firmware last wrote with Leds::setLed (0 = off, else brightness) */
int padColumnsOffset();                    /* PAD COLUMNS option * 4 */
bool hostSyncOn();

/* Runtime health counters (atomics), for get_param("stat_*") and the README's verification notes. */
struct Counters
{
    uint32_t renderBlocks;
    uint32_t renderSkippedNotReady;   /* silence because the firmware was still booting */
    uint32_t renderSkippedGate;       /* silence because the UI thread held the audio-interrupt gate > 2 ms */
    uint32_t uiLoops;
    uint32_t eventsDropped;
    uint32_t midiDropped;
    uint32_t midiOutDropped;          /* MIDI the firmware tried to send (the host ignores a VST's MIDI out) */
};
Counters counters();

/* True once Tracker::init() has finished on the UI thread. */
bool ready();
void logf(const char* fmt, ...);   /* <root>/trackerxxl.log */
} // namespace xxl
