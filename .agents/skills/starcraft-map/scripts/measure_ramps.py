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


def ramp_direction(cli: Cli, d: dict, tiles: dict, tmp: str,
                   base_path: str, low: int) -> tuple[str, int]:
    """**램프칸이 고지대 덩이의 어느 쪽에 붙어 있나**로 방향을 읽는다.

    앞서 세 방법이 다 실패했다 (고도 무늬·통과 여부·CLI 거절). 되는 것은
    이것이다: 두뎃을 평지에 놓고 제가 쓴 타일만 보면,

        두뎃 340 (8x5)          두뎃 78 (6x6)
        000000R0                0000RR
        000000R0                000RRR
        000000R0                000RR0
        ......R0

    고지대 덩이(숫자)와 램프칸(R)이 나뉘어 있고, **R 이 놓인 쪽이 내려
    가는 쪽**이다. 340 은 오른쪽, 78 은 오른쪽 아래다.

    두뎃 크기와 실제로 쓰는 칸이 다르다는 것도 여기서 드러난다 — 6x6
    이라고 적혀 있어도 3x3 만 쓰기도 한다.
    """
    import shutil
    p = os.path.join(tmp, "dirprobe.scx")
    shutil.copy(base_path, p)
    c = Cli(p)
    w, h = d["w"], d["h"]
    try:
        scmap.place_doodad(c, d["id"], 20, 20, w, h)
    except CliError:
        return "?", 0
    g = c.tiles(20, 20, w, h)
    hi, rp = [], []
    for y, row in enumerate(g):
        for x, v in enumerate(row):
            if v == low:
                continue
            pr = tiles.get(v)
            if pr is None:
                continue
            (rp if pr[3] else hi).append((x, y))
    if not rp or not hi:
        return "?", len(rp)
    hx = sum(p2[0] for p2 in hi) / len(hi)
    hy = sum(p2[1] for p2 in hi) / len(hi)
    rx = sum(p2[0] for p2 in rp) / len(rp)
    ry = sum(p2[1] for p2 in rp) / len(rp)
    dx, dy = rx - hx, ry - hy
    if abs(dx) >= abs(dy):
        return ("right" if dx > 0 else "left"), len(rp)
    return ("down" if dy > 0 else "up"), len(rp)


def measure_one(tmp: str, ts: int, install: str, verbose=False) -> list[dict]:
    """한 타일셋의 램프를 찾아 방향까지 매긴다."""
    import shutil
    path = os.path.join(tmp, f"r{ts}.scx")
    cli = scmap.new_map(path, MAPW, MAPH, ts, terrain=None, melee=True,
                        install=install)
    tiles = scmap.tileset_tiles(cli, ts)
    low = next((t for t in sorted(tiles)
                if t >= 32 and tiles[t][1] and tiles[t][2]), None)
    if low is None:
        return []
    cli.edit("terrain", "fill", cli.path, "0", "0",
             str(MAPW), str(MAPH), str(low))
    base = os.path.join(tmp, f"base{ts}.scx")
    shutil.copy(cli.path, base)

    out = []
    for d in cli.doodad_catalogue():
        direction, n = ramp_direction(cli, d, tiles, tmp, base, low)
        if n == 0:
            continue
        if verbose:
            print(f"    두뎃 {d['id']:4d} {d['w']}x{d['h']} {d['kind']:22s} "
                  f"→ {direction:5s} (램프칸 {n})")
        out.append({"id": d["id"], "w": d["w"], "h": d["h"],
                    "kind": d["kind"], "dir": direction, "ramp_tiles": n})
    return out


def measure(tmp, ts, install, verbose=False):
    return measure_one(tmp, ts, install, verbose)


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
        json.dump({"_about": "타일셋의 **램프 두뎃** 목록. measure_ramps.py 가 "
                             "실측해 만든다. id 는 두뎃 번호(doodad place 에 "
                             "넣는 값), w·h 는 타일 수, kind 는 두뎃 갈래, "
                             "dir 는 **내려가는 쪽**(램프칸이 고지대 덩이의 "
                             "어느 쪽에 붙어 있나), ramp_tiles 는 Ramp 깃발이 "
                             "선 미니타일을 가진 칸 수.",
                   "ramps": data}, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
