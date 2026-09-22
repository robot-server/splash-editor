#!/usr/bin/env python3
"""**램프를 타일셋에서 직접 찾아 검증한다** — `data/ramps.json` 을 만든다.

## 왜 다시 하나

`scmap.RAMPS_BY_DIR` 은 손으로 적은 표였고 두 군데가 틀렸다.

1. **네 방향이 같은 타일 세트였다.** `down`·`up`·`left`·`right` 가 모두
   같은 바탕 타일(Badlands 0x4a70)에서 **행만 잘라** 쓰고 있었다. 우하단
   하나를 네 방향에 다 붙인 셈이라 세 방향이 깨진다.
2. **"이 타일셋은 램프가 없다" 고 적어 두었다.** Space·Desert·Ice·
   Twilight 넷이다. 타일셋 데이터를 읽어 보니 **넷 다 램프 타일이 있다**
   (Desert 607개, Space 484개, Ice 371개, Twilight 371개).

짐작할 일이 아니었다. 게임이 이미 알고 있다 — `sc.h` 의 미니타일 표에
`Ramp = BIT_4` 가 있고, `tileset-tiles` 가 그 표시를 그대로 내준다.

## 램프는 두뎃이다

처음에는 생타일 덩이로 보고 `(그룹, 서브)` 평면에서 잘라 내려 했는데
**하나도 못 찾았다.** 램프 표시가 붙은 타일이 전부 그룹 1024 이상 —
**두뎃 영역**이었다. 손으로 적어 둔 표의 바탕값 0x4a70 도 그룹 1191 이니
애초에 두뎃 번호를 생타일처럼 쓰고 있었던 것이다.

그래서 이렇게 찾는다:

1. 두뎃을 평지에 하나씩 놓아 보고 **램프 표시 타일이 나오는 것**을
   후보로 고른다. Badlands 에서는 `Cliff` 와 `Structure Wall` 갈래다.
2. 후보마다 네 방향을 다 시험한다. 한쪽을 고지대로 올리고 경계에 놓는다.
   `doodad place` 는 `DoodadPlacibility` 를 보므로 **안 맞는 자리에서는
   스스로 거절한다** — 그 거절이 곧 "이 방향에는 안 쓴다" 는 답이다.
3. 놓인 것만 **미니타일 길찾기로** 고지대↔저지대가 통하는지 본다.

2번이 사용자가 짚은 것이다: "경사로 타일이 세트로 구성되고 벽에 접합이
되는지도 StarEdit 에 이미 있다." 그 표가 `dddata.bin` 의
`DoodadPlacibility` 다.

    python3 measure_ramps.py            # 여덟 타일셋 모두
    python3 measure_ramps.py --tileset 5 --verbose
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "data", "ramps.json")
NAMES = {0: "badlands", 1: "space", 2: "installation", 3: "ashworld",
         4: "jungle", 5: "desert", 6: "ice", 7: "twilight"}
MAPW = MAPH = 64


RAMP_KINDS = ("Cliff", "Structure Wall", "Wall")


def ramp_candidates(cli: Cli, tiles: dict[int, tuple], tmp: str,
                    ts: int) -> list[dict]:
    """**램프는 두뎃이다.** 그 사실부터 재서 확인한다.

    처음에는 램프를 생타일 덩이로 보고 `(그룹, 서브)` 평면에서 잘라 내려
    했는데 하나도 못 찾았다. 램프 표시가 붙은 타일이 전부 그룹 1024 이상
    — **두뎃 영역**이었다. 손으로 적어 둔 표의 바탕값 0x4a70 도 그룹
    1191 이니 애초에 두뎃 번호였다.

    그래서 두뎃을 하나씩 평지에 놓아 보고, 램프 표시가 붙은 타일이
    나오는 것만 후보로 고른다. Badlands 에서는 `Cliff` 와
    `Structure Wall` 갈래에서만 나온다.
    """
    import shutil
    base = os.path.join(tmp, f"cand{ts}.scx")
    shutil.copy(cli.path, base)
    b = Cli(base)
    low = next((t for t in sorted(tiles)
                if t >= 32 and tiles[t][1] and tiles[t][2]
                and tiles[t][0] == 0), None)
    if low is None:
        return []
    b.edit("terrain", "fill", b.path, "0", "0", str(MAPW), str(MAPH), str(low))
    out = []
    for d in b.doodad_catalogue():
        if not any(k in d["kind"] for k in RAMP_KINDS):
            continue
        p = os.path.join(tmp, "probe.scx")
        shutil.copy(b.path, p)
        c = Cli(p)
        try:
            c.edit("doodad", "place", p, str(d["id"]), "20", "20",
                   "--install", c.install)
        except CliError:
            continue
        g = c.tiles(20, 20, d["w"], d["h"])
        n = sum(1 for row in g for v in row
                if tiles.get(v, (0, 0, 0, 0))[3])
        if n:
            out.append({**d, "ramp_tiles": n})
    return out


def _isom_half(c: Cli, ts: int, direction: str, low_idx: int, high_idx: int):
    """한쪽을 **ISOM 으로** 고지대로 올린다 — 그래야 진짜 절벽이 생긴다.

    생타일로 채우면 고지대와 저지대가 맞닿기만 하고 절벽이 안 생겨
    **그냥 걸어 넘어간다.** 처음 판이 그랬고, 그래서 후보가 전부
    네 방향을 다 통과했다 (140개 전원 통과 = 시험이 아무것도 안 잼).
    """
    strokes = []
    for j in range(MAPH):
        for i in range(j & 1, MAPW // 2, 2):
            x = 2 * i
            if direction == "down":
                hi = j < MAPH // 2
            elif direction == "up":
                hi = j >= MAPH // 2
            elif direction == "left":
                hi = x < MAPW // 2
            else:
                hi = x >= MAPW // 2
            strokes.append((x, j, high_idx if hi else low_idx, 1))
    c.isom_batch(strokes)


def _connected(c: Cli, ts: int, hp, lp) -> bool:
    grid = scmap.walk_grid(c, ts, 0, 0, MAPW, MAPH)
    a2 = scmap.nearest_walkable(grid, hp[0] * 4 + 2, hp[1] * 4 + 2, radius=8)
    b2 = scmap.nearest_walkable(grid, lp[0] * 4 + 2, lp[1] * 4 + 2, radius=8)
    if a2 is None or b2 is None:
        return False
    return scmap.walk_reachable(grid, a2, b2)


def try_direction(cli: Cli, ts: int, dood: dict, direction: str,
                  tiles: dict[int, tuple], tmp: str,
                  low_idx: int, high_idx: int) -> bool:
    """**절벽으로 막힌 자리를 램프가 뚫는가** — 차이를 잰다.

    한쪽을 ISOM 으로 올려 절벽을 세우고,

      1. 램프 없이 고지대↔저지대가 **막혀 있는지** 먼저 본다.
         안 막혀 있으면 이 시험은 아무것도 재지 못한다 — 건너뛴다.
      2. 램프를 경계에 놓는다. `doodad place` 가 거절하면 그 방향이
         아니다 (`DoodadPlacibility` 가 붙는 자리를 안다).
      3. 그제야 통하면 그 두뎃이 그 방향의 램프다.

    1번이 없으면 "원래 통하던 것" 을 램프 덕이라고 세게 된다.
    """
    import shutil
    p = os.path.join(tmp, "dir.scx")
    shutil.copy(cli.path, p)
    c = Cli(p)
    _isom_half(c, ts, direction, low_idx, high_idx)
    cx, cy = MAPW // 2, MAPH // 2
    w, h = dood["w"], dood["h"]
    if direction == "down":
        hp, lp = (cx, cy - 10), (cx, cy + 10)
    elif direction == "up":
        hp, lp = (cx, cy + 10), (cx, cy - 10)
    elif direction == "left":
        hp, lp = (cx - 10, cy), (cx + 10, cy)
    else:
        hp, lp = (cx + 10, cy), (cx - 10, cy)
    if _connected(c, ts, hp, lp):
        return False                      # 절벽이 안 섰다 — 잴 수 없다
    ax, ay = cx - w // 2, cy - h // 2
    for dy in (-2, -1, 0, 1, 2):          # 경계 높이가 한두 칸 밀릴 수 있다
        q = os.path.join(tmp, "try.scx")
        shutil.copy(p, q)
        cq = Cli(q)
        try:
            cq.edit("doodad", "place", q, str(dood["id"]),
                    str(ax if direction in ("down", "up") else ax + dy),
                    str(ay + dy if direction in ("down", "up") else ay),
                    "--install", cq.install)
        except CliError:
            continue
        if _connected(cq, ts, hp, lp):
            return True
    return False


def measure(tmp: str, ts: int, install: str, verbose=False) -> list[dict]:
    path = os.path.join(tmp, f"r{ts}.scx")
    cli = scmap.new_map(path, MAPW, MAPH, ts, terrain=None, melee=True,
                        install=install)
    tiles = scmap.tileset_tiles(cli, ts)
    # ISOM 으로 절벽을 세우려면 저지대·고지대 지형 번호가 필요하다
    types = cli.terrain_types()
    tt = scmap.terrain_types_table(ts)
    def lev(name):
        v = tt.get(name, {}).get("levels") or {}
        return int(max(v, key=v.get)) if v else 0
    lows = sorted((n for n in types if lev(n) == 0), key=lambda n: types[n])
    highs = sorted((n for n in types if lev(n) > 0), key=lambda n: types[n])
    if not lows or not highs:
        if verbose:
            print("    저지대/고지대 지형을 못 찾음 — 이 타일셋은 건너뜀")
        return []
    low_idx, high_idx = types[lows[0]], types[highs[0]]
    if verbose:
        print(f"    절벽: {lows[0]}({low_idx}) ↔ {highs[0]}({high_idx})")

    cands = ramp_candidates(cli, tiles, tmp, ts)
    if verbose:
        print(f"    램프 후보 두뎃 {len(cands)}종")
    found = []
    for d in cands:
        dirs = []
        for direction in ("down", "up", "left", "right"):
            try:
                if try_direction(cli, ts, d, direction, tiles, tmp,
                                 low_idx, high_idx):
                    dirs.append(direction)
            except Exception:
                pass
        if dirs and verbose:
            print(f"    두뎃 {d['id']:4d} {d['w']}x{d['h']} {d['kind']:15s} "
                  f"→ {', '.join(dirs)}")
        for direction in dirs:
            found.append({"id": d["id"], "w": d["w"], "h": d["h"],
                          "kind": d["kind"], "dir": direction})
    return found


def main(argv=None):
    ap = argparse.ArgumentParser(description="타일셋에서 램프를 찾아 검증한다")
    ap.add_argument("--tileset", type=int)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--install")
    a = ap.parse_args(argv)

    install = a.install or scmap.find_install()
    which = [a.tileset] if a.tileset is not None else list(range(8))
    data = {}
    with tempfile.TemporaryDirectory() as tmp:
        for ts in which:
            print(f"=== {NAMES[ts]}")
            try:
                found = measure(tmp, ts, install, a.verbose)
            except Exception as e:
                print(f"    실패: {e}", file=sys.stderr)
                continue
            by_dir = collections.Counter(f["dir"] for f in found)
            data[NAMES[ts]] = found
            print(f"    통하는 램프 {len(found)}개  " +
                  (", ".join(f"{d} {n}" for d, n in sorted(by_dir.items()))
                   or "없음"))
    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"_about": "타일셋에서 찾아 **걸어서 검증한** 램프. "
                             "measure_ramps.py 가 만든다. base 는 왼위 타일 "
                             "번호, w·h 는 타일 수, dir 는 고지대가 있는 쪽.",
                   "ramps": data}, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
