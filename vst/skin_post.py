#!/usr/bin/env python3
"""Fretty-specific adjustments to generated MPC skin JSON."""
import json
import sys
from pathlib import Path

skin_dir = Path(sys.argv[1])
tui_path = next(skin_dir.rglob("TUI.json"))
data = json.loads(tui_path.read_text())

components = {}

def collect(obj):
    if isinstance(obj, dict):
        key = obj.get("key")
        if isinstance(key, str) and key.startswith("shStepText_"):
            components[key] = obj
        for value in obj.values():
            collect(value)
    elif isinstance(obj, list):
        for value in obj:
            collect(value)

collect(data)

verified = set()
for key, component in components.items():
    def resize(node):
        if isinstance(node, dict):
            font = node.get("font")
            if isinstance(font, dict) and font.get("name") == "Titillium Web":
                font["height"] = 30.0
                verified.add(key)
            for value in node.values():
                resize(value)
        elif isinstance(node, list):
            for value in node:
                resize(value)

    resize(component.get("value", {}))

if len(verified) != 4:
    raise SystemExit(
        f"ERROR: Expected 4 selector components with Titillium Web fonts; "
        f"found {len(verified)} of {len(components)}."
    )

tui_path.write_text(json.dumps(data, indent=2) + "\n")
print(f"Fretty skin post-processing: verified {len(verified)} selector live-value fonts at 30.0.")
