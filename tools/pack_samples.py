#!/usr/bin/env python3
"""Pack Fretty's instruments into the sample files its engine loads (one file per instrument, in engine/).

    python3 -I tools/pack_samples.py <out dir> --shiny <karoryfer.shinyguitar> --emily <karoryfer.emilyguitar>
                                     --vcsl <VCSL checkout> --vsco <VSCO-2-CE checkout> [instrument ...] [--full]

Instruments (all CC0, see CREDITS.md):
  acoustic     Karoryfer Shinyguitar, an archtop: the magnetic pickup and the microphone of each take are kept as a
               pair (L = pickup, R = mic) so BLEND can mix them sample-accurately.
  electric     Karoryfer Emilyguitar, an Epiphone SG with flatwounds, recorded direct (mono).
  folkharp     VCSL Folk Harp (stereo).
  concertharp  VCSL Concert Harp (stereo).
  strumstick   VCSL Strumstick, played with the fingers (stereo): somewhere between a banjo and a guitar.
  violin       VSCO 2 CE Solo Violin (stereo): bowed with vibrato (looped, so a held note sustains) and pizzicato.

The essential set (default) keeps up to three velocity layers, the two most typical takes of each (closest to the
layer's median level, so no odd takes), notes up to 4 s with a natural 0.5 s fade, and two takes of each release and
noise. --full keeps every take and layer of the guitars (several times larger).

Each file is losslessly compressed (fixed linear prediction + Rice codes, like FLAC's simplest mode); the plugin
decodes it into RAM when an instrument is first used. Format: src/engine.c, "sample file". Every instrument gets a
level trim in its header so they all play at about the same loudness.
FRETTY_DUMP_RAW=<dir> writes each instrument's raw 16-bit audio too, for tools/check_codec.c.
"""
import glob
import os
import re
import struct
import sys
import wave

import numpy as np

ARGS = sys.argv[1:]
OUT = ARGS[0]
OPT = {}
WANT = []
i = 1
while i < len(ARGS):
    if ARGS[i] in ("--shiny", "--emily", "--vcsl", "--vsco"):
        OPT[ARGS[i][2:]] = ARGS[i + 1]
        i += 2
    elif ARGS[i] == "--full":
        OPT["full"] = True
        i += 1
    else:
        WANT.append(ARGS[i])
        i += 1
FULL = OPT.get("full", False)
ALL = ["acoustic", "electric", "folkharp", "concertharp", "strumstick", "violin"]
WANT = WANT or ALL

TRIM_DB = -66.0
MAX_NOTE_SEC = 6.0 if FULL else 4.0
NOTE_FADE_MS = 40 if FULL else 500
FADE_MS = 40
KEEP_RR = 8 if FULL else 2
MAX_RR = 8
REF_RMS = None            # hardest-layer median attack loudness every instrument is trimmed to: the archtop's own
ARCHTOP_RMS = 0.0747      # that value for the archtop (used when it isn't packed in the same run)
VIOLIN_TRIM = 1.0        # bowed notes: within 1.5 dB of the archtop's first half second (render_demo pitch 5); held notes add the rest
PIZZ_GAIN = 2.6          # the pizzicato takes are recorded ~10 dB softer than the bowed ones (render_demo pitch 6)
# Final level trims, calibrated by playing each instrument through the engine (tools/render_demo.c pitch <n>: E2-C6 at
# velocity 100) and matching the median power of every note's first 0.5 s to the archtop's. The attack-RMS estimate
# above is only the starting point: decay shapes differ, so the ear-level match is set here.
LEVEL_TRIM = {"electric": 1.008, "folkharp": 1.263, "concertharp": 1.901, "strumstick": 1.481, "violin": VIOLIN_TRIM}
ZT_NOTE, ZT_RELEASE, ZT_NOISE = 0, 1, 2
FLAG_DUAL, FLAG_MONO, FLAG_BOWED = 1, 2, 4   # BOWED: a loop table follows the zones; zone "art" byte = articulation

