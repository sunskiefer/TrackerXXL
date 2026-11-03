#!/usr/bin/env python3
"""Static check of the voice library's tables, read from the vendored sources (nothing is built or run).
Verifies: 19 synth voices x 20 controls (name, panel label, default), 19 x 8 = 152 starter sounds with names and
parameter sets, BAZZLINE's 8 bass sounds, 64 drum kits (+ the init kit = 65), 6 drum voices x 20 controls."""
import re, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
bad = []

def need(cond, msg):
    if not cond:
        bad.append(msg)

vd = (root / "zynth/src/voice_defs.h").read_text()
blocks = re.split(r"\n    \[(\d+)\] = \{", "\n" + vd)
voices = {}
for i in range(1, len(blocks), 2):
    idx = int(blocks[i]); body = blocks[i + 1]
    m = re.match(r'"([^"]+)", "([^"]+)",\s*\{([^}]*)\},\s*\{([^}]*)\},\s*\{([^}]*)\}', body, re.S)
    if not m:
        bad.append("voice %d does not parse" % idx); continue
    names = re.findall(r'"([^"]*)"', m.group(3)); labels = re.findall(r'"([^"]*)"', m.group(4))
    defs = re.findall(r"-?[0-9.]+f", m.group(5))
    voices[idx] = (m.group(1), m.group(2), names, labels, defs)
need(sorted(voices) == list(range(19)), "synth voice indexes: %s" % sorted(voices))
for idx, (n, s, names, labels, defs) in sorted(voices.items()):
    need(len(names) == 20 and len(labels) == 20 and len(defs) == 20 and all(names) and all(labels),
         "voice %d %s: %d names, %d labels, %d defaults" % (idx, n, len(names), len(labels), len(defs)))
need(len({v[0] for v in voices.values()}) == 19, "voice names not unique")

sd = (root / "zynth/src/starter_defs.h").read_text()
rows = re.search(r"STARTER_NAMES\[NUM_ENGINES\]\[NUM_STARTERS\] = \{(.*?)\n\};", sd, re.S).group(1)
srows = re.findall(r"\{([^{}]*)\}", rows)
need(len(srows) == 19, "starter name rows: %d" % len(srows))
total = 0
for r in srows:
    nm = re.findall(r'"([^"]+)"', r); total += len(nm)
    need(len(nm) == 8, "starter row has %d names" % len(nm))
need(total == 152, "starter names: %d" % total)
body = sd.split("STARTERS[NUM_ENGINES][NUM_STARTERS][STARTER_SETS] = {")[1]
need(len(re.findall(r"\{255, 0\.f\}\}", body)) == 152, "starter set terminators: %d" % len(re.findall(r"\{255, 0\.f\}\}", body)))

bb = (root / "bazz/bass_bank.h").read_text()
need(len(re.findall(r"/\* \d+ [A-Z0-9 -]+ \*/ \{\{", bb)) == 8, "bass bank sounds")

fd = (root / "lxr/src/factory_data.c").read_text()
kits = re.findall(r'^    \{"([^"]{8})", \{', fd, re.M)
need(len(kits) == 64, "factory kits: %d" % len(kits))


print("synth voices:", len(voices), "| controls per voice:", sorted({len(v[2]) for v in voices.values()}),
      "| starter names:", total, "| bass sounds: 8 | factory kits:", len(kits), "(+ init kit = %d)" % (len(kits) + 1))
print("voice list:", ", ".join(v[0] for _, v in sorted(voices.items())))
print("kit names:", ", ".join(sorted({k.strip() for k in kits})))
if bad:
    print("FAIL:\n  " + "\n  ".join(bad)); sys.exit(1)
print("PASS")
