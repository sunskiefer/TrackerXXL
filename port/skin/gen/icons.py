"""The 33 icon PNGs/BMPs of core/extras as picture widgets chosen by a parameter (icon id = option index).
Each icon slot icNN is an option parameter: 0 = hidden, 1..N = which icon; one gated Image per (slot, icon), at the slot's
rect from layout.csv (default: parked along the right edge until the user places them)."""
import os, glob
import common as C
from model import Widget
from PIL import Image
EX = os.path.join(C.CORE, "extras")
ICONS = sorted(glob.glob(os.path.join(EX, "*", "*.png")) + glob.glob(os.path.join(EX, "*", "*.bmp")))
def build(S, P, controls, art):
    names = []
    for n, f in enumerate(ICONS):
        im = Image.open(f).convert("RGBA")
        name = "icon_%02d_%s.png" % (n, os.path.splitext(os.path.basename(f))[0])
        S.save_png(name, im); names.append((name, im.size))
    for slot in range(4):
        x, y = 1040, 120 + slot * 90
        for n, (name, (w, h)) in enumerate(names):
            S.image(name, x, y, w, h, when=S.when("ic%02d" % slot, n + 1), name="icon")
        controls.append(Widget(id="icons.slot%d" % slot, screen="icons", type="icon", x=x, y=y, w=80, h=80, label="", live="icon id 1..%d" % len(names), px="", parameter="ic%02d" % slot))
