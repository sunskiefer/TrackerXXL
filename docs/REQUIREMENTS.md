# TrackerXXL requirements (locked 2026-11-03; every agent must follow ALL of this)

Target: Polyend **Tracker+** feature set (16 tracks, synth page, Tracker+ screens) as an Akai Force / MPC OS VST2 plugin, ARMv7 Linux, glibc <= 2.31. NOT the original Tracker.

## Sound
- Synth voices: ALL 19 LXRXXL ZYNTH voices + BAZZLINE bass sound bank, delay and gate/duck. Drums: LXRXXL **0.5.10** (LXR02Open) drum voices and kits. Library: port/voices (trackerxxl_voices.h, libtrackerxxl_voices.a).
- Polyend's closed engines (ACD, FAT, VAP, WTFM, PERC, DIRT) are NOT used; libPolySynth.a/libPolyDSP.a are NEVER linked. LXRXXL 0.6 (GPL-3 Erica engine) is NOT used.
- Sequencer, sample engine (playback, slicing, granular, FX, mixer): Polyend core, exact behaviour.

## Screen / UI
- Same interface, screen, size, colours and engine as the Tracker. The 216 drawn states are the layouts and looks to follow EXACTLY: docs/screens/<screen>/*_800x480.png. Theme: Original.
- Skin-only (MPC native TUI.json skin bound to VST params): every number/name/cell is a plain live TEXT BOX that changes like parameter values do. NO added shading, panels or accent colours on text boxes. Overlay (DRM plane like VADER 0.5.0) only as a last-resort fallback if skin-only does not work on the Force.
- Sample waveform MUST be shown (sample editor, playback, recorder): picture columns driven by parameters (Liminal Hz technique in third_party/mpc-vst-plugins/docs/COMMUNITY_SKINS.md), with markers/playhead in the Tracker's colours.
- The 33 icons in core/extras/"tracker patch edit icons" and "tracker synth model icons" MUST be used.
- The pattern grid behaves exactly as the source code: cursor parked on row index 7 of 15, grid scrolls around it every step, playhead per source. Do NOT replace with paging or any invented behaviour. All behaviour = what the source code does; do not ask the user to explain it.
- Text never small: pattern grid fills the 1280x628 canvas at ~1.59x (text 27 px); Meta4 sizes where knobs exist (name 35.7 px, value 37.4 px, header/readouts 26-34 px).
- One editable layout file (id,x,y,w,h,label,live label,px,parameter) is the single source of layout; ship it with a numbered overlay. Never auto-tidy.
- Touch works (cells, buttons, lists); mainly the macros (Q-Links 1-4 carry the focused parameter); the Force data wheel scrolls the selected row/value everywhere. Knobs turn in place (rotating filmstrips).

## Build / release rules
- ARMv7 build only (Zig, glibc 2.31). Never build or test x86/desktop.
- Vendor name L'Cronx: the apostrophe must be escaped/quoted everywhere (shell, paths, XML, JSON); release fails unless every install.sh/uninstall.sh passes `sh -n`. Use the mpc-vst-plugins wrapper with the Q-Link/automation UpdateDisplay fix (#486-#489), release.py + catalog_check.py escaped (sunskiefer/mpc-vst-plugins @ 3845bea or ZYNTH's vendored copy third_party/mpc-vst-plugins).
- Deliver the real packaged release zip (TrackerXXL-<version>-mpc-armv7.zip: install.sh, uninstall.sh, ARMv7 build, skin) plus source zip; not a bare folder.
- Repo (sunskiefer/TrackerXXL) gets README, user guide, changelog, tested.json, screenshots, ALL licences (Polyend LICENSE incl. Part 2 clone-restriction note, LXR02 licence, MIT for Braids/Plaits/stmlib, Apache 2.0 for DX7 table, SIL OFL), source code, porting notes. Name stays exactly "TrackerXXL".
- NOTHING is pushed, released or opened as a PR until the user says yes. Catalog target when it happens: open-mpc/mpc-vst-plugins (PR with the standing PR text).
- No questions to the user; decide from the source and these rules; report honestly what is unverified (nothing can run here: no emulator, no Force).
