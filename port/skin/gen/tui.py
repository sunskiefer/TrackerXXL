"""TUI.json writer for the TrackerXXL skin.  Component shapes are the ones tools/shadow_skin.py already emits and
that were verified on a Force (readout Value label, IndexedEnabling-gated Image, Knob FilmStrip meter, Button);
only the way they are assembled differs (no layout.conf / art renderer: every image is made here, with Pillow).
All coordinates are canvas pixels (1280 x 628), y from the top of the plugin area."""
import os, sys, json
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "..", "zynth", "third_party", "mpc-vst-plugins", "tools"))
sys.path.insert(0, "/root/work/zynth/third_party/mpc-vst-plugins/tools")
import shadow_skin as SS
from PIL import Image

AKAI = SS.AKAI

class Skin:
    def __init__(self, params, outdir, name="TrackerXXL", vendor="L'Cronx"):
        self.P = params                      # model.Params
        self.dir = outdir                    # <...>/Plugin Skins
        os.makedirs(self.dir, exist_ok=True)
        self.name, self.vendor = name, vendor
        self.kids = []                       # components of the single page, in z-order (later = on top)
        self.defs = {}
        self.images = {}                     # filename -> True (written)
        self.count = {"text": 0, "image": 0, "strip": 0, "button": 0, "enum": 0}
    # ---------------------------------------------------------------- gating
    def when(self, key, opt):
        """IndexedEnabling handle string: shown only while option parameter `key` is at option `opt`."""
        i = self.P.index[key]
        n = self.P.list[i]["n"]
        assert 0 <= opt < n, (key, opt, n)
        return "IndexedEnabling/%d/%d/Parameter %d" % (opt, n, i)
    def _gate(self, comp, when):
        if when:
            comp["bounds"]["showWhenDataModelInvalid"] = "Show"
            comp["bounds"]["additionalInvalidatingHandles"] = [when] if isinstance(when, str) else list(when)
        return comp
    def _placed(self, ctype, name, pidx, x, y, w, h, focus="No", show="Hide", extra=None):
        comp = {"version": 2,
                "componentData": {"version": 1, "name": name, "type": ctype, "data": {"version": 1, "handleName": "Data"}},
                "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % pidx}] + [{"key": k, "value": "Parameter %d" % v} for k, v in (extra or {}).items()]},
                "bounds": SS._bounds(x, y, w, h, focus=focus, show=show)}
        return comp
    # ---------------------------------------------------------------- live text (Value label of a parameter)
    def text(self, key, x, y, w, h, px, colour, weight="Regular", font="Roboto", just="left", when=None, name="t"):
        jst = {"left": "left verticallyCentred", "center": "horizontallyCentred verticallyCentred",
               "right": "right verticallyCentred"}[just]
        dk = "txt_%dx%d_%d_%s_%s_%s_%s" % (w, h, px, colour, weight, font, just)
        if dk not in self.defs:
            self.defs[dk] = SS._local(dk, [], [SS._value_label(0, 0, w, h, float(px), colour, jst, style=weight, font=font)])
        bind = key + "_t" if (key + "_t") in self.P.index else key      # text pairs: bound to the text serial, gated by the state
        c = self._placed(dk, name, self.P.index[bind], x, y, w, h)
        self.kids.append(self._gate(c, when))
        self.count["text"] += 1
        return c
    # ---------------------------------------------------------------- picture (static or gated)
    def image(self, png, x, y, w, h, when=None, name="img"):
        c = SS._sub("Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": png},
                    SS._bounds(x, y, w, h, focus="No", show="Show"), name)
        self.kids.append(self._gate(c, when))
        self.count["image"] += 1
        return c
    def save_png(self, name, im):
        if name not in self.images:
            im.save(os.path.join(self.dir, name))
            self.images[name] = True
        return name
    # ---------------------------------------------------------------- filmstrip bound to an int parameter
    def strip(self, key, png, frames, x, y, w, h, when=None, name="strip"):
        dk = "strip_%s_%dx%d_f%d" % (os.path.splitext(png)[0], w, h, frames)
        if dk not in self.defs:
            self.defs[dk] = SS._local(dk, [], [SS._sub("Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": png,
                                                                "numFrames": frames, "invert": False,
                                                                "dragOrientation": "Vertical", "handleName": "Data"},
                                                       SS._bounds(0, 0, w, h), "Strip")])
        c = self._placed(dk, name, self.P.index[key], x, y, w, h)
        self.kids.append(self._gate(c, when))
        self.count["strip"] += 1
        return c
    # ---------------------------------------------------------------- touch: momentary button (transparent plate)
    def button(self, key, x, y, w, h, when=None, name="btn"):
        hit = SS.write_hit_png(self.dir, 8, 8) if False else self._hit_png()
        dk = "hit_%dx%d" % (w, h)
        if dk not in self.defs:
            self.defs[dk] = SS._local(dk, [SS._action("Mouse Down", "Toggle Switch")], [SS._button(hit, hit, 1, 1, w, h)])
        c = self._placed(dk, name, self.P.index[key], x, y, w, h, focus="No")
        self.kids.append(self._gate(c, when))
        self.count["button"] += 1
        return c
    def _hit_png(self):
        n = "tx_hit.png"
        if n not in self.images:
            Image.new("RGBA", (8, 8), (0, 0, 0, 1)).save(os.path.join(self.dir, n)); self.images[n] = True
        return n
    # ---------------------------------------------------------------- shield: a transparent plate with NO action on top of a display-only filmstrip
    # (a filmstrip is a Knob: a finger drag on it would change the parameter the engine drives)
    def shield(self, key, x, y, w, h, when=None, name="shield"):
        hit = self._hit_png()
        dk = "shield_%dx%d" % (w, h)
        if dk not in self.defs:
            self.defs[dk] = SS._local(dk, [], [SS._button(hit, hit, 0, 1, w, h)])
        c = self._placed(dk, name, self.P.index[key], x, y, w, h, focus="No")
        self.kids.append(self._gate(c, when))
        self.count["button"] += 1
    # ---------------------------------------------------------------- touch: transparent enum (n segments) -> option index
    def enum(self, key, x, y, w, h, vertical=False, when=None, name="enum", segments=None, first=1):
        n = self.P.list[self.P.index[key]]["n"]
        hit = self._hit_png()
        nseg = segments or (n - first)
        assert first + nseg == n, (key, first, nseg, n)
        for o in range(first):      # parked option(s): a 1 px sliver at the box corner, so the switch group has every option (skin_check OPTS)
            dk = "seg_%dx%d_%d_%d" % (1, 1, o, n)
            if dk not in self.defs:
                self.defs[dk] = SS._local(dk, [SS._action("Mouse Down", "Q-Link")], [SS._button(hit, hit, o, n, 1, 1)])
            c = self._placed(dk, "%s park %d" % (name, o), self.P.index[key], x + w - 1, y, 1, 1, focus="No")
            self.kids.append(self._gate(c, when)); self.count["enum"] += 1
        for o0 in range(nseg):
            o = o0 + first
            if vertical: sx_, sy_, sw_, sh_ = x, y + (h * o0) // nseg, w, (h * (o0 + 1)) // nseg - (h * o0) // nseg
            else: sx_, sy_, sw_, sh_ = x + (w * o0) // nseg, y, (w * (o0 + 1)) // nseg - (w * o0) // nseg, h
            dk = "seg_%dx%d_%d_%d" % (sw_, sh_, o, n)
            if dk not in self.defs:
                self.defs[dk] = SS._local(dk, [SS._action("Mouse Down", "Q-Link")], [SS._button(hit, hit, o, n, sw_, sh_)])
            c = self._placed(dk, "%s %d" % (name, o), self.P.index[key], sx_, sy_, sw_, sh_, focus="No")
            self.kids.append(self._gate(c, when))
            self.count["enum"] += 1
    # ---------------------------------------------------------------- Q-Link bound invisible readout
    def qlink_anchor(self, key, x, y, w, h):
        dk = "qanchor_%dx%d" % (w, h)
        if dk not in self.defs:
            self.defs[dk] = SS._local(dk, [SS._action("Mouse Down", "Q-Link")], [SS._focus(w, h),
                                      SS._value_label(0, 0, w, h, 26.0, "none", "left verticallyCentred")])
        self.kids.append(self._placed(dk, "qanchor", self.P.index[key], x, y, w, h, focus="Yes"))
    # ---------------------------------------------------------------- the data wheel: a focused control; turning it moves `key`, pressing it fires `enter_key`
    def wheel_anchor(self, key, enter_key, x, y, w, h):
        dk = "wheelanchor_%dx%d" % (w, h)
        if dk not in self.defs:
            self.defs[dk] = SS._local(dk, [SS._action("Mouse Down", "Q-Link"), SS._action("Enter Pressed", "Toggle Switch", handle="Enter")],
                                      [SS._focus(w, h), SS._value_label(0, 0, w, h, 26.0, "none", "left verticallyCentred")])
        self.kids.append(self._placed(dk, "wheel", self.P.index[key], x, y, w, h, focus="Yes", extra={"Enter": self.P.index[enter_key]}))
    # ---------------------------------------------------------------- write
    def write(self, tabs, bg_hex="000000"):
        """tabs: list of dict(name, kids, qkeys) - one Force tab per screen. Tab t sits on function key min(t,7); the tabs past 8
        are sub-tabs of the last key. qkeys = the (up to 8) parameters that tab's Q-Links/macros turn."""
        clear = {"version": 1, "colour": "ff" + bg_hex, "image": ""}
        tabj, qmap = [], []
        for t, tab in enumerate(tabs):
            fk, sp = min(t, 7), max(0, t - 7)
            comp = "TrackerXXL|%s" % tab["name"]
            self.defs[comp] = {"key": comp, "value": {
                "version": 4, "actions": [],
                "backgroundData": {"version": 1, "focussed": clear, "unfocussed": dict(clear)},
                "ignoreMousePresses": False, "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": True,
                "componentsData": tab["kids"]}}
            tabj.append({"version": 3, "tabName": tab["name"], "fnKeyIndex": fk, "fnKeySubIndex": sp, "qlinkBoundsData": ["0 0 0 0"],
                         "componentName": comp, "initialSize": "0 0 %d %d" % (1280, 628), "scale": 1.0})
            ql = {"Q-Link %d" % (q + 1): -1 for q in range(16)}
            for s, k in enumerate(tab.get("qkeys", [])):
                ql["Q-Link %d" % SS.qlink_for_slot(s)] = self.P.index[k]
            qmap.append({"Tab": fk + 1, "SubTab": sp + 1, "Bank Direction": "Column", "Q-Links": ql})
        comps = list(self.defs.values())   # SS._local() entries are already {"key":..,"value":..}
        tui = {"pageData": {"version": 1,
                            "componentDefinitions": {"version": 2, "importFiles": [AKAI + "Generic/Generic Knob Overlay.json",
                                                                                  AKAI + "Generic/Generic Menu Overlay.json"],
                                                     "localComponentDefinitions": comps},
                            "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabj}}
        qj = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
              "Screen Mode Q-Links": {"version": 4, "map": qmap}, "Program Mode Q-Links": dict(qmap[0]["Q-Links"])}
        text = json.dumps(tui, separators=(",", ":"))
        open(os.path.join(self.dir, "TUI.json"), "w").write(text)
        for f in ("Q-Links.json", "Q-Links - 8by1.json"):
            open(os.path.join(self.dir, f), "w").write(json.dumps(qj, indent=1))
        return len(text)
