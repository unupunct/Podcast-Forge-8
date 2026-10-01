"""Generates resources/PodcastForge8.ico (16, 24, 32, 48, 64, 128, 256 px) with no dependencies.

The mark: a studio microphone (capsule with grille, yoke and stand) inside an accent ring on the
app's dark tile. Drawn with signed-distance shapes and 4x4 supersampling; stored as PNG frames.
Run:  python scripts/make-icon.py
"""
import math
import os
import struct
import zlib

BG = (17, 19, 23)
TILE = (26, 29, 35)
ACCENT = (63, 169, 245)
MIC = (230, 232, 235)
GRILLE = (139, 146, 158)
RED = (229, 57, 53)


def sd_round_rect(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - hw + r
    qy = abs(py - cy) - hh + r
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def sd_circle(px, py, cx, cy, r):
    return math.hypot(px - cx, py - cy) - r


def sample(x, y):
    """Colour (r, g, b, a) at unit coordinates (0..1)."""
    layers = []
    # Tile.
    d = sd_round_rect(x, y, 0.5, 0.5, 0.47, 0.47, 0.11)
    if d <= 0:
        layers.append(TILE)
    else:
        return None
    # Accent ring.
    ring = abs(sd_circle(x, y, 0.5, 0.47, 0.34)) - 0.028
    if ring <= 0:
        layers.append(ACCENT)
    # Stand: yoke (U arc), stem, base.
    yoke = abs(sd_circle(x, y, 0.5, 0.44, 0.165)) - 0.022
    if yoke <= 0 and y > 0.44:
        layers.append(MIC)
    if sd_round_rect(x, y, 0.5, 0.69, 0.022, 0.06, 0.01) <= 0:
        layers.append(MIC)
    if sd_round_rect(x, y, 0.5, 0.765, 0.13, 0.022, 0.02) <= 0:
        layers.append(MIC)
    # Capsule with grille slots and a red "on air" dot.
    cap = sd_round_rect(x, y, 0.5, 0.36, 0.095, 0.155, 0.095)
    if cap <= 0:
        layers.append(MIC)
        for gy in (0.28, 0.33, 0.38):
            if sd_round_rect(x, y, 0.5, gy, 0.06, 0.011, 0.011) <= 0:
                layers.append(GRILLE)
        if sd_circle(x, y, 0.5, 0.445, 0.022) <= 0:
            layers.append(RED)
    return layers[-1] + (255,)


def render(size):
    ss = 4 if size >= 32 else 6
    rows = []
    for j in range(size):
        row = bytearray([0])  # PNG filter: none
        for i in range(size):
            r = g = b = a = 0
            for sj in range(ss):
                for si in range(ss):
                    c = sample((i + (si + 0.5) / ss) / size, (j + (sj + 0.5) / ss) / size)
                    if c:
                        r += c[0]
                        g += c[1]
                        b += c[2]
                        a += 255
            n = ss * ss
            if a:
                row += bytes((r * 255 // a, g * 255 // a, b * 255 // a, a // n))
            else:
                row += bytes((0, 0, 0, 0))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    sizes = [256, 128, 64, 48, 32, 24, 16]
    frames = [render(s) for s in sizes]
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    for s, png in zip(sizes, frames):
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "resources", "PodcastForge8.ico")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(header + entries + b"".join(frames))
    with open(os.path.join(root, "resources", "icon-256.png"), "wb") as f:
        f.write(frames[0])
    print("wrote", out)


if __name__ == "__main__":
    main()
