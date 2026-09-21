#!/usr/bin/env python3
"""컨트롤(전투) 유즈맵 — 가운데 싸움터에 스폰 주머니를 둘러 놓는다.

디펜스가 "막는" 고리라면 컨트롤은 "싸우는" 고리다. 되먹임이 훨씬 짧다 —
죽으면 바로 다시 받고, 잡으면 바로 점수가 오른다. 실측에서 컨트롤 맵의
트리거 수가 디펜스 다음으로 많은 까닭이다.

    ┌───────────────────────────┐
    │  ▣        싸움터        ▣ │   ▣ = 스폰 주머니 (플레이어마다)
    │        (장애물 몇 개)      │   가운데는 트여 있어야 싸움이 난다
    │  ▣                     ▣ │   주머니는 통로로 싸움터와 이어진다
    └───────────────────────────┘

점수는 게임이 세 주는 **죽인 수**를 쓴다 (`Leader Board Kills`,
`Kill` 조건). 직접 세려고 `Set Deaths` 를 쓰면 누가 잡았는지 못 가린다.

보기:
    python3 make_control.py out.scx --players 6 --goal 40
"""
from __future__ import annotations

import argparse
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus
import scmap
from scmap import Cli, CliError

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}
VOID_TILE = 0

# 고를 수 있는 병력. 값이 비슷해야 싸움이 된다.
SQUADS = [
    ("Terran Marine", 6, "머린 6기"),
    ("Protoss Zealot", 4, "질럿 4기"),
    ("Zerg Hydralisk", 4, "히드라 4기"),
    ("Terran Vulture", 3, "벌처 3기"),
]


def pocket_spots(players, W, H, pocket, margin=3):
    """스폰 주머니를 가장자리에 고르게 돌려 놓는다."""
    cx, cy = W / 2.0, H / 2.0
    rx, ry = cx - pocket / 2 - margin, cy - pocket / 2 - margin
    out = []
    for i in range(players):
        a = -math.pi / 2 + 2 * math.pi * i / players
        x = int(cx + rx * math.cos(a) - pocket / 2)
        y = int(cy + ry * math.sin(a) - pocket / 2)
        out.append((max(margin, min(W - pocket - margin, x)),
                    max(margin, min(H - pocket - margin, y))))
    return out



