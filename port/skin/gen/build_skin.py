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
mods = [m for m in MODULES if m != "imageViewer" and (not a.modules or m in a.modules.split(","))]   # the image viewer is not part of the plugin
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
# Performance page: the firmware's 12 multi-line columns become a name label + a 4-row list per column (our own engine drives them)
_pm = [c for c in table["performanceMode"] if c["type"] not in ("cLabel", "cMultiLabel") or (c.get("var") or "").startswith("label[")]
for _i in range(12):
    _x = 42 + 65 * _i
    _pm.append(dict(type="cLabel", var="fxName[%d]" % _i, cond=False, x=_x, y=162, w=62, h=70, style=524, value=0, text=None, data=0, colors="display.getTheme.perfFxNameTextLabelColors"))
    _pm.append(dict(type="cList", var="col[%d]" % _i, cond=False, x=_x - 31, y=34, w=62, h=124, style=8448, value=0, text=0, data=0, colors=0))
table["performanceMode"] = _pm
gen = generic.Generic(P, S, table, mods, controls, detail=a.detail)
gen.plan()
scan = {m: generic.scan_roles(m, f) for m, f in MODULES.items()}
gen.declare(scan)
P.region("icons", [("ic%02d" % i, "opt", dict(n=len(icons.ICONS) + 1)) for i in range(4)])

art = os.path.join(C.SKIN, "art")
S.kids = []; pat.build(art); pat_kids = S.kids
gen.build(art)
S.kids = []; wav.build(art); wave_kids = S.kids
S.kids = []; icons.build(S, P, controls, art); icon_kids = S.kids
S.kids = []

# ---- one Force tab per screen (module index = screen id); the last two are touch pages for the panel buttons
from PIL import Image, ImageDraw, ImageFont
FONT = os.path.join(C.PORT, "platform", "third_party", "mpc-vst-plugins", "tools", "html_art", "fonts", "TitilliumWeb-SemiBold.ttf")
SCREEN_TABS = [("PATTERN", 1), ("INSTRUMENT", 6), ("SAMPLE LOAD", 3), ("SAMPLE PLAY", 4), ("SAMPLE EDIT", 2), ("SONG", 7), ("PROJECT", 8),
               ("CONFIG", 9), ("MASTER", 10), ("PERFORM", 11)]
tabs = []
bw = C.CANVAS_W // 8
for tname, mi in SCREEN_TABS:
    S.kids = []
    kids = []
    if mi == 1: kids += pat_kids
    kids += gen.pages.get((mi, True), []) + gen.pages.get((mi, False), [])
    if mi in (2, 4): kids += wave_kids
    if mi == 1: kids += icon_kids
    S.kids = []
    for i in range(8):
        S.button("bs%02d_%d" % (mi, i + 1), i * bw, C.CANVAS_H - 44, bw, 44, name="bottom %d" % (i + 1))
    kids += S.kids
    S.kids = []
    S.wheel_anchor("wh%02d" % mi, "we%02d" % mi, 196, 2, 116, 32)   # the focused control: the Force data wheel turns the parameter that has the focus, so the wheel needs one on every tab
    for q in range(1, 9): S.qlink_anchor("mt%02d_%d" % (mi, q), 320 + (q - 1) * 120, 2, 116, 32)
    kids += S.kids
    tabs.append(dict(name=tname, kids=kids, qkeys=["mt%02d_%d" % (mi, q) for q in range(1, 9)]))
for i in range(8):
    controls.append(model.Widget(id="bottom.%d" % i, screen="all", type="button", x=i * bw, y=C.CANVAS_H - 44, w=bw, h=44,
                                 label="F%d" % (i + 1), live="", px=26, parameter="btn%d" % (i + 1)))

CTRL_PAGES = [
    ("TRANSPORT", [
        [("play", "PLAY"), ("stop", "STOP"), ("edit", "EDIT"), ("shift", "SHIFT")],
        [("cur_up", "UP"), ("cur_down", "DOWN"), ("cur_left", "LEFT"), ("cur_right", "RIGHT")],
        [("copy", "COPY"), ("insert", "INSERT"), ("delete", "DELETE"), ("", "")],
        [("wheel_down", "WHEEL -"), ("wheel_up", "WHEEL +"), ("", ""), ("", "")],
    ]),
    ("FUNCTION", [
        [("btn1", "BTN 1"), ("btn2", "BTN 2"), ("btn3", "BTN 3"), ("btn4", "BTN 4")],
        [("btn5", "BTN 5"), ("btn6", "BTN 6"), ("btn7", "BTN 7"), ("btn8", "BTN 8")],
    ]),
]
cw, chh, gap = 308, 148, 8
for pname, prows in CTRL_PAGES:
    S.kids = []
    for r, row in enumerate(prows):
        for c, (key, label) in enumerate(row):
            if not key or key not in P.index:
                continue
            x, y = 4 + c * (cw + gap), 4 + r * (chh + gap)
            im = Image.new("RGBA", (cw, chh), (22, 22, 22, 255))
            d = ImageDraw.Draw(im)
            d.rectangle((0, 0, cw - 1, chh - 1), outline=(96, 96, 96, 255), width=2)
            f = ImageFont.truetype(FONT, 48)
            d.multiline_text((cw // 2, chh // 2), label, font=f, fill=(235, 235, 235, 255), anchor="mm", align="center", spacing=2)
            png = S.save_png("ctl_%s.png" % key, im)
            S.image(png, x, y, cw, chh, name="ctl plate %s" % key)
            S.button(key, x, y, cw, chh, name="ctl %s" % key)
            controls.append(model.Widget(id="controls.%s" % key, screen="controls", type="button", x=x, y=y, w=cw, h=chh, label=label.replace("\n", " "), live="", px=48, parameter=key))
    tabs.append(dict(name=pname, kids=S.kids, qkeys=[]))
nbytes = S.write(tabs)

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
open(os.path.join(out, "own_ctl.h"), "w").write("/* generated by build_skin.py: every pooled control by name (own engine) */\n#pragma once\nstruct OwnCtl { short mi; char cls; short pool; const char* name; short x, y, w, h; unsigned char nva, nvb, nvars, rows, nplaces; };\nstatic const OwnCtl OWN_CTL[%d] = {\n%s};\n" % (len(gen.own), ",\n".join(gen.own)))
open(os.path.join(out, "own_places.json"), "w").write(json.dumps(gen.own_places))
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
