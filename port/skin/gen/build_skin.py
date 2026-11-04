#!/usr/bin/env python3
"""TrackerXXL skin build:   python3 build_skin.py [--views core|all] [--detail full|low] [--modules a,b] [--out DIR]
Emits  <out>/Plugin Skins/{TUI.json,Q-Links*.json,*.png}, <out>/params.json, <out>/vst.json, <out>/scene_layout.h,
       port/skin/layout/layout.csv (the single editable layout: id,screen,type,x,y,w,h,label,live,px,parameter,...),
       port/skin/overlays/<screen>.png (numbered boxes, number = row order in layout.csv),
and runs third_party/mpc-vst-plugins/tools/skin_check.py on the result."""
import os, sys, json, argparse, subprocess, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C, model, tui, pattern, generic, icons, wave
from ctl_extract import MODULES
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument("--views", default="core", choices=["core", "all"])
ap.add_argument("--detail", default="low", choices=["full", "low"])
ap.add_argument("--modules", default="")
ap.add_argument("--out", default=os.path.join(C.SKIN, "build"))
a = ap.parse_args()

table = json.load(open(os.path.join(C.HERE, "ctl_table.json")))
mods = [m for m in MODULES if not a.modules or m in a.modules.split(",")]
P = model.Params(); controls = []
out = a.out; skd = os.path.join(out, "Plugin Skins"); os.makedirs(skd, exist_ok=True)
S = tui.Skin(P, skd)

# inputs first so their indices are stable: Q-Link 1-4 (focused parameter), data wheel, 8 bottom buttons, title bar page switch
# the platform's own controls keep their VST indices (port/platform/params.json, params.lock): scene slots are appended after them
PLAT = json.load(open(os.path.join(C.PORT, "platform", "params.json")))["params"]
P.preload(PLAT)
pat = pattern.Pattern(P, S, pattern.MODES if a.views == "all" else pattern.CORE_MODES, controls)
pat.declare()
wav = wave.Wave(P, S, controls)
wav.declare()
gen = generic.Generic(P, S, table, mods, controls, detail=a.detail)
gen.plan()
scan = {m: generic.scan_roles(m, f) for m, f in MODULES.items()}
gen.declare(scan)
P.region("icons", [("ic%02d" % i, "opt", dict(n=len(icons.ICONS) + 1)) for i in range(4)])

art = os.path.join(C.SKIN, "art")
pat.build(art)
gen.build(art)
wav.build(art)
icons.build(S, P, controls, art)

# bottom buttons: 8 touch boxes under the grid (button order = hardware under-screen buttons)
bw = C.CANVAS_W // 8
for i in range(8):
    S.button("btn%d" % (i + 1), i * bw, C.CANVAS_H - 44, bw, 44, name="bottom %d" % (i + 1))
    controls.append(model.Widget(id="bottom.%d" % i, screen="all", type="button", x=i * bw, y=C.CANVAS_H - 44, w=bw, h=44,
                                 label="F%d" % (i + 1), live="", px=26, parameter="btn%d" % (i + 1)))

