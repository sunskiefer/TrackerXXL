"""Static extraction of every control the firmware creates (display.createControl<T>(&props)) from the core/
sources: type, rect, style, colour role, data symbol, text -- evaluated the way the C++ would (loops unrolled,
defaults for run-time config).  Output: ctl_table.json (screen -> [control]).  The same table is what the bridge
checks at run time (bridge/display_bridge.cpp: control id + rect must match the creation order)."""
import re, sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C

# ------------------------------------------------------------------ tiny C expression evaluator
TOK = re.compile(r"\s*(?:(0[xX][0-9a-fA-F]+|\d+(?:\.\d*)?)|([A-Za-z_][\w]*)|(<<|>>|<=|>=|==|!=|&&|\|\||::|->)|(.))")
class Unknown(Exception): pass
class Sym(str): pass       # an opaque symbol (theme role, data pointer ...)

def tokenize(s):
    out = []
    pos = 0
    while pos < len(s):
        m = TOK.match(s, pos)
        if not m: break
        pos = m.end()
        if m.group(1): out.append(("n", m.group(1)))
        elif m.group(2): out.append(("i", m.group(2)))
        elif m.group(3): out.append(("o", m.group(3)))
        elif m.group(4) and not m.group(4).isspace(): out.append(("o", m.group(4)))
    return out

