#!/usr/bin/env python3
"""Fixture: the 'Pattern 1' page of the drawn set (patternEditor_normal_page1) as strTrackerPattern values -> preview PNG."""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C, model, tui, pattern, scene_ref, preview
def cell(n="", i="", a="", b=""): return dict(note=n or "---", instr=i or "--", fx0=a or "----", fx1=b or "----")
MODES_USED = pattern.MODES if os.environ.get('ALLMODES') else pattern.CORE_MODES
def build(mode, outpng, sel=None):
    P = model.Params(); P.preload(json.load(open(os.path.join(C.PORT, "platform", "params.json")))["params"])
    S = tui.Skin(P, "/tmp/_unused_skin"); pat = pattern.Pattern(P, S, MODES_USED, []); pat.declare()
    rows = [[cell() for _ in range(15)] for _ in range(16)]
    # tracks 1-4 of the page, rows 0..14 (labels 14,15,16,1,...,12)
    for (c, r, n, i, a, b) in [(0,1,"C3","01","V 64",""),(0,3,"C3","01","V100",""),(0,7,"C3","01","",""),(0,11,"C3","01","C 50",""),(0,13,"D#3","01","","s 40"),
                               (1,1,"CUT","","",""),(1,5,"D3","02","P  0",""),(1,9,"OFF","","",""),(1,13,"E3","02","","r<<<"),
                               (2,3,"A2","03","T120",""),(2,7,"FAD","","",""),(2,11,"F#2","03","",""),
                               (3,0,"","","","D 10"),(3,4,"C4","04","",""),(3,8,"D4","04","V 80",""),(3,12,"E4","04","","")]:
        rows[c][r] = cell(n, i, a, b)
    t = dict(rows=rows, actualTrack=0, actualStep=4, firstVisibleTrack=0, patternLength=16, stepDevider=4, showPlayhead=False, playheadPosition=0,
             selectState=1 if not sel else 2, selectedParam=0, selectColor=1, selectStartTrack=0, selectEndTrack=0, selectStartStep=4, selectEndStep=4,
             popupMode=0, inactive=[0] * 16, showTrackNames=True, names=["Track %d" % (i + 1) for i in range(16)])
    if sel: t.update(sel)
    sc = scene_ref.pattern_scene(pat, t, mode, from1=True)
    os.makedirs("/tmp/_unused_skin", exist_ok=True)
    return sc
if __name__ == "__main__":
    sc = build(0, None)
    json.dump(sc, open(sys.argv[1], "w"))
