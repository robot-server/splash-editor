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
import random
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


def in_base_or_exit(tx: int, ty: int, starts, cx: float, cy: float,
                    base_w: int = 14, base_h: int = 10,
                    corridor: int = 34, half: int = 9) -> bool:
    """이 자리가 본진 언저리이거나 본진에서 가운데로 나가는 통로 안인지.

    통로를 비워 두지 않으면 지형 덩이가 출구를 막아 본진이 섬이 된다.
    """
    for (sx, sy) in starts:
        if abs(tx - sx) < base_w and abs(ty - sy) < base_h:
            return True
        vx, vy = cx - sx, cy - sy
        length = math.hypot(vx, vy) or 1.0
        ux, uy = vx / length, vy / length
        # 통로 선분 위로 사영해 거리를 잰다
        px, py = tx - sx, ty - sy
        t = px * ux + py * uy
        if 0 <= t <= corridor:
            if abs(px - ux * t) <= half and abs(py - uy * t) <= half:
                return True
    return False


def plateau_strokes(cx: int, cy: int, half_w: int, half_h: int,
                    terrain: int, width: int, height: int):
    """(cx, cy) 를 가운데로 하는 고지대 붓질 목록.

    ISOM 은 마름모 격자라 가로는 두 칸이 한 걸음이다. 가로를 짝수 걸음으로
    훑어야 빈 줄이 남지 않는다.
    """
    out = []
    for ty in range(cy - half_h, cy + half_h + 1):
        if not (2 <= ty < height - 2):
            continue
        for tx in range(cx - half_w, cx + half_w + 1, 2):
            if not (2 <= tx < width - 2):
                continue
            out.append((tx, ty, terrain))
    return out


