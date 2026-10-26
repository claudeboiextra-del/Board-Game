#!/usr/bin/env python3
"""Generate a simple 320x176 ICON0.PNG for Board Game if none exists."""
import sys
import struct
import zlib

def write_png(path, w, h, rgba_func):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

    raw = b""
    for y in range(h):
        raw += b"\x00"  # filter none
        for x in range(w):
            r, g, b, a = rgba_func(x, y, w, h)
            raw += bytes((r, g, b, a))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", ihdr)
    data += chunk(b"IDAT", zlib.compress(raw, 9))
    data += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)

def color(x, y, w, h):
    # dark blue background + yellow star-ish circle
    cx, cy = w // 2, h // 2
    dx, dy = x - cx, y - cy
    dist = (dx*dx + dy*dy) ** 0.5
    if dist < 40:
        return (241, 196, 15, 255)  # gold
    if dist < 55:
        return (243, 156, 18, 255)
    # subtle gradient
    t = y / h
    r = int(20 + 30 * t)
    g = int(25 + 20 * t)
    b = int(45 + 40 * t)
    return (r, g, b, 255)

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "ICON0.PNG"
    write_png(out, 320, 176, color)
    print("Wrote", out)
