#!/usr/bin/env python3
"""밀리맵 뼈대를 만든다 — 대칭 스타팅, 본진 고지대, 램프, 앞마당.

밀리맵에서 다투는 것은 밸런스다. 밸런스는 "누구도 자리 때문에 손해보지
않는 것"이고, 그 뿌리는 **대칭**이다. 그래서 이 스크립트는 지형·자원을
한 곳에만 그린 뒤 대칭 자리마다 같은 붓질을 되풀이한다.

`terrain mirror` 로 베끼지 않는 까닭: 그 명령은 타일 값을 그대로 옮긴다.
타일 그림에는 방향이 있어서(남향 절벽 타일은 옮겨도 남향이다) 베낀 쪽의
절벽이 뒤집혀 버린다. ISOM 붓질을 회전 좌표에 다시 놓으면 절벽이 제대로
선다 — 실제로 견주어 확인했다.

보기:
    python3 make_melee.py out.scx --players 4 --tileset jungle
    python3 make_melee.py out.scx --players 2 --size 128x96 --name "시험맵"
"""
from __future__ import annotations

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError

# 타일셋 이름 → 번호, 그리고 그 타일셋에서 쓸 저지대·고지대 ISOM 지형 이름.
# 이름은 `splash-cli terrain types <맵> --install ...` 이 내는 것과 같다.
TILESETS = {
    "badlands": (0, "Dirt", "High Dirt"),
    "space":    (1, "Platform", "High Platform"),
    "install":  (2, "Substructure", "Substructure"),
    "ashworld": (3, "Magma", "High Dirt"),
    "jungle":   (4, "Dirt", "High Dirt"),
    "desert":   (5, "Tar", "High Dirt"),
    "ice":      (6, "Ice", "High Snow"),
    "twilight": (7, "Dirt", "High Dirt"),
}

# 스타팅 수마다 어떤 대칭을 쓰는지. 공식 리그 맵 56개를 재어 보니 2인용은
# 4개 모두 180도 회전 대칭이었고 (좌우·상하 거울은 0개), 4인용은 35개 중
# 23개가 180도, 18개가 90도 회전 대칭이었다.
DEFAULT_SYMMETRY = {2: "rot180", 3: "radial", 4: "rot90",
                    5: "radial", 6: "radial", 7: "radial", 8: "radial"}


def parse_size(text: str) -> tuple[int, int]:
    m = text.lower().replace(" ", "").split("x")
    if len(m) != 2:
        raise argparse.ArgumentTypeError("크기는 128x128 꼴로 줍니다")
    return int(m[0]), int(m[1])


def start_positions(players: int, symmetry: str, width: int, height: int,
                    inset: int) -> list[tuple[int, int]]:
    """스타팅 자리를 대칭에 맞춰 고른다."""
    if symmetry == "rot90":
        if width != height:
            raise CliError("90도 회전 대칭은 가로세로가 같은 맵에서만 됩니다.")
        base = (inset, inset)
        pts = scmap.symmetric_points(base[0], base[1], "rot90", 4, width, height)
        return [(int(round(x)), int(round(y))) for x, y in pts][:players]

    if symmetry == "rot180":
        base = (inset, inset)
        pts = scmap.symmetric_points(base[0], base[1], "rot180", 2, width, height)
        return [(int(round(x)), int(round(y))) for x, y in pts][:players]

    # 방사 대칭 — 가운데를 축으로 고르게 돌린다.
    cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
    radius = min(cx, cy) - inset
    pts = []
    for k in range(players):
        a = -math.pi / 2 + 2 * math.pi * k / players
        pts.append((int(round(cx + radius * math.cos(a))),
                    int(round(cy + radius * math.sin(a)))))
    return pts


def quadrant_facing(x: int, y: int, width: int, height: int) -> int:
    """이 자리가 맵 가운데를 기준으로 어느 쪽인지 — 90도 단위."""
    cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
    angle = math.atan2(y - cy, x - cx)
    return int(round(angle / (math.pi / 2))) % 4


def paint_plateau(cli: Cli, cx: int, cy: int, half_w: int, half_h: int,
                  terrain: int, width: int, height: int):
    """(cx, cy) 를 가운데로 하는 고지대를 칠한다.

    ISOM 은 마름모 격자라 가로는 두 칸이 한 걸음이다. 가로를 짝수 걸음으로
    훑어야 빈 줄이 남지 않는다.
    """
    for ty in range(cy - half_h, cy + half_h + 1):
        if not (2 <= ty < height - 2):
            continue
        for tx in range(cx - half_w, cx + half_w + 1, 2):
            if not (2 <= tx < width - 2):
                continue
            cli.isom(tx, ty, terrain)