def paint_plateau(cli: Cli, cx: int, cy: int, half_w: int, half_h: int,
                  terrain: int, width: int, height: int):
    cli.isom_batch(plateau_strokes(cx, cy, half_w, half_h, terrain, width, height))


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
    ap.add_argument("--no-center", action="store_true",
                    help="가운데 지형을 얹지 않는다")
    ap.add_argument("--features", type=int, default=4,
                    help="대칭으로 얹을 지형 덩이 수 (고지대·다른 바닥 지형)")
    ap.add_argument("--doodads", type=int, default=34,
                    help="놓을 두들 수. 공식 밀리맵 183곳 전수 중앙값이 34다 "
                         "(4분위 0~208). 앞서 135로 알았던 것은 유즈맵이 섞인 값")
    ap.add_argument("--seed", type=int, default=1, help="두들 자리 난수 씨앗")
    ap.add_argument("--plateau", action="store_true",
                    help="본진을 고지대에 올리고 램프를 낸다. 걸어서 통하는 "
                         "램프를 못 찾으면 그 본진은 평지로 되돌린다")
    ap.add_argument("--no-plateau", action="store_true",
                    help="(옛 이름) 기본이 평지라 아무 일도 하지 않는다")
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true",
                    help="이미 있는 맵을 덮어쓴다. 기본은 거절한다 — 뼈대를 "
                         "다시 만들면 그 위에 쓴 트리거·유닛이 모두 사라진다")
    args = ap.parse_args(argv)

    # 뼈대 생성은 맵을 처음부터 다시 만든다. 작성해 둔 내용이 있으면
    # 통째로 날아간다. 실제로 그렇게 트리거를 날린 적이 있다.
    if os.path.exists(args.out) and not args.force:
        print(f"이미 있습니다: {args.out}\n"
              f"  뼈대를 다시 만들면 그 위에 쓴 내용이 모두 사라집니다.\n"
              f"  내용을 고치려면 trigger show/apply 로 트리거만 손보세요.\n"
              f"  정말 처음부터 다시 만들려면 --force 를 주세요.", file=sys.stderr)
        return 2

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
    low_terrain = types.get(low_name, 0)

    starts = start_positions(args.players, symmetry, width, height, args.inset)
    print("스타팅 자리:", starts)

    # 1) 본진 고지대 — 대칭 자리마다 같은 붓질을 되풀이한다.
    #    기본은 평지다. 고지대는 램프가 실제로 통해야 뜻이 있는데, 아직
    #    모든 지형에서 통하는 램프를 찾지 못한다 (references/melee-terrain.md).
    args.no_plateau = not args.plateau
    if not args.no_plateau:
        print("본진 고지대를 칠합니다...")
        strokes = []
        for (sx, sy) in starts:
            strokes += plateau_strokes(sx, sy, scmap.MAIN_PLATEAU_HALF_W,
                                       scmap.MAIN_PLATEAU_HALF_H,
                                       high_terrain, width, height)
        cli.isom_batch(strokes)

    # 3) 스타팅 표시와 본진 자원
    print("본진 자원을 놓습니다...")
    cx_mid, cy_mid = (width - 1) / 2.0, (height - 1) / 2.0
    for i, (sx, sy) in enumerate(starts):
        # 자원은 맵 바깥쪽으로 — 안쪽(램프 쪽)을 비워 둔다.
        out_x = -1 if sx <= cx_mid else 1
        out_y = -1 if sy <= cy_mid else 1
        scmap.place_base(cli, sx, sy, owner=i + 1,
                         minerals=args.main_minerals, gas=args.main_gas,
                         out_x=out_x, out_y=out_y, width=width, height=height)

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
            scmap.place_base(cli, nx, ny, owner=12,
                             minerals=args.natural_minerals,
                             gas=args.natural_gas,
                             out_x=-1 if nx <= cx else 1,
                             out_y=-1 if ny <= cy else 1,
                             width=width, height=height,
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

    # 6) 가운데 지형 — 본진 언덕만 있으면 맵이 아니라 벌판이다.
    if not args.no_center and not args.no_plateau:
        print("가운데 지형을 얹습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        # 한가운데 섬 하나 + 스타팅마다 같은 상대 위치의 능선 하나.
        for ty in range(int(cy) - 6, int(cy) + 7):
            for tx in range(int(cx) - 9, int(cx) + 10, 2):
                if 2 <= tx < width - 2 and 2 <= ty < height - 2:
                    cli.isom(tx, ty, high_terrain)
        for (sx, sy) in starts:
            vx, vy = cx - sx, cy - sy
            length = math.hypot(vx, vy) or 1.0
            # 본진과 가운데 사이 절반 지점에 옆으로 누운 능선을 놓는다.
            mx = int(round(sx + vx / length * (length * 0.5)))
            my = int(round(sy + vy / length * (length * 0.5)))
            px, py = -vy / length, vx / length      # 직각 방향
            for t in range(-7, 8):
                tx = int(round(mx + px * t))
                ty = int(round(my + py * t))
                for d in (-1, 0, 1):
                    if 2 <= tx + d < width - 2 and 2 <= ty < height - 2:
                        cli.isom(tx + d - ((tx + d) % 2), ty, high_terrain)

    # 6b) 지형 무늬 — 고지대 덩이와 다른 바닥 지형을 섞는다.
    #     공식 맵은 타일 그룹을 484개(중앙값) 쓴다. 한 가지 지형으로만
    #     칠하면 서른 개도 안 나온다.
    if args.features > 0:
        print(f"지형 덩이를 얹습니다 ({args.features}곳씩)...")
        rng2 = random.Random(args.seed + 7)
        feature_strokes = []
        alt_names = [n for n in types
                     if not n.lower().startswith("high") and n != low_name]
        cxm, cym = (width - 1) / 2.0, (height - 1) / 2.0
        for k in range(args.features):
            # 자리·크기·모양을 모두 흔든다. 같은 네모를 고른 간격으로 찍으면
            # 한눈에 기계가 만든 티가 난다.
            ang = rng2.uniform(0, 2 * math.pi)
            rad = rng2.uniform(0.10, 0.44) * min(width, height)
            bx = int(cxm + rad * math.cos(ang))
            by = int(cym + rad * math.sin(ang))
            area = rng2.randrange(14, 46)
            blob = scmap.organic_blob(rng2, area,
                                      elongate=rng2.uniform(0.5, 2.2))
            if k % 2 == 0 or not alt_names:
                terrain = high_terrain
            else:
                terrain = types[rng2.choice(alt_names)]
            # 대칭 자리마다 같은 모양을 돌려 놓는다
            spots = scmap.symmetric_points(bx, by, symmetry, args.players,
                                           width, height)
            for turn, (px, py) in enumerate(spots):
                px, py = int(round(px)), int(round(py))
                for (dx, dy) in blob:
                    if symmetry in ("rot90", "rot180"):
                        odx, ody = scmap.rotate_offset(dx, dy, turn)
                    else:
                        odx, ody = dx, dy
                    tx, ty = px + int(odx), py + int(ody)
                    if not (3 <= tx < width - 3 and 3 <= ty < height - 3):
                        continue
                    if in_base_or_exit(tx, ty, starts, cxm, cym):
                        continue
                    feature_strokes.append((tx - (tx % 2), ty, terrain))
        cli.isom_batch(feature_strokes)

    # 7) 지형지물 — 두들. 공식 맵 57개 중앙값이 135개다. 없으면 벌판이다.
    if args.doodads > 0:
        print(f"두들을 놓습니다 (목표 {args.doodads}개)...")
        cat = cli.doodad_catalogue()
        # 길을 막지 않는 작은 장식만 고른다 (4x4 이하, 절벽·다리 제외)
        picks = [d for d in cat
                 if d["w"] <= 4 and d["h"] <= 4
                 and d["kind"] not in ("Cliff", "Bridges", "Wall")]
        if not picks:
            print("  놓을 만한 두들이 없어 건너뜁니다")
        else:
            rng = random.Random(args.seed)
            placed = 0
            tries = 0
            per_sector = max(1, args.doodads // max(1, len(starts)))
            while placed < args.doodads and tries < args.doodads * 6:
                tries += 1
                d = rng.choice(picks)
                bx = rng.randrange(4, width - 8)
                by = rng.randrange(4, height - 8)
                # 대칭 자리마다 같은 두들을 놓는다
                spots = scmap.symmetric_points(bx, by, symmetry, args.players,
                                               width, height)
                ok = True
                for (px, py) in spots:
                    px, py = int(round(px)), int(round(py))
                    if not (2 <= px < width - 6 and 2 <= py < height - 6):
                        ok = False
                        break
                    try:
                        cli.edit("doodad", "place", cli.path, str(d["id"]),
                                 str(px), str(py), "--install", cli.install)
                        placed += 1
                    except CliError:
                        ok = False
                        break
            print(f"  두들 {placed}개")

    # 7b) 램프 — 지형을 다 얹은 **뒤에** 낸다.
    #
    #      먼저 내면 나중에 얹은 지형 덩이가 램프 바깥을 막아 본진이
    #      섬이 된다. 실제로 그렇게 갇힌 맵이 나왔다.
    #
    # 절벽 줄은 **높이**로 찾고, 찍은 뒤에는 **미니타일 길찾기**로 실제로
    # 통하는지 확인한다. 눈으로만 보고 판단하면 막힌 램프를 놓게 된다.
    ramp_at = {}
    if not args.no_plateau:
        print("램프를 놓습니다...")
        cy_mid = (height - 1) / 2.0
        for (sx, sy) in starts:
            downward = sy < cy_mid          # 위쪽 본진은 아래로 내려간다
            y_end = min(height - 6, sy + 16) if downward else max(4, sy - 16)
            edge = scmap.find_elevation_edge(cli, tileset_id, sx, sy, y_end,
                                             vertical=True)
            if edge is None:
                print(f"  ({sx},{sy}) 절벽 줄을 못 찾아 건너뜁니다")
                continue
            # 목표는 **언덕 바깥 열네 칸**. 맵 한가운데까지 요구하면 램프는
            # 멀쩡한데 바깥 지형이 막았다는 이유로 램프가 퇴짜를 맞는다.
            # 바깥이 막힌 것은 뒤의 연결성 복구가 길을 내어 푼다.
            low_x = sx
            low_y = edge + 14 if downward else edge - 14
            low_y = max(2, min(height - 3, low_y))
            cands = (scmap.ramp_candidates(tileset_id, downward) +
                     scmap.ramp_candidates(tileset_id, not downward))
            rw = max((c[2] for c in cands), default=6)
            rx = sx - rw // 2
            rx -= rx % 2                    # 마름모 격자에 맞춰 짝수로
            rx = max(0, min(width - rw, rx))
            got = scmap.place_ramp_checked(
                cli, tileset_id, rx, edge,
                high_point=(sx, sy), low_point=(low_x, low_y),
                downward=downward, candidates=cands)
            if got is None:
                print(f"  ({sx},{sy}) 통하는 램프를 못 찾았습니다")
            else:
                base, ry = got
                ramp_at[(sx, sy)] = (rx, ry, downward)
                print(f"  ({sx},{sy}) → 램프 ({rx},{ry}) 0x{base:04x} [길찾기 통과]")

        # 하나라도 램프를 못 내면 **모든** 본진의 고지대를 걷어낸다.
        # 한 곳만 언덕이면 그 자리가 유리해져 밸런스가 깨진다 — 밀리맵에서
        # 자리 차이는 지형 차이보다 나쁘다.
        if ramp_at and len(ramp_at) < len(starts):
            print(f"  {len(starts) - len(ramp_at)}곳이 램프를 못 내 "
                  f"모든 본진을 평지로 되돌립니다 (대칭이 먼저다)")
            strip = []
            for (sx, sy) in starts:
                strip += plateau_strokes(sx, sy, scmap.MAIN_PLATEAU_HALF_W + 1,
                                     scmap.MAIN_PLATEAU_HALF_H + 1,
                                     low_terrain, width, height)
            cli.isom_batch(strip)
            ramp_at = {}
        elif not ramp_at:
            print("  램프를 하나도 내지 못해 평지로 갑니다")
            strip = []
            for (sx, sy) in starts:
                strip += plateau_strokes(sx, sy, scmap.MAIN_PLATEAU_HALF_W + 1,
                                     scmap.MAIN_PLATEAU_HALF_H + 1,
                                     low_terrain, width, height)
            cli.isom_batch(strip)

    # 8) 연결성 복구 — 갇힌 본진이 있으면 길을 낸다.
    #
    # 램프가 통해도 그 바깥을 지형 덩이가 막으면 본진이 섬이 된다.
    # 램프 하나만 보지 말고 **맵 전체에서** 스타팅끼리 닿는지 봐야 한다.
    print("연결성을 확인합니다...")
    for attempt in range(4):
        grid = scmap.walk_grid(cli, tileset_id, 0, 0, width, height)
        pts = [scmap.nearest_walkable(grid, sx * 4 + 2, sy * 4 + 2, radius=48)
               for (sx, sy) in starts]
        if any(p is None for p in pts):
            print("  스타팅 자리에 걸을 땅이 없습니다")
            break
        bad = [i for i in range(1, len(pts))
               if not scmap.walk_reachable(grid, pts[0], pts[i])]
        if not bad:
            print(f"  스타팅 {len(starts)}곳이 모두 이어집니다")
            break
        print(f"  {[i + 1 for i in bad]} 번이 갇혀 길을 냅니다 ({attempt + 1}번째)")
        # 갇힌 본진에서 가운데로 낮은 땅 띠를 낸다. 대칭을 지키려고
        # 모든 스타팅에 같은 붓질을 되풀이한다.
        carve = []
        cx_m, cy_m = (width - 1) / 2.0, (height - 1) / 2.0
        for (sx, sy) in starts:
            # **램프 출구**에서 시작한다. 본진에서 몇 칸 떨어진 자리를 잡으면
            # 병목(언덕 바로 아래)을 건너뛰어 길이 이어지지 않는다.
            spot = ramp_at.get((sx, sy))
            if spot:
                rx, ry, downward = spot
                ox = rx + 3
                oy = ry + 7 if downward else ry - 2
            else:
                ox, oy = sx, sy
            vx, vy = cx_m - ox, cy_m - oy
            length = math.hypot(vx, vy) or 1.0
            ux, uy = vx / length, vy / length
            for step in range(2, int(length) + 1):
                tx = int(round(ox + ux * step))
                ty = int(round(oy + uy * step))
                for d in (-4, -2, 0, 2, 4):
                    px = tx + d
                    if not (3 <= px < width - 3 and 3 <= ty < height - 3):
                        continue
                    # **램프 위는 절대 칠하지 않는다.** 칠하면 램프가 지워져
                    # 본진이 다시 갇힌다 — 길을 낼수록 더 막히는 꼴이 된다.
                    if any(rx0 - 1 <= px <= rx0 + 6 and ry0 - 1 <= ty <= ry0 + 6
                           for (rx0, ry0, _d) in ramp_at.values()):
                        continue
                    carve.append((px - (px % 2), ty, low_terrain))
        cli.isom_batch(carve)

    # 자원량은 다 놓은 뒤 한 번에 맞춘다.
    scmap.set_all_resources(cli)

    # 플레이어 슬롯을 스타팅 수에 맞춘다. 남는 슬롯을 열어 두면 대기실에서
    # 스타팅 없는 자리를 받는 사람이 생긴다.
    print("플레이어 슬롯을 맞춥니다...")
    scmap.setup_melee_players(cli, args.players)

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