def build_triggers(players, goal, respawn_s, arena_names):
    T = []
    add = T.append
    HUMANS = ",".join(f'"Player {p}"' for p in range(1, players + 1))

    add(f'''Trigger({HUMANS}){{
Conditions:
\tAlways();

Actions:
\tSet Resources("Current Player", Set To, 300, ore);
\tDisplay Text Message(Always Display, "\\x04컨트롤 대전\\x02 — 먼저 \\x07{goal}킬\\x02 하면 이깁니다.");
\tDisplay Text Message(Always Display, "\\x03주머니 안 비콘을 밟아 병력을 고르세요. 죽으면 {respawn_s}초 뒤 다시 나옵니다.");
\tSet Countdown Timer(Set To, {respawn_s});
}}''')

    # 순위표 — 게임이 세 주는 죽인 수를 쓴다
    add(f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tLeader Board Kills("\\x07잡은 수", "Any unit");
\tPreserve Trigger();
}}''')

    # 되살리기 — 주머니가 비면 채워 준다
    for i, a in enumerate(arena_names):
        p = f"Player {i + 1}"
        add(f'''Trigger("{p}"){{
Conditions:
\tCountdown Timer(At most, 0);
\tCommand("{p}", "Men", At most, 2);

Actions:
\tCreate Unit("{p}", "Terran Marine", 6, "{a} Spawn");
\tDisplay Text Message(Always Display, "\\x03병력이 다시 나왔습니다.");
\tCenter View("{a} Spawn");
\tPreserve Trigger();
}}''')
        # 병력 고르기 — 비콘마다 다른 부대
        for k, (unit, n, label) in enumerate(SQUADS):
            add(f'''Trigger("{p}"){{
Conditions:
\tBring("{p}", "Any unit", "{a} Buy{k + 1}", At least, 1);
\tAccumulate("{p}", At least, 100, ore);

Actions:
\tSet Resources("{p}", Subtract, 100, ore);
\tCreate Unit("{p}", "{unit}", {n}, "{a} Gate");
\tDisplay Text Message(Always Display, "\\x03{label} 구입! \\x02-100");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
        # 잡으면 돈 — 되먹임을 짧게
        add(f'''Trigger("{p}"){{
Conditions:
\tKill("{p}", "Any unit", At least, 1);

Actions:
\tSet Resources("{p}", Add, 25, ore);
\tPreserve Trigger();
}}''')
        # 이김
        add(f'''Trigger("{p}"){{
Conditions:
\tKill("{p}", "Any unit", At least, {goal});

Actions:
\tDisplay Text Message(Always Display, "\\x07{goal}킬 달성!");
\tVictory();
}}''')

    # 되살리기 시계
    add(f'''Trigger("All players"){{
Conditions:
\tCountdown Timer(At most, 0);

Actions:
\tSet Countdown Timer(Set To, {respawn_s});
\tPreserve Trigger();
}}''')
    return "\n\n//-----------------------------------------------------------------//\n\n".join(T)


def main(argv=None):
    ap = argparse.ArgumentParser(description="컨트롤(전투) 유즈맵")
    ap.add_argument("out")
    ap.add_argument("--players", type=int, default=6)
    ap.add_argument("--goal", type=int, default=40, help="이기는 데 필요한 킬")
    ap.add_argument("--respawn", type=int, default=20)
    ap.add_argument("--size", default="128x128")
    ap.add_argument("--tileset", default="badlands", choices=sorted(TILESETS))
    ap.add_argument("--name", default="컨트롤 대전")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    if not (2 <= a.players <= 8):
        ap.error("2~8명입니다.")

    rng = random.Random(a.seed)
    W, H = (int(v) for v in a.size.lower().split("x"))
    ts = TILESETS[a.tileset]
    POCKET = 22

    print(f"컨트롤 {W}x{H} {a.tileset}, {a.players}명, {a.goal}킬")
    print("  " + corpus.describe("usemap", a.tileset))
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    groups = corpus.pick_floor_groups(corpus.load(), ts, "usemap", 14)

    spots = pocket_spots(a.players, W, H, POCKET)
    cx, cy = W // 2, H // 2
    AR = min(W, H) // 2 - POCKET - 4          # 싸움터 반지름

    print("벽을 세웁니다...")
    cli.edit("terrain", "fill", cli.path, "0", "0", str(W), str(H), str(VOID_TILE))

    # 싸움터를 둥글게 뚫는다 (네모로 뚫으면 형상 검사에 걸린다)
    print("싸움터를 뚫습니다...")
    tiles = scmap.tileset_tiles(cli, ts)
    floor = None
    for g in groups:
        good = [t for t in range(g * 16, g * 16 + 16)
                if t in tiles and tiles[t][1] and tiles[t][2]]
        if good:
            floor = good[0]
            break
    grid = cli.tiles(0, 0, W, H)
    for y in range(H):
        for x in range(W):
            d = math.hypot(x - cx, (y - cy) * 1.15)
            wob = 3.0 * math.sin(math.atan2(y - cy, x - cx) * 3 + 1.1)
            if d < AR + wob:
                grid[y][x] = floor
    cli.paste_tiles(0, 0, grid)

    print(f"스폰 주머니 {len(spots)}개와 통로를 뚫습니다...")
    regions = [(max(0, cx - AR - 4), max(0, cy - AR - 4),
                min(W, 2 * AR + 8), min(H, 2 * AR + 8))]
    for (px, py) in spots:
        cli.edit("terrain", "fill", cli.path, str(px), str(py),
                 str(POCKET), str(POCKET), str(floor))
        regions.append((px, py, POCKET, POCKET))
        # 주머니 → 싸움터 통로
        sx, sy = px + POCKET // 2, py + POCKET // 2
        steps = int(math.hypot(sx - cx, sy - cy))
        for t in range(steps + 1):
            gx = int(sx + (cx - sx) * t / max(1, steps))
            gy = int(sy + (cy - sy) * t / max(1, steps))
            cli.edit("terrain", "fill", cli.path, str(max(0, gx - 3)),
                     str(max(0, gy - 3)), "7", "7", str(floor))

    print("바닥을 칠합니다 (그룹을 섞어)...")
    scmap.paint_floor_mixed(cli, ts, rng, regions, groups)
    ch = scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)
    print(f"  변종 {ch}칸")

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, a.players, [])

    print("로케이션을 놓습니다...")
    loc = lambda n, x0, y0, x1, y1: cli.edit(
        "location", "add", cli.path, str(x0), str(y0), str(x1), str(y1),
        "--tiles", "--name", n)
    names = []
    for i, (px, py) in enumerate(spots):
        A = f"P{i + 1}"
        names.append(A)
        loc(f"{A} Spawn", px + 2, py + 2, px + 8, py + 8)
        loc(f"{A} Gate", px + POCKET // 2 - 3, py + POCKET - 6,
            px + POCKET // 2 + 3, py + POCKET - 1)
        for k in range(len(SQUADS)):
            bx = px + 2 + (k % 2) * 9
            by = py + 11 + (k // 2) * 5
            loc(f"{A} Buy{k + 1}", bx, by, bx + 4, by + 3)
        loc(f"{A} All", px, py, px + POCKET, py + POCKET)
    loc("Arena", cx - AR, cy - AR, cx + AR, cy + AR)

    print("유닛을 놓습니다...")
    for i, (px, py) in enumerate(spots):
        p = i + 1
        cli.place(scmap.START_LOCATION, px + 5, py + 5, owner=p)
        for k in range(6):
            cli.place("Terran Marine", px + 3 + k, py + 8, owner=p)
        for k in range(len(SQUADS)):
            bx = px + 4 + (k % 2) * 9
            by = py + 12 + (k // 2) * 5
            cli.place("Terran Beacon", bx, by, owner=p)
        # 업그레이드 건물 — 실측상 유즈맵 절반 이상이 둔다
        cli.place("Terran Engineering Bay", px + POCKET - 5, py + 3, owner=p)
        cli.place("Terran Armory", px + POCKET - 5, py + 7, owner=p)
    # 싸움터 가운데 장애물 — 트인 벌판이면 컨트롤이 안 나온다
    for k in range(10):
        ang = 2 * math.pi * k / 10
        ox = int(cx + AR * 0.45 * math.cos(ang))
        oy = int(cy + AR * 0.45 * math.sin(ang))
        cli.place("Protoss Pylon", ox, oy, owner=12)

    print("시야를 엽니다...")
    cli.edit("scenario", "revealers", cli.path, "--owner", "1", "--spacing", "16")

    print("트리거를 짭니다...")
    cli.apply_triggers(build_triggers(a.players, a.goal, a.respawn, names))
    if a.name:
        cli.set_map_name(a.name)

    info = cli.info()
    print(f"\n만들었습니다: {a.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']} {info['version']}")
    print(f"  유닛 {info['units']}  트리거 {info['triggers']}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
