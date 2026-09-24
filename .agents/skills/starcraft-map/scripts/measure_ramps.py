#!/usr/bin/env python3
"""타일셋 램프 doodad의 방향·국소 연결 후보를 측정해 `data/ramps.json` 으로 쓴다.

이 표는 `DoodadPlacibility` 배치 허용 여부나 게임 엔진 동작을 검증하지 않는다.

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
   현재 CLI `doodad place` 는 `DoodadPlacibility` 를 검사하지 않는다.
   명령 성공을 배치 가능성의 증거로 사용하지 않는다.
3. 놓은 결과에서 **미니타일 길찾기로** 고지대↔저지대가 통하는지 본다.
   이 검사는 시험 지형의 보행 연결만 확인한다. 에디터 배치 허용 여부와
   게임에서의 유닛 통과는 별도 검증 대상이다.

`DoodadPlacibility` 는 에디터가 doodad 배치를 허용하는 지형 조합 표다.
현재 측정기는 이 표의 결과를 후보 데이터에 반영하지 않는다.

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


def walk_check(cli: Cli, doodad_id: int, w: int, h: int, tileset_id: int,
               tmp: str, base_path: str) -> bool:
    """**고지대에서 저지대로 걸어서 통하는가**를 실제로 본다.

    이것이 없으면 "램프" 가 아니라 "램프 깃발이 선 칸이 있는 두뎃" 일
    뿐이다. 앞서 이 단계를 빠뜨리고도 출력에는 "통하는 램프" 라고
    찍었다 — 적대적 검토에서 잡혔다.

    **절벽은 ISOM 으로만 생긴다.** 처음에 `terrain fill` 로 고지대
    타일을 깔았더니 걷는 칸이 65536/65536 — 절벽이 없어 두뎃 없이도
    통했고, 그래서 모든 두뎃이 "안 통함" 으로 나왔다. ISOM 으로
    칠하니 64212/65536 이 되고 막힌다.
    """
    import shutil
    p = os.path.join(tmp, "walkprobe.scx")
    shutil.copy(base_path, p)
    c = Cli(p)
    mid = MAPH // 2

    def probe():
        g = scmap.walk_grid(c, tileset_id, 0, 0, MAPW, MAPH)
        a_ = scmap.nearest_walkable(g, MAPW * 2, (mid // 2) * 4)
        b_ = scmap.nearest_walkable(g, MAPW * 2, ((mid + MAPH) // 2) * 4)
        return bool(a_ and b_ and scmap.walk_reachable(g, a_, b_))

    if probe():
        return False        # 두뎃 없이도 통하면 시험이 안 된다
    try:
        scmap.place_doodad(c, doodad_id, MAPW // 2, mid, w, h)
    except CliError:
        return False
    return probe()


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

    # 걷기 시험용 바탕은 **ISOM 으로** 만든다 — 생타일로 깔면 절벽이
    # 안 생겨 시험이 성립하지 않는다.
    ids = scmap.isom_terrain_ids(cli, ts)
    base = os.path.join(tmp, f"base{ts}.scx")
    shutil.copy(cli.path, base)

    # 걷기 시험용 바탕 — **고지대 지형마다 하나씩** 만든다.
    #
    # 처음에는 `ids["high"][0]` 하나로만 시험했다가 Ice·Twilight 이
    # 0개로 나왔다. Ice 는 첫 고지대가 `Outpost` 인데 `Cliff` 램프는
    # Ice↔High Ice 에 붙는 것이라 안 통한 것이다. **두뎃 갈래 이름이
    # 곧 지형 종류 이름**이므로 (docs/tileset/doodads.md) 갈래에 맞는
    # 지형부터 시험하고, 안 되면 나머지도 돌아본다.
    inv = {v: k for k, v in ids["names"].items()}
    walkbases = {}
    if ids["low"] and ids["high"]:
        for hi in ids["high"]:
            path_h = os.path.join(tmp, f"walkbase{ts}_{hi}.scx")
            shutil.copy(cli.path, path_h)
            wc = Cli(path_h)
            scmap.isom_fill(wc, ids["low"][0], 0, 0, MAPW, MAPH)
            scmap.isom_fill(wc, hi, 0, 0, MAPW, MAPH // 2)
            walkbases[inv.get(hi, str(hi))] = path_h

    out = []
    for d in cli.doodad_catalogue():
        direction, n = ramp_direction(cli, d, tiles, tmp, base, low)
        if n == 0:
            continue
        if verbose:
            print(f"    두뎃 {d['id']:4d} {d['w']}x{d['h']} {d['kind']:22s} "
                  f"→ {direction:5s} (램프칸 {n})")
        walks, walks_with = None, None
        if walkbases:
            # 갈래 이름과 가장 가까운 지형부터
            kind = d["kind"]
            order = sorted(walkbases,
                           key=lambda nm: (kind not in nm and nm not in kind,
                                           len(nm)))
            walks = False
            for nm in order:
                if walk_check(cli, d["id"], d["w"], d["h"], ts, tmp,
                              walkbases[nm]):
                    walks, walks_with = True, nm
                    break
        out.append({"id": d["id"], "w": d["w"], "h": d["h"],
                    "kind": d["kind"], "dir": direction, "ramp_tiles": n,
                    "walks": walks, "walks_with": walks_with})
    return out


def measure(tmp, ts, install, verbose=False):
    return measure_one(tmp, ts, install, verbose)


def main(argv=None):
    ap = argparse.ArgumentParser(description="타일셋 램프 doodad의 방향·국소 보행 후보를 측정한다")
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
            n_walk = sum(1 for f in found if f.get("walks"))
            print(f"    램프 깃발이 선 두뎃 {len(found)}개 "
                  f"(시험 지형에서 국소 경로가 이어진 후보 {n_walk}개)  " +
                  (", ".join(f"{d} {n}" for d, n in sorted(by_dir.items()))
                   or "없음"))
    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"_about": "타일셋의 램프 doodad 방향·국소 보행 후보. "
                             "measure_ramps.py 가 만든다. id 는 doodad 번호, "
                             "w·h 는 타일 수, kind 는 갈래, dir 는 Ramp 깃발이 "
                             "놓인 방향, ramp_tiles 는 해당 타일 수, walks 는 "
                             "시험 지형의 미니타일 경로 연결 여부다. "
                             "DoodadPlacibility 배치 허용 여부나 게임 엔진 "
                             "통과를 보증하지 않는다.",
                   "ramps": data}, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
