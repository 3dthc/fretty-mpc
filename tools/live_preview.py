#!/usr/bin/env python3
"""Native Tkinter live preview for Fretty skin artwork."""
import base64
import importlib
import io
import json
import sys
import tkinter as tk
from pathlib import Path
from tkinter import ttk

from PIL import Image, ImageTk
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tools" / "make_skin.py"
TMP = Path("/tmp/fretty-live-preview")
TMP.mkdir(parents=True, exist_ok=True)
HTML = TMP / "renderer.html"
THEMES = ["black", "sunburst", "grey", "teal", "violet"]


def build_page(skin):
    subs = {
        "FONT_URL": "file://" + str((ROOT / "vst/fonts/Yellowtail-Regular.ttf").resolve()),
        "NECK_JSON": json.dumps(skin.NECK),
        "PANELS_JSON": json.dumps(skin.PANELS),
        "STRIP_JSON": json.dumps(skin.STRIP),
        "COLW_JSON": str(skin.COLW),
        "KP_Y_JSON": str(skin.KP_Y),
        "KP_H_JSON": str(skin.KP_H),
        "METER_X0_JSON": str(skin.METER_X0),
        "STEPPERS_JSON": json.dumps(skin.STEPPERS),
        "ROW_STEP_DATA": json.dumps(skin.ROW_STEPPERS),
        "LAYER_GRID": json.dumps(skin.L_GRID),
        "LAYER_TITLES": json.dumps(skin.L_TITLES),
        "LAYER_KNOB_DATA": json.dumps(skin.L_KNOBS),
        "MIX_KNOB_JSON": json.dumps(skin.MIX_KNOB),
        "SYNTH_COLUMN_DATA": json.dumps(skin.S_STRIP),
        "WAVE_BOX_JSON": json.dumps(skin.WAVE_BOX),
        "PIC_STEP_JSON": json.dumps(skin.PIC_STEP),
        "INST_INFO_JSON": json.dumps(skin.INST_INFO),
        "INST_NAMES_JSON": json.dumps(skin.INSTRUMENTS),
        "PERF_PANELS": json.dumps(skin.P_PANELS),
        "PERF_STEPPERS": json.dumps(skin.P_STEPPERS),
        "PERF_KNOBS": json.dumps(skin.P_KNOBS),
        "PERF_TOGGLE": json.dumps(skin.P_TOGGLE),
        "TILE_JSON": json.dumps(skin.TILE),
        "NUMERALS_JSON": json.dumps(skin.NUMERALS),
        "KNOB_LABELS_JSON": json.dumps([
            list(skin.knob_xy(p, c, r)) + [label]
            for p, c, r, k, label in skin.KNOBS
        ]),
    }
    page = skin.PAGE
    for key in sorted(subs, key=len, reverse=True):
        if key not in page:
            raise RuntimeError(f"Missing template placeholder: {key}")
        page = page.replace(key, subs[key])
    HTML.write_text(page)
    return HTML.resolve().as_uri()


def render_all(browser, skin):
    page = browser.new_page(viewport={"width": 1280, "height": 628})
    try:
        page.goto(build_page(skin), wait_until="load")
        page.evaluate("document.fonts.load('74px Script')")
        page.wait_for_function("document.fonts.check('74px Script')")
        result = {}
        for theme in THEMES:
            data_url = page.evaluate(
                "API.background(%s)" % json.dumps(theme)
            )
            raw = base64.b64decode(data_url.split(",", 1)[1])
            with Image.open(io.BytesIO(raw)) as img:
                result[theme] = img.convert("RGBA").copy()
        return result
    finally:
        page.close()


def main():
    sys.path.insert(0, str(ROOT / "tools"))
    import make_skin

    skin = make_skin
    last_mtime = None
    images = {}

    root = tk.Tk()
    selected = tk.StringVar(master=root, value="black")
    root.title("Fretty — Live UI Preview")
    root.geometry("1280x800")
    root.minsize(800, 450)

    toolbar = ttk.Frame(root, padding=10)
    toolbar.pack(fill="x")

    ttk.Label(
        toolbar, text="FRETTY LIVE PREVIEW", font=("TkDefaultFont", 12, "bold")
    ).pack(side="left", padx=(0, 18))

    status = tk.StringVar(value="Starting preview renderer…")
    image_label = ttk.Label(root, anchor="center")
    image_label.pack(fill="both", expand=True, padx=10, pady=(0, 4))
    status_label = ttk.Label(root, textvariable=status, padding=(10, 4))
    status_label.pack(fill="x")

    photo = None

    def display_theme(theme):
        nonlocal photo
        if theme not in images:
            return
        img = images[theme]
        available_w = max(image_label.winfo_width() - 12, 320)
        available_h = max(image_label.winfo_height() - 12, 240)
        scale = min(available_w / img.width, available_h / img.height, 1.0)
        size = (max(1, int(img.width * scale)), max(1, int(img.height * scale)))
        shown = img.resize(size, Image.Resampling.LANCZOS) if scale < 1 else img
        photo = ImageTk.PhotoImage(shown)
        image_label.configure(image=photo)
        selected.set(theme)

    buttons = {}
    for theme in THEMES:
        button = ttk.Button(
            toolbar, text=theme.title(),
            command=lambda t=theme: display_theme(t)
        )
        button.pack(side="left", padx=3)
        buttons[theme] = button

    def refresh(force=False):
        nonlocal skin, last_mtime, images
        try:
            mtime = SOURCE.stat().st_mtime_ns
            if force or mtime != last_mtime:
                status.set("Rendering updated artwork…")
                root.update_idletasks()
                if last_mtime is not None:
                    skin = importlib.reload(skin)
                images = render_all(browser, skin)
                last_mtime = SOURCE.stat().st_mtime_ns
                display_theme(selected.get())
                status.set(
                    f"Up to date · watching make_skin.py · checking every 2 seconds"
                )
        except Exception as exc:
            status.set(f"Preview error: {exc}")
            print("Preview refresh error:", repr(exc), flush=True)
        root.after(2000, refresh)

    def resize_preview(_event=None):
        if images:
            display_theme(selected.get())

    image_label.bind("<Configure>", resize_preview)

    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        root.protocol("WM_DELETE_WINDOW", root.destroy)
        try:
            root.after(100, lambda: refresh(force=True))
            root.mainloop()
        finally:
            try:
                browser.close()
            except Exception:
                pass
            try:
                root.destroy()
            except tk.TclError:
                pass


if __name__ == "__main__":
    main()
