# Changelog

## 0.1.10 (2026-11-05) - pre-release
- Opening a screen no longer freezes the Force (user report: stays up). Two causes found by comparing with the working DINXXL/VADER/reMixXXL wrappers: (1) after a screen change the wrapper told MPC about every moved value inside one audio block and took the engine lock about 2000 times; it now walks the table in slices and reports at most 6 moved values per block. (2) the audio thread and touch input waited on the lock the screen builder holds while drawing a frame; they now read a small published copy.
- 0.1.9: crash recorder (writes a stack trace to trackerxxl.log), screen-change, button-tap and slow-frame logging, four generated demo samples in TrackerXXL/Samples.
- 0.1.7/0.1.8: firmware starts on the first audio block, threads are joined at unload (load / delete-track crash); CONTROLS page on.
- Not yet confirmed on a Force: sound, sample loading, Force Play starting the sequencer, pad notes.

## 0.1.4 (2026-11-04) - pre-release
- 0.1.2/0.1.3 took the whole Force down when the plugin loaded. The two things added since the version that loaded (0.1.1) are switched off again: the second skin page (CONTROLS) and the never-unload link flag. Build switches: `TXL_CONTROLS_PAGE=1` for the page, `NODELETE=1` for the link flag. The Play/Stop fallback and the pad modes stay.
- Back again: deleting a track that holds TrackerXXL crashed the Force in 0.1.1; that is not fixed in this build.

## 0.1.3 (2026-11-04) - pre-release
- Deleting a track that holds TrackerXXL took the whole MPC down ("recovered from internal error"). Likely cause: MPC unloads the plugin library while the firmware's threads still run inside it. The library is now linked so it is never unloaded (`-z nodelete`). Not yet confirmed on a Force.

## 0.1.2 (2026-11-04) - pre-release, first look at a Force screen
- New CONTROLS page (second skin tab): touch buttons for BTN 1-8, the 15 screen buttons, cursor, play/stop, shift, edit, copy, insert, delete and wheel +/-. Switch pages with the Force's page buttons, so Song, Sample, Instrument, File and the other screens can be reached.
- Play/Stop: when the host gives no valid song position or tempo, HOST SYNC now follows the host's play flag alone (MIDI Start/Stop plus a clock at the last known tempo, 120 until one is seen). Not yet confirmed on a Force.
- Known, not fixed: the bottom button labels (Pattern, Length, Step jump ...) and the title-bar labels stayed blank in the first screenshot; cause not yet known.

## 0.1.1 (2026-11-04) - pre-release, not device-tested
- The Force's 64 pads: fixed PLAY and CONTROL modes (pad 8 toggles; PAD MODE parameter). PLAY = the Tracker pad grid across all 12 columns, CONTROL = one Tracker button per pad. The pad note numbers (36-99, row-major) are an assumption.

## 0.1.0 (2026-11-03) - first release, pre-release, not device-tested
- Polyend Tracker+ firmware core (tracker-ce) ported to a Force/MPC VST2 plugin, ARMv7.
- 26 instrument types: 19 ZYNTH voices, 6 LXRXXL 0.5.10 drum voices, Polyend CHIP; BAZZLINE bass bank, delay, gate/duck.
- Native MPC skin (TUI.json) with live text boxes, the Tracker's Original theme, waveform columns, icons.
- Data wheel, Q-Links, buttons, pads, host sync. mpc-vst-plugins wrapper with the Q-Link/automation fix (#486-#489).
- Packaged release (install.sh, uninstall.sh) with apostrophe-safe tooling, passes catalog_check.
- Known: nothing has run on a Force; see README "Known limits".
