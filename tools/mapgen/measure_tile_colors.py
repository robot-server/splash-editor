#!/usr/bin/env python3
"""**타일 그룹의 실제 색을 잰다** — `data/tile-colors.json` 을 만든다.

왜 필요한가. 방 바닥·통로·테두리·발판을 서로 다른 그룹으로 나눠도,
고른 그룹이 다 같은 눈밭이면 화면에서 하나로 보인다. "실측에서 많이
쓴 순서" 만으로 고르면 그렇게 된다 — Ice 의 그룹 2 와 3 은 둘 다 눈이다.

색은 짐작할 수 없으니 **그려서 잰다.** 타일셋마다 빈 맵에 그룹 대표
타일을 격자로 깔고, 렌더러로 그린 뒤 가운데 색을 읽는다.

    python3 measure_tile_colors.py            # 여덟 타일셋 모두
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli

import preview

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "data", "tile-colors.json")
NAMES = {0: "badlands", 1: "space", 2: "installation", 3: "ashworld",
         4: "jungle", 5: "desert", 6: "ice", 7: "twilight"}
CELL = 2                      # 그룹 하나를 2x2 타일로 깐다
COLS = 24


def measure(tmp: str, tileset_id: int, install: str) -> dict[int, list[int]]:
    path = os.path.join(tmp, f"c{tileset_id}.scx")
    W = H = COLS * CELL
    cli = scmap.new_map(path, W, H, tileset_id, terrain=None, melee=False,
                        install=install)
    tiles = scmap.tileset_tiles(cli, tileset_id)
    groups = sorted({t >> 4 for t in tiles})
    out: dict[int, list[int]] = {}
    for base in range(0, len(groups), COLS * COLS):
        chunk = groups[base:base + COLS * COLS]
        rows = [[0] * W for _ in range(H)]
        for i, g in enumerate(chunk):
            rep = next((t for t in range(g * 16, g * 16 + 16) if t in tiles), None)
            if rep is None:
                continue
            gx, gy = (i % COLS) * CELL, (i // COLS) * CELL
            for dy in range(CELL):
                for dx in range(CELL):
                    rows[gy + dy][gx + dx] = rep
        cli.paste_tiles(0, 0, rows)
        ppm = os.path.join(tmp, "r.ppm")
        cli.render(ppm, units=False, locations=False)
        pw, ph, rgb = preview.read_ppm(ppm)
        px = pw // W                       # 타일 하나가 몇 픽셀인가
        for i, g in enumerate(chunk):
            gx, gy = (i % COLS) * CELL * px, (i // COLS) * CELL * px
            n = r = gg = b = 0
            for y in range(gy + px // 2, gy + CELL * px - px // 2, 4):
                for x in range(gx + px // 2, gx + CELL * px - px // 2, 4):
                    o = (y * pw + x) * 3
                    if o + 2 < len(rgb):
                        r += rgb[o]; gg += rgb[o + 1]; b += rgb[o + 2]; n += 1
            if n:
                out[g] = [r // n, gg // n, b // n]
    return out


def main():
    install = scmap.find_install()
    data = {}
    with tempfile.TemporaryDirectory() as tmp:
        for ts in range(8):
            try:
                c = measure(tmp, ts, install)
            except Exception as e:
                print(f"  {NAMES[ts]}: 실패 {e}", file=sys.stderr)
                continue
            data[NAMES[ts]] = {str(k): v for k, v in sorted(c.items())}
            print(f"  {NAMES[ts]}: 그룹 {len(c)}개의 색을 쟀습니다")
    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"_about": "타일 그룹 → 렌더러로 잰 평균 RGB. "
                             "measure_tile_colors.py 가 만든다.",
                   "colors": data}, f, ensure_ascii=False)
    print(f"\n썼습니다: {path}")


if __name__ == "__main__":
    main()
