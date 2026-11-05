# TrackerXXL v0.2.0

**The Polyend Tracker workflow on the Akai Force and MPC (MPC OS 3.x), as a VST2 plugin.** Pattern/step programming, instruments, sample loading and synths work as on the Tracker, with our own engine (own code, no Polyend firmware) and the synth voices of
[LXRXXL ZYNTH](https://github.com/sunskiefer/LXRXXL-ZYNTH), the bass sounds of [LXRXXL BAZZLINE](https://github.com/sunskiefer/LXRXXL-BAZZLINE) and the drums of
[LXRXXL](https://github.com/sunskiefer/LXR02XXL) (0.5.10).

> [!IMPORTANT]
> **Status: complete rewrite, not yet tested on a Force.** 0.2.0 replaces the port of Polyend's firmware by our own engine (`port/own`). It was checked on the desktop with a test harness and the skin preview, builds for the Force (ARMv7, glibc 2.31) and passes the catalog check, but **nothing has been run on a real Force yet**. There is no `tested.json` yet. Please report what you see under Issues.

> [!NOTE]
> **Not official Polyend software.** TrackerXXL is an independent re-implementation of the Tracker's workflow with the Tracker's look; it contains no Polyend code. "Polyend" and "Tracker" are trademarks of Polyend Sp. z o.o.; this project is free, not sold, and not affiliated with or endorsed by Polyend, Akai, Erica Synths or Sonic Potions.

## What it is

- **All 12 screens:** pattern (3 pages), instrument (sample and synth), sample loader (drive browser: BACK goes up to the list of all drives, the last folder is remembered), sample playback, sample editor, song, project, config, master (6 pages), performance (track mutes), recorder, image viewer.
- **Same screen, same colours:** the Tracker's art on the Force's native plugin skin.
- **Sound:** our sample engine (one-shot, loops, slices, beat, wavetable, granular, filter, envelope, sends, reverb, delay, EQ, limiter) plus 19 ZYNTH voices, the BAZZLINE bass bank and the LXRXXL 0.5.10 drum voices.
- **Controls:** Q-Links, data wheel, touch, host transport (play, stop, tempo, song position), and the Force's 64 pads (play / control modes). See the [user guide](docs/USER_GUIDE.md).

## Install

Download `TrackerXXL-0.2.0-mpc-armv7.zip` from [Releases](https://github.com/sunskiefer/TrackerXXL/releases), unzip it and follow `INSTALL.md` inside. Load **TrackerXXL** (L'Cronx) on a plugin track. Your data lives in the plugin folder's `TrackerXXL/` folder (Projects, Samples, Themes, Fonts), which the installer keeps on upgrade.

## Known limits

- **Untested on a Force.**
- **One instance at a time** (the others are silent).
- **Not in this version:** song arrangement grid (the song uses a slot list), sample recording, Polyend text keyboard (auto names), a few performance effects (mute buttons only), the Polyend closed synth engines.
- Signed bars are drawn as half-fill; the sample editor has the effects Normalizer, Crop, Reverse, Fade, Amplifier, Overdrive, Delay, Bitcrusher, Silence, Invert.

## Licence

LXR02 Open-Source licence: not for sale or commercial use, and every binary comes with its source (this repository). See [NOTICE.md](NOTICE.md) and [licenses/](licenses/).

## Build from source

ARMv7 only. Needs Zig (`pip install ziglang`), Python 3 with Pillow, numpy and scipy.

```
TXL_CONTROLS_PAGE=1 python3 port/skin/gen/build_skin.py
port/voices/build_voices.sh
port/own/build.sh                                      # platform/build/arm/trackerxxl.so
MPC_RELEASE_TOOLS=<mpc-vst-plugins @ 3845bea>/tools port/package_release.sh 0.2.0
```

Support: [ko-fi.com/lcronx](https://ko-fi.com/lcronx). L'Cronx, Croydon, UK.