rng = np.random.default_rng(1)
NOTE_PC = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def midi_of(name):
    """'db2', 'A#1', 'F#2', 'gb3', 'C4' -> MIDI note number (C4 = 60)."""
    m = re.fullmatch(r"([A-Ga-g])([#b]?)(-?\d)", name)
    if not m:
        return None
    pc = NOTE_PC[m.group(1).lower()] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1) + pc


def read_wav(path):
    """-> float64 array, shape (frames, channels)"""
    w = wave.open(path)
    assert w.getframerate() == 44100, path
    sw, n, ch = w.getsampwidth(), w.getnframes(), w.getnchannels()
    raw = np.frombuffer(w.readframes(n), dtype=np.uint8)
    if sw == 3:
        b = raw.reshape(-1, 3).astype(np.int32)
        v = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
        v = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float64) / 8388608.0
    elif sw == 2:
        v = raw.view("<i2").astype(np.float64) / 32768.0
    else:
        raise SystemExit("unsupported width %d in %s" % (sw, path))
    return v.reshape(-1, ch)


class Pack:
    def __init__(self, name, flags):
        self.name, self.flags = name, flags
        self.samples = []        # [float array (frames, ch), is_note]
        self.zones = {}          # (type, lo, hi, root, lv, hv[, art]) -> [sample idx]
        self.loops = {}          # sample idx -> loop start frame (the loop ends at the sample's end)

    def add(self, audio, is_note):
        self.samples.append([audio, is_note])
        return len(self.samples) - 1

    def top_layer(self):
        """Indices of the note samples in each pitch's hardest velocity layer."""
        out = set()
        for key, idxs in self.zones.items():
            if key[0] == ZT_NOTE and key[5] == 127 and (len(key) < 7 or key[6] == 0):
                out.update(idxs)
        return out


def level(a):
    return float(np.sqrt(np.mean(a[:22050] ** 2)))


def pick_takes(paths, loader):
    """The KEEP_RR takes closest to the group's median level."""
    if len(paths) <= KEEP_RR:
        return paths
    lev = [level(loader(p)) for p in paths]
    med = float(np.median(lev))
    return [p for _, p in sorted(zip([abs(x - med) for x in lev], paths))[:KEEP_RR]]


def spread(roots, lo_end=28, hi_end=108):
    """Sampled pitches -> key ranges that meet halfway between neighbours."""
    roots = sorted(roots)
    out = {}
    for i, r in enumerate(roots):
        lo = lo_end if i == 0 else (roots[i - 1] + r) // 2 + 1
        hi = hi_end if i == len(roots) - 1 else (r + roots[i + 1]) // 2
        out[r] = (lo, hi)
    return out


# ---------------------------------------------------------------- acoustic: Shinyguitar (SFZ mapping, pickup+mic pairs)
def parse_sfz(path):
    text = re.sub(r"//[^\n]*", "", open(path).read())
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
        if mode == "group":
            group[k] = v.strip()
        elif mode == "region":
            cur[k] = v.strip()
    if cur is not None:
        regions.append(cur)
    return regions


def build_acoustic():
    src = OPT["shiny"]
    pk = Pack("acoustic", FLAG_DUAL)
    layers = {2: (1, 56), 3: (57, 96), 4: (97, 127)}

    def pair(name):
        stem = re.sub(r"_1\.wav$", "", name.replace("\\", "/").split("/")[-1])
        a = read_wav(os.path.join(src, "Samples", "electric", stem + "_2.wav"))[:, 0]
        b = read_wav(os.path.join(src, "Samples", "acoustic", stem + "_1.wav"))[:, 0]
        n = min(len(a), len(b))
        return np.stack([a[:n], b[:n]], axis=1)

    for fname, zt in (("acoustic_one.sfz", ZT_NOTE), ("acoustic_release.sfz", ZT_RELEASE), ("acoustic_noises.sfz", ZT_NOISE)):
        cand = {}
        for r in parse_sfz(os.path.join(src, "Programs", fname)):
            if "sample" not in r:
                continue
            lo, hi = int(r["lokey"]), int(r["hikey"])
            m = re.match(r"([a-g]b?\d)_", r["sample"].replace("\\", "/").split("/")[-1])
            root = midi_of(m.group(1)) if m else int(r.get("pitch_keycenter", 60))   # one group has no keycenter
            lv, hv = (int(r.get("lovel", 1)), int(r.get("hivel", 127))) if zt == ZT_NOTE else (1, 127)
            if zt == ZT_NOTE and not FULL:
                lm = re.search(r"_vl(\d)_", r["sample"])
                if not lm or int(lm.group(1)) not in layers:
                    continue
                lv, hv = layers[int(lm.group(1))]
            cand.setdefault((zt, lo, hi, root, lv, hv), []).append(r["sample"])
        for key, names in cand.items():
            for n in pick_takes(names, pair):
                pk.zones.setdefault(key, []).append(pk.add(pair(n), zt == ZT_NOTE))
    return pk


