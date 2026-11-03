"""Parameter registry + widget list (the in-memory form of layout.csv) for the TrackerXXL skin.
Every parameter is a scene slot: the bridge writes it, the skin shows it.
  opt   option parameter; numeric value = option index (state, drives when=/IndexedEnabling), optional dynamic text
        (get_param("<key>_display") -> the Value label text: wrapper 'dynamic_display')
  int   whole-number parameter 0..max (filmstrip frame / picture level)
  trig  momentary trigger written by the skin (touch) and read by the bridge (input event)
  enc   relative encoder (Q-Link / data wheel) 0..127 whole numbers
"""
import csv, os

class Params:
    def __init__(self):
        self.list = []          # dicts in VST index order
        self.index = {}
        self.regions = {}       # name -> (base index, count)
    def preload(self, ext):
        """The platform's own parameters (port/platform/params.json) keep their VST indices; scene slots are appended."""
        for e in ext:
            self.add(e["key"], "ext", raw=e)
        self.n_ext = len(ext)
    def add(self, key, kind, **kw):
        assert key not in self.index, key
        p = dict(key=key, kind=kind, **kw)
        self.index[key] = len(self.list)
        self.list.append(p)
        if kind == "opt" and kw.get("text"):
            # a text slot is a PAIR: <key> = state (what shows / which colour), <key>_t = text serial.  MPC only repaints a
            # readout when the value of the parameter it is bound to changes, so the text widgets bind to <key>_t, whose value
            # is a 16-bit hash of the text (changes exactly when the text does) and whose display string is the text itself.
            self.add(key + "_t", "txt")
        return self.index[key]
    def region(self, name, keys_kinds):
        base = len(self.list)
        for key, kind, kw in keys_kinds:
            self.add(key, kind, **kw)
        self.regions[name] = (base, len(self.list) - base)
        return base
    def to_json(self):
        out = []
        for p in self.list:
            k = p["kind"]
            if k == "ext":
                out.append(p["raw"]); continue
            e = {"key": p["key"], "name": p.get("name", p["key"])[:31]}
            if k == "txt":
                e.update(min=0, max=65535, default=0, display="int", dynamic_display=True, poll=False)
            elif k == "opt":
                n = p["n"]
                e["options"] = [str(i) for i in range(n)]
                e["default"] = 0
                e["poll"] = False
            elif k == "int":
                e.update(min=0, max=p["max"], default=0, display="int")
            elif k == "trig":
                e.update(min=0, max=1, default=0, momentary=True)
            elif k == "enc":
                e.update(min=0, max=127, default=64, display="int")
            out.append(e)
        return {"params": out}

class Widget:
    """One skin element.  Columns of layout.csv: id,screen,type,x,y,w,h,label,live,px,parameter (+when,colour,extra)."""
    __slots__ = ("id", "screen", "type", "x", "y", "w", "h", "label", "live", "px", "parameter", "when", "colour", "extra")
    def __init__(self, **kw):
        for k in self.__slots__: setattr(self, k, kw.get(k, ""))
    def row(self):
        return [getattr(self, k) for k in self.__slots__]

CSV_COLS = list(Widget.__slots__)

def write_layout_csv(path, widgets):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(CSV_COLS)
        for wd in widgets: w.writerow(wd.row())

def read_layout_csv(path):
    out = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            out[r["id"]] = r
    return out