class Ev:
    BIN = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!="], ["<", ">", "<=", ">="], ["<<", ">>"], ["+", "-"], ["*", "/", "%"]]
    def __init__(self, env): self.env = env
    def eval(self, s):
        self.t = tokenize(s); self.p = 0
        v = self.ternary()
        return v
    def peek(self): return self.t[self.p] if self.p < len(self.t) else (None, None)
    def take(self): self.p += 1; return self.t[self.p - 1]
    def ternary(self):
        c = self.binary(0)
        if self.peek()[1] == "?":
            self.take(); a = self.ternary()
            if self.peek()[1] == ":": self.take()
            b = self.ternary()
            return a if (c and not isinstance(c, Sym)) else b
        return c
    def binary(self, lvl):
        if lvl >= len(self.BIN): return self.unary()
        v = self.binary(lvl + 1)
        while self.peek()[1] in self.BIN[lvl]:
            op = self.take()[1]; r = self.binary(lvl + 1)
            try:
                if isinstance(v, str) or isinstance(r, str): v = Sym("(%s%s%s)" % (v, op, r)); continue
                v = {"||": lambda a, b: int(bool(a or b)), "&&": lambda a, b: int(bool(a and b)), "|": lambda a, b: int(a) | int(b),
                     "^": lambda a, b: int(a) ^ int(b), "&": lambda a, b: int(a) & int(b), "==": lambda a, b: int(a == b),
                     "!=": lambda a, b: int(a != b), "<": lambda a, b: int(a < b), ">": lambda a, b: int(a > b),
                     "<=": lambda a, b: int(a <= b), ">=": lambda a, b: int(a >= b), "<<": lambda a, b: int(a) << int(b),
                     ">>": lambda a, b: int(a) >> int(b), "+": lambda a, b: a + b, "-": lambda a, b: a - b,
                     "*": lambda a, b: a * b, "/": lambda a, b: (a // b if isinstance(a, int) and isinstance(b, int) else a / b),
                     "%": lambda a, b: a % b}[op](v, r)
            except ZeroDivisionError: v = 0
        return v
    def unary(self):
        k, v = self.peek()
        if v in ("-", "!", "~", "&", "*", "+"):
            self.take(); x = self.unary()
            if isinstance(x, str): return Sym(("&" if v == "&" else "") + str(x)) if v == "&" else Sym("%s%s" % (v, x))
            return {"-": -x, "!": int(not x), "~": ~int(x), "+": x, "*": x}[v]
        if v == "(":
            # cast or parenthesised
            save = self.p
            self.take()
            if self.peek()[0] == "i" and self.peek()[1] in ("uint8_t", "uint16_t", "uint32_t", "int8_t", "int16_t", "int32_t", "int", "char", "float", "bool", "size_t", "unsigned", "const", "int64_t", "uint64_t"):
                while self.peek()[1] not in (")", None): self.take()
                self.take(); return self.unary()
            x = self.ternary()
            if self.peek()[1] == ")": self.take()
            return self.postfix(x)
        return self.postfix(self.primary())
    def primary(self):
        k, v = self.take()
        if k == "n":
            if v.lower().startswith("0x"): return int(v, 16)
            return float(v) if "." in v else int(v)
        if k == "i":
            name = v
            while self.peek()[1] in ("::",):   # scope
                self.take(); name += "::" + self.take()[1]
            return self.env_get(name)
        return 0
    def env_get(self, name):
        if name in self.env: return self.env[name]
        if name in ("nullptr", "NULL", "false"): return 0
        if name == "true": return 1
        if name.startswith("controlStyle") and name in C.STYLES: return C.STYLES[name]
        if name in ("sizeof",): return 1
        return Sym(name)
    def postfix(self, v):
        while True:
            k, t = self.peek()
            if t == "[":
                self.take(); idx = self.ternary()
                if self.peek()[1] == "]": self.take()
                v = Sym("%s[%s]" % (v, idx)) if isinstance(v, str) else v
            elif t in (".", "->"):
                self.take(); name = self.take()[1]
                if self.peek()[1] == "(":     # call
                    depth = 0
                    while True:
                        tk = self.take()
                        if tk[1] == "(": depth += 1
                        elif tk[1] == ")":
                            depth -= 1
                            if depth == 0: break
                        if self.p >= len(self.t): break
                v = Sym("%s.%s" % (v, name))
            elif t == "(" and isinstance(v, str):   # function-like macro / call: opaque
                depth = 0
                while True:
                    tk = self.take()
                    if tk[1] == "(": depth += 1
                    elif tk[1] == ")":
                        depth -= 1
                        if depth == 0: break
                    if self.p >= len(self.t): break
                v = Sym("%s()" % v)
            else: return v

# ------------------------------------------------------------------ source -> statements
def strip_comments(s):
    s = re.sub(r"//[^\n]*", "", s)
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return s

def split_top(body):
    """Split a function body into statements/blocks: ('stmt', text) | ('for', header, [..]) | ('if', cond, [..], [..]) ..."""
    i, n, out = 0, len(body), []
    while i < n:
        while i < n and body[i].isspace(): i += 1
        if i >= n: break
        if body[i] == "#":                       # preprocessor line: keep the first branch, skip the directive
            j = body.find("\n", i); i = n if j < 0 else j + 1
            continue
        if body.startswith("for", i) and re.match(r"for\s*\(", body[i:]):
            j = body.index("(", i); k = match(body, j, "(", ")")
            header = body[j + 1:k]
            blk, i = block_or_stmt(body, k + 1)
            out.append(("for", header, split_top(blk)))
            continue
        if re.match(r"(if|while|switch)\s*\(", body[i:]):
            kw = re.match(r"(if|while|switch)", body[i:]).group(1)
            j = body.index("(", i); k = match(body, j, "(", ")")
            cond = body[j + 1:k]
            blk, i = block_or_stmt(body, k + 1)
            els = None
            m = re.match(r"\s*else\b", body[i:])
            if kw == "if" and m:
                i += m.end()
                if re.match(r"\s*if\s*\(", body[i:]):
                    # else if: treat as nested if statement
                    rest_end = i
                    # parse the nested if via recursion on the remainder statement
                    sub, i2 = take_statement(body, i)
                    els = split_top(sub); i = i2
                else:
                    eblk, i = block_or_stmt(body, i); els = split_top(eblk)
            out.append((kw, cond, split_top(blk), els))
            continue
        if body[i] == "{":
            k = match(body, i, "{", "}")
            out.append(("block", split_top(body[i + 1:k]))); i = k + 1
            continue
        j = i; depth = 0
        while j < n:
            ch = body[j]
            if ch in "({[": depth += 1
            elif ch in ")}]": depth -= 1
            elif ch == ";" and depth <= 0: break
            elif ch == "{" and depth > 0: pass
            j += 1
        out.append(("stmt", body[i:j].strip())); i = j + 1
    return out

def take_statement(body, i):
    s = body[i:]
    m = re.match(r"\s*", s); i += m.end()
    if body.startswith("if", i):
        j = body.index("(", i); k = match(body, j, "(", ")")
        blk, e = block_or_stmt(body, k + 1)
        m2 = re.match(r"\s*else\b", body[e:])
        if m2:
            e2 = e + m2.end()
            if re.match(r"\s*if\s*\(", body[e2:]):
                _, e3 = take_statement(body, e2)
                return body[i:e3], e3
            _, e3 = block_or_stmt(body, e2)
            return body[i:e3], e3
        return body[i:e], e
    blk, e = block_or_stmt(body, i)
    return blk, e

def match(s, i, o, c):
    d = 0
    for j in range(i, len(s)):
        if s[j] == o: d += 1
        elif s[j] == c:
            d -= 1
            if d == 0: return j
    return len(s) - 1

def block_or_stmt(body, i):
    while i < len(body) and body[i].isspace(): i += 1
    if body[i] == "{":
        k = match(body, i, "{", "}")
        return body[i + 1:k], k + 1
    d, j = 0, i
    while j < len(body):
        if body[j] in "({[": d += 1
        elif body[j] in ")}]": d -= 1
        elif body[j] == ";" and d <= 0: break
        j += 1
    return body[i:j + 1], j + 1

def find_function(src, name):
    """Body of every definition of cClass::name(...) in src."""
    out = []
    for m in re.finditer(r"\b(\w+)::%s\s*\([^)]*\)\s*(?:const\s*)?(?:override\s*)?\{" % re.escape(name), src):
        k = match(src, m.end() - 1, "{", "}")
        out.append((m.group(1), src[m.end():k]))
    return out

# ------------------------------------------------------------------ interpreter
CTRL_RE = re.compile(r"^(?:(?P<lhs>[\w\[\]\.\->]+)\s*=\s*)?(?:\w+\.)*display\.createControl<(?P<type>\w+)>\(\s*&?(?P<props>\w+)\s*\)$")
ASSIGN_RE = re.compile(r"^(?P<obj>\w+)\.(?P<field>x|y|w|h|style|value|text|data|colors)\s*=\s*(?P<expr>.+)$", re.S)
DECL_RE = re.compile(r"^(?:const\s+)?strControlProperties\s+(\w+)(?:\s*=.*)?$")

class Interp:
    def __init__(self, consts, mod):
        self.consts = dict(consts); self.props = {}; self.out = []; self.mod = mod; self.env = dict(consts)
        self.depth = 0
    def ev(self, e):
        try: return Ev(self.env).eval(e)
        except Exception as ex: return Sym("?" + e.strip())
    def run(self, stmts, cond=False):
        for st in stmts:
            k = st[0]
            if k == "stmt": self.stmt(st[1], cond)
            elif k == "block": self.run(st[1], cond)
            elif k == "for":
                m = re.match(r"\s*(?:u?int\d*_t|int|size_t|auto)?\s*(\w+)\s*=\s*([^;]+);\s*\1\s*(<|<=)\s*([^;]+);\s*(?:\+\+\1|\1\+\+|\1\s*\+=\s*1)\s*", st[1])
                if not m: self.run(st[2], True); continue
                v0 = self.ev(m.group(2)); v1 = self.ev(m.group(4))
                if not isinstance(v0, int) or not isinstance(v1, int): self.run(st[2], True); continue
                end = v1 + (1 if m.group(3) == "<=" else 0)
                for i in range(v0, min(end, v0 + 64)):
                    self.env[m.group(1)] = i
                    self.run(st[2], cond)
                self.env.pop(m.group(1), None)
            elif k in ("if", "while"):
                c = self.ev(st[1])
                if isinstance(c, Sym):
                    self.run(st[2], True)
                    if st[3]: self.run(st[3], True)
                else:
                    if c: self.run(st[2], cond)
                    elif st[3]: self.run(st[3], cond)
            elif k == "switch": self.run(st[2], True)
    def stmt(self, s, cond):
        s = re.sub(r"\s+", " ", s).strip()
        if not s: return
        m = DECL_RE.match(s)
        if m:
            self.props[m.group(1)] = dict(x=0, y=0, w=0, h=0, style=0, value=0, text=None, data=None, colors=None); return
        m = CTRL_RE.match(s)
        if m:
            p = self.props.get(m.group("props"))
            if p is None: return
            lhs = m.group("lhs")
            if lhs: lhs = re.sub(r"\[(\w+)\]", lambda q: "[%s]" % self.env[q.group(1)] if isinstance(self.env.get(q.group(1)), int) else q.group(0), lhs)
            self.out.append(dict(type=m.group("type"), var=lhs, cond=cond, **{k: (str(v) if isinstance(v, Sym) else v) for k, v in p.items()}))
            return
        m = ASSIGN_RE.match(s)
        if m and m.group("obj") in self.props:
            v = self.ev(m.group("expr")); self.props[m.group("obj")][m.group("field")] = v; return
        # local constant / variable:  uint32_t commonStyle = ...;   const uint8_t n = 4;
        m = re.match(r"^(?:static\s+)?(?:constexpr\s+|const\s+)*(?:u?int\d*_t|int|bool|size_t|auto)\s+(\w+)\s*=\s*(.+)$", s)
        if m: self.env[m.group(1)] = self.ev(m.group(2)); return
        # control handle copy: patternControl = verticalPatternControl;  (ignored)

def class_consts(src_texts):
    """constexpr / static const integer constants from sources (enough for loops and rects)."""
    c = {}
    for t in src_texts:
        for m in re.finditer(r"(?:static\s+)?(?:constexpr|const)\s+(?:u?int\d*_t|int|size_t)\s+(\w+)\s*=\s*([^;]+);", strip_comments(t)):
            try:
                v = Ev(c).eval(m.group(2))
                if isinstance(v, (int, float)): c[m.group(1)] = v
            except Exception: pass
        for m in re.finditer(r"#define\s+(\w+)\s+(\d+)\b", strip_comments(t)):
            c.setdefault(m.group(1), int(m.group(2)))
    return c

MODULES = {   # screen id -> (source files, function owner class)
    "patternEditor":   ["interface/patternEditor/gPatternEdit.cpp"],
    "sampleEditor":    ["interface/sampleEditor/gSampleEdit.cpp"],
    "sampleImporter":  ["interface/sampleImporter/gSampleImport.cpp"],
    "samplePlayback":  ["interface/samplePlayback/gSamplePlay.cpp"],
    "sampleRecorder":  ["interface/sampleRecorder/gSampleRec.cpp"],
    "instrumentEditor":["interface/instrumentEditor/gInstrEdit.cpp"],
    "songEditor":      ["interface/songEditor/gSongEdit.cpp"],
    "projectEditor":   ["interface/projectEditor/gProjEdit.cpp"],
    "configEditor":    ["interface/configEditor/gConfigEdit.cpp"],
    "masterParams":    ["interface/masterParams/gMasterParams.cpp"],
    "performanceMode": ["interface/performanceMode/gPerformance.cpp"],
    "imageViewer":     ["interface/imageViewer/gImageViewer.cpp"],
}
GLOBALS = {   # controls created once at start-up (before the module destroy point): file, function
    "global.titleBar":   ("interface/core/titleBar.cpp", "initControls"),
    "global.songTimer":  ("interface/core/songTimer.cpp", None),
    "global.popups":     ("interface/core/interfacePopups.cpp", None),
    "global.misc":       ("interface/core/interfaceMisc.cpp", None),
    "global.menuPopup":  ("interface/core/interfaceMenuPopup.cpp", None),
}

def extract():
    consts = dict(class_consts([open(os.path.join(C.CORE, f)).read() for f in
        ["interface/core/interfaceDefs.h", "interface/sampleEditor/sampleEditor.h", "libs/NewDisplay/displayStructs.h",
         "interface/configEditor/configEditor.h", "interface/instrumentEditor/instrumentEditor.h",
         "interface/performanceMode/performanceMode.h", "interface/sampleRecorder/sampleRecorder.h",
         "interface/masterParams/masterParams.h", "interface/projectEditor/projectEditor.h"] if os.path.exists(os.path.join(C.CORE, f))]))
    table = {}
    # run-time configuration defaults (what a fresh Tracker+ has): patternInfo off, decimal numbering, no shadows
    env0 = dict(consts)
    for k in ("GUI_HANDLE_SYNTH", "FEATURE_SYNTH"): env0[k] = 1
    env0.update(MAX_16BIT=65535, RADIO_COL1=1, RADIO_COL2=2, RADIO_COL3=3, METRONOME_COL=3, LEVEL_COL=4, GAIN_COL=5,
                MONITOR_COL=6, kMenuWidth=440, kMenuHeight=330)
    for scr, files in MODULES.items():
        src = strip_comments(open(os.path.join(C.CORE, files[0])).read())
        funcs = find_function(src, "initDisplayControls")
        ctl = []
        for cls, body in funcs:
            ip = Interp(env0, scr)
            ip.run(split_top(body))
            ctl += ip.out
        table[scr] = ctl
    for g, (f, fn) in GLOBALS.items():
        src = strip_comments(open(os.path.join(C.CORE, f)).read())
        funcs = find_function(src, fn) if fn else []
        if not fn:
            # every function that creates controls
            for m in re.finditer(r"\b\w+::(\w+)\s*\([^)]*\)\s*(?:const\s*)?\{", src):
                k = match(src, m.end() - 1, "{", "}")
                b = src[m.end():k]
                if "createControl<" in b: funcs.append((m.group(1), b))
        ctl = []
        for cls, body in funcs:
            ip = Interp(env0, g); ip.run(split_top(body)); ctl += ip.out
        table[g] = ctl
    return table

if __name__ == "__main__":
    t = extract()
    json.dump(t, open(os.path.join(C.SKIN, "gen", "ctl_table.json"), "w"), indent=1)
    for k, v in t.items():
        print("%-18s %3d controls  %s" % (k, len(v), dict((tp, sum(1 for c in v if c["type"] == tp)) for tp in sorted({c["type"] for c in v}))))
    print("total", sum(len(v) for v in t.values()))
