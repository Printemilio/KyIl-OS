"""Compare real VM captures; exclude only menu bounds and the actual cursor.

Usage: python check-context-menu-pixels.py before.png after.png x y w h
       [--exclude x y w h] [--max-changed N] [--expect-change]
No screenshot repair or fabricated pixels are performed.
"""
import argparse
import json
import numpy as np
from PIL import Image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("before")
p.add_argument("after")
p.add_argument("region", nargs=4, type=int)
p.add_argument("--exclude", nargs=4, type=int, action="append", default=[])
p.add_argument("--max-changed", type=int, default=0)
p.add_argument("--expect-change", action="store_true")
p.add_argument("--require-red", nargs=2, type=int, help="Native close-control center")
a = p.parse_args()
before = np.array(Image.open(a.before).convert("RGB"))
after = np.array(Image.open(a.after).convert("RGB"))
assert before.shape == after.shape, "Capture dimensions changed"
x, y, w, h = a.region
assert min(x, y) >= 0 and min(w, h) > 0
assert x + w <= before.shape[1] and y + h <= before.shape[0]
mask = np.zeros(before.shape[:2], dtype=bool)
mask[y:y+h, x:x+w] = True
for ex, ey, ew, eh in a.exclude:
    mask[ey:ey+eh, ex:ex+ew] = False
changed = int(np.count_nonzero(np.any(before != after, axis=2) & mask))
passed = changed > 0 if a.expect_change else changed <= a.max_changed
if a.require_red:
    cx, cy = a.require_red
    for capture in (before, after):
        r, g, b = map(int, capture[cy, cx])
        passed = passed and r >= 220 and g <= 100 and b <= 100
print(json.dumps(dict(region=a.region, changed_pixels=changed, passed=passed)))
raise SystemExit(0 if passed else 1)