# ---------------------------------------------------------------- electric: Emilyguitar (file names, mono)
def build_electric():
    src = OPT["emily"]
    pk = Pack("electric", FLAG_MONO)
    layers = {"mp": (1, 56), "mf": (57, 96), "f": (97, 127)} if not FULL else \
        {"p": (1, 32), "mp": (33, 64), "mf": (65, 96), "f": (97, 127)}
    load = lambda p: read_wav(p)
    groups = {}
    for p in glob.glob(os.path.join(src, "notes", "*.wav")):
        m = re.fullmatch(r"([a-g]b?\d)_(p|mp|mf|f)_rr(\d)\.wav", os.path.basename(p))
        if m and m.group(2) in layers:
            groups.setdefault((midi_of(m.group(1)), m.group(2)), []).append(p)
    ranges = spread({r for r, _ in groups})
    for (root, dyn), paths in sorted(groups.items()):
        lo, hi = ranges[root]
        lv, hv = layers[dyn]
        for p in pick_takes(sorted(paths), load):
            pk.zones.setdefault((ZT_NOTE, lo, hi, root, lv, hv), []).append(pk.add(load(p), True))
    rel = {}
    for p in glob.glob(os.path.join(src, "release", "*.wav")):
        m = re.fullmatch(r"([a-g]b?\d)_release_rr(\d)\.wav", os.path.basename(p))
        if m:
            rel.setdefault(midi_of(m.group(1)), []).append(p)
    rr = spread(rel)
    for root, paths in sorted(rel.items()):
        for p in sorted(paths)[:KEEP_RR]:
            pk.zones.setdefault((ZT_RELEASE, rr[root][0], rr[root][1], root, 1, 127), []).append(pk.add(load(p), False))
    # noises onto the same keys as the archtop's: dead strings C0-E0, pick rest F0, finger squeak F#0
    noise_keys = {"muted1": 12, "muted2": 13, "muted3": 14, "muted4": 15, "muted5": 16, "pickrest1": 17, "fingering1": 18}
    for stem, key in noise_keys.items():
        for p in sorted(glob.glob(os.path.join(src, "noises", stem + "_rr*.wav")))[:KEEP_RR]:
            pk.zones.setdefault((ZT_NOISE, key, key, key, 1, 127), []).append(pk.add(load(p), False))
    return pk


# ---------------------------------------------------------------- VCSL instruments (file names, stereo)
def build_vcsl(name, folder, pattern, layer_of):
    """pattern: regex with groups (note, layer token); layer_of: token -> (lv, hv) or None to skip."""
    src = os.path.join(OPT["vcsl"], "Chordophones", "Composite Chordophones", folder)
    pk = Pack(name, 0)
    groups = {}
    for p in glob.glob(os.path.join(src, "**", "*.wav"), recursive=True):
        m = re.fullmatch(pattern, os.path.basename(p))
        if not m:
            continue
        rng_ = layer_of(m.group(2))
        if rng_:
            groups.setdefault((midi_of(m.group(1)), rng_), []).append(p)
    ranges = spread({r for r, _ in groups})
    for (root, (lv, hv)), paths in sorted(groups.items()):
        for p in pick_takes(sorted(paths), read_wav):
            pk.zones.setdefault((ZT_NOTE, ranges[root][0], ranges[root][1], root, lv, hv), []).append(pk.add(read_wav(p), True))
    return pk


