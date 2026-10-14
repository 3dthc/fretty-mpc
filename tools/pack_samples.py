#!/usr/bin/env python3
"""Pack Karoryfer's Shinyguitar (CC0) into the single sample file the Fretty engine loads.

    python3 -I tools/pack_samples.py <karoryfer.shinyguitar checkout> <out dir> [--full]

Default (essential set): three velocity layers (the source's layers 2, 3 and 4), the two most typical round robins
of each (closest to the layer's median level, so no odd takes), notes up to 4 s with a natural 0.5 s fade, two takes
of each release and noise. --full keeps everything (4 layers x 4 takes; several times larger).

The file is losslessly compressed (fixed linear prediction + Rice codes, like FLAC's simplest mode): the plugin decodes
it into RAM when it loads, bit-for-bit the 16-bit audio. Format: src/engine.c, "sample file".

Each zone pairs the two recordings of one take: the magnetic pickup (electric/*_2.wav) and the
microphone (acoustic/*_1.wav). They are stored as one interleaved int16 frame pair (L = pickup,
R = mic) so the BLEND knob can mix them sample-accurately. Tails are trimmed where both channels
stay under the threshold, then faded; 24-bit input is dithered to 16-bit.

Output: <out dir>/fretty.smp (format in src/smp_format.h) and CREDITS-samples.txt.
Only the WAVs and the SFZ key/velocity mapping are used: not the GUI art or the signed bank XML."""
import os
import re
import struct
import sys
import wave
import numpy as np

SRC = sys.argv[1]
OUT = sys.argv[2]
FULL = "--full" in sys.argv[3:]
PROG = os.path.join(SRC, "Programs")
TRIM_DB = -66.0                            # tail threshold
MAX_NOTE_SEC = 6.0 if FULL else 4.0        # cap per note sample
NOTE_FADE_MS = 40 if FULL else 500         # a cut tail fades out like a natural decay
FADE_MS = 40
KEEP_RR = 8 if FULL else 2                 # takes kept per zone
LITE_LAYERS = {2: (1, 56), 3: (57, 96), 4: (97, 127)}   # essential set: source layer -> velocity range

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
    fade = min(end, int((NOTE_FADE_MS if is_note and end >= MAX_NOTE_SEC * 44100 - 1 else FADE_MS) * 44.1))
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


def take_level(name):
    """Level of a take's first half second (mic + pickup), to pick the most typical takes."""
    pick, mic, _ = sample_paths(name)
    a, b = read_wav(pick)[:22050], read_wav(mic)[:22050]
    return float(np.sqrt(np.mean(a * a) + np.mean(b * b)))


def collect(fname, ztype):
    cand = {}
    for r in parse_sfz(os.path.join(PROG, fname)):
        if "sample" not in r:
            continue
        lo, hi = int(r["lokey"]), int(r["hikey"])
        root = note_root(r["sample"], int(r.get("pitch_keycenter", 60)))   # SFZ's default keycenter is 60
        lv, hv = vel_range(r) if ztype == ZT_NOTE else (1, 127)
        if ztype == ZT_NOTE and not FULL:
            m = re.search(r"_vl(\d)_", r["sample"])
            layer = int(m.group(1)) if m else 0
            if layer not in LITE_LAYERS:
                continue
            lv, hv = LITE_LAYERS[layer]
        cand.setdefault((ztype, lo, hi, root, lv, hv), []).append(r["sample"])
    for key, names in cand.items():
        if len(names) > KEEP_RR:
            lev = [take_level(n) for n in names]
            med = float(np.median(lev))
            names = [n for _, n in sorted(zip([abs(x - med) for x in lev], names))[:KEEP_RR]]
        for n in names:
            zones.setdefault(key, []).append(add_sample(n, ztype == ZT_NOTE))


collect("acoustic_one.sfz", ZT_NOTE)
collect("acoustic_release.sfz", ZT_RELEASE)
collect("acoustic_noises.sfz", ZT_NOISE)

BLOCK = 1024
ESC_Q = 24      # a unary run this long means: 20-bit raw value follows


def residuals(x, order):
    p = np.concatenate([np.zeros(3, np.int64), x])
    n = len(x)
    if order == 0:
        return x.copy()
    if order == 1:
        return p[3:] - p[2:2 + n]
    if order == 2:
        return p[3:] - 2 * p[2:2 + n] + p[1:1 + n]
    return p[3:] - 3 * p[2:2 + n] + 3 * p[1:1 + n] - p[0:n]


