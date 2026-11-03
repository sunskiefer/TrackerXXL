/* Calls into the Polyend firmware that need core headers; implemented in FirmwareGlue.cpp (the only file besides the
 * firmware itself that includes mtSequencer.h / mtConfig.h). Runtime.cpp stays free of firmware headers. */
#pragma once
#include <stdint.h>

namespace xxl
{
class SwitchGroupImpl;
void fw_encoder(int clicks);                 /* onPotChange(0, clicks) */
bool fw_isPlaying();                         /* sequencer.isPlay() */
void fw_applyHostSync(bool on);              /* clkIn/transportIn <- USB (host), or the user's saved values */
void fw_buttonCallbacksInstall(SwitchGroupImpl* buttons, SwitchGroupImpl* pads);   /* SwitchGroupImpl* x2: onButtonPush ... onPadHold */
void fw_gateEnterRender();                   /* mark the calling thread as the audio "interrupt" thread */
void fw_gateLeaveRender();
bool fw_gateAcquire();                       /* host thread: lock out disableAudioInterrupt() sections; false after ~2 ms */
void fw_gateRelease();
bool fw_audioMuted();
extern const int kFwPlayButton;
} // namespace xxl
