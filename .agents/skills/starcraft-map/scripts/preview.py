#!/usr/bin/env python3
"""맵을 눈으로 보기 — 그려서 PNG 로 만든다.

`splash-cli render` 는 PPM 을 낸다. 읽어 볼 수 있게 PNG 로 바꾸고, 필요하면
일부만 오려 낸다. 만든 맵이 말이 되는지는 숫자만으로는 알 수 없다 —
반드시 한 번은 그려 봐야 한다.

보기:
    python3 preview.py map.scx out.png                     # 맵 전체
    python3 preview.py map.scx out.png --tiles 0 0 40 30   # 타일 범위만
    python3 preview.py map.scx out.png --scale 2           # 2배로 줄여서
"""
from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap


def read_ppm(path: str):
    """P6 PPM 을 (너비, 높이, bytes) 로 읽는다."""
    with open(path, "rb") as f:
        data = f.read()
    pos = 0
    fields = []
    while len(fields) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[pos:pos + width * height * 3]


def write_png(path: str, width: int, height: int, rgb: bytes):
    """PNG 로 쓴다. 바깥 라이브러리를 쓰지 않는다 (zlib 은 표준)."""
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)                                   # 필터: None
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def crop(width, height, rgb, x, y, w, h):
    x, y = max(0, x), max(0, y)
    w, h = min(w, width - x), min(h, height - y)
    out = bytearray()
    for row in range(y, y + h):
        start = (row * width + x) * 3
        out += rgb[start:start + w * 3]
    return w, h, bytes(out)


def downscale(width, height, rgb, factor: int):
    """정수 배수로 줄인다 (칸 평균)."""
    if factor <= 1:
        return width, height, rgb
    nw, nh = width // factor, height // factor
    out = bytearray(nw * nh * 3)
    for y in range(nh):
        for x in range(nw):
            r = g = b = 0
            for dy in range(factor):
                base = ((y * factor + dy) * width + x * factor) * 3
                for dx in range(factor):
                    r += rgb[base + dx * 3]
                    g += rgb[base + dx * 3 + 1]
                    b += rgb[base + dx * 3 + 2]
            n = factor * factor
            at = (y * nw + x) * 3
            out[at] = r // n
            out[at + 1] = g // n
            out[at + 2] = b // n
    return nw, nh, bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="맵을 그려 PNG 로 만든다")
    ap.add_argument("map")
    ap.add_argument("out", help="만들 PNG 경로")
    ap.add_argument("--tiles", nargs=4, type=int, metavar=("X", "Y", "W", "H"),
                    help="타일 범위만 오려 낸다")
    ap.add_argument("--scale", type=int, default=0,
                    help="정수 배수로 줄인다. 0 이면 1600px 아래로 알아서")
    ap.add_argument("--no-units", action="store_true")
    ap.add_argument("--locations", action="store_true")
    ap.add_argument("--install", default=None)
    args = ap.parse_args(argv)

    cli = scmap.Cli(args.map, args.install)
    with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as f:
        ppm = f.name
    try:
        cli.render(ppm, units=not args.no_units, locations=args.locations)
        width, height, rgb = read_ppm(ppm)
    finally:
        os.unlink(ppm)

    if args.tiles:
        x, y, w, h = args.tiles
        width, height, rgb = crop(width, height, rgb,
                                  x * scmap.TILE, y * scmap.TILE,
                                  w * scmap.TILE, h * scmap.TILE)

    scale = args.scale
    if scale == 0:
        scale = 1
        while max(width, height) // scale > 1600:
            scale += 1
    width, height, rgb = downscale(width, height, rgb, scale)

    write_png(args.out, width, height, rgb)
    print(f"{args.out}  {width}x{height}" + (f" (1/{scale})" if scale > 1 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
