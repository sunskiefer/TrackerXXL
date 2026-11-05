#!/usr/bin/env python3
"""Offline preview of the generated skin: renders build/Plugin Skins/TUI.json for a given scene (parameter values + texts) the way MPC
would show it (IndexedEnabling gating, value labels, images, filmstrip frames).  NOT MPC: the font here is DejaVu Sans scaled by
height/1.52 (MPC's 'height' is 1.52 x the CSS size), so widths differ; positions, colours and gating are exact.
   preview.py <scene.json> <out.png>      scene.json = {"key": value | [value, "text"], ...}"""
import os, sys, json, re
from PIL import Image, ImageDraw, ImageFont
import matplotlib
FONT = os.path.join(os.path.dirname(matplotlib.__file__), "mpl-data", "fonts", "ttf", "DejaVuSans.ttf")
HERE = os.path.dirname(os.path.abspath(__file__)); SKIN = os.path.dirname(HERE)

def load(skin_dir, params_json):
    tui = json.load(open(os.path.join(skin_dir, "TUI.json")))
    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    keys = [p["key"] for p in json.load(open(params_json))["params"]]
    return tui, defs, keys

def render(skin_dir, params_json, scene, out, size=(1280, 628), tab=0):
    tui, defs, keys = load(skin_dir, params_json)
    page = defs[tui["pageData"]["tabs"][tab]["componentName"]]
    val = {}; txt = {}
    for k, v in scene.items():
        if isinstance(v, list): val[k], txt[k] = v[0], v[1]
        else: val[k] = v
    im = Image.new("RGBA", size, (0, 0, 0, 255)); cache = {}
    def pimg(n):
        if n not in cache: cache[n] = Image.open(os.path.join(skin_dir, n)).convert("RGBA")
        return cache[n]
    def pidx(kid):
        for m in kid["handle remapping"]["map"]:
            if m["key"] == "Data": return int(m["value"].split()[-1])
    shown = 0
    for kid in page["componentsData"]:
        b = kid["bounds"]; x, y, w, h = [int(v) for v in b["bounds"].split()]
        ok = True
        for hd in b.get("additionalInvalidatingHandles", []):
            m = re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)", hd)
            if m and int(val.get(keys[int(m.group(3))], 0)) != int(m.group(1)): ok = False
        if not ok: continue
        t = kid["componentData"]["type"]; shown += 1
        if t == "Image":
            src = pimg(kid["componentData"]["data"]["image"]); im.alpha_composite(src.resize((w, h)) if src.size != (w, h) else src, (x, y))
        elif t in defs:
            for ch in defs[t]["componentsData"]:
                ct = ch["componentData"]["type"]; d = ch["componentData"]["data"]
                if ct == "Label" and d.get("type") == "Value":
                    k = keys[pidx(kid)]; s = txt.get(k, "")
                    ts = d["textStyle"]; col = ts["colour"]
                    if col == "00000000" or not s: continue
                    f = ImageFont.truetype(FONT, max(6, int(round(ts["font"]["height"] / 1.52 * 1.15))))
                    dr = ImageDraw.Draw(im); tw = dr.textlength(s, font=f)
                    j = ts["justification"]; tx = x + (0 if j.startswith("left") else (w - tw) / 2 if "horizontallyCentred" in j else w - tw)
                    dr.text((tx, y + h / 2), s, font=f, fill="#" + col[2:], anchor="lm")
                elif ct == "Knob":
                    fr = pimg(d["filmStrip"]); n = d["numFrames"]; fh = fr.height // n
                    i = max(0, min(n - 1, int(val.get(keys[pidx(kid)], 0))))
                    im.alpha_composite(fr.crop((0, i * fh, fr.width, (i + 1) * fh)), (x, y))
    im.convert("RGB").save(out)
    return shown

if __name__ == "__main__":
    build = os.path.join(SKIN, "build")
    render(os.path.join(build, "Plugin Skins"), os.path.join(build, "params.json"), json.load(open(sys.argv[1])), sys.argv[2])
