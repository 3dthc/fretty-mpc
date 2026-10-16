#!/usr/bin/env python3
"""Fretty-specific adjustments to generated MPC skin JSON."""
import json
import sys
from pathlib import Path

skin_dir = Path(sys.argv[1])
tui_path = next(skin_dir.rglob("TUI.json"))
data = json.loads(tui_path.read_text())
changed = 0

def walk(obj):
    global changed
    if isinstance(obj, dict):
        key = obj.get("key", "")
        if isinstance(key, str) and key.startswith("shStepText_"):
            def resize(node):
                global changed
                if isinstance(node, dict):
                    font = node.get("font")
                    if isinstance(font, dict) and font.get("name") == "Titillium Web":
                        if font.get("height") == 26.0:
                            font["height"] = 30.0
                            changed += 1
                    for value in node.values():
                        resize(value)
                elif isinstance(node, list):
                    for value in node:
                        resize(value)
            resize(obj.get("value", {}))
        for value in obj.values():
            walk(value)
    elif isinstance(obj, list):
        for value in obj:
            walk(value)

walk(data)
if changed == 0:
    raise SystemExit("ERROR: No selector live-value fonts were changed; refusing to rewrite TUI.json.")

tui_path.write_text(json.dumps(data, indent=2) + "\n")
print(f"Fretty skin post-processing: enlarged {changed} selector live-value font(s) to 30.0.")
