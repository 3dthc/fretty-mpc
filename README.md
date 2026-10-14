# Fretty

A sampled archtop guitar instrument for Akai MPC OS standalone devices (native VST2, built with
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)). Status: **engine v0.1, not yet tested on a device.**
The touchscreen page: a horizontal fretboard whose strings light up and ring as you play, 14 knobs, the playing
switches, and three themes (MPC Black, Sunburst, MPC Grey).

## How it plays
- **Six strings.** Every note is placed on a string and fret. A chord that arrives together is spread across strings
  like a real voicing; POSITION fixes the hand at a fret (AUTO follows the music), CAPO and DROP D♭ change the strings.
- **One sound per string.** A new note on a string cuts the old one off. With **LEGATO** on, playing a new note while the
  previous key on that string is still held slurs into it (a hammer-on or pull-off) instead of re-picking.
- **STRUM.** Chords are strummed low string to high; STRUM SPEED sets the gap between strings.
- **Articulations:** SUSTAIN or PALM MUTE (also on keyswitches A♯0 / B0).
- **ENGINE** layers a plucked-string model under the samples (MIX, COLOR, SUSTAIN, DETUNE).
- Mod wheel adds vibrato; pitch bend is ±2 semitones.

## Controls
| Control | What it does |
| --- | --- |
| BLEND | magnetic pickup ↔ microphone (both recorded on the same takes) |
| TONE | brightness |
| DAMP | how quickly notes die away (palm-mute-like at the top) |
| VIBRATO | delayed vibrato depth |
| POSITION / CAPO | hand position (AUTO or a fret) / capo fret |
| STRUM SPEED | gap between strings in a strum |
| NOISE | release and finger noise level |
| SPACE / VOLUME | room reverb / output level |

## Extra keys
| Keys | Sound |
| --- | --- |
| C0–F0 (12–17) | dead (muted) string, one per string |
| F♯0–A0 (18–21) | muted strings strummed |
| A♯0 / B0 (22 / 23) | keyswitch: Sustain / Palm Mute |
| C1–D♯1 (24–27) | body knocks |
| A1 and up | the guitar (D♭2 with DROP D♭ on) |

## Build
Next to a checkout of `mpc-vst-plugins`, from Ubuntu (WSL) with Docker and `python3` (`python3-numpy` only to re-pack the samples):

```sh
vst/build.sh
```

The samples ship ready in `engine/fretty.smp`; the plugin needs that `engine/` folder next to `fretty.so` in its
plugin folder (`make_release.sh` puts it there).

**The essential sample set:** from Shinyguitar's 4 velocity layers x 4 takes, Fretty keeps 3 layers (soft, medium,
hard) and the 2 most typical takes of each, notes up to 4 s with a natural fade, and 2 takes of every release and noise.
It is stored losslessly compressed (~20 MB) and decoded into RAM when the plugin loads (~72 MB, shared by all
instances). `tools/pack_samples.py` rebuilds it from the source (`--full` keeps everything; much larger).
`tools/check_codec.c` checks the decode is bit-exact.

## The page
`tools/make_skin.py` draws all the artwork (backgrounds per theme, the chrome wordmark, the knob, the switches and
the string pictures) and writes `vst/layout.conf`; its output in `vst/art/` is committed, so the build only reads it.
Each string has two display filmstrips driven by the engine (`s<n>_vib`: ring level 0-3, `s<n>_fret`: the lit
fret). MPC only swaps those pictures when a value changes (a few times per note), never animates continuously.

## Offline checks
- `../mpc-vst-plugins/tools/test_port.sh vst/vst.json` (ASan/UBSan host test)
- `tools/render_demo.c`: plays a demo through the engine and writes a WAV (`render_demo <dir with fretty.smp> out.wav`).

## Credits
Samples: Shinyguitar by Karoryfer Lecolds, CC0. See [CREDITS.md](CREDITS.md).
