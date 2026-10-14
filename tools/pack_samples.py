#!/usr/bin/env python3
"""Pack Karoryfer's Shinyguitar (CC0) into the single sample file the Archtop engine loads.

    python3 -I tools/pack_samples.py <karoryfer.shinyguitar checkout> <out dir>

Each zone pairs the two recordings of one take: the magnetic pickup (electric/*_2.wav) and the
microphone (acoustic/*_1.wav). They are stored as one interleaved int16 frame pair (L = pickup,
R = mic) so the BLEND knob can mix them sample-accurately. Tails are trimmed where both channels
stay under the threshold, then faded; 24-bit input is dithered to 16-bit.

Output: <out dir>/archtop.smp (format in src/smp_format.h) and CREDITS-samples.txt.
Only the WAVs and the SFZ key/velocity mapping are used: not the GUI art or the signed bank XML."""
import os
import re
import struct
import sys
import wave
import numpy as np

SRC = sys.argv[1]
OUT = sys.argv[2]
PROG = os.path.join(SRC, "Programs")
TRIM_DB = -66.0          # tail threshold
MAX_NOTE_SEC = 6.0       # hard cap per note sample
FADE_MS = 40

ZT_NOTE, ZT_RELEASE, ZT_NOISE = 0, 1, 2


def parse_sfz(path):
    """-> list of regions: dict of merged <group> + <region> opcodes."""
    text = open(path).read()
    text = re.sub(r"//[^\n]*", "", text)
    regions, group, cur, mode = [], {}, None, None
    for tok in re.findall(r"<\w+>|[^\s=]+=[^\n]*", text):
        if tok.startswith("<"):
            if cur is not None:
                regions.append(cur)
                cur = None
            if tok == "<group>":
                group, mode = {}, "group"
            elif tok == "<region>":
                cur, mode = dict(group), "region"
            else:
                mode = None
            continue
        k, v = tok.split("=", 1)
        v = v.strip()
        if mode == "group":
            group[k] = v
        elif mode == "region":
            cur[k] = v
    if cur is not None:
        regions.append(cur)
    return regions


def read_wav(path):
    w = wave.open(path)
    assert w.getnchannels() == 1 and w.getframerate() == 44100, path
    sw, n = w.getsampwidth(), w.getnframes()
    raw = np.frombuffer(w.readframes(n), dtype=np.uint8)
    if sw == 3:
        b = raw.reshape(-1, 3).astype(np.int32)
        v = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
        v = np.where(v & 0x800000, v - 0x1000000, v)
        return v.astype(np.float64) / 8388608.0
    if sw == 2:
        return raw.view("<i2").astype(np.float64) / 32768.0
    raise SystemExit("unsupported width %d in %s" % (sw, path))


def sample_paths(name):
    """'acoustic\\db2_vl1_rr1_1.wav' -> (pickup path, mic path)"""
    base = name.replace("\\", "/").split("/")[-1]
    stem = re.sub(r"_1\.wav$", "", base)
    mic = os.path.join(SRC, "Samples", "acoustic", stem + "_1.wav")
    pick = os.path.join(SRC, "Samples", "electric", stem + "_2.wav")
    for p in (mic, pick):
        if not os.path.exists(p):
            raise SystemExit("missing " + p)
    return pick, mic, stem


rng = np.random.default_rng(1)
cache = {}
samples = []          # (stem, int16 bytes, frames)


