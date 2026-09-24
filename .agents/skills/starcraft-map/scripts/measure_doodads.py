#!/usr/bin/env python3
"""**두뎃이 길을 막는지 미리 잰다** — `data/doodad-walk.json` 을 만든다.

왜 필요한가. 방 테두리를 두뎃으로 꾸미려다 동선이 끊겼다. 놓은 뒤
길찾기로 확인하고 되돌리는 식으로 하니 밀도를 올릴 수가 없고 (실측
중앙 868칸인데 48칸에서 멈췄다) 맵 하나에 1분이 넘게 걸렸다.

두뎃이 걷기를 막는지는 **두뎃 종류가 정하는 성질**이다. 맵마다 다시
재지 말고 타일셋마다 한 번 재서 표로 남긴다. 그러면 꾸미기는 안전한
것만 골라 쓰면 되고, 되돌릴 일이 없으니 빠르다.

재는 방법: 빈 맵에 걷는 바닥을 깔고 두뎃을 격자로 다 놓은 뒤, 타일을
한 번에 읽어 두뎃이 덮은 칸이 여전히 걷을 수 있는지 본다.

    python3 measure_doodads.py            # 여덟 타일셋 모두
"""
from __future__ import annotations

import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "data", "doodad-walk.json")
NAMES = {0: "badlands", 1: "space", 2: "installation", 3: "ashworld",
         4: "jungle", 5: "desert", 6: "ice", 7: "twilight"}
CELL = 10                 # 두뎃 하나에 10x10 칸을 준다 (가장 큰 것이 14x9)
COLS = 25                 # 250x... 격자


def measure(tmp: str, ts: int, install: str) -> dict:
    path = os.path.join(tmp, f"d{ts}.scx")
    cli = scmap.new_map(path, 256, 256, ts, terrain=None, melee=False,
                        install=install)
    tiles = scmap.tileset_tiles(cli, ts)
    # 걷을 수 있는 바닥 하나로 전부 깐다
    floor = next((t for t in sorted(tiles)
                  if t >= 32 and tiles[t][1] and tiles[t][2]), None)
    if floor is None:
        raise CliError(f"타일셋 {ts}: 바닥 타일을 못 찾음")
    cli.edit("terrain", "fill", cli.path, "0", "0", "256", "256", str(floor))

    cat = [d for d in cli.doodad_catalogue() if d["w"] <= CELL and d["h"] <= CELL]
    spots = {}
    strokes = []
    for i, d in enumerate(cat):
        gx, gy = (i % COLS) * CELL, (i // COLS) * CELL
        if gy + CELL >= 256:
            break
        spots[d["id"]] = (gx, gy, d)
        strokes.append((d["id"], gx, gy))
    for (did, gx, gy) in strokes:
        try:
            cli.edit("doodad", "place", cli.path, str(did), str(gx), str(gy),
                     "--install", cli.install)
        except CliError:
            spots.pop(did, None)

    g = cli.tiles(0, 0, 256, 256)
    out = {}
    for did, (gx, gy, d) in spots.items():
        walk_all = True
        changed = 0
        for ty in range(gy, min(256, gy + d["h"])):
            for tx in range(gx, min(256, gx + d["w"])):
                v = g[ty][tx]
                if v != floor:
                    changed += 1
                p = tiles.get(v)
                if p is None or not p[1]:
                    walk_all = False
        out[str(did)] = {"w": d["w"], "h": d["h"], "kind": d["kind"],
                         "walk": walk_all, "tiles": changed}
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
            ok = sum(1 for v in c.values() if v["walk"])
            data[NAMES[ts]] = c
            print(f"  {NAMES[ts]:13s} 두뎃 {len(c):3d}종 중 "
                  f"**걷기를 안 막는 것 {ok}종**")
    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"_about": "두뎃 번호 → 크기·갈래·걷기를 막는가. "
                             "measure_doodads.py 가 만든다. 걷는 바닥에 "
                             "놓아 보고 덮인 칸이 여전히 걷을 수 있는지 잰 값.",
                   "doodads": data}, f, ensure_ascii=False)
    print(f"\n썼습니다: {path}")


if __name__ == "__main__":
    main()
