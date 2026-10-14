#!/usr/bin/env python3
"""Draws Fretty's skin artwork and writes the matching layout, from one set of coordinates.

    python3 tools/make_skin.py            (needs Playwright's Chromium and Pillow; run on a PC, not in the build)

Writes vst/art/... (PNGs) and vst/layout.conf; both are committed, so the plugin build only reads them.
Two pages: PLAY (the fretboard, the guitar, FX and output knobs, instrument and theme) and SYNTH (layer B's
oscillator, filter and envelope, the pluck layer, and the A / B layer switches and levels).

What is static (the chassis, the neck, panel titles, the chrome wordmark) is one background picture per theme. What
moves is a control: knobs (one filmstrip for every theme), the switches (an off/on picture pair per theme) and, per
string, two display filmstrips the engine drives: s<n>_vib (how hard it rings: 0-3) and s<n>_fret (0 none, 1 open,
2.. fret 1..15). MPC only swaps pictures when those values change (a few times per note), so the page never animates
continuously (that would cost MPC's screen thread up to a core: mpc-vst-plugins docs/NOTES.md).

Coordinates below are the plugin area (1280 x 628); layout.conf's are 86 px lower (MPC's header).
"""
import base64
import os
import sys

from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
VST = os.path.join(HERE, "..", "vst")
ART = os.path.join(VST, "art")
Y_OFF = 86

THEMES = ["black", "sunburst", "grey"]          # index = the "theme" parameter's option
TOGGLES = [   # key, x, y, w, h, label: the PLAY page's button row (y 236)
    ("artic", 8, 236, 326, 52, None),          # one picture with two lit halves: SUSTAIN | PALM MUTE
    ("legato", 348, 236, 156, 52, "LEGATO"),
    ("strum", 518, 236, 156, 52, "STRUM"),
    ("drop", 688, 236, 156, 52, "DROP D\u266d"),
    ("engine", 858, 236, 156, 52, "PLUCK"),
]
COLW = 141
PANELS = [("GUITAR", "", 8, 4), ("FX", "", 8 + 4 * COLW + 8, 2), ("OUTPUT", "", 8 + 6 * COLW + 16, 1)]
KP_Y, KP_H = 300, 324
KNOB_R = 44                                      # the builder's knob box is 2r + 10
KNOB_ROWS = (404, 544)
KNOBS = [  # panel, col, row, key, label
    (0, 0, 0, "blend", "BLEND"), (0, 1, 0, "tone", "TONE"), (0, 2, 0, "damp", "DAMP"), (0, 3, 0, "vibrato", "VIBRATO"),
    (0, 0, 1, "position", "POSITION"), (0, 1, 1, "capo", "CAPO"), (0, 2, 1, "strum_speed", "STRUM SPD"), (0, 3, 1, "noise", "NOISE"),
    (1, 0, 0, "drive", "DRIVE"), (1, 1, 0, "chorus", "CHORUS"), (1, 0, 1, "eng_mix", "PLUCK"), (1, 1, 1, "eng_sustain", "RING"),
    (2, 0, 0, "space", "SPACE"), (2, 0, 1, "volume", "VOLUME"),
]
STRIP = (1032, 236, 240, 384)                    # right column: wordmark + steppers (MPC's Q-Link sidebar may cover it)
STEPPERS = [  # key, label, cx, cy, w, h, text size: the PLAY page's right column
    ("instrument", "INSTRUMENT", 1152, 432, 224, 52, 15),
    ("theme", "THEME", 1152, 552, 224, 52, 17),
]
NECK = dict(x0=132, x1=1272, yc=106, hn=70, he=84)
METER_X0 = 36                                    # string pictures span the headstock badges to the right edge

# ---- the SYNTH page
S_PANELS = [  # title, x, y, w, h
    ("OSCILLATOR", 8, 8, 580, 284), ("PLUCK LAYER", 596, 8, 420, 284),
    ("ENVELOPE", 8, 300, 564, 324), ("FILTER", 580, 300, 436, 324),
]
S_STRIP = (1032, 8, 240, 616)
WAVE_BOX = (28, 44, 260, 136)                    # the waveform picture
WAVE_STEP = ("wave", 158, 236, 260, 52, 17)      # key, cx, cy, w, h, text size
S_KNOBS = [  # key, label, cx, cy, bw
    ("octave", "OCTAVE", 390, 150, 124), ("syn_detune", "DETUNE", 516, 150, 124),
    ("eng_color", "COLOR", 736, 150, 136), ("eng_detune", "DETUNE", 876, 150, 136),
    ("attack", "ATTACK", 78, 520, 136), ("decay", "DECAY", 219, 520, 136), ("sustain", "SUSTAIN", 360, 520, 136),
    ("release", "RELEASE", 501, 520, 136),
    ("cutoff", "CUTOFF", 711, 520, 136), ("resonance", "RESONANCE", 885, 520, 136),
    ("a_level", "A LEVEL", 1092, 500, 112), ("b_level", "B LEVEL", 1212, 500, 112),
]
S_TOGGLES = [("a_on", 1048, 180, 208, 56, "A \u00b7 GUITAR"), ("b_on", 1048, 252, 208, 56, "B \u00b7 SYNTH")]


def knob_xy(p, col, row):
    return PANELS[p][2] + COLW * (col + 0.5), KNOB_ROWS[row]


