# Changelog

## 0.1.1 (2026-11-04) - pre-release, not device-tested
- The Force's 64 pads: fixed PLAY and CONTROL modes (pad 8 toggles; PAD MODE parameter). PLAY = the Tracker pad grid across all 12 columns, CONTROL = one Tracker button per pad. The pad note numbers (36-99, row-major) are an assumption.

## 0.1.0 (2026-11-03) - first release, pre-release, not device-tested
- Polyend Tracker+ firmware core (tracker-ce) ported to a Force/MPC VST2 plugin, ARMv7.
- 26 instrument types: 19 ZYNTH voices, 6 LXRXXL 0.5.10 drum voices, Polyend CHIP; BAZZLINE bass bank, delay, gate/duck.
- Native MPC skin (TUI.json) with live text boxes, the Tracker's Original theme, waveform columns, icons.
- Data wheel, Q-Links, buttons, pads, host sync. mpc-vst-plugins wrapper with the Q-Link/automation fix (#486-#489).
- Packaged release (install.sh, uninstall.sh) with apostrophe-safe tooling, passes catalog_check.
- Known: nothing has run on a Force; see README "Known limits".
