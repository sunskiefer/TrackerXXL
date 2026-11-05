"""TrackerXXL skin generator: shared constants, theme parsing, screen->canvas transform.
Everything here is derived from the Polyend source in core/ (theme colours from core/source/Themes.cpp,
control style flags from core/libs/NewDisplay/displayStructs.h) so nothing is typed in twice."""
import os, re, json

HERE = os.path.dirname(os.path.abspath(__file__))
SKIN = os.path.dirname(HERE)                       # port/skin
PORT = os.path.dirname(SKIN)                       # port
ROOT = os.path.dirname(PORT)                       # TrackerXXL
CORE = os.path.join(ROOT, "core")
MPCVST = "/root/work/zynth/third_party/mpc-vst-plugins"
SCRATCH = "/tmp/claude-0/-home-claude/196b1c20-bcbc-5df0-8e18-2e4e79256657/scratchpad"
PAGES = os.path.join(SCRATCH, "pages")
TOOLS = os.path.join(MPCVST, "tools")

CANVAS_W, CANVAS_H = 1280, 628
SRC_W, SRC_H = 800, 480
# One transform for every screen (editable in layout.csv through the per-control x,y,w,h; this only places the
# FIRST layout).  Text is sized from SX (the user's 1.59x rule: 17 px source font -> 27 px), the vertical scale is
# what makes title bar + 15 grid rows + 8 bottom buttons fit the 628 px tall plugin area.
SX = 1.59
X0 = (CANVAS_W - SRC_W * SX) / 2.0                  # 4 px side margin
SY = CANVAS_H / float(SRC_H)                        # 1.3083
MIN_TEXT_PX = 26                                    # Meta4 floor for header/readout text (user hard rule)

def tx(x): return int(round(X0 + x * SX))
def ty(y): return int(round(y * SY))
def tw(w): return int(round(w * SX))
def th(h): return int(round(h * SY))
def rect_t(x, y, w, h):
    x1, y1, x2, y2 = tx(x), ty(y), tx(x + w), ty(y + h)
    return x1, y1, x2 - x1, y2 - y1

# ---- control styles (displayStructs.h) ----
def parse_enum_styles():
    src = open(os.path.join(CORE, "libs/NewDisplay/displayStructs.h")).read()
    body = src[src.index("enum controlsStyle"):]
    body = body[:body.index("};")]
    vals, cur = {}, -1
    for m in re.finditer(r"(controlStyle\w+)\s*=\s*([^,/\n]+)", body):
        expr = m.group(2).strip()
        expr = re.sub(r"\(1\s*<<\s*(\d+)\)", lambda k: str(1 << int(k.group(1))), expr)
        expr = re.sub(r"controlStyle\w+", lambda k: str(vals.get(k.group(0), 0)), expr)
        vals[m.group(1)] = eval(expr)
    return vals
STYLES = parse_enum_styles()

# ---- fonts (display.cpp fontsData1 = Roboto Mono; advance/height from the decoded bitmap fonts) ----
FONTS = [  # index = ((style>>8)&15)-1
    dict(name="Roboto Mono Light 14", weight="Light", adv=8, h=18, em=14),
    dict(name="Roboto Mono Light 17", weight="Light", adv=10, h=22, em=17),
    dict(name="Roboto Mono Regular 14", weight="Regular", adv=8, h=19, em=14),
    dict(name="Roboto Mono Bold 17", weight="Bold", adv=10, h=22, em=17),
    dict(name="Roboto Mono Regular 12", weight="Regular", adv=7, h=16, em=12),
]
def font_of_style(style):
    i = ((style >> 8) & 15) - 1
    return FONTS[i if i >= 0 else 0]
def live_px(font):
    """Skin text size for a source font: 1.59x, never below the Meta4 floor."""
    return max(MIN_TEXT_PX, int(round(font["em"] * SX)))

# ---- theme (Themes.cpp themeDefault 'Original') ----
def parse_theme():
    src = open(os.path.join(CORE, "source/Themes.cpp")).read()
    consts = {m.group(1): int(m.group(2), 16)
              for m in re.finditer(r"constexpr uint32_t (\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*;", src)}
    i = src.index("constexpr Theme themeDefault")
    j = src.index("};", src.index("patternTrackerColors", i))
    # the initializer ends at the first '\n};' after i
    end = src.index("\n};", i)
    block = src[i:end]
    block = re.sub(r"//[^\n]*", "", block)
    roles = {}
    for m in re.finditer(r"\.(\w+)\s*=\s*(\{[^}]*\}|[\w]+)\s*,?", block):
        name, val = m.group(1), m.group(2)
        if val.startswith("{"):
            items = [s.strip() for s in val.strip("{}").split(",") if s.strip()]
            cols = []
            for s in items:
                cols.append(int(s, 16) if s.lower().startswith("0x") else consts.get(s, 0))
            roles[name] = cols
        else:
            roles[name] = consts.get(val, int(val, 0) if re.match(r"^(0x)?[0-9a-fA-F]+$", val) else 0)
    return consts, roles

def theme_member_order():
    """Theme struct array members in declaration order: [(name, length)] (Themes.h)."""
    src = open(os.path.join(CORE, "source/Themes.h")).read()
    return [(m.group(1), int(m.group(2))) for m in re.finditer(r"uint32_t\s+(\w+)\[(\d+)\];", src)]

CONSTS, ROLES = parse_theme()
def hex6(c): return "%06x" % (c & 0xFFFFFF)

def load_json(p): return json.load(open(p))
def save_json(p, o, **kw): json.dump(o, open(p, "w"), **kw)
