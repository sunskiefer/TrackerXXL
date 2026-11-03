"""Every other control the firmware creates -> pooled scene slots + skin widgets.
A pool entry is one control instance of a class (label, bar, list ...).  Its state parameter says WHICH module's geometry
is live and which colour/variant is wanted: st = 1 + module*NR + variant (0 = hidden), so widgets of different screens
share the same parameters and `IndexedEnabling` shows exactly one of them.  Colour variants are keyed by the colour
VALUES a control uses (bridge reads colors[] at run time and matches them against the generated table), so no theme
pointer bookkeeping is needed."""
import os, re, json
from PIL import Image, ImageDraw
import common as C
from model import Widget

LABEL_T = {"cLabel", "cTextBox", "cTextPopup", "cSimpleText"}
BAR_T = {"cBar"}
LIST_T = {"cList", "cParamValueList", "NewList"}
PV_T = {"cParamValueList", "NewList"}      # two-column lists: parameter name left, value right
CLASS_OF = {}
for t in LABEL_T: CLASS_OF[t] = "LBL"
for t in BAR_T: CLASS_OF[t] = "BAR"
for t in LIST_T: CLASS_OF[t] = "LST"
CLASS_OF["cBgLabel"] = "BGL"
CLASS_OF["cFrame"] = "FRM"
BAR_FRAMES = 21           # 5 % steps; strip height frames*h stays under the 12288 px MPC limit for h <= 585
MAX_ROWS = 14
NR = {"LBL": 8, "BAR": 6, "LST": 6, "BGL": 1, "FRM": 12}   # state slots per module for each class

def rgb(c): return ((c >> 16) & 255, (c >> 8) & 255, c & 255)
def hx(c): return "%06x" % (c & 0xFFFFFF)

def role_colors(name):
    return C.ROLES.get(name)

def role_of_sym(s):
    """'display.getTheme.activeLabelsColors' / '&display.getTheme.red' -> theme member name"""
    if not s or not isinstance(s, str): return None
    m = re.findall(r"(\w+)$", s.replace("()", ""))
    return m[0] if m else None

def scan_roles(module, files):
    """var name (without index) -> {literal index or '*': set(role)} from setControlColors calls of a module's sources."""
    out = {}
    d = os.path.join(C.CORE, os.path.dirname(files[0]))
    for fn in sorted(os.listdir(d)):
        if not fn.endswith(".cpp"): continue
        src = open(os.path.join(d, fn)).read()
        for m in re.finditer(r"setControlColors\(\s*(\w+)(?:\[(\w+)\])?\s*,\s*(?:&?display\.getTheme\(\)\.(\w+)|(\w+))", src):
            var, idx, role, other = m.groups()
            idx = idx if (idx is not None and idx.isdigit()) else "*"
            out.setdefault(var, {}).setdefault(idx, set()).add(role or other)
    return out