def build_folkharp():
    return build_vcsl("folkharp", "Folk Harp", r"EWHarp_Normal_([A-G]#?\d)_v(\d)_RR\d\.wav",
                      lambda t: {"2": (1, 80), "3": (81, 127)}.get(t))


def build_concertharp():
    # most notes have mf and f takes; a few have only p/mp in place of mf
    def lay(t):
        t = re.sub(r"\d+$", "", t)
        return {"p": (1, 80), "mp": (1, 80), "mf": (1, 80), "f": (81, 127)}.get(t)
    return build_vcsl("concertharp", "Concert Harp", r"KSHarp_([A-G]#?\d)_((?:mf|mp|p|f)\d)\.wav", lay)


def build_strumstick():
    return build_vcsl("strumstick", "Strumstick", r"Strumstick_Finger_Str\d_Main_([A-G]#?\d)_vl(\d)_rr\d\.wav",
                      lambda t: {"1": (1, 48), "2": (49, 90), "3": (91, 127)}.get(t))


# ---------------------------------------------------------------- violin: VSCO 2 CE (bowed loops + pizzicato)
LOOP_START, LOOP_END = (1.4, 3.4)       # seconds after the bow starts: the loop sits in the settled sustain
XFADE = 0.25


def onset_trim(a, rel_db=-40.0):
    """Drop the silence before the note (some soft takes start half a second late)."""
    m = np.abs(a).max(axis=1)
    thr = m.max() * 10 ** (rel_db / 20)
    on = int(np.argmax(m >= thr))
    return a[max(0, on - 220):]


def make_loop(a):
    """-> (audio cut to the loop end with a crossfaded tail, loop start). The tail [le-X, le) fades from the
    original into the audio just before the loop start, so jumping from le back to ls is seamless."""
    sr, X = 44100, int(XFADE * 44100)
    ls = int(LOOP_START * sr)
    mono = a.mean(axis=1)
    ref = mono[ls - X:ls]
    best, bc = None, -2.0
    lo, hi = int((LOOP_END - 0.6) * sr), min(int(LOOP_END * sr), len(a))
    for step, rng_ in ((64, range(lo, hi, 64)), (1, None)):
        cand = rng_ if rng_ is not None else range(max(lo, best - 64), min(hi, best + 64))
        for le in cand:
            seg = mono[le - X:le]
            c = float(np.dot(seg, ref) / (np.linalg.norm(seg) * np.linalg.norm(ref) + 1e-12))
            if c > bc:
                bc, best = c, le
    le = best
    t = np.linspace(0.0, 1.0, X, endpoint=False)[:, None]
    if bc > 0.6:
        wi, wo = t, 1 - t                                 # correlated: a straight crossfade keeps the level
    else:
        wi, wo = np.sin(t * np.pi / 2), np.cos(t * np.pi / 2)    # not: equal power
    out = a[:le].copy()
    out[le - X:le] = a[le - X:le] * wo + a[ls - X:ls] * wi
    return out, ls


