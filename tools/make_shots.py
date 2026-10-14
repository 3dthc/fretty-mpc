#!/usr/bin/env python3
"""Pictures of Fretty's pages for the README (docs/images/): each page drawn from vst/layout.conf and vst/art/
exactly as MPC lays it out, with the engine's own values and text (tools/shot_state.c), plus a GIF of the strings
animating through a strummed chord.

    cc -O2 -I../mpc-vst-plugins/wrapper tools/shot_state.c src/engine.c -lm -lpthread -o /tmp/shot_state
    python3 tools/make_shots.py /tmp/shot_state           (needs Pillow; the fonts come from mpc-vst-plugins)

MPC draws its live text (knob values, stepper names, readouts) in Titillium Web; this draws the same with the same
font, so the pictures read like the device's screen (sizes are the layout's; MPC's own anti-aliasing differs a bit).
"""
import json
import os
import re
import shlex
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
VST = os.path.join(ROOT, "vst")
OUT = os.path.join(ROOT, "docs", "images")
FONTS = os.path.join(ROOT, "..", "mpc-vst-plugins", "tools", "html_art", "fonts")
Y_OFF = 86
INK = (239, 233, 216)
STATE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/shot_state"

PARAMS = json.load(open(os.path.join(VST, "params.json")))["params"]
PBY = {p["key"]: p for p in PARAMS}
PRESETS = {p["name"]: p["values"] for p in json.load(open(os.path.join(VST, "presets.json")))["presets"]}


def font(px, weight="SemiBold"):
    return ImageFont.truetype(os.path.join(FONTS, "TitilliumWeb-%s.ttf" % weight), int(round(px)))


def parse_layout():
    tabs, cur = [], None
    for line in open(os.path.join(VST, "layout.conf")):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"\[tab (.+)\]", line)
        if m:
            cur = {"name": m.group(1), "items": []}
            tabs.append(cur)
            continue
        if cur is None:
            continue
        parts = shlex.split(line)
        kind, attrs = parts[0], {}
        for a in parts[1:]:
            if "=" in a:
                k, v = a.split("=", 1)
                attrs[k] = v
        cur["items"].append((kind, attrs))
    return tabs


def settings_for(preset, extra):
    vals = dict(PRESETS[preset]) if preset else {}
    vals.update(extra)
    lines = []
    for k, v in vals.items():
        if isinstance(v, str) and k in PBY and "options" in PBY[k]:
            v = PBY[k]["options"].index(v)
        lines.append("%s %s" % (k, v))
    return lines


def engine_state(lines, chord=None, noplay=False):
    keys = [p["key"] for p in PARAMS if not p.get("momentary")] + ["chord_%d" % i for i in range(1, 8)]
    inp = list(lines) + ["#key %s" % k for k in keys]
    if chord:
        inp.append("#chord " + ",".join(str(c) for c in chord))
    if noplay:
        inp.append("#noplay 1")
    out = subprocess.run([STATE, os.path.join(ROOT, "engine")], input="\n".join(inp) + "\n", capture_output=True,
                         text=True, check=True).stdout
    st = {}
    for row in out.splitlines():
        k, v, d = (row.split("\t") + ["", ""])[:3]
        st[k] = (v, d)
    return st


def norm(key, val):
    p = PBY.get(key)
    if not p:
        return 0.0
    if "options" in p:
        n = len(p["options"])
        return float(val) / (n - 1) if n > 1 else 0.0
    lo, hi = float(p.get("min", 0)), float(p.get("max", 1))
    return (float(val) - lo) / (hi - lo) if hi > lo else 0.0


def visible(attrs, st):
    w = attrs.get("when")
    if not w:
        return True
    key, opt = w.split(":", 1)
    cur = int(round(float(st.get(key, ("0", ""))[0] or 0)))
    p = PBY.get(key, {})
    if opt.isdigit():
        return cur == int(opt)
    return "options" in p and p["options"].index(opt) == cur


def strip_frame(path, frames, idx):
    im = Image.open(os.path.join(VST, path)).convert("RGBA")
    W, H = im.size
    idx = max(0, min(frames - 1, idx))
    if W > H:      # frames side by side
        fw = W // frames
        return im.crop((idx * fw, 0, (idx + 1) * fw, H))
    fh = H // frames
    return im.crop((0, idx * fh, W, (idx + 1) * fh))


def paste_center(page, im, cx, cy, w=None, h=None):
    if w and h and im.size != (w, h):
        im = im.resize((w, h), Image.LANCZOS)
    page.alpha_composite(im, (int(round(cx - im.size[0] / 2)), int(round(cy - im.size[1] / 2))))


def text_center(draw, s, cx, cy, px, weight="SemiBold", fill=INK, maxw=None):
    f = font(px, weight)
    while maxw and px > 9 and draw.textlength(s, font=f) > maxw:
        px -= 1
        f = font(px, weight)
    draw.text((cx, cy), s, font=f, fill=fill, anchor="mm")


def option_text(key, st):
    p = PBY[key]
    i = int(round(float(st[key][0])))
    return p["options"][max(0, min(len(p["options"]) - 1, i))]


