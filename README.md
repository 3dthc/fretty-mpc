# Archtop (working name)

A sampled archtop guitar instrument for Akai MPC OS standalone devices (native VST2, built with
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)). Status: **engine v0.1, not yet tested on a device.**
The touchscreen page is still a placeholder (auto-layout) until the UI design is signed off.

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
Next to a checkout of `mpc-vst-plugins`, from Ubuntu (WSL) with Docker, `python3` and `python3-numpy`:

```sh
vst/build.sh
```

The first build clones Shinyguitar (pinned commit) into `third_party/` and packs it into `vst/build/engine/archtop.smp`
(~250 MB). The plugin needs that `engine/` folder next to `archtop.so` in its plugin folder.

**Memory:** the pack is memory-mapped, not loaded: the first 0.35 s of every sample (~26 MB) sits in RAM so notes
start instantly, and a background thread reads each playing note's next second ahead of time.

## Offline checks
- `../mpc-vst-plugins/tools/test_port.sh vst/vst.json` (ASan/UBSan host test)
- `tools/render_demo.c`: plays a demo through the engine and writes a WAV (`render_demo <dir with archtop.smp> out.wav`).

## Credits
Samples: Shinyguitar by Karoryfer Lecolds, CC0. See [CREDITS.md](CREDITS.md).
