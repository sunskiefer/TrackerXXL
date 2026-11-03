"""Sample waveform + markers as picture columns driven by parameters (the Liminal Hz technique of docs/COMMUNITY_SKINS.md), for the
four sample screens (sampleEditor, samplePlayback, sampleImporter, sampleRecorder = every cSpectrum / cPoints / cSlicePoints /
cProgressCursor / cLineIndicator the firmware creates).

cSpectrum (spectrum.cpp): 600 data points per control, int8 upperData/lowerData per x, drawn as a 1 px bar from centre-upper*k to
centre-lower*k, k = height / 109.  Here: WCOLS columns, each = two filmstrip pictures (upper half / lower half), one int slot each
(frame = bar length 0..WLV).  The firmware has at most two spectrum controls (stereo: two of height 109; mono: one of height 218), at x 0
(editor, playback) or x 100 (importer, recorder): six placements, selected by the geometry slot wg0/wg1 (0 = hidden).
cPoints / cSlicePoints / cProgressCursor / cLineIndicator: one marker filmstrip per marker column (frame = what is drawn in that column:
start/end/loop1/loop2 line+flag in white/red(selected)/green(zero-crossed), shaded start-end area, playhead, slice, granular cursor)."""
import os
from PIL import Image, ImageDraw
import common as C
from model import Widget

WCOLS = 100            # 6 source px per column
WLV = 24               # bar length levels per half
MCOLS = 150            # 4 source px per marker column
SRC_W = 600
GEOM0 = [("m0", 0, 116, 218), ("l0", 0, 116, 109), ("m1", 100, 116, 218), ("l1", 100, 116, 109)]   # slot 0: mono / stereo-left at x0 0 | 100
GEOM1 = [("r0", 0, 225, 109), ("r1", 100, 225, 109)]                                                # slot 1: stereo-right
# marker frames: 0 none, 1 shade only, then kinds x (plain, shaded)
KINDS = ["start_w", "start_r", "start_g", "end_w", "end_r", "end_g", "loop1_w", "loop1_r", "loop1_g", "loop2_w", "loop2_r", "loop2_g",
         "play", "granular", "slice_w", "slice_r", "slice_g"]
NMK = 2 + 2 * len(KINDS)
def mk_frame(kind_idx, shade): return 2 + kind_idx * 2 + shade   # kind_idx -1 = none
WHITE = 0xFFFFFF
def rgb(c): return ((c >> 16) & 255, (c >> 8) & 255, c & 255)
def hx(c): return "%06x" % (c & 0xFFFFFF)

