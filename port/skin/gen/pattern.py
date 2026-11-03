"""Pattern editor (cTracker) -> scene slots + skin widgets.  Every number below is tracker.cpp (ROW_NUM build) of core/libs/NewDisplay:
cursor row = row 7 of 15, the grid scrolls around it, the playhead is a row band, columnsCount follows popupMode, rowCount 8 while
the Fill popup is up, row numbers wrap with the track-end line, selection = box + white text.

A cell is a slot PAIR (see model.py): pcRRSS = state, pcRRSS_t = text.
  state 0 hidden | 1+6*s+k, s = id of the set of view modes that put this field at this x, k = 0 normal, 1 dim (empty ---),
  2 inactive track (colors[12]), 3/4/5 selected (white text on a red/green/white box).
Text is plain (no shading): colour only, exactly the colour role the source uses."""
import os
from PIL import Image, ImageDraw
import common as C
from model import Widget

LEFT = RIGHT = 27
OFF4 = [8, 52, 86, 140]; TWO1 = [7, 10, 5, 0]; TWO2 = [0, 61, 50, 50]; ONE = [32, 37, 27, 27]
SELOFF4 = [1, 44, 78, 132]; SELW4 = [40, 30, 50, 50]
T1O = [1, 1, 1, 0]; T1W = [40, 33, 43, 0]; T2O = [0, 52, 44, 44]; T2W = [0, 37, 45, 45]
OSO = [24, 29, 19, 19]; OSW = [40, 30, 50, 50]
PARAM_LEN = [3, 2, 4, 4]
MODES = [0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 12]   # patternViewMode values that exist (the viewMode_NN pages)
CORE_MODES = [0]
POS_Y, HEIGHT = 28, 395
STEP_H = HEIGHT // 15                         # 26
CURSOR_ROW = 7
COLORS = C.ROLES["patternTrackerColors"]
NK = 6                                        # cell kinds
KIND_NAMES = ["normal", "dim", "inactive", "selR", "selG", "selW"]
NAME_MAXLEN = {4: 18, 8: 9}                   # track-name box length classes (characters, 10 source px each)

def types_of(mode): return [t for t in range(4) if (mode >> t) & 1] if mode else [0, 1, 2, 3]
def cols_of(mode): return 4 if mode == 0 else 8
def tw_of(mode): return (800 - (LEFT + RIGHT)) // (4 if mode == 0 else 8)

def field_off(mode, T):
    if mode == 0: return OFF4[T]
    pc = len(types_of(mode))
    if pc == 1: return ONE[T]
    if pc == 2:
        if T == 0: return TWO1[0]
        if T == 1: return TWO2[1] if mode & 1 else TWO1[1]
        if T == 2: return TWO1[2] if mode & 8 else TWO2[2]
        return TWO2[3]
    return OFF4[T]

def which_of_two(mode, sel):
    if mode & 1: return 0 if sel == 0 else 1
    if mode & 2: return 0 if sel == 1 else 1
    if mode & 4: return 0 if sel == 2 else 1
    return 1

def sel_box(mode, T):
    """(offset, width) of the box under field T inside its track cell (tracker.cpp selection())."""
    pc = len(types_of(mode)) if mode else 4
    if pc == 1: return OSO[T], OSW[T]
    if pc == 2:
        if which_of_two(mode, T) == 0: return T1O[T], T1W[T]
        return T2O[T], T2W[T]
    return SELOFF4[T], SELW4[T]

def rgb(c): return ((c >> 16) & 255, (c >> 8) & 255, c & 255)
def hx(c): return "%06x" % (c & 0xFFFFFF)