class Generic:
    def __init__(self, P, S, table, modules, controls, detail="full"):
        self.P, self.S, self.table, self.modules, self.controls = P, S, table, modules, controls
        self.detail = detail
        self.mods = ["global"] + list(modules)          # module index 0 = global controls
        self.items = []                                 # one dict per control instance
        self.pool_n = {}                                # class -> pool size
        self.hdr_rows = []
    # ------------------------------------------------------------------ plan: pool assignment + variants
    def plan(self):
        order_global = [k for k in self.table if k.startswith("global.")]
        gcount, mcount = {}, {}
        # global controls first (pool prefix), then each module
        for key in order_global:
            for n, c in enumerate(self.table[key]):
                self._add(key, 0, n, c, gcount)
        self.G = dict(gcount)
        for mi, key in enumerate(self.modules, start=1):
            local = {}
            for n, c in enumerate(self.table[key]):
                self._add(key, mi, n, c, local, base=self.G)
            for k, v in local.items(): mcount[k] = max(mcount.get(k, 0), v)
        for k in set(self.G) | set(mcount):
            self.pool_n[k] = self.G.get(k, 0) + mcount.get(k, 0)
    def _add(self, scr, mi, n, c, counter, base=None):
        cl = CLASS_OF.get(c["type"])
        it = dict(scr=scr, mi=mi, n=n, c=c, cls=cl)
        if cl:
            ordv = counter.get(cl, 0); counter[cl] = ordv + 1
            it["pool"] = (base or {}).get(cl, 0) + ordv if mi else ordv
        self.items.append(it)
    # ------------------------------------------------------------------ roles per control
    def roles_of(self, it, scan):
        c = it["c"]
        var = c.get("var") or ""
        base = re.sub(r"\[.*\]", "", var)
        idx = None
        m = re.search(r"\[(\d+)\]", var)
        if m: idx = m.group(1)
        else:
            # loop-built handle: label[i] -> use creation order inside its loop (n-th created of this var)
            same = [x for x in self.items if x["scr"] == it["scr"] and (x["c"].get("var") or "") == var]
            idx = str(same.index(it)) if "[i]" in var else None
        names = []
        cs = role_of_sym(c.get("colors"))
        if cs: names.append(cs)
        sc = scan.get(it["scr"], {}).get(base, {})
        for k in ("*", idx):
            for r in sc.get(k, ()): 
                if r not in names: names.append(r)
        return [n for n in names if n in C.ROLES]
    # ------------------------------------------------------------------ declare params
    def declare(self, scan):
        P = self.P
        self.scan = scan
        NM = len(self.mods)
        for it in self.items:
            it["roles"] = self.roles_of(it, scan)
        nstate = lambda cl, extra=1: 1 + NM * NR[cl] * extra
        # pools
        P.region("lbl", sum(([("lb%02da" % k, "opt", dict(n=nstate("LBL"), text=True)),
                              ("lb%02db" % k, "opt", dict(n=1 + NM * 4, text=True)),
                              ("lb%02dc" % k, "opt", dict(n=1 + NM * 16))] for k in range(self.pool_n.get("LBL", 0))), []))
        P.region("bar", sum(([("br%02ds" % k, "opt", dict(n=nstate("BAR"))), ("br%02dv" % k, "int", dict(max=BAR_FRAMES - 1))]
                             for k in range(self.pool_n.get("BAR", 0))), []))
        self.pv_pools = sorted({it["pool"] for it in self.items if it.get("cls") == "LST" and it["c"]["type"] in PV_T})
        P.region("lst", sum(([("ls%02ds" % k, "opt", dict(n=1 + NM * 4))] +
                             [("ls%02dr%02d" % (k, r), "opt", dict(n=nstate("LST"), text=True)) for r in range(MAX_ROWS)] +
                             ([("ls%02dr%02dv" % (k, r), "opt", dict(n=2, text=True)) for r in range(MAX_ROWS)] if k in self.pv_pools else [])
                             for k in range(self.pool_n.get("LST", 0))), []))
        lh = []
        for it in self.items:
            if it.get("cls") == "LST":
                it["rows"] = self.rows_of(it); it["lh"] = "lh%02dm%02d" % (it["pool"], it["mi"])
                if it["lh"] not in [k for k, _, _ in lh]: lh.append((it["lh"], "opt", dict(n=it["rows"] + 1)))
                else: it["rows"] = max(it["rows"], dict((k, v["n"] - 1) for k, _, v in lh)[it["lh"]])
        P.region("lsthit", lh)
        P.region("bgl", [("bg%02d" % k, "opt", dict(n=nstate("BGL"))) for k in range(self.pool_n.get("BGL", 0))])
        P.region("frm", [("fr%02d" % k, "opt", dict(n=nstate("FRM"))) for k in range(self.pool_n.get("FRM", 0))])
    def rows_of(self, it):
        c = it["c"]
        return max(1, min(MAX_ROWS, (c["h"] - 4) // 27, self.lines_of(it)))
    def st(self, cl, mi, v): return 1 + mi * NR[cl] + v
    # ------------------------------------------------------------------ style helpers
    @staticmethod
    def justify(style):
        if style & C.STYLES["controlStyleCenterX"]: return "center"
        if style & C.STYLES["controlStyleRightX"]: return "right"
        return "left"
    # ------------------------------------------------------------------ build
    def build(self, art_dir):
        S = self.S
        for it in self.items:
            cl = it["cls"]
            if not cl: continue
            getattr(self, "b_" + cl.lower())(it)
    def reg(self, it, p):
        c = it["c"]
        self.controls.append(Widget(id="%s.%s.%d" % (it["scr"], c["type"], it["n"]), screen=it["scr"], type=c["type"],
                                    x=p[0], y=p[1], w=p[2], h=p[3], label="", live=it.get("live", ""), px=it.get("px", ""),
                                    parameter=it.get("param", ""), extra="var=%s style=%d value=%s" % (c.get("var"), c["style"], c["value"])))
    # --------------------------------------------------------------- LBL
    def variants_a(self, it):
        c = it["c"]; has_bg = bool(c["style"] & C.STYLES["controlStyleBackground"])
        roles = it["roles"] or ([role_of_sym(c.get("colors"))] if c.get("colors") else ["defaultTextColors"])
        v = []
        for r in roles:
            col = C.ROLES.get(r)
            if not col or len(col) < 2: continue
            key = (col[0], col[1] if has_bg else None)
            if key not in v: v.append(key)
        return v or [(0xFFFFFF, None)]
    def variants_b(self, it):
        v = []
        for r in (it["roles"] or ["defaultTextColors"]):
            col = C.ROLES.get(r)
            if col and len(col) > 3 and col[3] not in v: v.append(col[3])
        return v or [0xFFFFFF]
    def b_lbl(self, it):
        S, P = self.S, self.P
        c = it["c"]; k = it["pool"]; mi = it["mi"]
        x, y, w, h = c["x"], c["y"], c["w"], c["h"]
        style = c["style"]; font = C.font_of_style(style); px = C.live_px(font)
        just = self.justify(style)
        cy = bool(style & C.STYLES["controlStyleCenterY"])
        va, vb = self.variants_a(it), self.variants_b(it)
        it["va"], it["vb"] = va, vb
        wt = font["weight"]
        # box
        if w == 0: w = 270
        bx = x - w // 2 if just == "center" else (x - w if just == "right" else x)
        for vflag in (0, 1):
            if vflag and not cy: continue
            yc = (y + h / 2.0 - (10 if vflag else 0)) if cy else (y + 18.0)
            ty_ = C.ty(yc - 13); th_ = C.th(26)
            for vi, (c0, c1) in enumerate(va):
                state = self.st("LBL", mi, vi * 2 + vflag)
                if c1 is not None and h > 0 and vflag == 0:
                    n = "bg_%dx%d_%s.png" % (C.tw(w), C.th(h), hx(c1))
                    S.save_png(n, Image.new("RGBA", (C.tw(w), C.th(h)), (*rgb(c1), 255)))
                    S.image(n, C.tx(bx), C.ty(y), C.tw(w), C.th(h), when=S.when("lb%02da" % k, state), name="label bg")
                S.text("lb%02da" % k, C.tx(bx), ty_, C.tw(w), th_, px, hx(c0), weight=wt, just=just,
                       when=S.when("lb%02da" % k, state), name="label")
        # value line (text2)
        if True:
            yc2 = ((y + h / 2.0 - 10) if cy else (y + 18.0)) + 20
            for vi, c3 in enumerate(vb):
                S.text("lb%02db" % k, C.tx(bx), C.ty(yc2 - 13), C.tw(w), C.th(26), px, hx(c3), weight=wt, just=just,
                       when=S.when("lb%02db" % k, 1 + mi * 4 + vi), name="label value")
        # line / dots (data pointer): state c = 1 red, 2 green, 3 white line; 4.. dots(count 2..3, current)
        if "data" in c and c.get("data") not in (None, 0, "0"):
            self.line_widgets(it, bx, w, y, h, cy)
        it["live"], it["px"], it["param"] = "lb%02da/b" % k, px, "lb%02d" % k
        self.reg(it, (C.tx(bx), C.ty(y), C.tw(w), C.th(h)))
    def line_widgets(self, it, bx, w, y, h, cy):
        S = self.S; c = it["c"]; k = it["pool"]; mi = it["mi"]
        # underline drawn by strLabelData.line: bottom buttons use (-40,50)-(40,50) around the label centre; title bar full width.
        for opt, col in ((1, C.ROLES["red"] if isinstance(C.ROLES.get("red"), int) else 0xFF0000), (2, C.CONSTS["one_true_green"]), (3, 0xFFFFFF)):
            ycen = y + h / 2.0
            lx0, lx1 = (bx + w / 2.0 - 40, bx + w / 2.0 + 40)
            ly = y + 50 if h >= 40 else y + h
            n = "line_%d_%s.png" % (C.tw(80), hx(col))
            S.save_png(n, Image.new("RGBA", (C.tw(80), 3), (*rgb(col), 255)))
            S.image(n, C.tx(lx0), C.ty(ly), C.tw(80), 3, when=S.when("lb%02dc" % k, 1 + mi * 16 + opt), name="label line")
        for count in (2, 3):
            for cur in range(count):
                opt = 4 + (count - 2) * 3 + cur
                n = "dots_%d_%d.png" % (count, cur)
                im = Image.new("RGBA", (C.tw(30), C.th(8)), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
                for j in range(count):
                    cx = C.tw(15) + (j - (count - 1) / 2.0) * C.tw(10)
                    d.ellipse([cx - 3, im.height / 2 - 3, cx + 3, im.height / 2 + 3], fill=(*rgb(0xBBBBBB if j == cur else 0x444444), 255))
                S.save_png(n, im)
                S.image(n, C.tx(bx + w / 2.0) - C.tw(15), C.ty(y + h - 14), C.tw(30), C.th(8), when=S.when("lb%02dc" % k, 1 + mi * 16 + opt), name="label dots")
    # --------------------------------------------------------------- BGL (bottom bar background, static)
    def b_bgl(self, it):
        S = self.S; c = it["c"]; k = it["pool"]; mi = it["mi"]
        cols = C.ROLES.get(role_of_sym(c.get("colors")) or "bottomButtonsBgColors")
        x, y, w, h = c["x"], c["y"], c["w"], c["h"]
        px0, py0, pw, ph = C.tx(x + 1), C.ty(y), C.tw(w - 3), C.th(h)
        im = Image.new("RGBA", (pw, ph), (*rgb(cols[1]), 255)); d = ImageDraw.Draw(im)
        mask = int(c["value"]) if isinstance(c["value"], int) else 255
        for i in range(1, 8):
            if mask & (1 << i):
                lx = C.tx((w // 8) * i + x) - px0
                d.line([lx, ph // 4, lx, ph], fill=(*rgb(cols[0]), 255), width=1)
        n = "bgl_%d_%d_%d.png" % (mi, it["n"], mask)
        S.save_png(n, im)
        S.image(n, px0, py0, pw, ph, when=S.when("bg%02d" % k, self.st("BGL", mi, 0)), name="bottom bar bg")
        it["param"] = "bg%02d" % k
        self.reg(it, (px0, py0, pw, ph))
    # --------------------------------------------------------------- FRM (place highlight)
    def places(self, scr):
        """frame places of a module: parse `const uint16_t xxx[N][4]` tables and frameData.places[i] = &xxx[j][0]"""
        d = None
        from ctl_extract import MODULES, strip_comments, Ev
        src = strip_comments(open(os.path.join(C.CORE, MODULES[scr][0])).read())
        tabs = {}
        for m in re.finditer(r"(?:const|static const)\s+uint16_t\s+(\w+)\s*\[[^\]]*\]\s*\[4\]\s*=\s*\{(.*?)\};", src, re.S):
            rows = re.findall(r"\{([^{}]*)\}", m.group(2))
            tabs[m.group(1)] = [[Ev({}).eval(v) for v in r.split(",") if v.strip()] for r in rows]
        out = {}
        for m in re.finditer(r"frameData\.places\[(\d+)\]\s*=\s*&(\w+)\[(\d+)\]\[0\]", src):
            t = tabs.get(m.group(2))
            if t and int(m.group(3)) < len(t): out[int(m.group(1))] = t[int(m.group(3))]
        return [out[i] for i in sorted(out)]
    def b_frm(self, it):
        S = self.S; c = it["c"]; k = it["pool"]; mi = it["mi"]
        pl = self.places(it["scr"]) if it["scr"] in self.modules else []
        it["places"] = pl
        col = C.ROLES["frameColors"][0]
        for i, (px_, py_, pw_, ph_) in enumerate(pl[:NR["FRM"] - 1]):
            x0, y0, w0, h0 = C.rect_t(px_, py_, pw_, ph_)
            n = "frm_%dx%d.png" % (w0, h0)
            im = Image.new("RGBA", (w0, h0), (0, 0, 0, 0)); ImageDraw.Draw(im).rectangle([0, 0, w0 - 1, h0 - 1], outline=(*rgb(col), 255), width=2)
            S.save_png(n, im)
            S.image(n, x0, y0, w0, h0, when=S.when("fr%02d" % k, self.st("FRM", mi, 1 + i)), name="frame place %d" % i)
        it["param"] = "fr%02d" % k
        self.reg(it, C.rect_t(0, 0, 800, 480))
    # --------------------------------------------------------------- BAR (filmstrip)
    def variants_bar(self, it):
        v = []
        for r in (it["roles"] or []):
            col = C.ROLES.get(r)
            if col and len(col) >= 5: v.append(tuple(col[:5]))
        if not v:
            col = C.ROLES["activeBarColors"]; v.append(tuple(col[:5]))
        out = []
        for t in v:
            if t not in out: out.append(t)
        return out[:3]
    def b_bar(self, it):
        S = self.S; c = it["c"]; k = it["pool"]; mi = it["mi"]
        x, y, w, h = c["x"], c["y"], c["w"], c["h"]
        style = c["style"]
        bg = bool(style & C.STYLES["controlStyleBackground"])
        v100 = bool(style & C.STYLES["controlStyleValue_0_100"]); lr = bool(style & (C.STYLES["controlStyleValueLeftRight_100_100"] | C.STYLES["controlStyleValueLeftRight_24_24"]))
        x0, y0, w0, h0 = C.rect_t(x, y, w, h)
        it["vars"] = self.variants_bar(it)
        for vi, cols in enumerate(it["vars"]):
            fill, c1, c2, cont, back = cols
            # strip of BAR_FRAMES frames: background, container, fill
            n = "bar_%dx%d_%s_%d%d.png" % (w0, h0, "".join(hx(q) for q in (fill, cont, back)), int(bg), int(lr))
            if n not in S.images:
                strip = Image.new("RGBA", (w0, h0 * BAR_FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
                bw = w - 20; bxx = (w - bw) / 2.0; by = 10; bh = h - 25
                for f in range(BAR_FRAMES):
                    oy = f * h0
                    if bg: d.rectangle([0, oy, w0, oy + h0], fill=(*rgb(back), 255))
                    d.rectangle([C.tw(10), oy + C.th(11), C.tw(w - 10), oy + C.th(h - 14)], fill=(*rgb(cont), 255))
                    frac = f / float(BAR_FRAMES - 1)
                    if lr:
                        half = bh / 2.0; v = (frac * 2 - 1)
                        top = by + half - (half * v if v > 0 else 0); bot = by + half + (half * -v if v < 0 else 0)
                        d.rectangle([C.tw(bxx + 1), oy + C.th(top), C.tw(bxx + bw - 1), oy + C.th(bot)], fill=(*rgb(fill), 255))
                    else:
                        top = by + bh - bh * frac
                        d.rectangle([C.tw(bxx + 1), oy + C.th(top), C.tw(bxx + bw - 1), oy + C.th(by + bh)], fill=(*rgb(fill), 255))
                S.save_png(n, strip)
            S.strip("br%02dv" % k, n, BAR_FRAMES, x0, y0, w0, h0, when=S.when("br%02ds" % k, self.st("BAR", mi, vi * 2)), name="bar")
            if bg:   # value hidden (value<0): background only
                n2 = "barbg_%dx%d_%s.png" % (w0, h0, hx(back))
                S.save_png(n2, Image.new("RGBA", (w0, h0), (*rgb(back), 255)))
                S.image(n2, x0, y0, w0, h0, when=S.when("br%02ds" % k, self.st("BAR", mi, vi * 2 + 1)), name="bar bg")
        it["param"] = "br%02d" % k
        self.reg(it, (x0, y0, w0, h0))
    # --------------------------------------------------------------- LST
    def lines_of(self, it):
        """largest linesCount the sources ever give this list's data struct (std::min<..>(x, N) counts as N)"""
        sym = re.sub(r"[&\[\]\w]*?(\w+)(\[\w+\])?$", r"\1", str(it["c"].get("data") or "")).strip("&")
        if not sym or sym == "0": return MAX_ROWS
        d = os.path.join(C.CORE, os.path.dirname(__import__("ctl_extract").MODULES.get(it["scr"], ["interface/core/x"])[0]))
        best = 0
        for fn in sorted(os.listdir(d)):
            if not fn.endswith((".cpp", ".h")): continue
            src = open(os.path.join(d, fn)).read()
            for m in re.finditer(r"\b%s(?:\[\w+\])?\.linesCount\s*=\s*([^;]+);" % re.escape(sym), src):
                r = m.group(1)
                mm = re.search(r"std::min<[^>]*>\([^,]*,\s*(\d+)\)", r)
                if mm: best = max(best, int(mm.group(1))); continue
                if re.fullmatch(r"\s*\d+\s*", r): best = max(best, int(r)); continue
                best = max(best, MAX_ROWS)
        return best or MAX_ROWS
    def b_lst(self, it):
        S = self.S; c = it["c"]; k = it["pool"]; mi = it["mi"]
        x, y, w, h = c["x"], c["y"], c["w"], c["h"]
        style = c["style"]; font = C.font_of_style(style) if (style >> 8) & 15 else C.FONTS[2]; px = C.live_px(font)
        roles = it["roles"] or ["activeListColors"]
        vars_ = []
        for r in roles:
            col = C.ROLES.get(r)
            if col and len(col) >= 6:
                key = (col[3], col[4], col[5], col[0], col[1])
                if key not in vars_: vars_.append(key)
        vars_ = (vars_ or [tuple(C.ROLES["activeListColors"][i] for i in (3, 4, 5, 0, 1))])[: (1 if self.detail == "low" else 2)]
        it["vars"] = vars_
        rows = it["rows"]
        for r in range(rows):
            ry = y + 4 + r * 27
            for vi, (bgc, c4, c5, c0, c1) in enumerate(vars_):
                for kind in (0, 1, 2):
                    if kind == 2 and self.detail == "low": continue
                    colr = (c4, c5, 0xFF0000)[kind] if kind != 2 else (c5 if c5 != c4 else 0xCD1818)
                    stt = self.st("LST", mi, vi * 3 + kind)
                    S.text("ls%02dr%02d" % (k, r), C.tx(x + 13), C.ty(ry), C.tw(w - 20), C.th(27), px, hx(colr), weight=font["weight"], just="left",
                           when=S.when("ls%02dr%02d" % (k, r), stt), name="list row")
                    if c["type"] in PV_T:     # value column, right aligned (paramValueList.cpp: x_pos_end = right edge - 7), same colour, same state
                        S.text("ls%02dr%02dv" % (k, r), C.tx(x + 13), C.ty(ry), C.tw(w - 26), C.th(27), px, hx(colr), weight=font["weight"], just="right",
                               when=S.when("ls%02dr%02d" % (k, r), stt), name="list value")
                    if kind == 1:   # current row frame
                        wb = w - 6
                        n = "lstbar_%dx%d_%s.png" % (C.tw(wb), C.th(27), hx(c0))
                        im = Image.new("RGBA", (C.tw(wb), C.th(27)), (0, 0, 0, 0)); ImageDraw.Draw(im).rectangle([0, 0, im.width - 1, im.height - 1], outline=(*rgb(c0), 255), width=2)
                        S.save_png(n, im)
                        S.image(n, C.tx(x + 3), C.ty(ry), C.tw(wb), C.th(27), when=S.when("ls%02dr%02d" % (k, r), stt), name="list cursor")
        # list background (style Background): static fill gated by the list's own state param
        if style & C.STYLES["controlStyleBackground"]:
            for vi, (bgc, *_rest) in enumerate(vars_):
                n = "lstbg_%dx%d_%s.png" % (C.tw(w), C.th(h), hx(bgc))
                S.save_png(n, Image.new("RGBA", (C.tw(w), C.th(h)), (*rgb(bgc), 255)))
                S.image(n, C.tx(x), C.ty(y), C.tw(w), C.th(h), when=S.when("ls%02ds" % k, 1 + mi * 4 + vi), name="list bg")
        # touch: rows
        for vi in range(len(vars_)):
            S.enum(it["lh"], C.tx(x), C.ty(y + 4), min(C.tw(w), C.CANVAS_W - C.tx(x)), min(C.th(27 * rows), C.CANVAS_H - C.ty(y + 4)), vertical=True, name="list hit",
                   when=S.when("ls%02ds" % k, 1 + mi * 4 + vi), segments=rows, first=1)
        it["param"] = "ls%02d" % k
        self.reg(it, C.rect_t(x, y, w, h))

    # ------------------------------------------------------------------ header for the bridge: one row per pooled control
    CTL_ID = {"cLabel": "controlIdLabel", "cTextBox": "controlIdTextBox", "cTextPopup": "controlIdTextPopup", "cSimpleText": "controlIdSimpleText",
              "cBar": "controlIdBar", "cList": "controlIdList", "cParamValueList": "controlIdParamValueList", "NewList": "controlIdImgList", "cBgLabel": "controlIdBgLabel", "cFrame": "controlIdFrame"}
    def header(self):
        o = ["/* pooled controls (generic.py): one row per control the firmware creates, in creation order per module */",
             "#define SCN_NMODULES %d" % len(self.mods),
             "static const char* const SCN_MODULES[%d] = {%s};" % (len(self.mods), ",".join('"%s"' % m for m in self.mods)),
             "#ifdef SCN_WANT_CTL", "struct ScnCtl { short mi; char cls; short pool; short id; short x, y, w, h; unsigned style; unsigned char nva, nvb, nvars, rows, nplaces;",
             "  unsigned va[3][2]; unsigned vb[3]; unsigned vars[3][5]; unsigned bar[3][5]; };", "#endif"]
        rows = []; self.handles = []
        OWNER = {"global.titleBar": "titleBar", "global.songTimer": "songTimer", "global.popups": "mtPopups", "global.misc": "mtInterface",
                 "global.menuPopup": "mtMenuPopup"}
        for it in self.items:
            cl = it.get("cls")
            if not cl or "param" not in it: continue
            var = it["c"].get("var") or ""
            own = OWNER.get(it["scr"], it["scr"])
            import re as _re
            self.handles.append("(int)(%s.%s)" % (own, var) if _re.fullmatch(r"[A-Za-z_]\w*(\[\d+\])?", var) else "-1 /* %s.%s: not a plain member: matched by control id + order */" % (own, var))
            c = it["c"]
            va = [list(v) + [0] * (2 - len(v)) for v in it.get("va", [])][:3]
            va = [[a, 0 if b is None else b] for a, b in va]
            vb = list(it.get("vb", []))[:3]
            vars_ = [list(v) for v in it.get("vars", [])][:3] if cl == "LST" else []
            bar = [list(v) for v in it.get("vars", [])][:3] if cl == "BAR" else []
            pad = lambda L, n, w: (L + [[0] * w] * n)[:n] if w else (L + [0] * n)[:n]
            rows.append("{%d,'%s',%d,%s,%d,%d,%d,%d,%du,%d,%d,%d,%d,%d,{%s},{%s},{%s},{%s}}" % (
                it["mi"], {"LBL": "L", "BAR": "B", "LST": "S", "BGL": "G", "FRM": "F"}[cl], it["pool"], self.CTL_ID[c["type"]], c["x"], c["y"], c["w"], c["h"], c["style"] & 0xFFFFFFFF,
                len(va), len(vb), len(vars_) or len(bar), it.get("rows", 0), len(it.get("places", [])),
                ",".join("{%du,%du}" % tuple(v) for v in pad(va, 3, 2)), ",".join("%du" % v for v in pad(vb, 3, 0)),
                ",".join("{%s}" % ",".join("%du" % q for q in v) for v in pad(vars_, 3, 5)),
                ",".join("{%s}" % ",".join("%du" % q for q in v) for v in pad(bar, 3, 5))))
        o.append("#define SCN_NCTL %d" % len(rows))
        o.append("#define SCN_DETAIL_LOW %d" % (1 if self.detail == "low" else 0))
        o.append("#define SCN_BAR_FRAMES %d\n#define SCN_COL_RED 0x%06xu\n#define SCN_COL_GREEN 0x%06xu" % (BAR_FRAMES, C.ROLES["red"] if isinstance(C.ROLES.get("red"), int) else 0xFF0000, C.CONSTS["one_true_green"]))
        o.append("#ifdef SCN_WANT_CTL   /* needs the firmware's control ids (bridge sources only) */")
        o.append("static const ScnCtl SCN_CTL[%d] = {\n%s};" % (len(rows), ",\n".join(rows)))
        o.append("#endif")
        o.append("#define SCN_LBL_POOL %d\n#define SCN_BAR_POOL %d\n#define SCN_LST_POOL %d\n#define SCN_LST_ROWS %d\n#define SCN_BGL_POOL %d\n#define SCN_FRM_POOL %d" % (
            self.pool_n.get("LBL", 0), self.pool_n.get("BAR", 0), self.pool_n.get("LST", 0), MAX_ROWS, self.pool_n.get("BGL", 0), self.pool_n.get("FRM", 0)))
        o.append("#define SCN_NR_LBL %d\n#define SCN_NR_BAR %d\n#define SCN_NR_LST %d\n#define SCN_NR_FRM %d" % (NR["LBL"], NR["BAR"], NR["LST"], NR["FRM"]))
        return "\n".join(o)
