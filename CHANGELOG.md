# Changelog

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
