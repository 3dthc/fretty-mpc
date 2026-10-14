# Fretty

A guitar instrument for Akai MPC OS standalone devices (native VST2, built with
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)), with a synth layer as a twist.
Status: **v0.3.0, testing on a device.**

At its core it plays real, clean, raw recordings on a six-string playing model; drive, chorus, the pluck layer and
the synth turn that into many more sounds without a sample library.

## Layer A: the instrument (INSTRUMENT ◀ ▶ on the PLAY page)
| Instrument | Recording | Notes |
| --- | --- | --- |
| Acoustic | Karoryfer Shinyguitar, an archtop | pickup and microphone of the same take: BLEND mixes them |
| Electric | Karoryfer Emilyguitar, an Epiphone SG (flatwounds) | straight from the pickups, no amp: add DRIVE / CHORUS |
| Folk Harp | VCSL | stereo |
| Concert Harp | VCSL | stereo |
| Strumstick | VCSL, finger-picked | between a banjo and a guitar |

All CC0 (public domain), so the plugin can be shared. Each instrument loads the first time it is picked (a few
seconds on the MPC; silence until then) and stays loaded while Fretty is in use.

## How it plays
- **Six strings.** Every note is placed on a string and fret. A chord that arrives together is spread across strings
  like a real voicing; POSITION fixes the hand at a fret (AUTO follows the music), CAPO and DROP D♭ change the strings.
- **One sound per string.** A new note on a string cuts the old one off. With **LEGATO** on, playing a new note while the
  previous key on that string is still held slurs into it (a hammer-on or pull-off) instead of re-picking.
- **STRUM.** Chords are strummed low string to high; STRUM SPD sets the gap between strings.
- **Articulations:** SUSTAIN or PALM MUTE (also on keyswitches A♯0 / B0).
- **PLUCK** layers a plucked-string model under the samples (PLUCK level and RING on the PLAY page, COLOR and DETUNE on
  the SYNTH page).
- Mod wheel adds vibrato; pitch bend is ±2 semitones. Velocity picks soft / medium / hard recordings.
- The neck shows each played string glowing at its fret, settling as it fades.

## Layer B: the synth (SYNTH page)
Every note can also play two detuned oscillators (SINE, TRIANGLE, SAW, SQUARE, picked with ◀ ▶ and shown on the
screen) through a resonant low-pass FILTER and an ATTACK / DECAY / SUSTAIN / RELEASE envelope, with OCTAVE and DETUNE.
**A · GUITAR** and **B · SYNTH** switch each layer on or off: both on to layer them, one off to solo the other;
A LEVEL and B LEVEL balance them.

## Effects (PLAY page)
DRIVE (an amp-style clipper, from warm to distorted), CHORUS, SPACE (room reverb). Each is completely off at zero, so
the default sound is the raw recording.

## Presets
23 starters in MPC's PRESET menu: acoustic (Clean Archtop, Acoustic Strum, Nylon-Style Soft, Front Porch, Funk
Mutes, Ballad Strum, Lead Lines), electric (Clean Electric, Chorus Clean, Crunch, Distorted Rhythm, Electric Mutes,
Surf-ish, Lead Drive), Folk Harp, Concert Harp Glide, Strumstick Banjo-ish, Old-Time Picker, and A + B sounds
(Guitar + Pad, Plucked Glass, Electric + Saw Lead, Synth Pluck, Synth Pad). Presets never change the THEME.
There are no free nylon-string recordings that can be shared, so "Nylon-Style Soft" is the archtop's microphone,
softened and darkened. `tools/make_presets.py` writes `vst/presets.json`.

## Extra keys
| Keys | Sound |
| --- | --- |
| C0–F0 (12–17) | dead (muted) string (acoustic and electric) |
| F♯0–A0 (18–21) | muted strings strummed (acoustic) / finger squeak (electric) |
| A♯0 / B0 (22 / 23) | keyswitch: Sustain / Palm Mute |
| C1–D♯1 (24–27) | body knocks (acoustic) |
| A1 and up | the instrument (D♭2 with DROP D♭ on) |

## Build
Next to a checkout of `mpc-vst-plugins`, from Ubuntu (WSL) with Docker and `python3`:

```sh
bash make_release.sh
```

The instruments ship ready in `engine/*.smp` (losslessly compressed, ~46 MB in all); the plugin needs that `engine/`
folder next to `fretty.so` in its plugin folder (`make_release.sh` puts it there).

**The essential sample sets:** up to three velocity layers and the two most typical takes of each, notes up to 4 s
with a natural fade, two takes of every release and noise. Each instrument gets a level trim so they all play at the
same loudness. `tools/pack_samples.py` rebuilds them from the sources (needs `python3-numpy`):

```sh
python3 -I tools/pack_samples.py engine --shiny <karoryfer.shinyguitar> --emily <karoryfer.emilyguitar> --vcsl <VCSL>
```

`tools/check_codec.c` checks every file decodes bit-exactly.

## The pages
`tools/make_skin.py` draws all the artwork (backgrounds per theme for both pages, the chrome wordmark, the knob, the
switches, the waveform screens and the string pictures) and writes `vst/layout.conf`; its output in `vst/art/` is
committed, so the build only reads it. Each string has two display filmstrips driven by the engine (`s<n>_vib`: ring
level 0-3, `s<n>_fret`: the lit fret). MPC only swaps those pictures when a value changes (a few times per note).

## Offline checks
- `../mpc-vst-plugins/tools/test_port.sh vst/vst.json` (ASan/UBSan host test, presets included)
- `tools/render_demo.c`: plays through the engine into a WAV (`render_demo engine out.wav [tour | pitch <instrument>]`).

## Credits
Samples: Karoryfer Lecolds and Versilian Studios, all CC0. Font: Yellowtail (Apache 2.0). See [CREDITS.md](CREDITS.md).
