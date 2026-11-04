# TrackerXXL v0.1.1

**The Polyend Tracker+ on the Akai Force and MPC (MPC OS 3.x), as a VST2 plugin.** The Tracker's own sequencer, sample engine and screens, with the synth voices of
[LXRXXL ZYNTH](https://github.com/sunskiefer/LXRXXL-ZYNTH), the bass sounds of [LXRXXL BAZZLINE](https://github.com/sunskiefer/LXRXXL-BAZZLINE) and the drums of
[LXRXXL](https://github.com/sunskiefer/LXR02XXL) (0.5.10) in place of Polyend's closed synth engines.

> [!IMPORTANT]
> **Status: built, not yet tested on a Force.** 0.1.1 compiles and links for the Force (ARMv7, glibc 2.31), passes the catalog check and the installer
> syntax checks, but **nothing has been run on a real Force yet**: no audio, no screen, no touch. It is released so it can be tried on the Force at once.
> Please report what you see under Issues. There is no `tested.json` yet; it will be added after a device test. See [Known limits](#known-limits).

> [!NOTE]
> **Not official Polyend software.** TrackerXXL is a port of the open-source Polyend Tracker firmware (polyend/tracker-ce). "Polyend" and "Tracker" are
> trademarks of Polyend Sp. z o.o.; this project is free, not sold, and not affiliated with or endorsed by Polyend, Akai, Erica Synths or Sonic Potions.

## What it is

- **The Tracker+ firmware core running as a plugin:** the pattern editor (the cursor parked on the middle row, the grid scrolling around it), the song, project,
  config, master, performance, sample editor / player / importer / recorder and image-viewer screens, and the instrument and synth editors.
- **Same screen, same colours:** the screens are the Tracker's own, reproduced as live text boxes on the Force's native plugin skin (1280×628), in the
  Tracker's *Original* theme, with the Tracker's own icons. The pattern grid fills the page; text is 27 px.
- **Sound:**
  - Sequencer and sample engine (playback, slicing, granular, wavetable-from-sample, mixer, FX): the Polyend core, unchanged.
  - **26 instrument types:** all **19 ZYNTH voices** (ANALOG, TRIO, FM6, FM4, PHASE, WAVE, CHIP, MACRO, WHEEL, VOICE, PHYS, GRAIN, NOISE, LXR TONE,
    LXR METAL, BAZZ, DRONE, TEXTURE, FX), the **6 LXRXXL 0.5.10 drum voices** (DRUM1, DRUM2, DRUM3, SNARE, CLAP/CYMBAL, HIHAT) and Polyend's open CHIP synth.
    Each ZYNTH voice has 20 controls and starter sounds (152), plus the BAZZLINE bass bank (8) and the LXRXXL kits.
- **Controls:** the Force's **Q-Links 1-4**, **data wheel** and **touch**, the host transport (play, stop, tempo, song position) and the Force's 64 pads in two fixed modes (play the grid / press the Tracker's buttons). See the
  [user guide](docs/USER_GUIDE.md).

**The Polyend closed synth engines (ACD, FAT, VAP, WTFM, PERC, DIRT) are not in TrackerXXL**: they live in proprietary Cortex-M libraries that cannot run on the
Force and may not be reverse engineered. Their place is taken by the voices above. The GPL-3 sound engine of LXRXXL 0.6 is not used either (it cannot be
combined with the licences below), so the drums are LXRXXL 0.5.10's.

## Install

Download `TrackerXXL-0.1.1-mpc-armv7.zip` from [Releases](https://github.com/sunskiefer/TrackerXXL/releases), unzip it, and follow `INSTALL.md` inside (run
`install.sh` on the device as root, or copy `portable/L'Cronx - VST - TrackerXXL/` into a Synths folder yourself). Load **TrackerXXL** (L'Cronx) on a plugin
track. Your data (projects, samples, themes, fonts) lives in the plugin folder's `TrackerXXL/` folder, which the installer keeps on upgrade.

## Documentation

| | |
|---|---|
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | How to use it: the controls, every screen, the voices, projects and samples |
| [docs/screens/](docs/screens/) | The 216 screen states, drawn from the Tracker source at 800×480 (what the skin reproduces) |
| [docs/PORTING_NOTES.md](docs/PORTING_NOTES.md) | All porting information: the architecture, every patch to Polyend's source, the stubs, what is unverified |
| [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) | The decisions the port was built to |
| [CHANGELOG.md](CHANGELOG.md), [NOTICE.md](NOTICE.md), [licenses/](licenses/) | Changes, third-party notices, every licence text |
| `port/*/README.md` | Platform layer, voice library, synth layer |

## Known limits

- **Untested on a Force.** Expect first-run problems; the notes list what is unverified (boot of the firmware, audio, the data wheel, host sync, file system,
  CPU load, 14 MB of static RAM, widget count of the skin).
- **One instance at a time.** The firmware is a set of global objects, so a second TrackerXXL instance is silent.
- **Two threads.** The screen/logic loop and the audio run on separate threads; on the original chip they shared one core. If it clicks or crashes while editing
  during playback, report it.
- **Skin screen.** About 7,500 widgets and 1,983 parameters; the screen redraws on change only and is rate limited. The font is Roboto (the Force has no monospace
  font); the waveform is drawn as picture columns, quantised. Pattern view mode 0 only is generated; the other view modes are not.
- **Touch** is emulated through the firmware's cursor keys (the firmware has no touch).
- **Not implemented:** pitch bend and program change into the synth, the SDK sequencer, grid mapper and sample store of the Polyend synth SDK, the Tracker Mini
  layouts, USB audio/MIDI hardware, the radio and battery.
- **Drums:** notes land on 32-frame boundaries (up to 31 frames late); no choke; no percussion section lights.

## Licences

TrackerXXL combines parts under different licences, and the strictest one governs the binary: **the LXR02 Open-Source licence — not for sale or commercial
use, and every binary comes with its complete source** (each release has a `-source.zip`). See [NOTICE.md](NOTICE.md) and [licenses/](licenses/):

| Part | Licence |
|---|---|
| Polyend Tracker core (`core/`) | MIT, plus Polyend's *Wholesale Commercial Clone Restriction* (Part 2): free distribution only, no sale of a Tracker port, no Polyend trademarks. Patched; every change is in `docs/PORTING_NOTES.md` |
| Voice library: LXR02Open drums, ZYNTH and BAZZLINE voices (`port/voices`) | LXR02 Open-Source licence (non-commercial, source with binaries) |
| Braids, Plaits, stmlib (Mutable Instruments) | MIT |
| DX7 algorithm table (music-synthesizer-for-android) | Apache 2.0 |
| Polyend synth SDK headers (`port/synth/include`) | MIT (Polyend Part 3) |
| mpc-vst-plugins wrapper | MIT |
| Liberation Sans bitmap fonts, Roboto | SIL OFL 1.1 / Apache 2.0 (see notices) |

Polyend's proprietary `libPolySynth.a` and `libPolyDSP.a` are **not** used or linked.

## Build from source

ARMv7 only (the Force); there is no desktop build. Needs Zig (`pip install ziglang`), Python 3 with Pillow, numpy and scipy.

```
python3 -I port/skin/gen/build_skin.py --views core --detail low    # skin + parameter table
port/platform/build.sh                                              # build/arm/trackerxxl.so
MPC_RELEASE_TOOLS=<mpc-vst-plugins @ 3845bea>/tools port/package_release.sh 0.1.1
```

Support: [ko-fi.com/lcronx](https://ko-fi.com/lcronx). L'Cronx, Croydon, UK.
