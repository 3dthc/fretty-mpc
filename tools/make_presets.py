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
    # ---- zithers and dulcimers
    ("Dan Tranh", {"instrument": "Dan Tranh", "strum_speed": 0.6, "space": 0.3}),
    ("Dan Tranh Bends", {"instrument": "Dan Tranh", "legato": "On", "glide": 0.4, "vibrato": 0.2,
                         "strum_mode": "Strum Off", "space": 0.3}),
    ("Psaltery", {"instrument": "Psaltery", "strum_speed": 0.5, "space": 0.35}),
    ("Bowed Psaltery", {"instrument": "Bowed Psaltery", "legato": "On", "strum_mode": "Strum Off", "space": 0.5}),
    # no free dulcimer recordings: a hammered one from the dan tranh's steel strings with a bright, doubled pluck
    # (hammered dulcimers have two or more strings per note), a mountain one from the strumstick (a close cousin)
    ("Hammered Dulcimer", {"instrument": "Dan Tranh", "tone": 1.0, "eng_mix": 0.4, "eng_color": 0.95,
                           "eng_sustain": 0.8, "eng_detune": 0.35, "chorus": 0.15, "strum_speed": 0.2,
                           "strum_mode": "Strum Alt", "space": 0.3}),
    ("Mountain Dulcimer", {"instrument": "Strumstick", "tone": 0.95, "strum_speed": 0.3, "strum_mode": "Strum Alt",
                           "noise": 0.3, "eng_mix": 0.15, "eng_color": 0.7, "eng_sustain": 0.7, "space": 0.15}),
    # ---- steel guitar: no free steel recordings either; the slide is GLIDE (LEGATO on: overlap the notes)
    ("Lap Steel", {"instrument": "Electric", "legato": "On", "glide": 0.4, "vibrato": 0.25, "tone": 0.85,
                   "a_attack": 0.12, "strum_mode": "Strum Off", "chorus": 0.15, "space": 0.35}),
    ("Pedal Steel Swells", {"instrument": "Electric", "legato": "On", "glide": 0.3, "vibrato": 0.15,
                            "a_attack": 0.5, "a_release": 0.5, "strum_mode": "Strum Off", "chorus": 0.3,
                            "space": 0.55}),
    ("Dobro Slide", {"instrument": "Acoustic", "blend": 0.8, "legato": "On", "glide": 0.35, "vibrato": 0.3,
                     "strum_mode": "Strum Off", "space": 0.2}),
    ("Weissenborn Slide", {"instrument": "Acoustic", "blend": 0.85, "tone": 0.9, "legato": "On", "glide": 0.45,
                           "vibrato": 0.35, "eng_mix": 0.2, "eng_color": 0.55, "eng_sustain": 0.9, "a_release": 0.5,
                           "strum_mode": "Strum Off", "echo": 0.15, "echo_time": 0.62, "space": 0.4}),
    # ---- late-80s / post-punk electric sounds (original settings in that spirit)
    ("Chorus Wash", {"instrument": "Electric", "tone": 0.9, "chorus": 0.85, "echo": 0.35, "echo_time": 0.68,
                     "space": 0.6, "strum_mode": "Strum Alt", "strum_speed": 0.35, "a_release": 0.4}),
    ("Bass VI Melody", {"instrument": "Electric", "tone": 0.75, "drive": 0.15, "chorus": 0.6, "echo": 0.3,
                        "echo_time": 0.6, "legato": "On", "strum_mode": "Strum Off", "space": 0.45,
                        "volume": 1.0}),
    ("Cold Wave Layers", {"instrument": "Electric", "chorus": 0.7, "echo": 0.3, "echo_time": 0.72, "space": 0.7,
                          "strum_mode": "Strum Alt", "strum_speed": 0.4, "b_on": "On", "wave": "Saw",
                          "attack": 0.55, "release": 0.65, "cutoff": 0.38, "syn_detune": 0.3, "mix": 0.3}),
    ("Post-Punk Grit", {"instrument": "Electric", "drive": 0.62, "tone": 1.0, "strum_mode": "Strum Down",
                        "strum_speed": 0.15, "echo": 0.18, "echo_time": 0.25, "space": 0.28}),
    ("Cold Room Clean", {"instrument": "Electric", "tone": 0.95, "drive": 0.08, "echo": 0.28, "echo_time": 0.3,
                         "space": 0.45, "strum_mode": "Strum Off", "volume": 0.95}),
    # ---- folk-fantasy: Slavic / Turkish / island folk colours (long-necked lutes, gusli, bowed fiddles,
    # hurdy-gurdy drones), built from the real instruments here: no free recordings of those can be shared
    ("Saz Picking", {"instrument": "Dan Tranh", "tone": 1.0, "drive": 0.12, "legato": "On", "glide": 0.22,
                     "vibrato": 0.1, "strum_mode": "Strum Alt", "strum_speed": 0.1, "echo": 0.12, "echo_time": 0.45,
                     "space": 0.35, "volume": 1.0}),
    ("Gusli Village", {"instrument": "Psaltery", "strum_mode": "Strum Alt", "strum_speed": 0.3, "eng_mix": 0.15,
                       "eng_color": 0.85, "eng_sustain": 0.75, "space": 0.45}),
    ("Wanderer's Lute", {"instrument": "Acoustic", "blend": 1.0, "tone": 0.62, "damp": 0.3, "eng_mix": 0.12,
                         "eng_color": 0.5, "eng_sustain": 0.6, "strum_mode": "Strum Down", "strum_speed": 0.55,
                         "noise": 0.3, "space": 0.4}),
    ("Kemenche Lament", {"instrument": "Violin", "tone": 0.6, "drive": 0.2, "legato": "On", "glide": 0.3,
                         "vibrato": 0.25, "strum_mode": "Strum Off", "echo": 0.1, "echo_time": 0.55, "space": 0.45}),
    ("Tavern Fiddle", {"instrument": "Violin", "tone": 0.75, "drive": 0.1, "legato": "On", "glide": 0.15,
                       "strum_mode": "Strum Off", "space": 0.3}),
    ("Wheel Drone", {"instrument": "Violin", "tone": 0.7, "drive": 0.35, "legato": "On", "strum_mode": "Strum Off",
                     "b_on": "On", "wave": "Square", "octave": -1, "attack": 0.35, "decay": 0.5, "sustain": 0.8,
                     "release": 0.45, "cutoff": 0.45, "resonance": 0.4, "syn_detune": 0.2, "mix": 0.4,
                     "space": 0.4, "volume": 0.6}),
    ("Island Harp", {"instrument": "Folk Harp", "strum_speed": 0.8, "echo": 0.2, "echo_time": 0.62, "space": 0.6}),
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
