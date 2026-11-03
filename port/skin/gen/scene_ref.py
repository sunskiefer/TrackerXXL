"""Reference model of the bridge's pattern-editor mapping (bridge/src/map_pattern.cpp does the same in C++).
Used only to make a fixture scene for preview.py and to check the generated parameter set; nothing here runs on the Force."""
import pattern as PT

def fnv16(s):
    h = 2166136261
    for ch in s.encode(): h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFFFF

def put(scene, key, state, text=None):
    scene[key] = state
    if text is not None: scene[key + "_t"] = [fnv16(text), text]

def pattern_scene(pat, t, mode, hexnum=False, from1=False):
    """pat = pattern.Pattern (declared), t = dict mirroring strTrackerPattern, mode = patternViewMode (cTracker value)."""
    M = pat.modes; mi = M.index(mode)
    pm = t.get("popupMode", 0)
    sc = {}
    tw = PT.tw_of(mode)
    cc = 8 if mode else 4
    if pm & 2: cc = 6 if mode else 3
    if pm & 4: cc = 4 if mode else 2
    npar = 4 if mode == 0 else bin(mode).count("1")
    rc = 8 if pm & 1 else 15
    first = t["firstVisibleTrack"]; act = t["actualStep"]; L = t["patternLength"]
    put(sc, "pvis", 1)
    rown = [0] * 15; endlines = False
    for i in range(rc):
        if not t["rows"][0][i]["note"]: continue
        row = act - 6 + i
        if row < 1:
            endlines = True; row = L + int(row - L * int(row / L))   # C '%' truncates toward zero
        elif row > L:
            endlines = True; row = (row - 1) % L + 1
        rown[i] = row
    for k, x in enumerate(pat.line_x):
        on = any(PT.LEFT + tw * i == x for i in range(cc))
        put(sc, "pl%02d" % k, 1 if on else 0)
    types = PT.types_of(mode)
    sel2 = t["selectState"] == 2
    if sel2:
        x0, x1 = sorted((t["selectStartTrack"], t["selectEndTrack"])); y0, y1 = sorted((t["selectStartStep"], t["selectEndStep"]))
    sp = t["selectedParam"]; mark = (sp if t["selectState"] else None)
    mtrack = t["actualTrack"] - first
    cursor_text = None
    for r in range(15):
        ph = t["showPlayhead"] and act - 8 < t["playheadPosition"] < act + 8 and (t["playheadPosition"] - act + 7) == r
        kind = 2 if ph else (1 if (L > 1 and rown[r] and (rown[r] - 1) % t["stepDevider"] == 0 and r < rc) else 0)
        line = 1 if (endlines and r >= 1 and r < rc and rown[r] == 1) else 0
        put(sc, "pb%02d" % r, kind * 2 + line)
        if rown[r]:
            n = rown[r] if from1 else rown[r] - 1
            s = ("%02X" % (n % 256)) if hexnum else str(n)
            put(sc, "pn%02d" % r, 2 if pm & 6 else 1, s)
        else:
            put(sc, "pn%02d" % r, 0, "")
        for (c, T) in pat.used:
            key = "pc%02d%02d" % (r, c * 4 + T)
            sid = pat.var.get((mode, c, T), -1)
            if r >= rc or c >= cc or T not in types or sid < 0:
                put(sc, key, 0, ""); continue
            f = t["rows"][first + c][r]
            text = [f["note"], f["instr"], f["fx0"], f["fx1"]][T]
            inactive = t["inactive"][first + c]
            sel = False
            if sel2 and sp == T and x0 <= first + c <= x1 and y0 <= act - 7 + r <= y1 and ((npar != 1) or ((1 << sp) & mode)):
                sel = True
            if sel: k = 3 + t["selectColor"]
            elif r == 7 and mark == T and c == mtrack:
                put(sc, key, 0, ""); cursor_text = text; continue
            elif inactive: k = 2
            elif text.startswith("-"): k = 1
            else: k = 0
            put(sc, key, 1 + PT.NK * sid + k, text)
    # cursor
    if t["selectState"] and 0 <= mtrack <= cc:
        pos = pat.mode_pos.get((mode, mtrack, sp), -1) if mtrack < 8 else -1
        if pos >= 0:
            put(sc, "pcur", 1 + pos * 6 + (3 if t["selectState"] == 1 else 0) + t["selectColor"])
            put(sc, "pcp", 1 + pos); put(sc, "pct", 1 if cursor_text else 0, cursor_text or "")
        else: put(sc, "pcur", 0); put(sc, "pcp", 0); put(sc, "pct", 0, "")
    else: put(sc, "pcur", 0); put(sc, "pcp", 0); put(sc, "pct", 0, "")
    # track names
    cls = "ptA" if mode == 0 else "ptB"; L_ = PT.NAME_MAXLEN[4 if mode == 0 else 8]
    for i in range(8):
        if t["showTrackNames"] and i < cc:
            nm = t["names"][first + i][:L_]
            put(sc, "pt%d" % i, 1 if mode == 0 else 2, nm)
            sc["%s%d" % (cls, i)] = (L_ if t["inactive"][first + i] else 0) + len(nm)
        else:
            put(sc, "pt%d" % i, 0, "")
            for c_ in ("ptA", "ptB"):
                if "%s%d" % (c_, i) in pat.P.index: sc["%s%d" % (c_, i)] = 0
    return sc