class Wave:
    def __init__(self, P, S, controls):
        self.P, self.S, self.controls = P, S, controls
    def declare(self):
        P = self.P
        P.region("wf_geom", [("wg0", "opt", dict(n=1 + len(GEOM0))), ("wg1", "opt", dict(n=1 + len(GEOM1))), ("wmg", "opt", dict(n=3)),
                             ("wnoop", "opt", dict(n=2))])
        P.region("wf_cols", [("wu%d_%03d" % (k, c), "int", dict(max=WLV)) for k in (0, 1) for c in range(WCOLS)]
                 + [("wd%d_%03d" % (k, c), "int", dict(max=WLV)) for k in (0, 1) for c in range(WCOLS)])
        P.region("wf_marks", [("wk%03d" % c, "int", dict(max=NMK - 1)) for c in range(MCOLS)])
    def build(self, art):
        S, P = self.S, self.P
        col = C.ROLES["waveformColors"][0]
        pcols = {"w": C.ROLES.get("pointsColors", [WHITE])[0] if "pointsColors" in C.ROLES else WHITE}
        # ---- waveform strips: one PNG per (frame width, half height, direction)
        made = {}
        def strip(cw, hh, up):
            n = "wv_%dx%d_%s.png" % (cw, hh, "u" if up else "d")
            if n not in made:
                im = Image.new("RGBA", (cw, hh * (WLV + 1)), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
                for f in range(1, WLV + 1):
                    ln = max(1, round(hh * f / WLV))
                    y0 = f * hh + (hh - ln if up else 0)
                    d.rectangle([0, y0, cw - 1, y0 + ln - 1], fill=(*rgb(col), 255))
                S.save_png(n, im); made[n] = 1
            return n
        for slot, geoms in ((0, GEOM0), (1, GEOM1)):
            for gi, (gname, x0, y0, h) in enumerate(geoms):
                half = h // 2
                for c in range(WCOLS):
                    xa, xb = C.tx(x0 + c * SRC_W / WCOLS), C.tx(x0 + (c + 1) * SRC_W / WCOLS)
                    cw = xb - xa
                    yc = C.ty(y0 + half) if True else 0
                    hh = C.ty(y0 + half) - C.ty(y0)
                    S.strip("wu%d_%03d" % (slot, c), strip(cw, hh, True), WLV + 1, xa, C.ty(y0), cw, hh, when=S.when("wg%d" % slot, gi + 1), name="wave up")
                    S.strip("wd%d_%03d" % (slot, c), strip(cw, hh, False), WLV + 1, xa, C.ty(y0 + half), cw, hh, when=S.when("wg%d" % slot, gi + 1), name="wave down")
        # ---- marker strips (two placements: x0 0 | 100), covering y 116 .. 334 plus the flags
        TOP, BOT = 8, 8
        for mg, x0 in ((1, 0), (2, 100)):
            y0, h = 116, 218
            for c in range(MCOLS):
                xa, xb = C.tx(x0 + c * SRC_W / MCOLS), C.tx(x0 + (c + 1) * SRC_W / MCOLS)
                cw = xb - xa
                H = C.ty(y0 + h + BOT) - C.ty(y0 - TOP)
                n = self.marker_png(cw, H, TOP)
                S.strip("wk%03d" % c, n, NMK, xa, C.ty(y0 - TOP), cw, H, when=S.when("wmg", mg), name="marker")
            S.shield("wnoop", C.tx(x0), C.ty(y0 - TOP), C.tw(SRC_W), C.ty(y0 + h + BOT) - C.ty(y0 - TOP), when=S.when("wmg", mg), name="wave shield")
        for slot, geoms in ((0, GEOM0), (1, GEOM1)):
            for gi, (gname, x0, y0, h) in enumerate(geoms):
                S.shield("wnoop", C.tx(x0), C.ty(y0), C.tw(SRC_W), C.ty(y0 + h) - C.ty(y0), when=S.when("wg%d" % slot, gi + 1), name="wave shield")
        for slot, geoms in ((0, GEOM0), (1, GEOM1)):
            for (gname, x0, y0, h) in geoms:
                self.controls.append(Widget(id="wave.%s" % gname, screen="waveform", type="cSpectrum", x=C.tx(x0), y=C.ty(y0), w=C.tw(SRC_W), h=C.ty(y0 + h) - C.ty(y0),
                                            label="", live="wu%d_NNN wd%d_NNN (%d columns x 2 halves)" % (slot, slot, WCOLS), px="", parameter="wg%d" % slot,
                                            extra="levels=%d src_px_per_column=%d" % (WLV, SRC_W // WCOLS)))
        for mg, x0 in ((1, 0), (2, 100)):
            self.controls.append(Widget(id="wave.markers.x%d" % x0, screen="waveform", type="cPoints/cSlicePoints/cProgressCursor", x=C.tx(x0), y=C.ty(116 - 8), w=C.tw(SRC_W),
                                        h=C.ty(218 + 16), label="", live="wkNNN (%d columns)" % MCOLS, px="", parameter="wmg", extra="frames=%d" % NMK))
    def marker_png(self, cw, H, top):
        n = "wk_%dx%d.png" % (cw, H)
        if n in self.S.images: return n
        pts = C.ROLES["waveformPointsColors"]; slc = C.ROLES["sliceColors"]
        colmap = {"w": pts[0], "r": pts[2], "g": pts[3]}
        # shade between start and end: the firmware adds colors[1] (0x0a0a0a) with SRC_ALPHA,ONE; white at alpha 10/255 gives the same on
        # black and leaves the white waveform white, so it can sit above the waveform pictures
        im = Image.new("RGBA", (cw, H * NMK), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        y_top, y_bot = C.th(top), H - C.th(top)
        def frame_rect(f): return f * H
        for f in range(NMK):
            base = f * H
            shaded = (f == 1) or (f >= 2 and (f - 2) % 2 == 1)
            if shaded: d.rectangle([0, base + y_top, cw - 1, base + y_bot], fill=(255, 255, 255, (pts[1] & 255) + 0))
            if f >= 2:
                k = KINDS[(f - 2) // 2]
                name, _, c = k.partition("_")
                if name == "play": colr = rgb(C.ROLES["waveformProgressColors"][1])
                elif name == "granular": colr = rgb(C.ROLES["granularProgressColors"][0])
                elif name == "slice": colr = rgb({"w": slc[0], "r": slc[1], "g": slc[2]}[c])
                else: colr = rgb(colmap[c])
                if name in ("start", "end"):
                    x = 0 if name == "start" else cw - 1
                    d.line([x, base, x, base + y_bot], fill=(*colr, 255), width=2)
                    sgn = 1 if name == "start" else -1
                    d.polygon([(x, base + 1), (x + sgn * min(cw - 1, C.tw(4)) if sgn > 0 else max(0, x - C.tw(4)), base + C.th(3)), (x, base + C.th(7))], outline=(*colr, 255))
                elif name in ("loop1", "loop2"):
                    x = 0 if name == "loop1" else cw - 1
                    d.line([x, base + y_top, x, base + H - 1], fill=(*colr, 255), width=2)
                    d.polygon([(x, base + y_bot + 1), (x + (C.tw(4) if name == "loop1" else -C.tw(4)), base + y_bot + C.th(4)), (x, base + y_bot + C.th(7))], outline=(*colr, 255))
                elif name == "slice":
                    x = cw // 2
                    d.line([x, base + y_top - (C.th(9) if c == "r" else 0), x, base + y_bot], fill=(*colr, 255), width=2)
                    d.polygon([(x - C.tw(4), base + C.th(1)), (x + C.tw(4), base + C.th(1)), (x, base + C.th(7))], outline=(*colr, 255))
                else:   # play, granular
                    x = cw // 2
                    d.line([x, base + y_top, x, base + y_bot], fill=(*colr, 255), width=2)
        self.S.save_png(n, im)
        return n
    def header(self):
        o = ["/* waveform / marker columns (wave.py) */",
             "#define SCN_WCOLS %d\n#define SCN_WLV %d\n#define SCN_MCOLS %d\n#define SCN_NMK %d\n#define SCN_SRC_W %d" % (WCOLS, WLV, MCOLS, NMK, SRC_W),
             "/* slot 0 geometries: x0,y,h ; slot 1 ; marker frames = 2 + 2*kind + shade, kinds: %s */" % " ".join(KINDS),
             "static const short SCN_WGEOM0[%d][3] = {%s};" % (len(GEOM0), ",".join("{%d,%d,%d}" % (x, y, h) for _, x, y, h in GEOM0)),
             "static const short SCN_WGEOM1[%d][3] = {%s};" % (len(GEOM1), ",".join("{%d,%d,%d}" % (x, y, h) for _, x, y, h in GEOM1)),
             "enum { SCN_MK_START_W = %d, SCN_MK_START_R = %d, SCN_MK_START_G = %d, SCN_MK_END_W = %d, SCN_MK_END_R = %d, SCN_MK_END_G = %d, SCN_MK_LOOP1_W = %d, SCN_MK_LOOP1_R = %d, SCN_MK_LOOP1_G = %d, SCN_MK_LOOP2_W = %d, SCN_MK_LOOP2_R = %d, SCN_MK_LOOP2_G = %d, SCN_MK_PLAY = %d, SCN_MK_GRANULAR = %d, SCN_MK_SLICE_W = %d, SCN_MK_SLICE_R = %d, SCN_MK_SLICE_G = %d };"
             % tuple(range(len(KINDS)))]
        return "\n".join(o)