def build_violin():
    src = os.path.join(OPT["vsco"], "Strings", "Solo Violin")
    pk = Pack("violin", FLAG_BOWED)
    layers = {"p": (1, 80), "f": (81, 127)}
    for art, folder, pat in ((0, "Arco Vib", r"LLVln_ArcoVib_([A-G]#?\d)_(p|f)\.wav"),
                             (1, "Pizz", r"LLVln_Pizz_([A-G]#?\d)_(p|f)(?:_RR\d)?\.wav")):
        groups = {}
        for p in glob.glob(os.path.join(src, folder, "*.wav")):
            m = re.fullmatch(pat, os.path.basename(p))
            if m:
                groups.setdefault((midi_of(m.group(1)), m.group(2)), []).append(p)
        ranges = spread({r for r, _ in groups})
        loud = {}
        for (root, dyn), paths in groups.items():
            body = lambda q: onset_trim(read_wav(q))[int(0.5 * 44100):int(3.0 * 44100)] if art == 0 else onset_trim(read_wav(q))
            loud.setdefault(root, {})[dyn] = float(np.median([np.sqrt(np.mean(body(q)[:110250] ** 2)) for q in paths]))
        for (root, dyn), paths in sorted(groups.items()):
            lv, hv = layers[dyn]
            # the soft takes are very soft (up to -15 dB): bring them to 6 dB under the loud ones, velocity does the rest
            g = 1.0
            if dyn == "p" and "f" in loud[root] and loud[root]["p"] > 0:
                g = float(np.clip(0.5 * loud[root]["f"] / loud[root]["p"], 1.0, 4.0))
            for p in pick_takes(sorted(paths), read_wav):
                a = onset_trim(read_wav(p)) * g * (PIZZ_GAIN if art == 1 else 1.0)
                if art == 0:
                    a, ls = make_loop(a)
                    idx = pk.add(a, True)
                    pk.loops[idx] = ls
                else:
                    idx = pk.add(a, True)
                pk.zones.setdefault((ZT_NOTE, ranges[root][0], ranges[root][1], root, lv, hv, art), []).append(idx)
    return pk


BUILDERS = {"acoustic": build_acoustic, "electric": build_electric, "folkharp": build_folkharp,
            "concertharp": build_concertharp, "strumstick": build_strumstick, "violin": build_violin}


# ---------------------------------------------------------------- trimming, 16-bit, level
def finish(pk):
    """float takes -> trimmed, faded, dithered int16 arrays; returns the level trim."""
    global REF_RMS
    out = []
    mono = pk.flags & FLAG_MONO
    top = pk.top_layer()
    louds = []
    for idx, (audio, is_note) in enumerate(pk.samples):
        a = audio[:, :1] if mono else (audio if audio.shape[1] == 2 else np.repeat(audio, 2, axis=1))
        if idx in pk.loops:      # a looped sustain: kept whole (its end joins its loop start), no fade
            x = a * 32767.0
            x += rng.random(x.shape) - rng.random(x.shape)
            out.append(np.clip(np.round(x), -32768, 32767).astype(np.int64))
            if idx in top:
                m = a[:22050].mean(axis=1)
                louds.append(float(np.sqrt(np.mean(m * m))))
            continue
        thr = 10 ** (TRIM_DB / 20)
        loud = np.nonzero((np.abs(a) >= thr).any(axis=1))[0]
        end = int(loud[-1]) + 1 if len(loud) else 1
        end = min(len(a), end + 441)
        if is_note:
            end = min(end, int(MAX_NOTE_SEC * 44100))
        fade = min(end, int((NOTE_FADE_MS if is_note and end >= MAX_NOTE_SEC * 44100 - 1 else FADE_MS) * 44.1))
        g = np.ones(end)
        g[end - fade:] = (end - np.arange(end - fade, end)) / fade
        x = a[:end] * g[:, None] * 32767.0
        x += rng.random(x.shape) - rng.random(x.shape)
        out.append(np.clip(np.round(x), -32768, 32767).astype(np.int64))
        if idx in top:   # how loud the hardest notes sound over their first half second, as the engine plays them
            m = a[:22050, 0] * 0.65 + a[:22050, 1] * 0.35 if pk.flags & FLAG_DUAL else a[:22050].mean(axis=1)
            louds.append(float(np.sqrt(np.mean(m * m))))
    rms = float(np.median(louds)) if louds else 0.0
    if pk.name == "acoustic":
        REF_RMS = rms
    ref = REF_RMS or ARCHTOP_RMS
    gain = 1.0 if pk.name == "acoustic" or not rms else float(np.clip(ref / rms, 0.25, 8.0))
    if pk.name in LEVEL_TRIM:   # measured from the engine itself (see LEVEL_TRIM): what the ear hears wins
        gain = LEVEL_TRIM[pk.name]
    if os.environ.get("FRETTY_SHOW_PEAKS"):
        print("%s: hardest-layer attack rms %.4f" % (pk.name, rms))
    return out, gain