def add_sample(name, is_note):
    pick, mic, stem = sample_paths(name)
    if stem in cache:
        return cache[stem]
    a, b = read_wav(pick), read_wav(mic)
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    thr = 10 ** (TRIM_DB / 20)
    loud = np.nonzero((np.abs(a) >= thr) | (np.abs(b) >= thr))[0]
    end = int(loud[-1]) + 1 if len(loud) else 1
    end = min(n, end + 441)
    if is_note:
        end = min(end, int(MAX_NOTE_SEC * 44100))
    fade = min(end, int(FADE_MS * 44.1))
    g = np.ones(end)
    g[end - fade:] = (end - np.arange(end - fade, end)) / fade
    st = np.stack([a[:end] * g, b[:end] * g], axis=1) * 32767.0
    st += rng.random(st.shape) - rng.random(st.shape)          # TPDF dither
    buf = np.clip(np.round(st), -32768, 32767).astype("<i2").tobytes()
    idx = len(samples)
    samples.append((stem, bytes(buf), end))
    cache[stem] = idx
    return idx


NOTE_PC = {"c": 0, "db": 1, "d": 2, "eb": 3, "e": 4, "f": 5, "gb": 6, "g": 7, "ab": 8, "a": 9, "bb": 10, "b": 11}


def note_root(name, fallback):
    """The recorded pitch from the file name (db2_..., c4_...): one SFZ group (59-61) has no pitch_keycenter."""
    m = re.match(r"([a-g]b?)(\d)_", name.replace("\\", "/").split("/")[-1])
    if not m or m.group(1) not in NOTE_PC:
        return fallback
    return 12 * (int(m.group(2)) + 1) + NOTE_PC[m.group(1)]


def vel_range(r):
    return int(r.get("lovel", 1)), int(r.get("hivel", 127))


zones = {}   # (type, lokey, hikey, root, lovel, hivel) -> [sample idx]


def collect(fname, ztype):
    for r in parse_sfz(os.path.join(PROG, fname)):
        if "sample" not in r:
            continue
        lo, hi = int(r["lokey"]), int(r["hikey"])
        root = note_root(r["sample"], int(r.get("pitch_keycenter", 60)))   # SFZ's default keycenter is 60
        lv, hv = vel_range(r) if ztype == ZT_NOTE else (1, 127)
        idx = add_sample(r["sample"], ztype == ZT_NOTE)
        zones.setdefault((ztype, lo, hi, root, lv, hv), []).append(idx)


collect("acoustic_one.sfz", ZT_NOTE)
collect("acoustic_release.sfz", ZT_RELEASE)
collect("acoustic_noises.sfz", ZT_NOISE)

os.makedirs(OUT, exist_ok=True)
MAX_RR = 8
zone_list = sorted(zones.items())
hdr = struct.pack("<4sIIII", b"ATSP", 1, len(samples), len(zone_list), 0)
table = bytearray()
off = 0
for stem, data, frames in samples:
    table += struct.pack("<II", off, frames)
    off += frames
zt = bytearray()
for (t, lo, hi, root, lv, hv), idxs in zone_list:
    idxs = idxs[:MAX_RR]
    zt += struct.pack("<BBBBBBBB", t, lo, hi, root, lv, hv, len(idxs), 0)
    zt += struct.pack("<%dH" % MAX_RR, *(idxs + [0] * (MAX_RR - len(idxs))))
data_off = len(hdr) + len(table) + len(zt)
hdr = struct.pack("<4sIIII", b"ATSP", 1, len(samples), len(zone_list), data_off)
path = os.path.join(OUT, "archtop.smp")
with open(path, "wb") as f:
    f.write(hdr)
    f.write(table)
    f.write(zt)
    for _, data, _ in samples:
        f.write(data)

total = sum(s[2] for s in samples)
print("samples %d, zones %d, %.1f s of stereo audio, %.1f MB" % (len(samples), len(zone_list), total / 44100,
                                                                 os.path.getsize(path) / 1e6))
with open(os.path.join(OUT, "CREDITS-samples.txt"), "w") as f:
    f.write("Samples: Shinyguitar by Karoryfer Lecolds (D. Smolken), archtop guitar.\n"
            "Source: https://github.com/sfzinstruments/karoryfer.shinyguitar\n"
            "License: CC0 1.0 Universal (public domain dedication).\n"
            "Repacked for Archtop: pickup + mic channels paired, tails trimmed, 16-bit.\n")