def find_cliff_row(cli: Cli, cx: int, y_from: int, y_to: int) -> int | None:
    """세로로 훑어 고지대가 끝나는 줄을 찾는다 — 램프를 걸 자리다."""
    step = 1 if y_to > y_from else -1
    rows = cli.tiles(cx, min(y_from, y_to), 1, abs(y_to - y_from) + 1)
    # rows[i] 는 (min(y_from,y_to) + i) 줄. 고지대 타일과 절벽 타일은 타일
    # 그룹이 다르다. 여기서는 "값이 크게 달라지는 첫 줄" 을 절벽으로 본다.
    values = [r[0] for r in rows]
    if step > 0:
        seq = list(enumerate(values))
    else:
        seq = list(reversed(list(enumerate(values))))
    prev = None
    for i, v in seq:
        if prev is not None and abs((v >> 4) - (prev >> 4)) > 8:
            return min(y_from, y_to) + i
        prev = v
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description="밀리맵 뼈대를 만든다")
    ap.add_argument("out", help="만들 맵 경로 (.scx 브루드워, .scm 하이브리드)")
    ap.add_argument("--players", type=int, default=4, help="스타팅 수 (2~8)")
    ap.add_argument("--size", type=parse_size, default=(128, 128),
                    help="맵 크기, 보기 128x128")
    ap.add_argument("--tileset", default="jungle", choices=sorted(TILESETS),
                    help="타일셋")
    ap.add_argument("--symmetry", default=None,
                    choices=["rot90", "rot180", "radial", "horizontal", "vertical"],
                    help="대칭 방식 (기본은 스타팅 수에 맞춰 고른다)")
    ap.add_argument("--name", default=None, help="맵 이름")
    ap.add_argument("--inset", type=int, default=10,
                    help="스타팅을 가장자리에서 몇 타일 안쪽에 둘지")
    ap.add_argument("--main-minerals", type=int, default=None)
    ap.add_argument("--main-gas", type=int, default=1)
    ap.add_argument("--natural-minerals", type=int, default=None)
    ap.add_argument("--natural-gas", type=int, default=None)
    ap.add_argument("--natural-distance", type=int, default=28,
                    help="본진에서 앞마당까지 타일 거리 (공식 맵 중앙값 28)")
    ap.add_argument("--expansions", type=int, default=2,
                    help="스타팅마다 놓을 바깥 멀티 수 "
                         "(본진·앞마당까지 합쳐 공식 맵 중앙값은 4곳)")
    ap.add_argument("--expansion-minerals", type=int, default=7)
    ap.add_argument("--expansion-gas", type=int, default=1)
    ap.add_argument("--favor", default="even",
                    choices=["even", "terran", "zerg", "protoss"],
                    help="자원 배치를 어느 종족 쪽으로 기울일지. "
                         "지형까지 기울이지는 않는다 — references/melee-balance.md 참고")
    ap.add_argument("--no-plateau", action="store_true",
                    help="본진 고지대를 만들지 않는다 (평지 맵)")
    ap.add_argument("--install", default=None)
    args = ap.parse_args(argv)

    width, height = args.size
    if not (2 <= args.players <= 8):
        ap.error("스타팅은 2~8 개입니다.")

    # 자원 지렛대. 근거는 references/melee-balance.md 에 적어 두었다.
    #   프로토스: 본진·앞마당 미네랄이 많을수록 유리 (초반 질럿 압박)
    #   저그    : 미네랄은 가난하되 가스는 있어야 유리 (확장력으로 이긴다)
    #   테란    : 앞마당에 가스가 없으면 유리 (가스를 적게 쓴다)
    FAVOR = {
        "even":    (9, 7, 1),
        "protoss": (10, 8, 1),
        "zerg":    (7, 6, 1),
        "terran":  (9, 7, 0),
    }
    d_main, d_nat, d_natgas = FAVOR[args.favor]
    if args.main_minerals is None:
        args.main_minerals = d_main
    if args.natural_minerals is None:
        args.natural_minerals = d_nat
    if args.natural_gas is None:
        args.natural_gas = d_natgas
    if args.favor != "even":
        print(f"자원을 {args.favor} 쪽으로 기울입니다: 본진 미네랄 {args.main_minerals}, "
              f"앞마당 미네랄 {args.natural_minerals}, 앞마당 가스 {args.natural_gas}")
    symmetry = args.symmetry or DEFAULT_SYMMETRY[args.players]
    if symmetry == "rot90" and width != height:
        symmetry = "radial"

    tileset_id, low_name, high_name = TILESETS[args.tileset]

    print(f"맵 {width}x{height} {args.tileset}, 스타팅 {args.players}개, 대칭 {symmetry}")
    cli = scmap.new_map(args.out, width, height, tileset_id,
                        terrain=low_name, melee=True, install=args.install)

    types = cli.terrain_types()
    if high_name not in types:
        # 타일셋마다 이름이 조금씩 다르다. "High" 로 시작하는 것을 쓴다.
        highs = [n for n in types if n.lower().startswith("high")]
        if not highs:
            print(f"  경고: {args.tileset} 에 고지대 지형이 없어 평지로 만듭니다.")
            args.no_plateau = True
            high_name = low_name
        else:
            high_name = highs[0]
            print(f"  고지대 지형을 {high_name} 로 잡습니다.")
    high_terrain = types.get(high_name, 0)

    starts = start_positions(args.players, symmetry, width, height, args.inset)
    print("스타팅 자리:", starts)

    # 1) 본진 고지대 — 대칭 자리마다 같은 붓질을 되풀이한다.
    if not args.no_plateau:
        print("본진 고지대를 칠합니다...")
        for i, (sx, sy) in enumerate(starts):
            paint_plateau(cli, sx, sy, half_w=11, half_h=7,
                          terrain=high_terrain, width=width, height=height)

    # 2) 램프 — 맵 가운데를 보는 쪽에 건다.
    ramp = scmap.default_ramp(tileset_id)
    if ramp is not None and not args.no_plateau:
        ramp_base, ramp_w, ramp_h = ramp
        print("램프를 놓습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        for (sx, sy) in starts:
            # 가운데를 향한 세로 방향으로 고지대가 끝나는 줄을 찾는다.
            toward = 1 if sy < cy else -1
            y_from = sy
            y_to = sy + toward * 12
            y_to = max(2, min(height - 8, y_to))
            row = find_cliff_row(cli, sx, y_from, y_to)
            if row is None:
                continue
            rx = sx - ramp_w // 2
            rx -= rx % 2                    # 짝수 칸에 맞춘다
            rx = max(0, min(width - ramp_w, rx))
            ry = max(0, min(height - ramp_h, row))
            try:
                scmap.place_ramp(cli, rx, ry, ramp_base, ramp_w, ramp_h)
            except CliError as e:
                print(f"  램프 건너뜀 ({rx},{ry}): {e}")

    # 3) 스타팅 표시와 본진 자원
    print("본진 자원을 놓습니다...")
    for i, (sx, sy) in enumerate(starts):
        facing = quadrant_facing(sx, sy, width, height)
        scmap.place_base(cli, sx, sy, owner=i + 1,
                         minerals=args.main_minerals, gas=args.main_gas,
                         facing=facing, width=width, height=height)

    # 4) 앞마당 — 본진에서 가운데 쪽으로 한 걸음.
    if args.natural_minerals > 0:
        print("앞마당을 놓습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        for i, (sx, sy) in enumerate(starts):
            vx, vy = cx - sx, cy - sy
            length = math.hypot(vx, vy) or 1.0
            nx = int(round(sx + vx / length * args.natural_distance))
            ny = int(round(sy + vy / length * args.natural_distance))
            nx = max(8, min(width - 9, nx))
            ny = max(8, min(height - 9, ny))
            facing = quadrant_facing(sx, sy, width, height)
            scmap.place_base(cli, nx, ny, owner=12,
                             minerals=args.natural_minerals,
                             gas=args.natural_gas,
                             facing=facing, width=width, height=height,
                             start_location=False)

    # 5) 바깥 멀티 — 스타팅마다 같은 상대 위치에 놓아 대칭을 지킨다.
    #    공식 맵은 스타팅당 자원 덩이가 중앙값 4곳이다 (본진·앞마당 포함).
    if args.expansions > 0:
        print(f"바깥 멀티 {args.expansions}곳씩 놓습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        # 스타팅에서 가운데를 보는 방향을 기준으로, 좌우로 벌려 놓는다.
        spread = [(-50, 1.55), (50, 1.55), (0, 2.1), (-70, 2.4), (70, 2.4)]
        for i, (sx, sy) in enumerate(starts):
            vx, vy = cx - sx, cy - sy
            base_angle = math.atan2(vy, vx)
            facing = quadrant_facing(sx, sy, width, height)
            for k in range(args.expansions):
                deg, scale = spread[k % len(spread)]
                a = base_angle + math.radians(deg)
                dist = args.natural_distance * scale
                ex = int(round(sx + dist * math.cos(a)))
                ey = int(round(sy + dist * math.sin(a)))
                ex = max(8, min(width - 9, ex))
                ey = max(8, min(height - 9, ey))
                scmap.place_base(cli, ex, ey, owner=12,
                                 minerals=args.expansion_minerals,
                                 gas=args.expansion_gas,
                                 facing=facing, width=width, height=height,
                                 start_location=False)

    # 자원량은 다 놓은 뒤 한 번에 맞춘다.
    scmap.set_all_resources(cli)

    if args.name:
        cli.set_map_name(args.name)

    info = cli.info()
    print(f"\n만들었습니다: {args.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']}  "
          f"유닛 {info['units']}  트리거 {info['triggers']}")
    print("\n다음으로:")
    print(f"  python3 {os.path.join(os.path.dirname(__file__), 'verify_map.py')} {args.out}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