# ---------------------------------------------------------------- the codec
BLOCK = 1024
ESC_Q = 24


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
        cnt = np.full(nb, BLOCK)
        cnt[-1] = BLOCK - pad
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
    kk = np.repeat(best_k, BLOCK)[:n].astype(np.uint64)
    q = best_u >> kk
    esc = q >= ESC_Q
    L_res = np.where(esc, ESC_Q + 20, q + 1 + kk).astype(np.int64)
    V_res = np.where(esc, best_u, (np.uint64(1) << kk) | (best_u & ((np.uint64(1) << kk) - np.uint64(1))))
    starts = np.arange(nb) * BLOCK
    L = np.insert(L_res, starts, 8)
    V = np.insert(V_res, starts, ((best_o << 5) | best_k).astype(np.uint64))
    end = np.cumsum(L) - 1
    total = int(end[-1]) + 1
    words = np.zeros(total // 64 + 2, np.uint64)
    w = end // 64
    s = (63 - end % 64).astype(np.uint64)
    np.bitwise_or.at(words, w, V << s)
    hi_mask = s > 0
    np.bitwise_or.at(words, w[hi_mask] - 1, V[hi_mask] >> (np.uint64(64) - s[hi_mask]))
    return words.astype(">u8").tobytes()[:(total + 7) // 8]


def write(pk, data, gain):
    """header: "ATSP", u32 version 3, u32 samples, u32 zones, u32 data offset, u32 flags, f32 level trim;
    per sample { u32 byte offset, u32 frames }; zones; with FLAG_BOWED a u32 loop start per sample (0 = none); data (per sample: u32 len0, u32 len1, streams)."""
    mono = pk.flags & FLAG_MONO
    coded = []
    for x in data:
        a = encode_channel(x[:, 0])
        b = b"" if mono else encode_channel(x[:, 1])
        coded.append(struct.pack("<II", len(a), len(b)) + a + b)
    table = bytearray()
    off = 0
    for x, c in zip(data, coded):
        table += struct.pack("<II", off, len(x))
        off += len(c)
    zt = bytearray()
    zone_list = sorted(pk.zones.items())
    for key, idxs in zone_list:
        t, lo, hi, root, lv, hv = key[:6]
        art = key[6] if len(key) > 6 else 0
        idxs = idxs[:MAX_RR]
        zt += struct.pack("<BBBBBBBB", t, lo, hi, root, lv, hv, len(idxs), art)
        zt += struct.pack("<%dH" % MAX_RR, *(idxs + [0] * (MAX_RR - len(idxs))))
    loops = b""
    if pk.flags & FLAG_BOWED:
        loops = struct.pack("<%dI" % len(data), *[pk.loops.get(i, 0) for i in range(len(data))])
    doff = 28 + len(table) + len(zt) + len(loops)
    path = os.path.join(OUT, pk.name + ".smp")
    with open(path, "wb") as f:
        f.write(struct.pack("<4sIIIIIf", b"ATSP", 3, len(data), len(zone_list), doff, pk.flags, gain))
        f.write(table)
        f.write(zt)
        f.write(loops)
        for c in coded:
            f.write(c)
    if os.environ.get("FRETTY_DUMP_RAW"):
        with open(os.path.join(os.environ["FRETTY_DUMP_RAW"], pk.name + ".raw"), "wb") as f:
            for x in data:
                st = np.repeat(x, 2, axis=1) if mono else x
                f.write(st.astype("<i2").tobytes())
    frames = sum(len(x) for x in data)
    print("%-12s %3d samples, %2d zones, %5.0f s, gain %.2f: %5.1f MB in RAM, %5.1f MB on disk" % (
        pk.name, len(data), len(zone_list), frames / 44100, gain, frames * 4 / 1e6, os.path.getsize(path) / 1e6))


os.makedirs(OUT, exist_ok=True)
for name in WANT:
    pk = BUILDERS[name]()
    data, gain = finish(pk)
    write(pk, data, gain)