def encode_channel(x):
    """int64 samples -> bytes: per 1024-frame block a header byte (order << 5 | k), then Rice-coded residuals."""
    n = len(x)
    nb = (n + BLOCK - 1) // BLOCK
    best_cost = np.full(nb, np.inf)
    best_u = np.zeros(n, np.uint64)
    best_k = np.zeros(nb, np.int64)
    best_o = np.zeros(nb, np.int64)
    pad = nb * BLOCK - n
    for order in range(4):
        r = residuals(x, order)
        u = np.where(r >= 0, 2 * r, -2 * r - 1).astype(np.int64)
        ub = np.concatenate([u, np.zeros(pad, np.int64)]).reshape(nb, BLOCK)
        cnt = np.full(nb, BLOCK); cnt[-1] = BLOCK - pad
        mean = ub.sum(1) / cnt
        k = np.clip(np.floor(np.log2(mean * 0.69 + 1)), 0, 20).astype(np.int64)
        q = ub >> k[:, None]
        cost = np.where(q >= ESC_Q, ESC_Q + 20, q + 1 + k[:, None])
        cost[-1, BLOCK - pad:] = 0
        cost = cost.sum(1) + 8
        better = cost < best_cost
        best_cost = np.where(better, cost, best_cost)
        best_k = np.where(better, k, best_k)
        best_o = np.where(better, order, best_o)
        sel = np.repeat(better, BLOCK)[:n]
        best_u = np.where(sel, u.astype(np.uint64), best_u)
    # codes: per block a header, then one code per residual; each code = value V written in L bits, MSB first
    kk = np.repeat(best_k, BLOCK)[:n].astype(np.uint64)
    q = best_u >> kk
    esc = q >= ESC_Q
    L_res = np.where(esc, ESC_Q + 20, q + 1 + kk).astype(np.int64)
    V_res = np.where(esc, best_u, (np.uint64(1) << kk) | (best_u & ((np.uint64(1) << kk) - np.uint64(1))))
    # interleave block headers in front of each block
    starts = np.arange(nb) * BLOCK
    L = np.insert(L_res, starts, 8)
    V = np.insert(V_res, starts, ((best_o << 5) | best_k).astype(np.uint64))
    end = np.cumsum(L) - 1                       # bit index of each code's last bit
    total = int(end[-1]) + 1
    words = np.zeros(total // 64 + 2, np.uint64)
    w = end // 64
    s = (63 - end % 64).astype(np.uint64)
    np.bitwise_or.at(words, w, V << s)
    hi_mask = s > 0
    np.bitwise_or.at(words, w[hi_mask] - 1, V[hi_mask] >> (np.uint64(64) - s[hi_mask]))
    return words.astype(">u8").tobytes()[:(total + 7) // 8]


os.makedirs(OUT, exist_ok=True)
MAX_RR = 8
zone_list = sorted(zones.items())
coded = []
for stem, data, frames in samples:
    st = np.frombuffer(data, "<i2").reshape(-1, 2).astype(np.int64)
    a, b = encode_channel(st[:, 0]), encode_channel(st[:, 1])
    coded.append(struct.pack("<II", len(a), len(b)) + a + b)
table = bytearray()
off = 0
for (stem, data, frames), c in zip(samples, coded):
    table += struct.pack("<II", off, frames)
    off += len(c)
zt = bytearray()
for (t, lo, hi, root, lv, hv), idxs in zone_list:
    idxs = idxs[:MAX_RR]
    zt += struct.pack("<BBBBBBBB", t, lo, hi, root, lv, hv, len(idxs), 0)
    zt += struct.pack("<%dH" % MAX_RR, *(idxs + [0] * (MAX_RR - len(idxs))))
data_off = 20 + len(table) + len(zt)
hdr = struct.pack("<4sIIII", b"ATSP", 2, len(samples), len(zone_list), data_off)
if os.environ.get("FRETTY_DUMP_RAW"):           # for the lossless check (tools/check_codec.c)
    with open(os.environ["FRETTY_DUMP_RAW"], "wb") as f:
        for _, data, _ in samples:
            f.write(data)
path = os.path.join(OUT, "fretty.smp")
with open(path, "wb") as f:
    f.write(hdr)
    f.write(table)
    f.write(zt)
    for c in coded:
        f.write(c)

total = sum(s[2] for s in samples)
print("samples %d, zones %d, %.1f s of stereo audio: %.1f MB in RAM, %.1f MB on disk" % (
    len(samples), len(zone_list), total / 44100, total * 4 / 1e6, os.path.getsize(path) / 1e6))
with open(os.path.join(OUT, "CREDITS-samples.txt"), "w") as f:
    f.write("Samples: Shinyguitar by Karoryfer Lecolds (D. Smolken), archtop guitar.\n"
            "Source: https://github.com/sfzinstruments/karoryfer.shinyguitar\n"
            "License: CC0 1.0 Universal (public domain dedication).\n"
            "Repacked for Fretty: pickup + mic channels paired, tails trimmed, 16-bit.\n")