def render(tab, st):
    page = Image.new("RGBA", (1280, 628), (11, 11, 11, 255))
    draw = ImageDraw.Draw(page)
    for kind, a in tab["items"]:
        if not visible(a, st):
            continue
        if kind == "art":
            im = Image.open(os.path.join(VST, a["file"])).convert("RGBA").resize((int(a["w"]), int(a["h"])), Image.LANCZOS)
            page.alpha_composite(im, (int(a["x"]), int(a["y"]) - Y_OFF))
        elif kind == "meter":
            v = int(round(float(st.get(a["key"], ("0", ""))[0] or 0)))
            fr = strip_frame(a["strip"], int(a["frames"]), v)
            paste_center(page, fr, int(a["cx"]), int(a["cy"]) - Y_OFF, int(a["w"]), int(a["h"]))
        elif kind == "knob":
            key = a["key"]
            fr = strip_frame(a["strip"], int(a["frames"]), int(round(norm(key, st[key][0]) * 127)))
            cx, cy = int(a["cx"]), int(a["cy"]) - Y_OFF
            paste_center(page, fr, cx, cy)
            text_center(draw, st[key][1] or st[key][0], cx, cy + 61, int(a.get("vs", 23)), maxw=int(a.get("bw", 136)) - 8)
        elif kind == "toggle":
            on = float(st[a["key"]][0] or 0) > 0.5
            im = Image.open(os.path.join(VST, a["img_on"] if on else a["img"])).convert("RGBA")
            paste_center(page, im, int(a["cx"]), int(a["cy"]) - Y_OFF, int(a["w"]), int(a["h"]))
        elif kind == "stepper":
            text_center(draw, option_text(a["key"], st), int(a["cx"]), int(a["cy"]) - Y_OFF, int(a.get("tsize", 17)),
                        maxw=int(a["w"]) - 2 * int(a["h"]) - 16)
        elif kind == "picture":
            files = a["files"].split(",")
            i = int(round(float(st[a["key"]][0])))
            im = Image.open(os.path.join(VST, files[max(0, min(len(files) - 1, i))])).convert("RGBA")
            page.alpha_composite(im.resize((int(a["w"]), int(a["h"])), Image.LANCZOS), (int(a["x"]), int(a["y"]) - Y_OFF))
        elif kind == "readout":
            txt = st.get(a["key"], ("", ""))[0]
            col = tuple(int(a.get("tcolor", "efe9d8")[i:i + 2], 16) for i in (0, 2, 4))
            text_center(draw, txt, int(a["cx"]), int(a["cy"]) - Y_OFF, int(a.get("tsize", 20)),
                        "Bold" if a.get("tweight") == "bold" else "SemiBold", col, int(a["w"]))
    return page


def save(im, name):
    os.makedirs(OUT, exist_ok=True)
    im.convert("RGB").save(os.path.join(OUT, name), optimize=True)
    print("wrote docs/images/%s" % name)


def main():
    tabs = {t["name"]: t for t in parse_layout()}
    AM = [45, 52, 57, 60, 64]
    # PLAY in each theme (a chord ringing, so the strings show)
    for th, name in ((0, "black"), (1, "sunburst"), (2, "grey")):
        st = engine_state(settings_for("ACO | Acoustic Strum", {"theme": th}), AM)
        save(render(tabs["PLAY"], st), "play_%s.png" % name)
    st = engine_state(settings_for("80S | Chorus Wash", {}), AM)
    save(render(tabs["PLAY"], st), "play_electric.png")            # BLEND and NOISE dimmed
    save(render(tabs["LAYER A"], engine_state(settings_for("STL | Weissenborn Slide", {}), AM)), "layer_a.png")
    save(render(tabs["LAYER B"], engine_state(settings_for("SYN | Guitar + Pad", {}), AM)), "layer_b.png")
    perf = {"chord_mode": 1, "chord_key": 9, "chord_scale": 1, "chord_voicing": 0, "hum_time": 0.3, "hum_vel": 0.25}
    save(render(tabs["PERFORM"], engine_state(settings_for("ACO | Acoustic Strum", perf), [65])), "perform.png")

    # the strings through a strummed chord: the engine's own s<n>_vib / s<n>_fret, every 40 ms
    lines = settings_for("ACO | Acoustic Strum", {"strum_speed": 0.6})
    out = subprocess.run([STATE, os.path.join(ROOT, "engine"), "strum", "40"], input="\n".join(lines + ["#chord 43,47,50,55,59,67"]) + "\n",
                         capture_output=True, text=True, check=True).stdout
    base = render(tabs["PLAY"], engine_state(settings_for("ACO | Acoustic Strum", {}), noplay=True))
    neck_items = [it for it in tabs["PLAY"]["items"] if it[0] == "meter" and it[1]["key"][:1] == "s"]
    frames = []
    rows = [r.split("\t") for r in out.splitlines() if r.startswith("t\t")]
    rows = [[None, "0"] + ["0"] * 12] * 6 + rows      # a moment of quiet before the strum
    for r in rows:
        st = {}
        for i in range(6):
            st["s%d_vib" % (i + 1)] = (r[2 + i], "")
            st["s%d_fret" % (i + 1)] = (r[8 + i], "")
        im = base.copy()
        for kind, a in neck_items:
            v = int(st[a["key"]][0])
            fr = strip_frame(a["strip"], int(a["frames"]), v)
            paste_center(im, fr, int(a["cx"]), int(a["cy"]) - Y_OFF, int(a["w"]), int(a["h"]))
        frames.append(im.crop((0, 0, 1280, 226)).convert("RGB").resize((960, 170), Image.LANCZOS))
    pal = frames[len(frames) // 3].quantize(colors=128, method=Image.MEDIANCUT)
    q = [f.quantize(palette=pal, dither=Image.NONE) for f in frames]
    q[0].save(os.path.join(OUT, "strum.gif"), save_all=True, append_images=q[1:], duration=40, loop=0, optimize=True)
    print("wrote docs/images/strum.gif (%d frames)" % len(q))


if __name__ == "__main__":
    main()