PAGE = r"""<!doctype html><html><head><meta charset="utf-8"><style>
@font-face { font-family: Script; src: url('FONT_URL'); }
body { margin: 0; background: transparent; } canvas { display: block; }
</style></head><body><canvas id="c"></canvas><script>
const NECK = NECK_JSON;
const SANS = 'Inter, "Titillium Web", "Helvetica Neue", Arial, sans-serif';
const T = {
  black:    { bg1:'#1d1d1d', bg2:'#0a0a0a', noise:0.05, panel1:'#191919', panel2:'#111111', edge:'rgba(255,255,255,0.08)', hi:'rgba(255,255,255,0.05)',
              title:'#ff7b1c', dim:'#8e8e8e', accent:'#ff7b1c', accentText:'#1b0d02', wood:['#dbb67c','#c09658','#efd3a0'], grain:'light',
              binding:'#161616', inlay:'#141414', head:['#1b1b1b','#0b0b0b'], btn1:'#282828', btn2:'#141414', btnEdge:'rgba(255,255,255,0.12)', btnText:'#eeeeee',
              screw:['#d6d6d6','#5a5a5a'], rule:'#ff7b1c', chassisText:'#8e8e8e' },
  sunburst: { bg1:'#2b1f16', bg2:'#0f0a07', noise:0.05, panel1:'#211812', panel2:'#17110c', edge:'rgba(214,219,226,0.26)', hi:'rgba(235,240,245,0.10)',
              title:'#d6dade', dim:'#ad9a7e', accent:'#e2ac4a', accentText:'#1b120a', wood:['#3a2316','#22140c','#56341f'], grain:'dark',
              binding:'#eadfc6', inlay:'#f4ecdb', head:['#2a190f','#160d08'], btn1:'#30241b', btn2:'#1b140f', btnEdge:'rgba(214,219,226,0.35)', btnText:'#f2e7d3',
              screw:['#f2f4f6','#6f747a'], rule:'#c9ced4', chassisText:'#ad9a7e' },
  grey:     { bg1:'#d6d3cb', bg2:'#aeaaa0', noise:0.06, panel1:'#2e2d2a', panel2:'#252422', edge:'rgba(0,0,0,0.35)', hi:'rgba(255,255,255,0.10)',
              title:'#e2553e', dim:'#a19d94', accent:'#d03f2a', accentText:'#fff6f1', wood:['#3b2519','#25160e','#543525'], grain:'dark',
              binding:'#efe6cf', inlay:'#efe8da', head:['#2c1b12','#170e09'], btn1:'#e6e3dc', btn2:'#c4c0b7', btnEdge:'rgba(0,0,0,0.22)', btnText:'#2a2926',
              screw:['#f1efea','#8b877e'], rule:'#d03f2a', chassisText:'#a19d94' },
};
const TUNING = [40, 45, 50, 55, 59, 64];
const NOTE = ['C','C♯','D','E♭','E','F','F♯','G','A♭','A','B♭','B'];
const SCALE = (NECK.x1 - 40 - NECK.x0) / (1 - Math.pow(2, -15 / 12));
const fretX = n => NECK.x0 + SCALE * (1 - Math.pow(2, -n / 12));
const halfH = x => NECK.hn + (NECK.he - NECK.hn) * ((x - NECK.x0) / (NECK.x1 - NECK.x0));
const stringY = (i, x) => NECK.yc + (2.5 - i) * (halfH(x) - 14) * 2 / 5;
const WIDTHS = [3.6, 3.0, 2.4, 1.9, 1.35, 1.05];

const cv = document.getElementById('c'); let ctx;
function setup(w, h) { cv.width = w; cv.height = h; ctx = cv.getContext('2d'); ctx.clearRect(0, 0, w, h); }
function mulberry32(a) { return function () { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
function rrect(x, y, w, h, r) { ctx.beginPath(); ctx.roundRect(x, y, w, h, r); }
function text(s, x, y, size, color, o = {}) {
  ctx.font = `${o.style || ''} ${o.weight || 600} ${size}px ${o.font || SANS}`; ctx.fillStyle = color;
  ctx.textAlign = o.align || 'left'; ctx.textBaseline = o.base || 'alphabetic'; ctx.letterSpacing = (o.ls || 0) + 'px';
  ctx.fillText(s, x, y); ctx.letterSpacing = '0px';
}
function panel(t, x, y, w, h, r = 14) {
  ctx.save(); ctx.shadowColor = 'rgba(0,0,0,0.35)'; ctx.shadowBlur = 18; ctx.shadowOffsetY = 6;
  const g = ctx.createLinearGradient(0, y, 0, y + h); g.addColorStop(0, t.panel1); g.addColorStop(1, t.panel2);
  rrect(x, y, w, h, r); ctx.fillStyle = g; ctx.fill(); ctx.restore();
  rrect(x + 0.5, y + 0.5, w - 1, h - 1, r); ctx.strokeStyle = t.edge; ctx.lineWidth = 1; ctx.stroke();
  ctx.beginPath(); ctx.moveTo(x + r, y + 1.5); ctx.lineTo(x + w - r, y + 1.5); ctx.strokeStyle = t.hi; ctx.stroke();
}
function screw(t, x, y) {
  const g = ctx.createRadialGradient(x - 1.5, y - 1.5, 0.5, x, y, 6); g.addColorStop(0, t.screw[0]); g.addColorStop(1, t.screw[1]);
  ctx.beginPath(); ctx.arc(x, y, 5.5, 0, 6.283); ctx.fillStyle = g; ctx.fill();
  ctx.save(); ctx.translate(x, y); ctx.rotate(0.6); ctx.fillStyle = 'rgba(0,0,0,0.45)'; ctx.fillRect(-4, -0.8, 8, 1.6); ctx.restore();
}
function noise(t, w, h) {
  const r = mulberry32(7), n = document.createElement('canvas'); n.width = n.height = 192;
  const nc = n.getContext('2d'), id = nc.createImageData(192, 192);
  for (let k = 0; k < id.data.length; k += 4) { const v = r() * 255; id.data[k] = id.data[k + 1] = id.data[k + 2] = v; id.data[k + 3] = 255; }
  nc.putImageData(id, 0, 0); ctx.globalAlpha = t.noise; ctx.fillStyle = ctx.createPattern(n, 'repeat'); ctx.fillRect(0, 0, w, h); ctx.globalAlpha = 1;
}
function wood(t) {
  const W = NECK.x1 - NECK.x0 + 40, H = 260, wc = document.createElement('canvas'); wc.width = W; wc.height = H;
  const g = wc.getContext('2d'), rr = mulberry32(11), base = g.createLinearGradient(0, 0, 0, H);
  base.addColorStop(0, t.wood[1]); base.addColorStop(0.5, t.wood[0]); base.addColorStop(1, t.wood[1]); g.fillStyle = base; g.fillRect(0, 0, W, H);
  for (let k = 0; k < 420; k++) {
    const y = rr() * H, amp = 1 + rr() * 5, f = 0.002 + rr() * 0.006, ph = rr() * 6.28, light = rr() < (t.grain === 'light' ? 0.55 : 0.35);
    g.strokeStyle = light ? t.wood[2] : t.wood[1]; g.globalAlpha = 0.08 + rr() * (t.grain === 'light' ? 0.22 : 0.3); g.lineWidth = 0.4 + rr() * 2.2;
    g.beginPath(); for (let x = 0; x <= W; x += 12) { const yy = y + Math.sin(x * f + ph) * amp + Math.sin(x * f * 3.1 + ph * 2) * amp * 0.3; x ? g.lineTo(x, yy) : g.moveTo(x, yy); } g.stroke();
  }
  g.globalAlpha = t.grain === 'light' ? 0.18 : 0.3;
  for (let k = 0; k < 900; k++) { g.fillStyle = rr() < 0.5 ? t.wood[1] : t.wood[2]; g.fillRect(rr() * W, rr() * H, 2 + rr() * 9, 0.6 + rr() * 0.8); }
  return wc;
}
function neckPath() {
  const { x0, x1, yc, hn } = NECK; ctx.beginPath();
  ctx.moveTo(x0, yc - hn); ctx.lineTo(x1 + 30, yc - halfH(x1 + 30)); ctx.lineTo(x1 + 30, yc + halfH(x1 + 30)); ctx.lineTo(x0, yc + hn); ctx.closePath();
}
function stringPath(i, x0) { ctx.beginPath(); ctx.moveTo(x0, stringY(i, x0)); ctx.lineTo(NECK.x1 + 30, stringY(i, NECK.x1 + 30)); }
function drawString(i, x0, color, alpha) {
  ctx.save(); ctx.globalAlpha = alpha; ctx.shadowColor = 'rgba(0,0,0,0.5)'; ctx.shadowBlur = 4; ctx.shadowOffsetY = 4;
  stringPath(i, x0); ctx.lineWidth = WIDTHS[i]; ctx.strokeStyle = color; ctx.stroke(); ctx.restore();
  ctx.save(); ctx.globalAlpha = alpha;
  if (i < 4) { stringPath(i, x0); ctx.setLineDash([1.3, 1.7]); ctx.lineWidth = WIDTHS[i] * 0.85; ctx.strokeStyle = 'rgba(40,35,30,0.38)'; ctx.stroke(); ctx.setLineDash([]); }
  stringPath(i, x0); ctx.lineWidth = Math.max(0.6, WIDTHS[i] * 0.3); ctx.strokeStyle = 'rgba(255,255,255,0.55)'; ctx.stroke(); ctx.restore();
}
function badge(x, y, label, lit, accent, accentText) {
  ctx.beginPath(); ctx.arc(x, y, 10.5, 0, 6.283); ctx.fillStyle = lit ? accent : 'rgba(0,0,0,0.45)'; ctx.fill();
  ctx.strokeStyle = lit ? 'rgba(255,255,255,0.35)' : 'rgba(255,255,255,0.12)'; ctx.lineWidth = 1; ctx.stroke();
  text(label, x, y + 1, 13, lit ? accentText : '#efe6d6', { align: 'center', base: 'middle', weight: 700 });
}
function chromeWordmark(word, cx, cy, size) {
  // drawn on its own layer so the shine stays inside the letters
  const L = document.createElement('canvas'); L.width = cv.width; L.height = cv.height; const g2 = L.getContext('2d');
  const main = ctx; ctx = g2;
  ctx.font = `${size}px Script`; ctx.textAlign = 'center'; ctx.textBaseline = 'alphabetic';
  const m = ctx.measureText(word), top = cy - m.actualBoundingBoxAscent, bot = cy + m.actualBoundingBoxDescent;
  const g = ctx.createLinearGradient(0, top, 0, bot);
  g.addColorStop(0.00, '#ffffff'); g.addColorStop(0.30, '#e3e6ea'); g.addColorStop(0.48, '#9aa0a8');
  g.addColorStop(0.52, '#3e434a'); g.addColorStop(0.60, '#7a8089'); g.addColorStop(0.82, '#d9dde2'); g.addColorStop(1.00, '#ffffff');
  ctx.fillStyle = g; ctx.fillText(word, cx, cy);
  ctx.globalCompositeOperation = 'source-atop';
  const s = ctx.createLinearGradient(cx - 110, top, cx + 30, bot);
  s.addColorStop(0.0, 'rgba(255,255,255,0)'); s.addColorStop(0.45, 'rgba(255,255,255,0.7)'); s.addColorStop(0.55, 'rgba(255,255,255,0)');
  ctx.fillStyle = s; ctx.fillRect(cx - 200, top - 10, 400, bot - top + 20);
  ctx.globalCompositeOperation = 'source-over';
  ctx.lineWidth = 0.9; ctx.strokeStyle = 'rgba(255,255,255,0.6)'; ctx.strokeText(word, cx, cy - 0.8);
  ctx = main;
  // dark outline + shadow under the chrome layer
  ctx.font = `${size}px Script`; ctx.textAlign = 'center'; ctx.textBaseline = 'alphabetic';
  ctx.save(); ctx.shadowColor = 'rgba(0,0,0,0.75)'; ctx.shadowBlur = 9; ctx.shadowOffsetY = 4;
  ctx.lineJoin = 'round'; ctx.lineWidth = 6; ctx.strokeStyle = '#121316'; ctx.strokeText(word, cx, cy); ctx.restore();
  ctx.lineWidth = 2.4; ctx.strokeStyle = '#80868e'; ctx.lineJoin = 'round'; ctx.strokeText(word, cx, cy);
  ctx.drawImage(L, 0, 0);
}

// ---------- the page background (one per theme) ----------
function background(name) {
  const t = T[name], W = 1280, H = 628; setup(W, H);
  const g = ctx.createLinearGradient(0, 0, 0, H); g.addColorStop(0, t.bg1); g.addColorStop(1, t.bg2); ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
  noise(t, W, H);
  // neck panel
  const P = { x: 8, y: 8, w: 1264, h: 214 }; panel(t, P.x, P.y, P.w, P.h);
  ctx.save(); rrect(P.x + 1, P.y + 1, P.w - 2, P.h - 2, 13); ctx.clip();
  const { x0, x1, yc, hn, he } = NECK;
  const hg = ctx.createLinearGradient(0, yc - 110, 0, yc + 110); hg.addColorStop(0, t.head[1]); hg.addColorStop(0.5, t.head[0]); hg.addColorStop(1, t.head[1]);
  ctx.beginPath(); ctx.moveTo(x0, yc - hn - 8); ctx.bezierCurveTo(x0 - 40, yc - hn - 6, x0 - 70, yc - hn - 18, x0 - 120, yc - hn - 22);
  ctx.lineTo(x0 - 120, yc + hn + 22); ctx.bezierCurveTo(x0 - 70, yc + hn + 18, x0 - 40, yc + hn + 6, x0, yc + hn + 8); ctx.closePath(); ctx.fillStyle = hg; ctx.fill();
  ctx.save(); ctx.shadowColor = 'rgba(0,0,0,0.6)'; ctx.shadowBlur = 16; ctx.shadowOffsetY = 5; neckPath(); ctx.fillStyle = t.wood[0]; ctx.fill(); ctx.restore();
  ctx.save(); neckPath(); ctx.clip(); ctx.drawImage(wood(t), x0, yc - 130);
  const rs = ctx.createLinearGradient(0, yc - he, 0, yc + he); rs.addColorStop(0, 'rgba(0,0,0,0.30)'); rs.addColorStop(0.18, 'rgba(0,0,0,0)'); rs.addColorStop(0.82, 'rgba(0,0,0,0)'); rs.addColorStop(1, 'rgba(0,0,0,0.35)');
  ctx.fillStyle = rs; ctx.fillRect(x0, yc - 130, x1 - x0 + 40, 260);
  const inlay = (n, dy) => { const x = (fretX(n - 1) + fretX(n)) / 2, y = yc + dy, gg = ctx.createRadialGradient(x - 2, y - 2, 1, x, y, 9);
    gg.addColorStop(0, t.inlay); gg.addColorStop(1, t.grain === 'light' ? '#000' : '#cfc3a8'); ctx.beginPath(); ctx.arc(x, y, 8, 0, 6.283); ctx.fillStyle = gg; ctx.fill(); };
  [3, 5, 7, 9, 15].forEach(n => inlay(n, 0)); { const sp = (halfH(fretX(12)) - 14) * 2 / 5; inlay(12, -sp * 1.5); inlay(12, sp * 1.5); }
  for (let n = 1; n <= 16; n++) {
    const x = fretX(n), h = halfH(x), fg = ctx.createLinearGradient(x - 2.5, 0, x + 2.5, 0);
    fg.addColorStop(0, '#868b90'); fg.addColorStop(0.45, '#f4f6f7'); fg.addColorStop(1, '#868b90');
    ctx.fillStyle = 'rgba(0,0,0,0.35)'; ctx.fillRect(x + 1.5, yc - h, 3, h * 2); ctx.fillStyle = fg; ctx.fillRect(x - 2.5, yc - h, 5, h * 2);
  }
  ctx.restore();
  ctx.lineWidth = 3; ctx.strokeStyle = t.binding;
  ctx.beginPath(); ctx.moveTo(x0, yc - hn + 1.5); ctx.lineTo(x1 + 30, yc - halfH(x1 + 30) + 1.5); ctx.stroke();
  ctx.beginPath(); ctx.moveTo(x0, yc + hn - 1.5); ctx.lineTo(x1 + 30, yc + halfH(x1 + 30) - 1.5); ctx.stroke();
  const ng = ctx.createLinearGradient(x0 - 10, 0, x0, 0); ng.addColorStop(0, '#bfb69f'); ng.addColorStop(0.5, '#ece5d2'); ng.addColorStop(1, '#cfc6b0');
  ctx.fillStyle = ng; ctx.fillRect(x0 - 10, yc - hn - 2, 11, hn * 2 + 4);
  for (let i = 0; i < 6; i++) drawString(i, x0, '#ecebe6', 1);
  [1, 3, 5, 7, 9, 12, 15].forEach(n => text(String(n), (fretX(n - 1) + fretX(n)) / 2, P.y + P.h - 7, 13, t.dim, { align: 'center', weight: 600 }));
  ctx.restore();
  for (let i = 0; i < 6; i++) badge(58, stringY(i, x0), NOTE[TUNING[i] % 12], false);
  screw(t, P.x + 14, P.y + 14); screw(t, P.x + 14, P.y + P.h - 14);
  // knob panels (dark in every theme, so MPC's live text reads the same)
  for (const [title, sub, x, cols] of PANELS_JSON) {
    const w = cols * COLW; panel(t, x, KP_Y, w, KP_H);
    text(title, x + 20, KP_Y + 24, 13, t.title, { weight: 800, ls: 3 });
    if (sub) text(sub, x + w - 20, KP_Y + 24, 11, '#8e8e8e', { weight: 600, ls: 1.6, align: 'right' });
  }
  for (const [x, y, label] of KNOB_LABELS_JSON) text(label, x, y - 56, 14, '#efe9d8', { align: 'center', weight: 700, ls: 1.6 });
  // right column: wordmark + instrument and theme steppers
  const [sx, sy, sw, sh] = STRIP_JSON; panel(t, sx, sy, sw, sh);
  wordPlate(sx + 14, sy + 14, sw - 28, 104, 60);
  for (const [key, label, cx, cy, w, h] of STEPPERS_JSON) {
    text(label, cx, cy - h / 2 - 14, 12, t.chassisText, { weight: 700, ls: 3, align: 'center' });
    arrowPlates(w, h, cx - w / 2, cy - h / 2);
  }
  return cv.toDataURL('image/png');
}
function wordPlate(x, y, w, h, size) {
  const dark = { panel1: '#151515', panel2: '#0c0c0c', edge: 'rgba(255,255,255,0.10)', hi: 'rgba(255,255,255,0.06)' };
  panel(dark, x, y, w, h, 12);
  chromeWordmark('Fretty', x + w / 2, y + h * 0.66, size);
}

// ---------- the SYNTH page background (one per theme) ----------
function synthBackground(name) {
  const t = T[name], W = 1280, H = 628; setup(W, H);
  const g = ctx.createLinearGradient(0, 0, 0, H); g.addColorStop(0, t.bg1); g.addColorStop(1, t.bg2); ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
  noise(t, W, H);
  for (const [title, x, y, w, h] of SYNTH_PANEL_DATA) { panel(t, x, y, w, h); text(title, x + 20, y + 24, 13, t.title, { weight: 800, ls: 3 }); }
  // the waveform screen's bezel, and the wave stepper
  { const [x, y, w, h] = WAVE_BOX_JSON; rrect(x - 6, y - 6, w + 12, h + 12, 12); ctx.fillStyle = '#0a0a0a'; ctx.fill();
    ctx.strokeStyle = 'rgba(255,255,255,0.10)'; ctx.lineWidth = 1; ctx.stroke(); }
  { const [key, cx, cy, w, h] = WAVE_STEP_JSON; arrowPlates(w, h, cx - w / 2, cy - h / 2); }
  // envelope and filter sketches above their knobs
  adsrIcon(40, 352, 500, 92, t.title); filterIcon(612, 352, 372, 92, t.title);
  // right column: wordmark, the layer switches, their levels
  const [sx, sy, sw, sh] = SYNTH_COLUMN_DATA; panel(t, sx, sy, sw, sh);
  wordPlate(sx + 14, sy + 14, sw - 28, 104, 60);
  text('LAYERS', sx + sw / 2, sy + 156, 12, t.chassisText, { weight: 700, ls: 3, align: 'center' });
  text('TAP TO SOLO OR LAYER', sx + sw / 2, sy + 330, 10, t.chassisText, { weight: 600, ls: 1.6, align: 'center' });
  for (const [key, label, cx, cy] of SYNTH_KNOB_DATA) text(label, cx, cy - 56, 14, '#efe9d8', { align: 'center', weight: 700, ls: 1.6 });
  return cv.toDataURL('image/png');
}
function adsrIcon(x, y, w, h, color) {
  ctx.save(); ctx.strokeStyle = color; ctx.lineWidth = 2.5; ctx.lineJoin = 'round'; ctx.globalAlpha = 0.85;
  ctx.beginPath(); ctx.moveTo(x, y + h); ctx.lineTo(x + w * 0.16, y); ctx.quadraticCurveTo(x + w * 0.24, y + h * 0.42, x + w * 0.38, y + h * 0.42);
  ctx.lineTo(x + w * 0.72, y + h * 0.42); ctx.quadraticCurveTo(x + w * 0.8, y + h * 0.42, x + w, y + h); ctx.stroke();
  ctx.globalAlpha = 0.12; ctx.fillStyle = color; ctx.lineTo(x, y + h); ctx.fill(); ctx.restore();
  ctx.save(); ctx.strokeStyle = 'rgba(255,255,255,0.10)'; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(x, y + h + 0.5); ctx.lineTo(x + w, y + h + 0.5); ctx.stroke(); ctx.restore();
}
function filterIcon(x, y, w, h, color) {
  ctx.save(); ctx.strokeStyle = color; ctx.lineWidth = 2.5; ctx.globalAlpha = 0.85;
  ctx.beginPath(); ctx.moveTo(x, y + h * 0.45);
  ctx.lineTo(x + w * 0.42, y + h * 0.45); ctx.quadraticCurveTo(x + w * 0.52, y + h * 0.45, x + w * 0.55, y + h * 0.05);
  ctx.quadraticCurveTo(x + w * 0.6, y + h * 0.5, x + w * 0.72, y + h * 0.9); ctx.lineTo(x + w, y + h); ctx.stroke();
  ctx.globalAlpha = 0.12; ctx.fillStyle = color; ctx.lineTo(x, y + h); ctx.lineTo(x, y + h * 0.45); ctx.fill(); ctx.restore();
}
// one picture per wave shape, the same in every theme
function wavePicture(kind, w, h) {
  setup(w, h);
  rrect(0, 0, w, h, 8); ctx.fillStyle = '#0d0d0d'; ctx.fill();
  ctx.save(); rrect(0, 0, w, h, 8); ctx.clip();
  ctx.strokeStyle = 'rgba(255,255,255,0.05)'; ctx.lineWidth = 1;
  for (let gx = 0; gx <= w; gx += w / 8) { ctx.beginPath(); ctx.moveTo(gx + 0.5, 0); ctx.lineTo(gx + 0.5, h); ctx.stroke(); }
  for (let gy = 0; gy <= h; gy += h / 4) { ctx.beginPath(); ctx.moveTo(0, gy + 0.5); ctx.lineTo(w, gy + 0.5); ctx.stroke(); }
  const pad = 16, mid = h / 2, amp = h * 0.32, cyc = 2;
  const f = u => { const ph = (u * cyc) % 1;
    return kind === 0 ? Math.sin(2 * Math.PI * ph) : kind === 1 ? 1 - 4 * Math.abs(ph - 0.5) : kind === 2 ? 2 * ph - 1 : (ph < 0.5 ? 1 : -1); };
  ctx.beginPath();
  for (let i = 0; i <= 400; i++) { const u = i / 400, x = pad + u * (w - 2 * pad), y = mid - f(u) * amp;
    if (kind >= 2 && i > 0) { const pu = (i - 1) / 400, jump = Math.abs(f(u) - f(pu)) > 1; if (jump) { ctx.lineTo(x, mid - f(pu) * amp); } }
    i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
  ctx.save(); ctx.shadowColor = 'rgba(255,236,200,0.9)'; ctx.shadowBlur = 10; ctx.strokeStyle = '#f6eedd'; ctx.lineWidth = 3; ctx.lineJoin = 'round'; ctx.stroke(); ctx.restore();
  ctx.restore();
  return cv.toDataURL('image/png');
}

// ---------- switches: off / on pictures, per theme ----------
function button(t, x, y, w, h, label, on) {
  ctx.save(); ctx.shadowColor = on ? t.accent : 'rgba(0,0,0,0.45)'; ctx.shadowBlur = on ? 10 : 6; ctx.shadowOffsetY = on ? 0 : 2;
  const g = ctx.createLinearGradient(0, y, 0, y + h);
  if (on) { g.addColorStop(0, t.accent); g.addColorStop(1, t.accent); } else { g.addColorStop(0, t.btn1); g.addColorStop(1, t.btn2); }
  rrect(x, y, w, h, 10); ctx.fillStyle = g; ctx.fill(); ctx.restore();
  if (on) { ctx.save(); rrect(x, y, w, h, 10); ctx.clip(); const s = ctx.createLinearGradient(0, y, 0, y + h);
    s.addColorStop(0, 'rgba(255,255,255,0.28)'); s.addColorStop(0.5, 'rgba(255,255,255,0)'); s.addColorStop(1, 'rgba(0,0,0,0.12)'); ctx.fillStyle = s; ctx.fillRect(x, y, w, h); ctx.restore(); }
  rrect(x + 0.5, y + 0.5, w - 1, h - 1, 10); ctx.strokeStyle = on ? 'rgba(255,255,255,0.25)' : t.btnEdge; ctx.lineWidth = 1; ctx.stroke();
  text(label, x + w / 2, y + h / 2 + 1, 15, on ? t.accentText : t.btnText, { align: 'center', base: 'middle', weight: 700, ls: 1.2 });
}
function toggleImage(name, w, h, label, on) {
  const t = T[name], pad = 8; setup(w + 2 * pad, h + 2 * pad);
  if (label === null) {      // articulation: two halves, the lit one is the current option
    const half = (w - 14) / 2;
    button(t, pad, pad, half, h, 'SUSTAIN', !on); button(t, pad + half + 14, pad, half, h, 'PALM MUTE', on);
  } else button(t, pad, pad, w, h, label, on);
  return cv.toDataURL('image/png');
}

// ---------- the knob: one filmstrip for every theme ----------
function knobStrip(frames, r) {
  const S = 2 * r; setup(S, S * frames);
  const a0 = Math.PI * 0.75, a1 = Math.PI * 2.25, kr = r - 13;
  for (let f = 0; f < frames; f++) {
    const v = f / (frames - 1), cx = r, cy = f * S + r, av = a0 + (a1 - a0) * v;
    ctx.lineCap = 'round';
    ctx.beginPath(); ctx.arc(cx, cy, r - 5, a0, a1); ctx.strokeStyle = 'rgba(255,255,255,0.10)'; ctx.lineWidth = 4.5; ctx.stroke();
    if (v > 0.002) { ctx.save(); ctx.shadowColor = 'rgba(255,244,220,0.55)'; ctx.shadowBlur = 6; ctx.beginPath(); ctx.arc(cx, cy, r - 5, a0, av);
      ctx.strokeStyle = '#f3ecdc'; ctx.lineWidth = 4.5; ctx.stroke(); ctx.restore(); }
    ctx.save(); ctx.shadowColor = 'rgba(0,0,0,0.6)'; ctx.shadowBlur = 8; ctx.shadowOffsetY = 4;
    const g = ctx.createLinearGradient(cx, cy - kr, cx, cy + kr); g.addColorStop(0, '#eef0f3'); g.addColorStop(1, '#6f747b');
    ctx.beginPath(); ctx.arc(cx, cy, kr, 0, 6.283); ctx.fillStyle = g; ctx.fill(); ctx.restore();
    ctx.strokeStyle = 'rgba(0,0,0,0.30)'; ctx.lineWidth = 1.3;
    for (let n = 0; n < 56; n++) { const a = n / 56 * 6.283 + av; ctx.beginPath();
      ctx.moveTo(cx + Math.cos(a) * (kr - 6), cy + Math.sin(a) * (kr - 6)); ctx.lineTo(cx + Math.cos(a) * (kr - 1.2), cy + Math.sin(a) * (kr - 1.2)); ctx.stroke(); }
    const cg = ctx.createLinearGradient(cx, cy - kr, cx, cy + kr); cg.addColorStop(0, '#3a3a3a'); cg.addColorStop(1, '#121212');
    ctx.beginPath(); ctx.arc(cx, cy, kr - 8, 0, 6.283); ctx.fillStyle = cg; ctx.fill();
    const sh = ctx.createRadialGradient(cx - 8, cy - 11, 2, cx, cy, kr - 8); sh.addColorStop(0, 'rgba(255,255,255,0.16)'); sh.addColorStop(1, 'rgba(255,255,255,0)');
    ctx.fillStyle = sh; ctx.fill();
    ctx.beginPath(); ctx.moveTo(cx + Math.cos(av) * 8, cy + Math.sin(av) * 8); ctx.lineTo(cx + Math.cos(av) * (kr - 12), cy + Math.sin(av) * (kr - 12));
    ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 3.5; ctx.lineCap = 'round'; ctx.stroke();
  }
  return cv.toDataURL('image/png');
}

// ---------- string pictures (every theme): ring levels and the lit fret ----------
function meterBox(i) {
  const ya = stringY(i, NECK.x0), yb = stringY(i, NECK.x1), y = Math.round((ya + yb) / 2);
  return { x: METER_X0, y: y - 16, w: 1272 - METER_X0, h: 32 };
}
function vibStrip(i) {
  const b = meterBox(i); setup(b.w * 4, b.h);
  for (let lvl = 1; lvl < 4; lvl++) {
    ctx.save(); ctx.translate(lvl * b.w - b.x, -b.y);
    ctx.beginPath(); ctx.rect(b.x, b.y, b.w, b.h); ctx.clip();
    const amp = [0, 2.2, 4.2, 7.0][lvl], x0 = NECK.x0, xb = NECK.x1 + 300;
    // a soft blur band (a string moving faster than the eye), brightest mid-string, then the string itself
    ctx.beginPath();
    for (let x = x0; x <= NECK.x1 + 30; x += 4) { const u = (x - x0) / (xb - x0), e = amp * Math.sin(Math.PI * u) + 0.6; ctx.lineTo(x, stringY(i, x) - e); }
    for (let x = NECK.x1 + 30; x >= x0; x -= 4) { const u = (x - x0) / (xb - x0), e = amp * Math.sin(Math.PI * u) + 0.6; ctx.lineTo(x, stringY(i, x) + e); }
    ctx.closePath(); ctx.fillStyle = 'rgba(255,248,232,' + (0.18 + 0.08 * lvl) + ')'; ctx.fill();
    ctx.lineWidth = 1; ctx.strokeStyle = 'rgba(0,0,0,' + (0.10 + 0.05 * lvl) + ')'; ctx.stroke();
    ctx.save(); ctx.shadowColor = 'rgba(255,240,210,0.9)'; ctx.shadowBlur = 4 + 3 * lvl; drawString(i, x0, '#fffdf6', 1); ctx.restore();
    ctx.restore();
  }
  return cv.toDataURL('image/png');
}
function fretStrip(i) {
  const b = meterBox(i), N = 17; setup(b.w * N, b.h);
  for (let f = 1; f < N; f++) {
    ctx.save(); ctx.translate(f * b.w - b.x, -b.y); ctx.beginPath(); ctx.rect(b.x, b.y, b.w, b.h); ctx.clip();
    const fret = f - 1;   // 0 = open
    if (fret === 0) badge(58, stringY(i, NECK.x0), NOTE[TUNING[i] % 12], true, '#f3ecdc', '#141414');
    else {
      const x = (fretX(fret - 1) + fretX(fret)) / 2 + 4, y = stringY(i, x);
      ctx.save(); ctx.shadowColor = 'rgba(255,240,210,0.95)'; ctx.shadowBlur = 12;
      ctx.beginPath(); ctx.arc(x, y, 11.5, 0, 6.283); ctx.fillStyle = '#141414'; ctx.fill(); ctx.restore();
      ctx.beginPath(); ctx.arc(x, y, 11.5, 0, 6.283); ctx.strokeStyle = '#f3ecdc'; ctx.lineWidth = 2; ctx.stroke();
      text(NOTE[(TUNING[i] + fret) % 12], x, y + 1, 11, '#f3ecdc', { align: 'center', base: 'middle', weight: 800 });
    }
    ctx.restore();
  }
  return cv.toDataURL('image/png');
}
function arrowPlates(w, h, ox, oy) {
  ctx.save(); ctx.translate(ox, oy);
  rrect(-8, -8, w + 16, h + 16, 14); ctx.fillStyle = '#101010'; ctx.fill();   // same in every theme: the arrows' corners match
  ctx.strokeStyle = 'rgba(255,255,255,0.07)'; ctx.lineWidth = 1; ctx.stroke();
  const plate = (x, d) => {
    ctx.save(); ctx.shadowColor = 'rgba(0,0,0,0.5)'; ctx.shadowBlur = 6; ctx.shadowOffsetY = 2;
    const g = ctx.createLinearGradient(0, 0, 0, h); g.addColorStop(0, '#2e2e2e'); g.addColorStop(1, '#151515');
    rrect(x + 1, 1, h - 2, h - 2, 9); ctx.fillStyle = g; ctx.fill(); ctx.restore();
    rrect(x + 1.5, 1.5, h - 3, h - 3, 9); ctx.strokeStyle = 'rgba(255,255,255,0.14)'; ctx.lineWidth = 1; ctx.stroke();
    const cx = x + h / 2, cy = h / 2, s = h * 0.2;
    ctx.beginPath(); ctx.moveTo(cx - d * s * 0.8, cy - s); ctx.lineTo(cx + d * s, cy); ctx.lineTo(cx - d * s * 0.8, cy + s); ctx.closePath();
    ctx.fillStyle = '#f3ecdc'; ctx.fill();
  };
  plate(0, -1); plate(w - h, 1);
  // the field between the arrows
  rrect(h + 4, 1, w - 2 * h - 8, h - 2, 9); ctx.fillStyle = '#0d0d0d'; ctx.fill();
  ctx.strokeStyle = 'rgba(255,255,255,0.10)'; ctx.stroke();
  ctx.restore();
}
const COLW = COLW_JSON, KP_Y = KP_Y_JSON, KP_H = KP_H_JSON, METER_X0 = METER_X0_JSON;
window.API = { arrowPlates, background, synthBackground, wavePicture, toggleImage, knobStrip, vibStrip, fretStrip, meterBox };
</script></body></html>"""


