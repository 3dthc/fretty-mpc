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
        "strum", "engine", "a_level", "b_level",            # old ones the engine no longer reads
        # the PERFORM page is how you play, not the sound: a preset leaves it alone (like THEME)
        "chord_mode", "chord_key", "chord_scale", "chord_voicing", "hum_time", "hum_vel", "echo_sync", "chord_deg"}

PRESETS = [  # "TAG | name": MPC's PRESET menu reads in groups (24 characters at most)
    # ---- ACO: acoustic (the archtop)
    ("ACO | Clean Archtop", {}),
    ("ACO | Acoustic Strum", {"blend": 0.6, "tone": 0.9, "strum_speed": 0.25, "strum_mode": "Strum Alt", "noise": 0.6,
                        "space": 0.15}),
    ("ACO | Nylon-Style Soft", {"blend": 1.0, "tone": 0.55, "damp": 0.25, "noise": 0.25, "strum_speed": 0.5, "vibrato": 0.1,
                          "eng_mix": 0.15, "eng_color": 0.3, "eng_sustain": 0.5, "space": 0.2}),
    ("ACO | Front Porch", {"blend": 0.9, "strum_speed": 0.4, "strum_mode": "Strum Alt", "space": 0.3}),
    ("ACO | Funk Mutes", {"artic": "Palm Mute", "strum_mode": "Strum Off", "tone": 0.8, "blend": 0.2, "noise": 0.4}),
    ("ACO | Ballad Strum", {"blend": 0.5, "tone": 0.85, "strum_speed": 0.75, "vibrato": 0.1, "a_release": 0.45,
                      "space": 0.35}),
    ("ACO | Lead Lines", {"strum_mode": "Strum Off", "legato": "On", "vibrato": 0.3, "position": 7, "blend": 0.3, "space": 0.2}),
    ("ACO | Warm Fingers", {"blend": 0.85, "tone": 0.65, "damp": 0.18, "noise": 0.18, "strum_mode": "Strum Off", "space": 0.22, "eng_mix": 0.12, "eng_color": 0.45, "eng_sustain": 0.45}),
    ("ACO | Muted Chords", {"artic": "Palm Mute", "strum_mode": "Strum Alt", "strum_speed": 0.35, "tone": 0.7, "damp": 0.25, "space": 0.1}),
    ("ACO | Bright Rhythm", {"tone": 0.98, "blend": 0.45, "strum_mode": "Strum Down", "strum_speed": 0.2, "noise": 0.35, "space": 0.15}),
    # ---- ELE: electric (the SG, direct)
    ("ELE | Clean Electric", {"instrument": "Electric", "space": 0.12}),
    ("ELE | Chorus Clean", {"instrument": "Electric", "chorus": 0.55, "space": 0.25}),
    ("ELE | Crunch", {"instrument": "Electric", "drive": 0.45, "tone": 0.85, "strum_mode": "Strum Alt", "space": 0.12}),
    ("ELE | Distorted Rhythm", {"instrument": "Electric", "drive": 0.8, "tone": 0.7, "strum_mode": "Strum Off", "space": 0.1}),
    ("ELE | Electric Mutes", {"instrument": "Electric", "artic": "Palm Mute", "drive": 0.35, "strum_mode": "Strum Off"}),
    ("ELE | Surf-ish", {"instrument": "Electric", "drive": 0.1, "chorus": 0.25, "vibrato": 0.2, "space": 0.55}),
    ("ELE | Lead Drive", {"instrument": "Electric", "drive": 0.7, "legato": "On", "strum_mode": "Strum Off", "vibrato": 0.35,
                    "space": 0.25}),
    ("ELE | Volume Swells", {"instrument": "Electric", "a_attack": 0.55, "a_release": 0.6, "chorus": 0.35, "legato": "On",
                       "strum_mode": "Strum Off", "space": 0.6}),
    ("ELE | Edge Breakup", {"instrument": "Electric", "drive": 0.25, "tone": 0.75, "chorus": 0.12, "strum_mode": "Strum Off", "space": 0.15}),
    ("ELE | Wide Clean", {"instrument": "Electric", "tone": 0.78, "chorus": 0.7, "echo": 0.12, "echo_time": 0.42, "space": 0.35}),
    ("ELE | Dark Rhythm", {"instrument": "Electric", "tone": 0.35, "drive": 0.42, "strum_mode": "Strum Down", "strum_speed": 0.25, "space": 0.18}),
    # ---- 80S: late-80s / post-punk electrics (original settings in that spirit)
    ("80s | Chorus Wash", {"instrument": "Electric", "tone": 0.9, "chorus": 0.85, "echo": 0.35, "echo_time": 0.68,
                     "space": 0.6, "strum_mode": "Strum Alt", "strum_speed": 0.35, "a_release": 0.4}),
    ("80s | Bass VI Melody", {"instrument": "Electric", "tone": 0.75, "drive": 0.15, "chorus": 0.6, "echo": 0.3,
                        "echo_time": 0.6, "legato": "On", "strum_mode": "Strum Off", "space": 0.45,
                        "volume": 1.0}),
    ("80s | Cold Wave Layers", {"instrument": "Electric", "chorus": 0.7, "echo": 0.3, "echo_time": 0.72, "space": 0.7,
                          "strum_mode": "Strum Alt", "strum_speed": 0.4, "b_on": "On", "wave": "Saw",
                          "attack": 0.55, "release": 0.65, "cutoff": 0.38, "syn_detune": 0.3, "mix": 0.3}),
    ("80s | Post-Punk Grit", {"instrument": "Electric", "drive": 0.62, "tone": 1.0, "strum_mode": "Strum Down",
                        "strum_speed": 0.15, "echo": 0.18, "echo_time": 0.25, "space": 0.28}),
    ("80s | Cold Room Clean", {"instrument": "Electric", "tone": 0.95, "drive": 0.08, "echo": 0.28, "echo_time": 0.3,
                         "space": 0.45, "strum_mode": "Strum Off", "volume": 0.95}),
    ("80s | Dreamy Bloom", {"instrument": "Electric", "tone": 0.72, "chorus": 0.65, "echo": 0.32, "echo_time": 0.68, "a_attack": 0.42, "a_release": 0.62, "strum_mode": "Strum Off", "space": 0.62}),
    # ---- STL: steel and slide: no free steel recordings, so GLIDE (LEGATO on: overlap the notes)
    ("STL | Lap Steel", {"instrument": "Electric", "legato": "On", "glide": 0.4, "vibrato": 0.25, "tone": 0.85,
                   "a_attack": 0.12, "strum_mode": "Strum Off", "chorus": 0.15, "space": 0.35}),
    ("STL | Pedal Steel Swells", {"instrument": "Electric", "legato": "On", "glide": 0.3, "vibrato": 0.15,
                            "a_attack": 0.5, "a_release": 0.5, "strum_mode": "Strum Off", "chorus": 0.3,
                            "space": 0.55}),
    ("STL | Dobro Slide", {"instrument": "Acoustic", "blend": 0.8, "legato": "On", "glide": 0.35, "vibrato": 0.3,
                     "strum_mode": "Strum Off", "space": 0.2}),
    ("STL | Weissenborn Slide", {"instrument": "Acoustic", "blend": 0.85, "tone": 0.9, "legato": "On", "glide": 0.45,
                           "vibrato": 0.35, "eng_mix": 0.2, "eng_color": 0.55, "eng_sustain": 0.9, "a_release": 0.5,
                           "strum_mode": "Strum Off", "echo": 0.15, "echo_time": 0.62, "space": 0.4}),
    # ---- HRP: harps
    ("HRP | Folk Harp", {"instrument": "Folk Harp", "strum_speed": 0.7, "space": 0.35}),
    ("HRP | Concert Harp Glide", {"instrument": "Concert Harp", "strum_speed": 0.9, "space": 0.45}),
    ("HRP | Island Harp", {"instrument": "Folk Harp", "strum_speed": 0.8, "echo": 0.2, "echo_time": 0.62, "space": 0.6}),
    ("HRP | Harp Arpeggio", {"instrument": "Concert Harp", "strum_mode": "Strum Alt", "strum_speed": 0.45, "echo": 0.18, "echo_time": 0.55, "space": 0.4}),
    # ---- VLN: violin (bowed, looped; pizzicato)
    ("VLN | Violin Lead", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "space": 0.35}),
    ("VLN | Electric Violin", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "tone": 0.8, "drive": 0.3,
                         "chorus": 0.3, "vibrato": 0.1, "space": 0.25}),
    ("VLN | Violin Pizzicato", {"instrument": "Violin Pizz", "strum_mode": "Strum Off", "space": 0.3}),
    ("VLN | Cinematic Bow", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "a_attack": 0.4, "a_release": 0.6, "vibrato": 0.18, "echo": 0.18, "echo_time": 0.65, "space": 0.65}),
    ("VLN | Folk Melody", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "a_attack": 0.05, "a_release": 0.3, "vibrato": 0.2, "glide": 0.1, "space": 0.25}),
    # ---- ZTH: zithers, and a hammered dulcimer from the dan tranh's steel strings with a bright doubled pluck
    ("ZTH | Dan Tranh", {"instrument": "Dan Tranh", "strum_speed": 0.6, "space": 0.3}),
    ("ZTH | Dan Tranh Bends", {"instrument": "Dan Tranh", "legato": "On", "glide": 0.4, "vibrato": 0.2,
                         "strum_mode": "Strum Off", "space": 0.3}),
    ("ZTH | Psaltery", {"instrument": "Psaltery", "strum_speed": 0.5, "space": 0.35}),
    ("ZTH | Bowed Psaltery", {"instrument": "Bowed Psaltery", "legato": "On", "strum_mode": "Strum Off", "space": 0.5}),
    ("ZTH | Hammered Dulcimer", {"instrument": "Dan Tranh", "tone": 1.0, "eng_mix": 0.4, "eng_color": 0.95,
                           "eng_sustain": 0.8, "eng_detune": 0.35, "chorus": 0.15, "strum_speed": 0.2,
                           "strum_mode": "Strum Alt", "space": 0.3}),
    ("ZTH | Bright Dan Tranh", {"instrument": "Dan Tranh", "tone": 0.95, "eng_mix": 0.25, "eng_color": 0.9, "strum_mode": "Strum Alt", "strum_speed": 0.25, "space": 0.25}),
    ("ZTH | Dusky Psaltery", {"instrument": "Psaltery", "tone": 0.4, "damp": 0.25, "echo": 0.15, "echo_time": 0.58, "space": 0.45}),
    # ---- FLK: folk: strumstick, mountain dulcimer, and folk-fantasy colours (saz, gusli, lute, kemenche, fiddle, drone)
    ("FLK | Strumstick Banjo", {"instrument": "Strumstick", "tone": 0.95, "strum_speed": 0.15,
                              "strum_mode": "Strum Alt", "noise": 0.3, "space": 0.1}),
    ("FLK | Old-Time Picker", {"instrument": "Strumstick", "damp": 0.2, "eng_mix": 0.2, "eng_color": 0.8,
                         "eng_sustain": 0.4, "strum_mode": "Strum Off"}),
    ("FLK | Mountain Dulcimer", {"instrument": "Strumstick", "tone": 0.95, "strum_speed": 0.3, "strum_mode": "Strum Alt",
                           "noise": 0.3, "eng_mix": 0.15, "eng_color": 0.7, "eng_sustain": 0.7, "space": 0.15}),
    ("FLK | Saz Picking", {"instrument": "Dan Tranh", "tone": 1.0, "drive": 0.12, "legato": "On", "glide": 0.22,
                     "vibrato": 0.1, "strum_mode": "Strum Alt", "strum_speed": 0.1, "echo": 0.12, "echo_time": 0.45,
                     "space": 0.35, "volume": 1.0}),
    ("FLK | Gusli Village", {"instrument": "Psaltery", "strum_mode": "Strum Alt", "strum_speed": 0.3, "eng_mix": 0.15,
                       "eng_color": 0.85, "eng_sustain": 0.75, "space": 0.45}),
    ("FLK | Wanderer's Lute", {"instrument": "Acoustic", "blend": 1.0, "tone": 0.62, "damp": 0.3, "eng_mix": 0.12,
                         "eng_color": 0.5, "eng_sustain": 0.6, "strum_mode": "Strum Down", "strum_speed": 0.55,
                         "noise": 0.3, "space": 0.4}),
    ("FLK | Kemenche Lament", {"instrument": "Violin", "tone": 0.6, "drive": 0.2, "legato": "On", "glide": 0.3,
                         "vibrato": 0.25, "strum_mode": "Strum Off", "echo": 0.1, "echo_time": 0.55, "space": 0.45}),
    ("FLK | Tavern Fiddle", {"instrument": "Violin", "tone": 0.75, "drive": 0.1, "legato": "On", "glide": 0.15,
                       "strum_mode": "Strum Off", "space": 0.3}),
    ("FLK | Wheel Drone", {"instrument": "Violin", "tone": 0.7, "drive": 0.35, "legato": "On", "strum_mode": "Strum Off",
                     "b_on": "On", "wave": "Square", "octave": -1, "attack": 0.35, "decay": 0.5, "sustain": 0.8,
                     "release": 0.45, "cutoff": 0.45, "resonance": 0.4, "syn_detune": 0.2, "mix": 0.4,
                     "space": 0.4, "volume": 0.6}),
    ("SYN | Guitar + Soft Sine", {"b_on": "On", "wave": "Sine", "attack": 0.25, "release": 0.5, "cutoff": 0.45, "mix": 0.25, "chorus": 0.2, "space": 0.35}),
    ("SYN | Violin + Low Drone", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "b_on": "On", "wave": "Square", "octave": -1, "attack": 0.3, "release": 0.6, "sustain": 0.7, "cutoff": 0.35, "mix": 0.4, "space": 0.45}),
    ("SYN | Harp + Airy Pad", {"instrument": "Folk Harp", "b_on": "On", "wave": "Triangle", "octave": 1, "attack": 0.5, "release": 0.7, "cutoff": 0.5, "mix": 0.25, "chorus": 0.3, "space": 0.5}),
    # ---- SYN: A + B: the instrument with the synth layer, and the synth on its own
    ("SYN | Guitar + Pad", {"b_on": "On", "wave": "Saw", "attack": 0.45, "release": 0.6, "cutoff": 0.45, "mix": 0.32,
                      "syn_detune": 0.25, "space": 0.35}),
    ("SYN | Violin + Pad", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "b_on": "On",
                              "wave": "Saw", "attack": 0.6, "release": 0.6, "cutoff": 0.38, "syn_detune": 0.3,
                              "octave": -1, "mix": 0.3, "chorus": 0.3, "space": 0.45}),
    ("SYN | Plucked Glass", {"instrument": "Folk Harp", "b_on": "On", "wave": "Sine", "octave": 1, "attack": 0.02,
                       "decay": 0.35, "sustain": 0.1, "release": 0.4, "mix": 0.35, "chorus": 0.3, "space": 0.4}),
    ("SYN | Electric Saw Lead", {"instrument": "Electric", "drive": 0.5, "b_on": "On", "wave": "Saw", "cutoff": 0.55,
                             "resonance": 0.3, "mix": 0.28, "legato": "On", "strum_mode": "Strum Off", "space": 0.2}),
    ("SYN | Synth Pluck B Solo", {"a_on": "Off", "b_on": "On", "wave": "Square", "attack": 0.0, "decay": 0.3,
                              "sustain": 0.2, "release": 0.25, "cutoff": 0.32, "resonance": 0.5, "b_fenv": 0.5}),
    ("SYN | Synth Pad B Solo", {"a_on": "Off", "b_on": "On", "wave": "Saw", "attack": 0.55, "release": 0.65,
                            "cutoff": 0.4, "syn_detune": 0.3, "chorus": 0.4, "space": 0.5}),

    # ---- Additional presets: balanced catalog expansion
    ("ACO | Dry Fingerstyle", {"strum_mode": "Strum Off", "tone": 0.78, "blend": 0.75, "damp": 0.12, "noise": 0.2, "space": 0.08}),
    ("80s | Gated Chime", {"instrument": "Electric", "tone": 0.92, "chorus": 0.62, "echo": 0.16, "echo_time": 0.32, "decay": 0.22, "release": 0.16, "space": 0.22}),
    ("80s | Jangle Drive", {"instrument": "Electric", "tone": 0.95, "drive": 0.22, "chorus": 0.32, "strum_mode": "Strum Down", "strum_speed": 0.2, "space": 0.18}),
    ("80s | Midnight Bass", {"instrument": "Electric", "octave": -1, "tone": 0.45, "chorus": 0.25, "drive": 0.12, "strum_mode": "Strum Off", "space": 0.18}),
    ("80s | Glass Delay", {"instrument": "Electric", "tone": 0.88, "chorus": 0.48, "echo": 0.42, "echo_time": 0.76, "a_attack": 0.12, "space": 0.4}),
    ("80s | Dry Post-Punk", {"instrument": "Electric", "tone": 0.42, "drive": 0.3, "damp": 0.12, "strum_mode": "Strum Down", "strum_speed": 0.18, "space": 0.08}),
    ("STL | Slow Bar Glide", {"instrument": "Electric", "legato": "On", "glide": 0.68, "vibrato": 0.16, "a_attack": 0.28, "a_release": 0.48, "strum_mode": "Strum Off", "space": 0.32}),
    ("STL | Crying Steel", {"instrument": "Electric", "legato": "On", "glide": 0.52, "vibrato": 0.42, "tone": 0.78, "a_release": 0.32, "strum_mode": "Strum Off", "space": 0.38}),
    ("STL | Dusty Slide", {"instrument": "Electric", "legato": "On", "glide": 0.32, "tone": 0.48, "damp": 0.18, "drive": 0.18, "strum_mode": "Strum Off", "space": 0.18}),
    ("STL | Glass Slide", {"instrument": "Electric", "legato": "On", "glide": 0.44, "tone": 0.96, "chorus": 0.38, "echo": 0.2, "echo_time": 0.58, "strum_mode": "Strum Off", "space": 0.4}),
    ("STL | Warm Slide Pad", {"instrument": "Electric", "legato": "On", "glide": 0.35, "a_attack": 0.5, "a_release": 0.68, "tone": 0.62, "chorus": 0.28, "strum_mode": "Strum Off", "space": 0.52}),
    ("STL | Dry Slide Lead", {"instrument": "Electric", "legato": "On", "glide": 0.28, "vibrato": 0.12, "tone": 0.72, "a_attack": 0.04, "a_release": 0.12, "strum_mode": "Strum Off", "space": 0.06}),
    ("STL | Echoed Weissen", {"instrument": "Electric", "legato": "On", "glide": 0.4, "tone": 0.82, "echo": 0.38, "echo_time": 0.7, "chorus": 0.22, "strum_mode": "Strum Off", "space": 0.42}),
    ("HRP | Dry Folk Harp", {"instrument": "Folk Harp", "tone": 0.82, "strum_speed": 0.65, "space": 0.08, "echo": 0.04}),
    ("HRP | Velvet Concert", {"instrument": "Concert Harp", "tone": 0.58, "strum_speed": 0.72, "a_release": 0.25, "space": 0.38}),
    ("HRP | Celestial Harp", {"instrument": "Concert Harp", "tone": 0.88, "strum_speed": 0.82, "echo": 0.28, "echo_time": 0.64, "space": 0.72}),
    ("HRP | Harp Ostinato", {"instrument": "Folk Harp", "strum_mode": "Strum Alt", "strum_speed": 0.22, "tone": 0.82, "echo": 0.1, "space": 0.22}),
    ("HRP | Dark Harp", {"instrument": "Concert Harp", "tone": 0.35, "damp": 0.2, "strum_speed": 0.58, "echo": 0.14, "echo_time": 0.42, "space": 0.38}),
    ("HRP | Harp Shimmer", {"instrument": "Concert Harp", "tone": 0.92, "strum_speed": 0.7, "chorus": 0.38, "echo": 0.3, "echo_time": 0.72, "space": 0.7}),
    ("HRP | Intimate Harp", {"instrument": "Folk Harp", "tone": 0.68, "strum_speed": 0.52, "noise": 0.2, "space": 0.16, "a_release": 0.18}),
    ("VLN | Dry Chamber", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "tone": 0.72, "vibrato": 0.08, "space": 0.08}),
    ("VLN | Slow Ensemble", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "a_attack": 0.58, "a_release": 0.72, "vibrato": 0.12, "chorus": 0.25, "space": 0.62}),
    ("VLN | Dark Cello-ish", {"instrument": "Violin", "octave": -1, "legato": "On", "strum_mode": "Strum Off", "tone": 0.38, "vibrato": 0.12, "space": 0.28}),
    ("VLN | Urgent Ostinato", {"instrument": "Violin", "legato": "Off", "strum_mode": "Strum Off", "tone": 0.86, "attack": 0.01, "release": 0.12, "space": 0.18}),
    ("VLN | Haunted Air", {"instrument": "Violin", "legato": "On", "strum_mode": "Strum Off", "a_attack": 0.65, "a_release": 0.8, "vibrato": 0.24, "echo": 0.24, "echo_time": 0.68, "space": 0.72}),
    ("VLN | Pizzicato Dry", {"instrument": "Violin Pizz", "strum_mode": "Strum Off", "tone": 0.82, "damp": 0.12, "space": 0.06}),
    ("ZTH | Dry Psaltery", {"instrument": "Psaltery", "tone": 0.78, "damp": 0.08, "space": 0.06}),
    ("ZTH | Zither Pulse", {"instrument": "Dan Tranh", "tone": 0.88, "strum_mode": "Strum Alt", "strum_speed": 0.18, "echo": 0.12, "echo_time": 0.36, "space": 0.18}),
    ("ZTH | Soft Bell Zither", {"instrument": "Psaltery", "tone": 0.9, "eng_mix": 0.2, "eng_color": 0.82, "eng_sustain": 0.68, "echo": 0.2, "echo_time": 0.62, "space": 0.42}),
    ("ZTH | Bowed Drone", {"instrument": "Bowed Psaltery", "legato": "On", "strum_mode": "Strum Off", "a_attack": 0.4, "a_release": 0.62, "vibrato": 0.18, "space": 0.48}),
    ("FLK | Dry Strumstick", {"instrument": "Strumstick", "tone": 0.78, "strum_speed": 0.3, "noise": 0.22, "space": 0.06}),
    ("FLK | Drone Lute", {"instrument": "Strumstick", "octave": -1, "tone": 0.58, "legato": "On", "glide": 0.12, "eng_mix": 0.16, "eng_sustain": 0.78, "space": 0.32}),
    ("SYN | Pluck + Sub", {"b_on": "On", "wave": "Triangle", "octave": -1, "attack": 0.01, "decay": 0.28, "sustain": 0.24, "release": 0.16, "cutoff": 0.48, "mix": 0.48, "space": 0.12}),
    ("SYN | Dark Pulse Solo", {"a_on": "Off", "b_on": "On", "wave": "Square", "octave": -1, "attack": 0.01, "decay": 0.32, "sustain": 0.55, "release": 0.18, "cutoff": 0.28, "resonance": 0.32, "b_fenv": 0.24, "mix": 1.0, "space": 0.12}),
    ("BAS | Round Sub", {"a_on": "Off", "b_on": "On", "wave": "Sine", "octave": -2, "attack": 0.01, "decay": 0.25, "sustain": 0.78, "release": 0.18, "cutoff": 0.42, "mix": 1.0, "space": 0.02}),
    ("BAS | Deep Triangle", {"a_on": "Off", "b_on": "On", "wave": "Triangle", "octave": -2, "attack": 0.01, "decay": 0.3, "sustain": 0.7, "release": 0.22, "cutoff": 0.36, "mix": 1.0, "space": 0.02}),
    ("BAS | Picked Low", {"instrument": "Electric", "octave": -1, "tone": 0.6, "damp": 0.2, "strum_mode": "Strum Off", "drive": 0.12, "space": 0.04}),
    ("BAS | Muted Low", {"instrument": "Electric", "octave": -1, "artic": "Palm Mute", "tone": 0.42, "damp": 0.35, "strum_mode": "Strum Off", "space": 0.02}),
    ("BAS | Grit Pulse", {"a_on": "Off", "b_on": "On", "wave": "Saw", "octave": -1, "attack": 0.01, "decay": 0.3, "sustain": 0.62, "release": 0.16, "cutoff": 0.38, "resonance": 0.22, "drive": 0.3, "mix": 1.0, "space": 0.02}),
    ("BAS | Soft Bass Pad", {"a_on": "Off", "b_on": "On", "wave": "Triangle", "octave": -1, "attack": 0.28, "decay": 0.38, "sustain": 0.72, "release": 0.42, "cutoff": 0.42, "mix": 1.0, "space": 0.12}),
    ("BAS | Rubber Synth", {"a_on": "Off", "b_on": "On", "wave": "Square", "octave": -1, "attack": 0.01, "decay": 0.22, "sustain": 0.42, "release": 0.12, "cutoff": 0.52, "resonance": 0.38, "b_fenv": 0.36, "mix": 1.0, "space": 0.02}),
    ("BAS | Warm Hybrid", {"instrument": "Electric", "octave": -1, "b_on": "On", "wave": "Sine", "tone": 0.58, "attack": 0.02, "decay": 0.3, "sustain": 0.65, "release": 0.2, "cutoff": 0.48, "mix": 0.42, "space": 0.04}),
    ("BAS | Dark Saw Bass", {"a_on": "Off", "b_on": "On", "wave": "Saw", "octave": -1, "attack": 0.01, "decay": 0.35, "sustain": 0.68, "release": 0.18, "cutoff": 0.25, "resonance": 0.28, "syn_detune": 0.08, "mix": 1.0, "space": 0.02}),
    ("BAS | Wide Bass", {"a_on": "Off", "b_on": "On", "wave": "Triangle", "octave": -1, "attack": 0.02, "decay": 0.3, "sustain": 0.68, "release": 0.2, "cutoff": 0.5, "chorus": 0.22, "syn_detune": 0.18, "mix": 1.0, "space": 0.08}),
    ("BAS | Sub Drop Db", {"a_on": "Off", "b_on": "On", "drop": "On", "wave": "Sine", "octave": -2, "attack": 0.01, "decay": 0.42, "sustain": 0.82, "release": 0.24, "cutoff": 0.32, "mix": 1.0, "space": 0.02}),

]


def main():
    params = json.load(open(os.path.join(VST, "params.json")))["params"]
    base = {}
    for p in params:
        k = p["key"]
        if k in SKIP or p.get("momentary") or p.get("display") == "string" or k.startswith(("s1_", "s2_", "s3_", "s4_", "s5_", "s6_")):
            continue
        base[k] = p["default"]
    out = []
    # Keep the MPC preset menu in alphabetical category/name order.
    ordered_presets = sorted(
        PRESETS,
        key=lambda item: (item[0].split(" | ", 1)[0], item[0].split(" | ", 1)[1].casefold())
    )
    for name, changes in ordered_presets:
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
