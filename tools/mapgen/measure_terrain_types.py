#!/usr/bin/env python3
"""**지형 종류마다 어떤 타일 그룹·고도가 나오는지 잰다.**

`data/terrain-types.json` 을 만든다.

왜 필요한가. 고도 형상을 재려는데 기준이 세 번 달라졌다.

  1차: 고도 >= 2 를 고지로 → 공식 맵 균일 창 65%, 고지 3%
  2차: 고도 >= 1 을 고지로 → 고지 41%. 그런데 Jungle 평지가 고도 0·1 이
       섞여 있어(Dirt 0, Jungle 1) 41% 는 평지의 타일 변종이었다.
  3차: 다시 >= 2 로 이진화 → **Fighting Spirit·Lost Temple·Tau Cross·
       Luna 가 "고지대 0.0%"** 로 나왔다. 램프가 있는 맵에 고지대가
       없을 수는 없다. 지표가 틀린 것이다.

타일셋의 고도 칸은 타일셋마다 다른 눈금을 쓴다. 그래서 짐작하지 않고
**ISOM 으로 지형 종류를 하나씩 칠해 보고** 어떤 그룹·고도가 나오는지
직접 잰다. "High" 로 시작하는 종류가 실제로 어떤 고도를 내는지 보면
그 타일셋에서 고지의 기준이 무엇인지 알 수 있다.

    python3 measure_terrain_types.py
"""
from __future__ import annotations

import collections
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "data", "terrain-types.json")
NAMES = {0: "badlands", 1: "space", 2: "installation", 3: "ashworld",
         4: "jungle", 5: "desert", 6: "ice", 7: "twilight"}
PATCH = 24          # 종류 하나를 24x24 타일에 칠한다


def measure(tmp: str, ts: int, install: str) -> dict:
    path = os.path.join(tmp, f"t{ts}.scx")
    cli = scmap.new_map(path, 128, 128, ts, terrain=None, melee=True,
                        install=install)
    tiles = scmap.tileset_tiles(cli, ts)
    types = cli.terrain_types()
    out = {}
    for name, idx in sorted(types.items(), key=lambda kv: kv[1]):
        # 종류마다 새 맵에 칠한다 — 이웃 종류가 경계를 바꾸면 안 된다
        p2 = os.path.join(tmp, f"t{ts}_{idx}.scx")
        c2 = scmap.new_map(p2, 64, 64, ts, terrain=None, melee=True,
                           install=install)
        strokes = []
        for j in range(8, 8 + PATCH):
            for i in range((j & 1) + 4, (8 + PATCH) // 2, 2):
                strokes.append((2 * i, j, idx, 1))
        try:
            c2.isom_batch(strokes)
        except Exception:
            continue
        g = c2.tiles(10, 10, PATCH - 4, PATCH - 4)
        cnt = collections.Counter()
        lev = collections.Counter()
        for row in g:
            for v in row:
                cnt[v >> 4] += 1
                lev[tiles.get(v, (None,))[0]] += 1
        out[name] = {"index": idx,
                     "groups": [g for g, _ in cnt.most_common(6)],
                     "levels": {str(k): v for k, v in lev.most_common()}}
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
            data[NAMES[ts]] = c
            print(f"\n=== {NAMES[ts]}")
            for name, v in c.items():
                top = max(v["levels"].items(), key=lambda kv: kv[1])[0] \
                    if v["levels"] else "?"
                print(f"   {name:24s} 번호 {v['index']:3d}  주고도 {top}  "
                      f"고도분포 {v['levels']}  그룹 {v['groups'][:4]}")
    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"_about": "지형 종류 → ISOM 번호·나오는 타일 그룹·고도 분포. "
                             "measure_terrain_types.py 가 실제로 칠해 보고 잰다.",
                   "types": data}, f, ensure_ascii=False)
    print(f"\n썼습니다: {path}")


if __name__ == "__main__":
    main()
