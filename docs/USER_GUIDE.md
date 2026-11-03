# TrackerXXL user guide (0.1.0)

> 0.1.0 has **not been run on a Force yet**. This guide describes what the code is built to do. Where it says "should", that is untested.

## 1. Install and load
1. Unzip `TrackerXXL-0.1.0-mpc-armv7.zip`, follow `INSTALL.md` (root shell access to the Force/MPC is needed; save your project first, the installer restarts MPC).
2. Add **TrackerXXL** (L'Cronx) to a track from the plugin browser (Instruments). Its screen appears in the plugin view.
3. Your files: `/sdcard/Synths/L'Cronx - VST - TrackerXXL/TrackerXXL/` holds `Projects/`, `Samples/`, `Themes/`, `Fonts/`. Put samples in `Samples/`, then load them from the Tracker's file screen. The installer keeps these folders on upgrade and uninstall.
4. Only one TrackerXXL can run at a time.

## 2. Controls
The Tracker's hardware is mapped to the plugin's host parameters, so everything is also automatable and MIDI-mappable.

| Force / MPC | Does |
|---|---|
| **Data wheel** (DATA WHEEL; WHEEL +1 / -1 for taps) | Scrolls the selected row or list and changes the selected value, on every screen |
| **Q-Link 1** (QL1 VALUE) | Changes the focused value, one step per click |
| **Q-Link 2** (QL2 COARSE) | Same value, four steps per click |
| **Q-Link 3 / 4** (FOCUS H / FOCUS V) | Move the focus left/right and up/down |
| **BTN 1-8** | The Tracker's 8 function buttons under the screen (labels are on the screen) |
| **UP DOWN LEFT RIGHT** | Cursor |
| **PLAY / STOP** | Tracker transport |
| **SHIFT** (latch) **EDIT COPY INSERT DELETE** | The Tracker's modifier and edit keys |
| **Screen buttons** PARAMS, PERFORM, FILE, CONFIG, MASTER, SAMPLE LOAD / PLAY / EDIT / REC, SONG, NOTE, INSTRUMENT, VOLUME, FX, PATTERN | Jump to that screen |
| **Pads 1-16** | The Tracker's pad grid (MIDI notes 36-51). PAD COLUMNS picks which 4 of the 12 pad columns (1-4, 5-8, 9-12) the 4x4 pads cover |
| **HOST SYNC** (default on) | Follows the MPC transport, tempo and song position |
| **Touch** | Taps on the screen are turned into cursor key taps (60 ms); the firmware has no touch of its own |

## 3. Screens
The pattern editor keeps the cursor on the middle row (the 8th of 15) and the grid scrolls around it every step, as on the Tracker. Other screens: song, project / file, config, master, performance, instrument and synth editors, sample editor / player / importer / recorder (the waveform is drawn as picture columns). The 216 reference states are in `docs/screens/`. 0.1.0 generates pattern view mode 0 only; some widget types are partial (see the notes).

## 4. Voices
Pick the instrument type in the instrument editor (26 types).
- **ZYNTH voices** (19): ANALOG, TRIO, FM6, FM4, PHASE, WAVE, CHIP, MACRO, WHEEL, VOICE, PHYS, GRAIN, NOISE, LXR TONE, LXR METAL, BAZZ, DRONE, TEXTURE, FX. Each has 20 controls and a starter bank; the BAZZLINE bass bank (8 sounds), delay and gate/duck come from BAZZLINE.
- **Drums:** LXRXXL 0.5.10 voices and kits. Notes are +3 semitones, open hat from note 72.
- Patches are plain files: `Patches/<type>/<name>.patch`, `Workspace/<n>.patch`.

## 5. Known problems
See "Known limits" in the README and `docs/PORTING_NOTES.md`. Report anything you see at https://github.com/sunskiefer/TrackerXXL/issues with what you did and what the screen showed.

## 6. About the plugin wrapper
TrackerXXL uses the mpc-vst-plugins wrapper including its fix for the Q-Link / automation display update (#486-#489), so Q-Link and automation moves refresh the screen. The installer and release tooling escape the apostrophe in "L'Cronx" everywhere.
