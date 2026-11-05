# TrackerXXL user guide (0.2.0)

> 0.2.0 has **not been run on a Force yet**. This guide describes what the code is built to do. Where it says "should", that is untested.

## 1. Install and load
1. Unzip `TrackerXXL-0.2.0-mpc-armv7.zip`, follow `INSTALL.md` (root shell access to the Force/MPC is needed; save your project first, the installer restarts MPC).
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
| **Pads 1-16** (host parameters) | The Tracker's pad grid. PAD COLUMNS picks which 4 of the 12 pad columns (1-4, 5-8, 9-12) the 4x4 pads cover |
| **The Force's 64 pads** | Two fixed modes, see section 2a |
| **HOST SYNC** (default on) | Follows the MPC transport, tempo and song position |
| **Touch** | Taps on the screen are turned into cursor key taps (60 ms); the firmware has no touch of its own |

## 2-. Pages
(Not in 0.2.0: the CONTROLS page is switched off until a Force confirms it loads.) The skin has two pages: **TRACKER** (the Tracker's screen) and **CONTROLS** (touch buttons: BTN 1-8, PARAMS, PERFORM, FILE, CONFIG, MASTER, SAMPLE LOAD / PLAY / EDIT / REC, SONG, NOTE, INSTR, VOLUME, FX, PATTERN, cursor, PLAY, STOP, SHIFT, EDIT, COPY, INSERT, DELETE, WHEEL -/+). Change page with the Force's page buttons or the tabs at the bottom. Button positions are in `port/skin/layout/layout.csv` (screen `controls`).

## 2a. Pad modes (the Force's 64 pads)
The 64 pads are read as MIDI notes 36-99, row by row from the bottom-left pad (note 36) to the top-right (note 99). **This note layout is an assumption: it has not been checked on a Force.** Pad 8 (bottom-right, note 43) toggles **PAD MODE** in both modes; the mode is also the PAD MODE parameter (PLAY / CONTROL). Switching releases anything held.

**PLAY (default):** the pads are the Tracker's pad grid, so they play and record notes and pick things exactly as the firmware does on each screen. The bottom 4 rows are grid columns 1-8 and the top 4 rows are columns 5-12 (columns 5-8 are on both halves).

**CONTROL:** every pad is one Tracker button (top row is row 8):

| Row | Pads 1 to 8 (left to right) |
|---|---|
| 8 (top) | BTN 1, BTN 2, BTN 3, BTN 4, BTN 5, BTN 6, BTN 7, BTN 8 |
| 7 | PARAMS, PERFORM, FILE, CONFIG, MASTER, SAMPLE LOAD, SAMPLE PLAY, SAMPLE EDIT |
| 6 | SAMPLE REC, SONG, NOTE, INSTRUMENT, VOLUME, FX, PATTERN, (empty) |
| 5 | UP, DOWN, LEFT, RIGHT, PLAY, STOP, SHIFT (hold), EDIT |
| 4 | COPY, INSERT, DELETE, (empty), (empty), WHEEL -1, WHEEL +1, (empty) |
| 1-3 | (empty) |

Pad lights are not driven, so the pads do not show which mode or function they have.

## 3. Screens
The pattern editor keeps the cursor on the middle row (the 8th of 15) and the grid scrolls around it every step, as on the Tracker. Other screens: song, project / file, config, master, performance, instrument and synth editors, sample editor / player / importer / recorder (the waveform is drawn as picture columns). The 216 reference states are in `docs/screens/`. 0.2.0 generates pattern view mode 0 only; some widget types are partial (see the notes).

## 4. Voices
Pick the instrument type in the instrument editor (26 types).
- **ZYNTH voices** (19): ANALOG, TRIO, FM6, FM4, PHASE, WAVE, CHIP, MACRO, WHEEL, VOICE, PHYS, GRAIN, NOISE, LXR TONE, LXR METAL, BAZZ, DRONE, TEXTURE, FX. Each has 20 controls and a starter bank; the BAZZLINE bass bank (8 sounds), delay and gate/duck come from BAZZLINE.
- **Drums:** LXRXXL 0.5.10 voices and kits. Notes are +3 semitones, open hat from note 72.
- Patches are plain files: `Patches/<type>/<name>.patch`, `Workspace/<n>.patch`.

## 5. Known problems
See "Known limits" in the README and `docs/PORTING_NOTES.md`. Report anything you see at https://github.com/sunskiefer/TrackerXXL/issues with what you did and what the screen showed.

## 6. About the plugin wrapper
TrackerXXL uses the mpc-vst-plugins wrapper including its fix for the Q-Link / automation display update (#486-#489), so Q-Link and automation moves refresh the screen. The installer and release tooling escape the apostrophe in "L'Cronx" everywhere.
