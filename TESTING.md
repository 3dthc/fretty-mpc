# Device test (v0.2.0)

Goal: confirm Fretty builds, loads on the MPC with its fretboard page, and plays with velocity.

**If you installed the earlier test build (Archtop):** remove it first (`uninstall.sh` from its zip, or the installer
app), since Fretty is a new plugin with its own folder.

## 1. Build (Ubuntu in WSL)
Put this folder in your Ubuntu home next to `mpc-vst-plugins`:

```
~/mpc-vst-plugins/
~/fretty-mpc/        <- this folder
```

Then:

```sh
cd ~/fretty-mpc
chmod +x make_release.sh vst/build.sh
./make_release.sh
```

The samples are already packed in `engine/fretty.smp` (~20 MB), so nothing is downloaded.
Expected at the end of the build output:
- `exported: VSTPluginMain`
- `highest glibc: GLIBC_2.x` (2.31 or lower is ideal; above 2.36 would not load)
- `dist/Fretty-0.2.0-mpc-armv7.zip`, about 23 MB

The first build with the new page also downloads the toolchain's artwork renderer image (Playwright, ~1-2 GB, once).

If the build fails, send me the last ~30 lines of output.

## 2. Install
Same as your other ports: the zip's `install.sh` (MPC is stopped, `MPC.settings` is backed up, MPC restarts), or drop
the zip into the MPC plugin installer app. The plugin folder is `3dthc - VST - Fretty` and holds `fretty.so`,
`Plugin Skins/` and `engine/fretty.smp` (~20 MB). In use it takes ~72 MB of RAM.

## 3. Checks on the MPC
| # | Check | How | Expect |
| --- | --- | --- | --- |
| 1 | It loads | Plugin browser → Fretty on a new track | the fretboard page appears, no crash |
| 2 | Samples load | Wait a few seconds after inserting (they are decoded into RAM) | notes play (silence before that is expected) |
| 3 | Single notes | One pad at a time | one clean guitar note per pad, natural ring |
| 4 | **Velocity** | Pads soft → hard (turn **Full Level** off) | soft hits clearly quieter and darker, hard hits louder and brighter |
| 5 | Chords + strum | Press 3–4 pads together | strummed low string to high; STRUM off = together |
| 6 | Legato | Hold a note, play the next one higher | slurs (no new pick) |
| 7 | Palm mute | ARTICULATION → Palm Mute, or play B0 | short, muted notes |
| 8 | Blend | BLEND fully left / right | pickup (rounder) / mic (airier) |
| 9 | Q-Links | Turn Q-Links | knobs follow |
| 10 | Save / reload | Save project, reload | settings come back |
| 11 | CPU | Play fast chords for a minute | no crackles or dropouts |
| 12 | Long notes | Hold a low chord 5+ s | rings and fades out naturally, no clicks |
| 13 | Strings | Play a chord, then single notes | each played string glows with its fret lit, and settles as it fades |
| 14 | Themes | THEME → Sunburst / MPC Grey | the whole page changes look |
| 15 | Screen load | Play busy chords with the page open | the screen stays responsive |

Also note roughly how long check 2 takes: the first insert decodes the samples (a few seconds on the MPC).

Report back which numbers passed, and anything that sounded wrong (a recording on the MPC helps).