# ---- page 2: CONTROLS (touch buttons for everything the Tracker's panel has and the skin did not draw; placement = layout.csv, screen "controls")
from PIL import Image, ImageDraw, ImageFont
page1 = S.kids
S.kids = []
FONT = os.path.join(C.PORT, "platform", "third_party", "mpc-vst-plugins", "tools", "html_art", "fonts", "TitilliumWeb-SemiBold.ttf")
CTRL_ROWS = [
    [("btn%d" % k, "BTN %d" % k) for k in range(1, 9)],
    [("scr_params", "PARAMS"), ("scr_perf", "PERFORM"), ("scr_file", "FILE"), ("scr_config", "CONFIG"), ("scr_master", "MASTER"), ("scr_sload", "SAMPLE\nLOAD"), ("scr_splay", "SAMPLE\nPLAY"), ("scr_sedit", "SAMPLE\nEDIT")],
    [("scr_srec", "SAMPLE\nREC"), ("scr_song", "SONG"), ("scr_note", "NOTE"), ("scr_instr", "INSTR"), ("scr_vol", "VOLUME"), ("scr_fx", "FX"), ("scr_pattern", "PATTERN"), ("pad_mode_btn", "")],
    [("cur_up", "UP"), ("cur_down", "DOWN"), ("cur_left", "LEFT"), ("cur_right", "RIGHT"), ("play", "PLAY"), ("stop", "STOP"), ("shift", "SHIFT"), ("edit", "EDIT")],
    [("copy", "COPY"), ("insert", "INSERT"), ("delete", "DELETE"), ("wheel_down", "WHEEL -"), ("wheel_up", "WHEEL +"), ("", ""), ("", ""), ("", "")],
]
cw, chh, gap = 154, 112, 6
for r, row in enumerate(CTRL_ROWS):
    for c, (key, label) in enumerate(row):
        if not key or key not in P.index:
            continue
        x, y = 4 + c * (cw + gap), 4 + r * (chh + gap)
        im = Image.new("RGBA", (cw, chh), (22, 22, 22, 255))
        d = ImageDraw.Draw(im)
        d.rectangle((0, 0, cw - 1, chh - 1), outline=(96, 96, 96, 255), width=2)
        f = ImageFont.truetype(FONT, 36)
        d.multiline_text((cw // 2, chh // 2), label, font=f, fill=(235, 235, 235, 255), anchor="mm", align="center", spacing=2)
        png = S.save_png("ctl_%s.png" % key, im)
        S.image(png, x, y, cw, chh, name="ctl plate %s" % key)
        S.button(key, x, y, cw, chh, name="ctl %s" % key)
        controls.append(model.Widget(id="controls.%s" % key, screen="controls", type="button", x=x, y=y, w=cw, h=chh, label=label.replace("\n", " "), live="", px=36, parameter=key))
page2 = S.kids
S.pages = [page1, page2]
S.kids = page1
for q in range(1, 5): S.qlink_anchor("ql%d" % q, 4 + (q - 1) * 300, 2, 290, 32)   # Q-Links 1-4 carry the focused parameter
nbytes = S.write(["ql1", "ql2", "ql3", "ql4"])

json.dump(P.to_json(), open(os.path.join(out, "params.json"), "w"), indent=1)
vj = json.load(open(os.path.join(C.PORT, "platform", "vst.json")))          # the platform's own (name, vendor "L'Cronx", uid, so, MODULE_DIR, HAS_SONG_POS)
vj["custom_skin"] = True                                                    # the skin is this generator's, not the framework's layout.conf renderer
vj["params"] = "params.json"
vj["defines"] = dict(vj.get("defines", {}), HAS_DISPLAY_REV=1, DISPLAY_REV_NO_UPDATE=1, PARAM_TEXT_MAX=64)   # engine-driven values reach MPC by Automate, no screen rebuild
json.dump(vj, open(os.path.join(out, "vst.json"), "w"), indent=1)
model.write_layout_csv(os.path.join(C.SKIN, "layout", "layout.csv"), controls)

# scene_layout.h
H = ["/* generated by port/skin/gen/build_skin.py - do not edit */", "#pragma once",
     "#define SCN_NPARAMS %d" % len(P.list), "#define SCN_FIRST %d  /* first scene parameter; 0..SCN_FIRST-1 are the platform's controls */" % P.n_ext, ""]
for name, (b, n) in P.regions.items(): H.append("#define SCN_%s_BASE %d\n#define SCN_%s_COUNT %d" % (name.upper(), b, name.upper(), n))
H.append(""); H.append(pat.header()); H.append(wav.header()); H.append(gen.header())
H.append("\nstatic const char* const SCN_KEYS[%d] = {%s};" % (len(P.list), ",".join('"%s"' % p["key"] for p in P.list)))
open(os.path.join(out, "scene_layout.h"), "w").write("\n".join(H) + "\n")
open(os.path.join(out, "bridge_handles.inc"), "w").write("/* generated by build_skin.py: the hControl of every pooled control, in SCN_CTL order (owner objects are the firmware's globals) */\n"
    + "".join("case %d: return %s;\n" % (i, h) for i, h in enumerate(gen.handles)))

# numbered overlays (one per screen)
by = collections.OrderedDict()
for i, w in enumerate(controls): by.setdefault(w.screen, []).append((i, w))
os.makedirs(os.path.join(C.SKIN, "overlays"), exist_ok=True)
for scr, ws in by.items():
    im = Image.new("RGBA", (C.CANVAS_W, C.CANVAS_H), (0, 0, 0, 255)); d = ImageDraw.Draw(im)
    for i, w in ws:
        x, y, ww, hh = [int(v) for v in (w.x, w.y, w.w, w.h)]
        d.rectangle([x, y, x + ww, y + hh], outline=(255, 200, 0, 255)); d.text((x + 2, y + 1), str(i), fill=(255, 255, 255, 255))
    im.save(os.path.join(C.SKIN, "overlays", scr + ".png"))

nw = sum(S.count.values())
print("views=%s detail=%s modules=%d params=%d widgets=%d (%s) tui=%d KB controls=%d" % (a.views, a.detail, len(mods), len(P.list), nw, dict(S.count), nbytes // 1024, len(controls)))
r = subprocess.run([sys.executable, os.path.join(C.TOOLS, "skin_check.py"), skd], capture_output=True, text=True)
print("skin_check rc=%d" % r.returncode); import re; o = r.stdout + r.stderr; print(collections.Counter(re.findall(r"TRACKER: (\w+)", o))); print(o[-900:])