class Pattern:
    def __init__(self, P, S, modes, controls):
        self.P, self.S, self.modes, self.controls = P, S, modes, controls
    # ------------------------------------------------------------------ parameters
    def declare(self):
        P, modes = self.P, self.modes
        self.fields = {}                      # (col, T, text x, box x, box w) -> set of modes
        for m in modes:
            for i in range(cols_of(m)):
                for T in types_of(m):
                    x = LEFT + field_off(m, T) + i * tw_of(m)
                    off, wd = sel_box(m, T)
                    bx = LEFT + 2 + i * tw_of(m) + off
                    self.fields.setdefault((i, T, x, bx, wd), set()).add(m)
        self.sets = sorted({frozenset(v) for v in self.fields.values()}, key=lambda s: sorted(s))
        self.sid_of = {s: n for n, s in enumerate(self.sets)}
        self.var = {}                         # (mode, col, T) -> set id
        for (i, T, x, bx, wd), ms in self.fields.items():
            for m in ms: self.var[(m, i, T)] = self.sid_of[frozenset(ms)]
        self.used = sorted({(i, T) for (i, T, x, bx, wd) in self.fields})
        self.pos = {}
        for m in modes:
            for i in range(cols_of(m)):
                for T in types_of(m):
                    x = LEFT + 2 + i * tw_of(m); off, wd = sel_box(m, T)
                    self.pos.setdefault((x + off, wd, x, tw_of(m) - 5), len(self.pos))
        self.mode_pos = {}
        for m in modes:
            for i in range(cols_of(m)):
                for T in types_of(m):
                    x = LEFT + 2 + i * tw_of(m); off, wd = sel_box(m, T)
                    self.mode_pos[(m, i, T)] = self.pos[(x + off, wd, x, tw_of(m) - 5)]
        self.line_x = sorted({LEFT + tw_of(m) * i for m in modes for i in range(cols_of(m))} - {LEFT})
        P.region("pat_flags", [("pvis", "opt", dict(n=2))] + [("pl%02d" % k, "opt", dict(n=2)) for k in range(len(self.line_x))])
        P.region("pat_cells", [("pc%02d%02d" % (r, c * 4 + T), "opt", dict(n=1 + NK * len(self.sets), text=True))
                               for r in range(15) for (c, T) in self.used])
        P.region("pat_rows", [("pn%02d" % r, "opt", dict(n=3, text=True)) for r in range(15)]
                 + [("pb%02d" % r, "opt", dict(n=6)) for r in range(15)])
        P.region("pat_cursor", [("pcur", "opt", dict(n=1 + len(self.pos) * 6)), ("pcp", "opt", dict(n=1 + len(self.pos))),
                                ("pct", "opt", dict(n=2, text=True))])
        names = [("ptA%d" % i, "int", dict(max=2 * NAME_MAXLEN[4])) for i in range(4)]
        names += [("ptB%d" % i, "int", dict(max=2 * NAME_MAXLEN[8])) for i in range(8)]
        P.region("pat_namebox", names)
        P.region("pat_names", [("pt%d" % i, "opt", dict(n=3, text=True)) for i in range(8)])
        P.region("pat_hit", [("ph%02d" % r, "opt", dict(n=21)) for r in range(15)])
    # ------------------------------------------------------------------ skin
    def build(self, art_dir):
        S, P = self.S, self.P
        os.makedirs(art_dir, exist_ok=True)
        gw, gh = C.tw(798), C.th(STEP_H * 15)
        gx, gy = C.tx(1), C.ty(POS_Y)
        S.save_png("pat_bg.png", Image.new("RGBA", (gw, gh), (*rgb(COLORS[1]), 255)))
        S.image("pat_bg.png", gx, gy, gw, gh, when=S.when("pvis", 1), name="grid bg")
        rowh = C.th(STEP_H)
        def band(kind, line):
            n = "pat_row_%d_%d.png" % (kind, line)
            col = {0: COLORS[1], 1: COLORS[11], 2: COLORS[13]}[kind]
            im = Image.new("RGBA", (gw, rowh), (*rgb(col), 255))
            if line: ImageDraw.Draw(im).rectangle([0, 0, gw, 0], fill=(*rgb(COLORS[20]), 255))
            S.save_png(n, im); return n
        for r in range(15):
            y = C.ty(POS_Y + STEP_H * r)
            for opt in range(6):
                kind, line = opt // 2, opt % 2
                if kind == 0 and line == 0: continue
                S.image(band(kind, line), gx, y, gw, rowh, when=S.when("pb%02d" % r, opt), name="row %d bg %d" % (r, opt))
        S.save_png("pat_vline.png", Image.new("RGBA", (1, gh), (*rgb(COLORS[15]), 255)))
        for xv in (LEFT, 799 - RIGHT):
            S.image("pat_vline.png", C.tx(xv), gy, 1, gh, when=S.when("pvis", 1), name="edge line")
        for k, xv in enumerate(self.line_x):
            S.image("pat_vline.png", C.tx(xv), gy, 1, gh, when=S.when("pl%02d" % k, 1), name="column line")
        sel_cols = [COLORS[10], COLORS[17], COLORS[18]]
        cy = C.ty(POS_Y + STEP_H * CURSOR_ROW); ch = C.th(STEP_H)
        for key_, idx in sorted(self.pos.items(), key=lambda kv: kv[1]):
            bx, bw, ox, ow = key_
            for outline in (0, 1):
                for colr in range(3):
                    opt = 1 + idx * 6 + outline * 3 + colr
                    n = "pat_cur_%d_%d_%d.png" % (idx, outline, colr)
                    px0 = C.tx(min(bx, ox)) - 2; px1 = C.tx(max(bx + bw, ox + ow)) + 2
                    im = Image.new("RGBA", (px1 - px0, ch), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
                    col = rgb(sel_cols[colr])
                    d.rectangle([C.tx(bx) - px0, 1, C.tx(bx + bw) - px0 - 1, ch - 2], fill=(*col, 255))
                    if outline: d.rectangle([C.tx(ox) - px0, 0, C.tx(ox + ow) - px0, ch - 1], outline=(*col, 255), width=2)
                    S.save_png(n, im)
                    S.image(n, px0, cy, px1 - px0, ch, when=S.when("pcur", opt), name="cursor %d" % opt)
        text_cols = [None, None, COLORS[12], COLORS[14], COLORS[14], COLORS[14]]
        for r in range(15):
            y = C.ty(POS_Y + STEP_H * r)
            for (i, T, x, bx, bwid), ms in sorted(self.fields.items()):
                key = "pc%02d%02d" % (r, i * 4 + T)
                sid = self.sid_of[frozenset(ms)]
                fw = C.tw(PARAM_LEN[T] * 10 + 6)
                for k in range(NK):
                    colr = COLORS[2 + T] if k == 0 else COLORS[6 + T] if k == 1 else text_cols[k]
                    st = 1 + NK * sid + k
                    if k >= 3:
                        n = "pat_sel_%d_%d_%d.png" % (C.tw(bwid), rowh, k)
                        S.save_png(n, Image.new("RGBA", (C.tw(bwid), rowh - 2), (*rgb(sel_cols[k - 3]), 255)))
                        S.image(n, C.tx(bx), y + 1, C.tw(bwid), rowh - 2, when=S.when(key, st), name="sel box")
                    S.text(key, C.tx(x), y, fw, rowh, 27, hx(colr), weight="Regular", font="Roboto", just="left", when=S.when(key, st), name="cell")
        for key_, idx in sorted(self.pos.items(), key=lambda kv: kv[1]):
            bx, bw, ox, ow = key_
            S.text("pct", C.tx(bx) + 2, cy, C.tw(bw), ch, 27, hx(COLORS[14]), weight="Regular", just="left", when=S.when("pcp", 1 + idx), name="cursor text")
        for r in range(15):
            y = C.ty(POS_Y + STEP_H * r)
            for st in (1, 2):
                S.text("pn%02d" % r, C.tx(3), y, C.tw(30), rowh, 26, hx(COLORS[16]), weight="Light", just="left", when=S.when("pn%02d" % r, st), name="rownum L")
            S.text("pn%02d" % r, C.tx(799 - 23), y, C.tw(30), rowh, 26, hx(COLORS[16]), weight="Light", just="left", when=S.when("pn%02d" % r, 1), name="rownum R")
        for cls, key, ncol in ((4, "ptA", 4), (8, "ptB", 8)):
            L = NAME_MAXLEN[cls]; tw_ = tw_of(0 if cls == 4 else 1)
            wpx, hpx = C.tw(tw_ - 2), C.th(27)
            strip = Image.new("RGBA", (wpx, hpx * (2 * L + 1)), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
            for inact in (0, 1):
                col = rgb(COLORS[12] if inact else COLORS[19])
                for n_ in range(1, L + 1):
                    f = 1 + inact * L + (n_ - 1)
                    d.rectangle([0, f * hpx, min(wpx - 1, C.tw(n_ * 10 + 4)), f * hpx + hpx - 1], fill=(*col, 255))
            nm = "pat_names_%d.png" % cls
            S.save_png(nm, strip)
            for i in range(ncol):
                S.strip("%s%d" % (key, i), nm, 2 * L + 1, C.tx(LEFT + i * tw_ + 1), C.ty(POS_Y), wpx, hpx, name="trackname box")
        for i in range(8):
            for st, tw_ in ((1, tw_of(0)), (2, tw_of(1))):
                if st == 1 and i >= 4: continue
                wpx, hpx = C.tw(tw_ - 2), C.th(27)
                S.text("pt%d" % i, C.tx(LEFT + i * tw_ + 4), C.ty(POS_Y), wpx - 8, hpx, 27, hx(COLORS[0]), weight="Regular", just="left",
                       when=S.when("pt%d" % i, st), name="trackname")
        for r in range(15):
            S.enum("ph%02d" % r, gx, C.ty(POS_Y + STEP_H * r), gw, rowh, name="grid hit r%d" % r, segments=20, when=S.when("pvis", 1))
        self.controls.append(Widget(id="patternEditor.grid", screen="patternEditor", type="cTracker", x=gx, y=gy, w=gw, h=gh,
                                    label="", live="pcRRSS pcRRSS_t pnRR pbRR pcur pcp pct ptA/ptB pt", px=27,
                                    parameter="pat_cells", extra="rows=15 step_h=%d cursor_row=%d modes=%s" % (STEP_H, CURSOR_ROW, ",".join(map(str, self.modes)))))
    @staticmethod
    def rgb(c): return rgb(c)
    @staticmethod
    def hex(c): return hx(c)
    # ------------------------------------------------------------------ header for the bridge
    def header(self):
        M = self.modes
        def arr(name, ctype, dims, f):
            def rec(level, idx):
                if level == len(dims): return str(f(*idx))
                return "{" + ",".join(rec(level + 1, idx + (k,)) for k in range(dims[level])) + "}"
            return "static const %s %s%s = %s;" % (ctype, name, "".join("[%d]" % d for d in dims), rec(0, ()))
        o = ["/* pattern editor geometry (tracker.cpp, source pixels) */",
             "#define SCN_PAT_MODES %d" % len(M),
             "static const unsigned char SCN_PAT_MODE_LIST[%d] = {%s};" % (len(M), ",".join(map(str, M))),
             "#define SCN_PAT_NSETS %d" % len(self.sets), "#define SCN_PAT_NK %d" % NK,
             "#define SCN_PAT_NPOS %d" % len(self.pos),
             "#define SCN_PAT_NLINES %d" % len(self.line_x),
             "static const short SCN_PAT_LINE_X[%d] = {%s};" % (len(self.line_x), ",".join(map(str, self.line_x))),
             arr("SCN_PAT_VAR", "signed char", (len(M), 8, 4), lambda m, c, T: self.var.get((M[m], c, T), -1)),
             arr("SCN_PAT_POS", "short", (len(M), 8, 4), lambda m, c, T: self.mode_pos.get((M[m], c, T), -1)),
             arr("SCN_PAT_TW", "unsigned char", (len(M),), lambda m: tw_of(M[m])),
             arr("SCN_PAT_COLS", "unsigned char", (len(M),), lambda m: cols_of(M[m])),
             arr("SCN_PAT_PARAMS", "unsigned char", (len(M),), lambda m: len(types_of(M[m]))),
             arr("SCN_PAT_TYPEMASK", "unsigned char", (len(M),), lambda m: sum(1 << t for t in types_of(M[m]))),
             "#define SCN_PAT_POS_Y %d\n#define SCN_PAT_STEP_H %d\n#define SCN_PAT_CURSOR_ROW %d\n#define SCN_PAT_NAME_A %d\n#define SCN_PAT_NAME_B %d"
             % (POS_Y, STEP_H, CURSOR_ROW, NAME_MAXLEN[4], NAME_MAXLEN[8])]
        return "\n".join(o)