def save(data_url, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(base64.b64decode(data_url.split(",", 1)[1]))


def main():
    import json
    page_src = (PAGE.replace("FONT_URL", "file://" + os.path.abspath(os.path.join(VST, "fonts", "Yellowtail-Regular.ttf")))
                .replace("NECK_JSON", json.dumps(NECK)).replace("PANELS_JSON", json.dumps(PANELS))
                .replace("STRIP_JSON", json.dumps(STRIP)).replace("COLW_JSON", str(COLW)).replace("KP_Y_JSON", str(KP_Y))
                .replace("KP_H_JSON", str(KP_H)).replace("METER_X0_JSON", str(METER_X0))
                .replace("STEPPERS_JSON", json.dumps(STEPPERS)).replace("SYNTH_PANEL_DATA", json.dumps(S_PANELS))
                .replace("SYNTH_COLUMN_DATA", json.dumps(S_STRIP)).replace("WAVE_BOX_JSON", json.dumps(WAVE_BOX))
                .replace("WAVE_STEP_JSON", json.dumps(WAVE_STEP)).replace("SYNTH_KNOB_DATA", json.dumps(S_KNOBS))
                .replace("KNOB_LABELS_JSON", json.dumps([list(knob_xy(p_, c, r)) + [lab] for p_, c, r, k, lab in KNOBS])))
    tmp = os.path.join(ART, "_page.html")
    os.makedirs(ART, exist_ok=True)
    open(tmp, "w").write(page_src)
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page()
        errs = []
        pg.on("pageerror", lambda e: errs.append(str(e)))
        pg.goto("file://" + tmp)
        pg.evaluate("document.fonts.load('74px Script')")
        pg.wait_for_function("document.fonts.check('74px Script')")
        for th in THEMES:
            save(pg.evaluate("API.background(%r)" % th), os.path.join(ART, th, "bg.png"))
            save(pg.evaluate("API.synthBackground(%r)" % th), os.path.join(ART, th, "synth_bg.png"))
            for key, x, y, w, h, label in TOGGLES + S_TOGGLES:
                for on in (0, 1):
                    save(pg.evaluate("API.toggleImage(%r, %d, %d, %s, %s)" % (th, w, h, json.dumps(label), "true" if on else "false")),
                         os.path.join(ART, th, "%s_%s.png" % (key, "on" if on else "off")))
        save(pg.evaluate("API.knobStrip(128, %d)" % (KNOB_R + 5)), os.path.join(ART, "knob.png"))
        for k, name in enumerate(["sine", "triangle", "saw", "square"]):
            save(pg.evaluate("API.wavePicture(%d, %d, %d)" % (k, WAVE_BOX[2], WAVE_BOX[3])), os.path.join(ART, "wave_%s.png" % name))
        boxes = []
        for i in range(6):
            save(pg.evaluate("API.vibStrip(%d)" % i), os.path.join(ART, "s%d_vib.png" % (i + 1)))
            save(pg.evaluate("API.fretStrip(%d)" % i), os.path.join(ART, "s%d_fret.png" % (i + 1)))
            boxes.append(pg.evaluate("API.meterBox(%d)" % i))
        b.close()
        if errs:
            sys.exit("page errors: %s" % errs)
    os.remove(tmp)
    write_layout(boxes)


def write_layout(boxes):
    L = []
    L.append("# Fretty's pages. Generated by tools/make_skin.py (edit that, not this file).")
    L.append("art_css=skin.css")
    for k, v in [("bg", "0b0b0b"), ("ink", "efe9d8"), ("ink_dim", "d9d3c5"), ("accent", "f3ecdc"), ("accent_hi", "ffffff"),
                 ("lcd", "141414"), ("line", "3a3a3a"), ("box", "141414"), ("seg_active", "f3ecdc"), ("seg_inactive", "141414"),
                 ("seg_active_tx", "141414")]:
        L.append("theme_%s=%s" % (k, v))
    keys = [k[3] for k in KNOBS]
    L.append("qlinks_track = " + ",".join(keys))

    def knob(cx, cy, key, label, bw=136):
        L.append('knob cx=%d cy=%d r=%d label="%s" key=%s ns=0 vs=21 bw=%d strip=art/knob.png frames=128' % (
            cx, cy + Y_OFF, KNOB_R, label, key, bw))

    def toggles(lst):
        for i, th in enumerate(THEMES):
            for key, x, y, w, h, label in lst:
                L.append('toggle cx=%d cy=%d w=%d h=%d label="" key=%s ns=0 img=art/%s/%s_off.png img_on=art/%s/%s_on.png when=theme:%d' % (
                    x + w // 2, y + h // 2 + Y_OFF, w + 16, h + 16, key, th, key, th, key, i))

    # ---- PLAY
    L.append("[tab PLAY]")
    for i, th in enumerate(THEMES):
        L.append("art file=art/%s/bg.png x=0 y=%d w=1280 h=628 fit=stretch when=theme:%d" % (th, Y_OFF, i))
    for i, b in enumerate(boxes):   # string pictures: ring first, the lit fret on top
        L.append("meter cx=%d cy=%d w=%d h=%d key=s%d_vib strip=art/s%d_vib.png frames=4" % (
            b["x"] + b["w"] // 2, b["y"] + b["h"] // 2 + Y_OFF, b["w"], b["h"], i + 1, i + 1))
    for i, b in enumerate(boxes):
        L.append("meter cx=%d cy=%d w=%d h=%d key=s%d_fret strip=art/s%d_fret.png frames=17" % (
            b["x"] + b["w"] // 2, b["y"] + b["h"] // 2 + Y_OFF, b["w"], b["h"], i + 1, i + 1))
    toggles(TOGGLES)
    for p, col, row, key, label in KNOBS:
        x, y = knob_xy(p, col, row)
        knob(x, y, key, label)
    for key, label, cx, cy, w, h, ts in STEPPERS:
        L.append('stepper cx=%d cy=%d w=%d h=%d label="" key=%s tsize=%d talign=center' % (cx, cy + Y_OFF, w, h, key, ts))
    L.append('qlinks "PLAY" = ' + ",".join(keys))

    # ---- SYNTH
    L.append("[tab SYNTH]")
    for i, th in enumerate(THEMES):
        L.append("art file=art/%s/synth_bg.png x=0 y=%d w=1280 h=628 fit=stretch when=theme:%d" % (th, Y_OFF, i))
    x, y, w, h = WAVE_BOX
    L.append('picture x=%d y=%d w=%d h=%d key=wave files="art/wave_sine.png,art/wave_triangle.png,art/wave_saw.png,art/wave_square.png"' % (
        x, y + Y_OFF, w, h))
    key, cx, cy, w, h, ts = WAVE_STEP
    L.append('stepper cx=%d cy=%d w=%d h=%d label="" key=%s tsize=%d talign=center' % (cx, cy + Y_OFF, w, h, key, ts))
    toggles(S_TOGGLES)
    for key, label, cx, cy, bw in S_KNOBS:
        knob(cx, cy, key, label, bw)
    L.append('qlinks "SYNTH" = ' + ",".join(k[0] for k in S_KNOBS))
    open(os.path.join(VST, "layout.conf"), "w").write("\n".join(L) + "\n")
    css = os.path.join(VST, "skin.css")
    if not os.path.exists(css):
        open(css, "w").write("/* Fretty: restyles the renderer's own drawings. */\n")


if __name__ == "__main__":
    main()
