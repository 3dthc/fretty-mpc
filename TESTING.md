# Device test (v0.8.0)

Goal: confirm Fretty v0.8.0 builds, loads with its four pages, plays every instrument (now with the zithers), and stays light on the CPU.

**If you installed the earlier test build (Archtop):** remove it first (`uninstall.sh` from its zip, or the installer
app), since Fretty is a new plugin with its own folder.

## 1. Install (Ubuntu in WSL)
Download all three zips to your Windows **Downloads** folder: `fretty-mpc-v0.8.0.zip`, `fretty-instruments-1-v0.6.0.zip`
and `fretty-instruments-2-v0.6.0.zip` (the instruments are split only because of a download size limit; they have not
changed since v0.6.0).
Start Docker Desktop, then paste this one line into Ubuntu:

```sh
rm -rf ~/fretty-mpc && python3 -m zipfile -e /mnt/c/Users/*/Downloads/fretty-mpc-v0.8.0.zip ~/ && bash ~/fretty-mpc/install.sh
```

`install.sh` adds the instruments from the other two zips, checks Docker and the toolchain (`~/mpc-vst-plugins`, cloned
if missing), builds, and copies `Fretty-0.8.0-mpc-armv7.zip` into your Windows Downloads. It stops with a plain
message if something is missing; fix that and run `bash ~/fretty-mpc/install.sh` again.
Expected at the end of the build output:
- `exported: VSTPluginMain`
- `highest glibc: GLIBC_2.x` (2.31 or lower is ideal; above 2.36 would not load)
- `dist/Fretty-0.8.0-mpc-armv7.zip`, about 68 MB

The first build with the new page also downloads the toolchain's artwork renderer image (Playwright, ~1-2 GB, once).

If the build fails, send me the last ~30 lines of output.

## 2. Install
Same as your other ports: the zip's `install.sh` (MPC is stopped, `MPC.settings` is backed up, MPC restarts), or drop
the zip into the MPC plugin installer app. The plugin folder is `3dthc - VST - Fretty` and holds `fretty.so`,
`Plugin Skins/` and `engine/` (~67 MB). RAM: ~72 MB for the acoustic, plus each other instrument once you pick it
(electric ~73 MB, harps ~24-32 MB, strumstick ~36 MB, violin ~28 MB, dan tranh ~25 MB, psaltery ~10 MB; all eight
together ~300 MB).
Because the instrument list grew, open a fresh Fretty rather than a project saved with an older version.

## 3. Checks on the MPC
| # | Check | How | Expect |
| --- | --- | --- | --- |
| 1 | It loads | Plugin browser → Fretty on a new track | the fretboard page appears, no crash |
| 2 | Samples load | Wait a few seconds after inserting (they are decoded into RAM) | notes play (silence before that is expected) |
| 3 | Single notes | One pad at a time | one clean guitar note per pad, natural ring |
| 4 | **Velocity** | Pads soft → hard (turn **Full Level** off) | soft hits clearly quieter and darker, hard hits louder and brighter |
| 5 | Chords + strum | Press 3–4 pads together | strummed low string to high; Strum Off = together |
| 6 | Legato | Hold a note, play the next one higher | slurs (no new pick) |
| 7 | Palm mute | ARTICULATION → Palm Mute, or play B0 | short, muted notes |
| 8 | Blend | BLEND fully left / right | pickup (rounder) / mic (airier) |
| 9 | Q-Links | Turn Q-Links | knobs follow |
| 10 | Save / reload | Save project, reload | settings come back |
| 11 | CPU | Play fast chords for a minute | no crackles or dropouts |
| 12 | Long notes | Hold a low chord 5+ s | rings and fades out naturally, no clicks |
| 13 | Strings | Play a chord, then single notes | each played string glows with its fret lit, and settles as it fades |
| 14 | Themes | THEME ◀ ▶ arrows (they wrap around) | the whole page changes look |
| 15 | Screen load | Play busy chords with the page open | the screen stays responsive |
| 16 | Instruments | INSTRUMENT ◀ ▶ (PLAY row, or LAYER A) through all ten | "LOADING SAMPLES…" over the neck, then it plays; similar loudness |
| 17 | Dimmed knobs | Pick Electric, then a harp | BLEND dims (archtop only); NOISE dims on the harps, strumstick, violin |
| 18 | Violin | Violin: hold a note 10 s; LEGATO on, play a line | it keeps bowing while held (no gap or click every few seconds); slurs smoothly |
| 19 | Pizzicato | Violin Pizz: notes and chords | short plucked notes; chords strum |
| 20 | Strum | STRUM ◀ ▶: Strum Down / Up / Alt / Off, play chords | down = low string first, up = high first, Alt = alternates while you keep strumming |
| 20b | Zithers | Dan Tranh, Psaltery, Bowed Psaltery: notes and chords | in tune with the guitars; the bowed one sustains while held |
| 20c | Glide | Lap Steel preset: play a line, overlapping each note with the next | each note slides into the next |
| 21 | Effects | DRIVE, CHORUS, ECHO, SPACE, PLUCK up and back to zero | at zero the sound is exactly the clean one |
| 21b | Echo | ECHO up, play one note, turn ECHO TIME while it repeats | repeats bounce left / right and fade; TIME glides with no clicks |
| 22 | Layer B | LAYER B page: B · SYNTH on, wave ◀ ▶, CUTOFF, FILTER ENV, envelope | the screen shows the wave; FILTER ENV adds a "pluck" to the synth |
| 23 | Mix + solo | MIX A / B left, middle, right (both on); then A off, then B off | left = mostly A, middle = both full, right = mostly B; a layer alone plays at full |
| 24 | Swell | LAYER A: SWELL up, play notes; RELEASE up | notes fade in like a volume knob; released notes ring on |
| 24b | Chord pads | PERFORM: CHORDS Triads, KEY A, SCALE Minor; tap pads (chromatic) | each pad strums a full chord; the tiles read Am Bdim C Dm Em F G and the playing one lights |
| 24c | Voicing / 7ths | VOICING Open / Barre / High; CHORDS 7ths | the shapes move (low and full / high and close); names show the 7ths |
| 24d | Feel | TIMING and VELOCITY up, play a steady pattern | slightly loose, never late enough to feel off |
| 24e | Echo sync | ECHO up on PLAY, ECHO SYNC on, turn ECHO TIME | the value reads 1/8, 1/4D...; repeats land on the beat at the project tempo |
| 24f | Memory | Pick several instruments one after another, wait 20 s, pick another | no glitches; an instrument picked again loads again |
| 25 | Q-Links | Each page: turn the Q-Links | they move that page's knobs |
| 26 | Presets | PRESET menu: try several from each group | each changes the sound, never the theme |
| 27 | CPU | Busy chords with a drive + synth preset for a minute | no crackles (`tools/bench.sh` gives the numbers) |

Also note roughly how long check 2 takes: the first insert decodes the samples (a few seconds on the MPC).

Report back which numbers passed, and anything that sounded wrong (a recording on the MPC helps).
