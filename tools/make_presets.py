#!/usr/bin/env python3
"""Writes vst/presets.json: Fretty's starter sounds, listed in MPC's PRESET menu.

    python3 tools/make_presets.py

Each preset is the clean default plus a few changes, and sets every sound parameter (so picking one always gives the
same sound whatever was loaded before). THEME is never in a preset: changing sounds never changes the page's look.
Values are in each parameter's own units (0..1 for knobs) or an option's label. The first preset is the default sound.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
VST = os.path.join(HERE, "..", "vst")

SKIP = {"theme", "status", "chord", "load_state",         # look and display-only parameters
        "strum", "engine", "a_level", "b_level"}            # old ones the engine no longer reads

PRESETS = [
    # ---- acoustic (the archtop: BLEND mixes its pickup and its microphone)
    ("Clean Archtop", {}),
    ("Acoustic Strum", {"blend": 0.6, "tone": 0.9, "strum_speed": 0.25, "strum_mode": "Strum Alt", "noise": 0.6,
                        "space": 0.15}),
    ("Nylon-Style Soft", {"blend": 1.0, "tone": 0.55, "damp": 0.25, "noise": 0.25, "strum_speed": 0.5, "vibrato": 0.1,
                          "eng_mix": 0.15, "eng_color": 0.3, "eng_sustain": 0.5, "space": 0.2}),
    ("Front Porch", {"blend": 0.9, "strum_speed": 0.4, "strum_mode": "Strum Alt", "space": 0.3}),
    ("Funk Mutes", {"artic": "Palm Mute", "strum_mode": "Strum Off", "tone": 0.8, "blend": 0.2, "noise": 0.4}),
    ("Ballad Strum", {"blend": 0.5, "tone": 0.85, "strum_speed": 0.75, "vibrato": 0.1, "a_release": 0.45,
                      "space": 0.35}),
    ("Lead Lines", {"strum_mode": "Strum Off", "legato": "On", "vibrato": 0.3, "position": 7, "blend": 0.3, "space": 0.2}),
    # ---- electric (the SG, direct)
    ("Clean Electric", {"instrument": "Electric", "space": 0.12}),
    ("Chorus Clean", {"instrument": "Electric", "chorus": 0.55, "space": 0.25}),
    ("Crunch", {"instrument": "Electric", "drive": 0.45, "tone": 0.85, "strum_mode": "Strum Alt", "space": 0.12}),
    ("Distorted Rhythm", {"instrument": "Electric", "drive": 0.8, "tone": 0.7, "strum_mode": "Strum Off", "space": 0.1}),
    ("Electric Mutes", {"instrument": "Electric", "artic": "Palm Mute", "drive": 0.35, "strum_mode": "Strum Off"}),
    ("Surf-ish", {"instrument": "Electric", "drive": 0.1, "chorus": 0.25, "vibrato": 0.2, "space": 0.55}),
    ("Lead Drive", {"instrument": "Electric", "drive": 0.7, "legato": "On", "strum_mode": "Strum Off", "vibrato": 0.35,
                    "space": 0.25}),
    ("Volume Swells", {"instrument": "Electric", "a_attack": 0.55, "a_release": 0.6, "chorus": 0.35, "legato": "On",
                       "strum_mode": "Strum Off", "space": 0.6}),
    # ---- harps and strumstick
    ("Folk Harp", {"instrument": "Folk Harp", "strum_speed": 0.7, "space": 0.35}),
    ("Concert Harp Glide", {"instrument": "Concert Harp", "strum_speed": 0.9, "space": 0.45}),
    ("Strumstick Banjo-ish", {"instrument": "Strumstick", "tone": 0.95, "strum_speed": 0.15,
                              "strum_mode": "Strum Alt", "noise": 0.3, "space": 0.1}),
    ("Old-Time Picker", {"instrument": "Strumstick", "damp": 0.2, "eng_mix": 0.2, "eng_color": 0.8,
                         "eng_sustain": 0.4, "strum_mode": "Strum Off"}),
    # ---- violin (bowed, looped sustain; pizzicato)
    ("Violin Lead", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "space": 0.35}),
    ("Electric Violin", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "tone": 0.8, "drive": 0.3,
                         "chorus": 0.3, "vibrato": 0.1, "space": 0.25}),
    ("Violin Pizzicato", {"instrument": "Violin Pizz", "strum_mode": "Strum Off", "space": 0.3}),
    # ---- A + B: the instrument with the synth layer, and the synth on its own
    ("Guitar + Pad", {"b_on": "On", "wave": "Saw", "attack": 0.45, "release": 0.6, "cutoff": 0.45, "mix": 0.32,
                      "syn_detune": 0.25, "space": 0.35}),
    ("Violin + Strings Pad", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "b_on": "On",
                              "wave": "Saw", "attack": 0.6, "release": 0.6, "cutoff": 0.38, "syn_detune": 0.3,
                              "octave": -1, "mix": 0.3, "chorus": 0.3, "space": 0.45}),
    ("Plucked Glass", {"instrument": "Folk Harp", "b_on": "On", "wave": "Sine", "octave": 1, "attack": 0.02,
                       "decay": 0.35, "sustain": 0.1, "release": 0.4, "mix": 0.35, "chorus": 0.3, "space": 0.4}),
    ("Electric + Saw Lead", {"instrument": "Electric", "drive": 0.5, "b_on": "On", "wave": "Saw", "cutoff": 0.55,
                             "resonance": 0.3, "mix": 0.28, "legato": "On", "strum_mode": "Strum Off", "space": 0.2}),
    ("Synth Pluck (B Solo)", {"a_on": "Off", "b_on": "On", "wave": "Square", "attack": 0.0, "decay": 0.3,
                              "sustain": 0.2, "release": 0.25, "cutoff": 0.32, "resonance": 0.5, "b_fenv": 0.5}),
    ("Synth Pad (B Solo)", {"a_on": "Off", "b_on": "On", "wave": "Saw", "attack": 0.55, "release": 0.65,
                            "cutoff": 0.4, "syn_detune": 0.3, "chorus": 0.4, "space": 0.5}),
]


def main():
    params = json.load(open(os.path.join(VST, "params.json")))["params"]
    base = {}
    for p in params:
        k = p["key"]
        if k in SKIP or p.get("momentary") or k.startswith(("s1_", "s2_", "s3_", "s4_", "s5_", "s6_")):
            continue
        base[k] = p["default"]
    out = []
    for name, changes in PRESETS:
        assert len(name) <= 24, name
        vals = {"instrument": changes.get("instrument", base["instrument"])}   # the instrument first: it loads its samples
        for k, v in base.items():
            if k != "instrument":
                vals[k] = changes.get(k, v)
        for k in changes:
            assert k in base, (name, k)
        out.append({"name": name, "values": vals})
    json.dump({"presets": out}, open(os.path.join(VST, "presets.json"), "w"), indent=1)
    print("%d presets, %d values each" % (len(out), len(out[0]["values"])))


if __name__ == "__main__":
    main()
