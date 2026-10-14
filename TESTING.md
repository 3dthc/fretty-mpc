# First device test (v0.1.0)

Goal: confirm Archtop builds on your machine, loads on the MPC, and plays with velocity.
The touchscreen page in this build is the **automatic test layout** (plain knobs and switches), not the final design.

## 1. Build (Ubuntu in WSL)
Put this folder in your Ubuntu home next to `mpc-vst-plugins`:

```
~/mpc-vst-plugins/
~/archtop-mpc/        <- this folder
```

Then:

```sh
cd ~/archtop-mpc
chmod +x make_release.sh vst/build.sh
./make_release.sh
```

The first build downloads Shinyguitar (~800 MB, pinned commit) into `third_party/` and packs it into
`vst/build/engine/archtop.smp` (~250 MB, a minute or two). It needs `git`, `python3` and numpy:
`sudo apt install python3-numpy`. Later builds reuse the packed file.
Expected at the end of the build output:
- `exported: VSTPluginMain`
- `highest glibc: GLIBC_2.x` (2.31 or lower is ideal; above 2.36 would not load)
- `dist/Archtop-0.1.0-mpc-armv7.zip`, about 160 MB

If the build fails, send me the last ~30 lines of output.

## 2. Install
Same as your other ports: the zip's `install.sh` (MPC is stopped, `MPC.settings` is backed up, MPC restarts), or drop
the zip into the MPC plugin installer app. The plugin folder is `3dthc - VST - Archtop` and holds `archtop.so`,
`Plugin Skins/` and `engine/archtop.smp` (~250 MB of free space needed).

## 3. Checks on the MPC
| # | Check | How | Expect |
| --- | --- | --- | --- |
| 1 | It loads | Plugin browser → Archtop on a new track | page appears, no crash |
| 2 | Samples load | Wait ~2 s after inserting | the first notes play (no silence) |
| 3 | Single notes | One pad at a time | one clean guitar note per pad, natural ring |
| 4 | **Velocity** | Pads soft → hard (turn **Full Level** off) | soft hits clearly quieter and darker, hard hits louder and brighter |
| 5 | Chords + strum | Press 3–4 pads together | strummed low string to high; STRUM off = together |
| 6 | Legato | Hold a note, play the next one higher | slurs (no new pick) |
| 7 | Palm mute | ARTICULATION → Palm Mute, or play B0 | short, muted notes |
| 8 | Blend | BLEND fully left / right | pickup (rounder) / mic (airier) |
| 9 | Q-Links | Turn Q-Links | knobs follow |
| 10 | Save / reload | Save project, reload | settings come back |
| 11 | CPU | Play fast chords for a minute | no crackles or dropouts |
| 12 | Long notes | Hold a low chord 5+ s | sustains smoothly, no glitch around 0.35 s in |

Check 12 matters: past 0.35 s the sound streams from storage. A click or dropout there means the read-ahead needs work.

Report back which numbers passed, and anything that sounded wrong (a recording on the MPC helps).
