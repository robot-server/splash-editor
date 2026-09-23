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
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus
import melee_shape
import scmap
from scmap import TILE, Cli, CliError

# 타일셋 이름 → 번호, 그리고 그 타일셋에서 쓸 저지대·고지대 ISOM 지형 이름.
# 이름은 `splash-cli terrain types <맵> --install ...` 이 내는 것과 같다.
TILESETS = {
    "badlands": (0, "Dirt", "High Dirt"),
    "space":    (1, "Platform", "High Platform"),
    "install":  (2, "Substructure", "Substructure"),
    "ashworld": (3, "Magma", "High Dirt"),
    "jungle":   (4, "Jungle", "High Dirt"),
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

    **네모로 그리지 않는다.** 꽉 찬 직사각형을 그리면 외접 사각형 채움이
    0.95 가 나오는데 공식 맵은 0.40~0.75 다. 네모난 언덕은 사람이 그린
    것으로 보이지 않는다 — 실제로 그렇게 만들어 보고 지적받았다.

    바깥 테두리를 각도에 따라 물결지게 깎되, 가운데는 반드시 남긴다.
    자원 아홉 덩이와 가스가 거기 들어가야 하기 때문이다.

    모든 본진이 같은 모양이다. 대칭 맵에서 자리마다 모양이 다르면
    그 자체가 자리 밸런스 문제가 된다.

    ISOM 은 마름모 격자라 가로는 두 칸이 한 걸음이다. 가로를 짝수
    걸음으로 훑어야 빈 줄이 남지 않는다.
    """
    CORE = 0.52          # 이 안쪽은 무조건 남긴다 (자원 자리)
    out = []
    for ty in range(cy - half_h, cy + half_h + 1):
        if not (2 <= ty < height - 2):
            continue
        for tx in range(cx - half_w, cx + half_w + 1, 2):
            if not (2 <= tx < width - 2):
                continue
            nx = (tx - cx) / float(half_w)
            ny = (ty - cy) / float(half_h)
            d = math.hypot(nx, ny)
            if d > CORE:
                a = math.atan2(ny, nx)
                # 각도에 따라 테두리를 들쭉날쭉하게. 위상은 고정 —
                # 본진마다 같은 모양이어야 자리가 공평하다.
                edge = 1.02 - 0.20 * math.sin(3 * a + 0.7) \
                            - 0.11 * math.sin(5 * a + 2.1)
                if d > edge:
                    continue
            out.append((tx, ty, terrain))
    return out


def paint_plateau(cli: Cli, cx: int, cy: int, half_w: int, half_h: int,
                  terrain: int, width: int, height: int):
    cli.isom_batch(plateau_strokes(cx, cy, half_w, half_h, terrain, width, height))


def _foot_ok(grid, tiles, px: int, py: int, w: int, h: int) -> bool:
    """자원 발자국이 전부 걷고 지을 수 있는 칸인지."""
    tx, ty = px // TILE, py // TILE
    for yy in range(ty - h // 2, ty + (h + 1) // 2):
        for xx in range(tx - w // 2, tx + (w + 1) // 2):
            if not (0 <= yy < len(grid) and 0 <= xx < len(grid[0])):
                return False
            p = tiles.get(grid[yy][xx])
            if p is None or not p[1] or not p[2]:
                return False
    return True


def recolor_floor(strokes, width, height, low_terrain, alts, rng,
                  starts, symmetry, players):
    """저지대 마름모를 큰 얼룩으로 다른 바닥 지형에 나눠 준다.

    흙 한 가지로만 칠하면 4x4 창이 그룹 두 개짜리 체커라 맵 대부분이
    균일 창으로 집계된다. 얼룩은 대칭 복사하고, 스타팅 반경 18타일은
    흙으로 둔다. 경계를 본진 안에 두면 그 칸이 짓기 불가가 되어
    미네랄이 빠지고, 자리마다 다른 칸으로 밀려 대칭이 깨진다.
    """
    if not alts:
        return strokes
    blobs = []
    for _ in range(18):
        bx, by = rng.randrange(width), rng.randrange(height)
        alt = rng.choice(alts)
        rad = rng.uniform(9.0, 16.0)
        for px, py in scmap.symmetric_points(bx, by, symmetry, players,
                                             width, height):
            blobs.append((px, py, alt, rad))
    guard = [(sx, sy, 18.0) for (sx, sy) in starts]
    out = []
    for st in strokes:
        x, y, terrain = st[0], st[1], st[2]
        brush = st[3] if len(st) > 3 else 1
        if terrain == low_terrain and not any(
                (x - gx) * (x - gx) + (y - gy) * (y - gy) <= gr * gr
                for gx, gy, gr in guard):
            best = None
            for bx, by, alt, rad in blobs:
                d2 = (x - bx) * (x - bx) + (y - by) * (y - by)
                if d2 <= rad * rad and (best is None or d2 < best[0]):
                    best = (d2, alt)
            if best is not None:
                terrain = best[1]
        out.append((x, y, terrain, brush))
    return out


def resource_anchor(cli: Cli, tileset_id: int, tile_x: int, tile_y: int,
                    minerals: int, gas: int, out_x: int, out_y: int,
                    width: int, height: int,
                    owner_xy: tuple[int, int] | None = None,
                    all_starts: list | None = None,
                    pack: float = 1.0, compact: bool = False) -> tuple[int, int]:
    """자원 아홉 덩이와 가스가 다 들어가는 가장 가까운 칸.

    계산한 자리가 절벽 띠면 나선형으로 한 칸씩 옮겨 본다. 못 찾으면
    원래 자리를 돌려주고, 놓는 쪽에서 빠진 개수를 알린다.
    owner_xy 가 있으면 그 스타팅에 더 가까운 칸만 받는다. 안 그러면
    나선이 경계 너머로 넘어가 옆 본진 확장으로 집계된다.
    """
    tiles = scmap.tileset_tiles(cli, tileset_id)
    grid = cli.tiles(0, 0, width, height)

    def mine(ax, ay):
        if owner_xy is None or not all_starts:
            return True
        d = math.dist((ax, ay), owner_xy)
        return all(math.dist((ax, ay), st) > d + 0.5
                   for st in all_starts if st != owner_xy)

    def ok(ax, ay):
        if not (8 <= ax < width - 8 and 8 <= ay < height - 8):
            return False
        if not mine(ax, ay):
            return False
        mins, gases = scmap.layout_resources(
            ax, ay, minerals, gas, out_x, out_y, pack, compact)
        flat = [c for group in mins + gases for c in group]
        # 자원끼리 높이가 같고, 그 높이에 4×3 본진 건물이 채집 거리에 서야 한다.
        return scmap.townhall_pad(grid, tiles, ax, ay, flat, width, height) is not None

    if ok(tile_x, tile_y):
        return tile_x, tile_y
    for r in range(1, 12):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if max(abs(dx), abs(dy)) != r:
                    continue
                if ok(tile_x + dx, tile_y + dy):
                    return tile_x + dx, tile_y + dy
    return tile_x, tile_y


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
    ap.add_argument("--doodads", type=int, default=None,
                    help="놓을 두뎃 수. 안 주면 **타일셋별 실측**에서 뽑는다 "
                         "(Badlands 중앙 208 · Jungle 145 · Space 0). "
                         "전체 중앙값 34를 박아 두었던 것이 잘못이었다 — "
                         "어느 타일셋에도 맞지 않는 수다")
    ap.add_argument("--seed", type=int, default=1, help="두뎃 자리 난수 씨앗")
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
    # 본진 미네랄은 스타팅에서 7타일 바깥이다. inset 10 이면 그 줄이
    # 맵 테두리와 절벽에 걸친다.
    if args.plateau and not args.no_plateau and args.inset < 28:
        args.inset = 28

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

    # 1) 고도. --plateau 일 때만. 고른 각도의 고리(design)는 지표는
    #    맞는데 그림이 눈송이라 쓰지 않는다. 본진·센터 능선·변 확장만
    #    있는 design_lanes 를 칠한다.
    args.no_plateau = not args.plateau
    shape = None
    if not args.no_plateau:
        print("본진·능선·확장 고도를 칠합니다...")
        shape = melee_shape.design_lanes(
            width, height, args.players, symmetry, starts,
            random.Random(args.seed))
        print(melee_shape.report(shape))
        alts = [types[n] for n in ("Mud", "Grass", "Rocky Ground") if n in types]
        strokes = recolor_floor(
            shape.strokes(low_terrain, high_terrain),
            width, height, low_terrain, alts, random.Random(args.seed + 3),
            starts, symmetry, args.players)
        cli.isom_batch(strokes)

    # 3) 스타팅 표시와 본진 자원
    print("본진 자원을 놓습니다...")
    cx_mid, cy_mid = (width - 1) / 2.0, (height - 1) / 2.0
    for i, (sx, sy) in enumerate(starts):
        # 자원은 맵 바깥쪽으로 — 안쪽(램프 쪽)을 비워 둔다.
        out_x = -1 if sx <= cx_mid else 1
        out_y = -1 if sy <= cy_mid else 1
        sx, sy = resource_anchor(cli, tileset_id, sx, sy,
                                 args.main_minerals, args.main_gas,
                                 out_x, out_y, width, height)
        _, _skip = scmap.place_base(cli, sx, sy, owner=i + 1,
                         minerals=args.main_minerals, gas=args.main_gas,
                         out_x=out_x, out_y=out_y, width=width, height=height,
                         tileset_id=tileset_id)
        if _skip:
            print(f"  !! 본진 {i+1}: 지을 수 없는 자리라 자원 {len(_skip)}개를 "
                  f"못 놓았습니다 {[(t, x, y) for t, x, y in _skip[:3]]}")

    # 4) 앞마당 — 본진에서 가운데 쪽으로 한 걸음.
    nat_placed = []
    if args.natural_minerals > 0:
        print("앞마당을 놓습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        # 정면(맵 중심)으로만 두면 3인 앞마당이 가운데에서 한 덩이가 된다.
        # 모든 스타팅에 같은 부호 각도를 더하면 회전 대칭은 유지된다.
        # 자원이 다 들어가는 자리를 고르고, 절벽이라 빠진 개수로 나머지
        # 앞마당을 깎지 않는다. 한 자리의 짧은 거리로 맞추지도 않는다.
        def natural_sites(ang: float, extra: int):
            sites = []
            for (sx, sy) in starts:
                base = math.atan2(cy - sy, cx - sx) + ang
                dist = args.natural_distance + extra
                tx = int(round(sx + dist * math.cos(base)))
                ty = int(round(sy + dist * math.sin(base)))
                if not (8 <= tx < width - 9 and 8 <= ty < height - 9):
                    return None
                ox = 1 if tx >= sx else -1
                oy = 1 if ty >= sy else -1
                ax, ay = resource_anchor(cli, tileset_id, tx, ty,
                                         args.natural_minerals, args.natural_gas,
                                         ox, oy, width, height)
                if math.hypot(ax - tx, ay - ty) > 8:
                    return None
                d_anchor = math.dist((sx, sy), (ax, ay))
                if not (args.natural_distance - 8 <= d_anchor <= args.natural_distance + 10):
                    return None
                sites.append((ax, ay, ox, oy))
            return sites

        sites = None
        angs = ((0.65, -0.65, 0.95, -0.95, 0.4, -0.4, 1.2, -1.2)
                if args.players == 3 else
                (0.35, -0.35, 0.55, -0.55, 0.8, -0.8))
        for ang in angs:
            for extra in (6, 10, 0, 14, -6, 18):
                sites = natural_sites(ang, extra)
                if sites:
                    break
            if sites:
                break
        if sites is None:
            sites = []
            for (sx, sy) in starts:
                base = math.atan2(cy - sy, cx - sx) + 0.65
                dist = float(args.natural_distance)
                tx = int(round(sx + dist * math.cos(base)))
                ty = int(round(sy + dist * math.sin(base)))
                tx = max(10, min(width - 11, tx))
                ty = max(10, min(height - 11, ty))
                sites.append((tx, ty, 1 if tx >= sx else -1, 1 if ty >= sy else -1))

        for i, (nx, ny, ox, oy) in enumerate(sites):
            _placed, _skip = scmap.place_base(cli, nx, ny, owner=12,
                             minerals=args.natural_minerals,
                             gas=args.natural_gas,
                             out_x=ox, out_y=oy,
                             width=width, height=height,
                             start_location=False, tileset_id=tileset_id)
            nat_placed.append((nx, ny, len(_placed), _skip))
            if _skip:
                print(f"  !! 앞마당 {i+1}: 자원 {len(_skip)}개를 못 놓았습니다")

        # **하나라도 덜 놓였으면 전부 그만큼으로 맞춘다.**
        #
        # 램프와 같은 원칙이다 — 밀리맵에서 자리 차이는 지형 차이보다
        # 나쁘다. [8,8,8,5] 처럼 한 자리만 적으면 그 자리를 받은 사람이
        # 손해를 본다. 그럴 바엔 넷 다 5 로 맞춘다.
        #
        # **미네랄과 가스를 따로 센다.** 합쳐 세었더니 가스를 못 놓은
        # 자리가 미네랄을 하나 더 가져 [4,4,4,5] · [1,1,1,0] 이 됐다.
        if nat_placed:
            def near(u, nx, ny):
                return (abs(u["x"] // 32 - nx) <= 8
                        and abs(u["y"] // 32 - ny) <= 8)

            for kind, label in (("Mineral Field", "미네랄"),
                                ("Vespene Geyser", "가스")):
                counts = []
                for (nx, ny, _c, _s) in nat_placed:
                    counts.append(sum(1 for u in cli.units()
                                      if kind in u["type_name"]
                                      and near(u, nx, ny)))
                if len(set(counts)) <= 1:
                    continue
                lo = min(counts)
                print(f"  앞마당 {label}이 자리마다 달라({counts}) "
                      f"**모두 {lo}개로 맞춥니다** — 자리 차이가 지형보다 나쁩니다")
                for (nx, ny, _c, _s), have in zip(nat_placed, counts):
                    drop = [u["index"] for u in cli.units()
                            if kind in u["type_name"] and near(u, nx, ny)]
                    for idx in sorted(drop, reverse=True)[:have - lo]:
                        cli.edit("unit", "remove", cli.path, str(idx))

            # 무게중심을 목표 거리로 민다. 짧은 자리의 거리로 나머지를
            # 당기지 않고, 유닛은 가장 가까운 앞마당 앵커에만 속한다.
            pools = [[] for _ in nat_placed]
            for u in cli.units():
                if "Mineral Field" not in u["type_name"] and "Vespene Geyser" not in u["type_name"]:
                    continue
                ux, uy = u["x"] / 32, u["y"] / 32
                best_i, best_d = None, 12.0
                for i, (nx, ny, _c, _s) in enumerate(nat_placed):
                    d = math.hypot(ux - nx, uy - ny)
                    if d < best_d:
                        best_i, best_d = i, d
                if best_i is not None:
                    pools[best_i].append(u)
            for (sx, sy), pool in zip(starts, pools):
                if not pool:
                    continue
                gx = sum(u["x"] / 32 for u in pool) / len(pool)
                gy = sum(u["y"] / 32 for u in pool) / len(pool)
                d = math.dist((sx, sy), (gx, gy))
                shift = args.natural_distance - d
                if abs(shift) < 1.5:
                    continue
                vx, vy = gx - sx, gy - sy
                L = math.hypot(vx, vy) or 1.0
                dx, dy = round(vx / L * shift), round(vy / L * shift)
                if dx == 0 and dy == 0:
                    continue
                for u in pool:
                    cli.edit("unit", "move", cli.path, str(u["index"]),
                             str(u["x"] // 32 + dx), str(u["y"] // 32 + dy),
                             "--tiles")

    # 5) 바깥 멀티 — 스타팅마다 같은 상대 위치에 놓아 대칭을 지킨다.
    #    공식 맵은 스타팅당 자원 덩이가 중앙값 4곳이다 (본진·앞마당 포함).
    if args.expansions > 0:
        print(f"바깥 멀티 {args.expansions}곳씩 놓습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        # 스타팅에서 가운데를 보는 방향을 기준으로, 좌우로 벌려 놓는다.
        spread = [(-50, 1.55), (50, 1.55), (0, 2.1), (-70, 2.4), (70, 2.4)]
        # 한 슬롯의 각·거리는 스타팅 전부에 같이 적용한다. 일부만 놓으면
        # 4인 두 번째 멀티처럼 대칭이 깨지고, 못 놓는 자리는 빠진다.
        # 각 본진에서는 앞마당보다 멀어야 그 앞마당으로 집계되지 않는다.
        exp_pts: list[tuple[int, int]] = []
        far = args.natural_distance + 4

        snap_why = {"edge": 0, "near": 0, "own": 0, "drift": 0,
                    "snapown": 0, "nat": 0, "exp": 0, "ok": 0}

        def snap_expansion(ex, ey, sx, sy):
            if not (8 <= ex < width - 9 and 8 <= ey < height - 9):
                snap_why["edge"] += 1
                return None
            own = math.dist((ex, ey), (sx, sy))
            if own < far:
                snap_why["near"] += 1
                return None
            if any(math.dist((ex, ey), st) <= own
                   for st in starts if st != (sx, sy)):
                snap_why["own"] += 1
                return None
            # 자원 줄은 짧게(pack) 본진 반대편에 둔다. 본진 쪽이면 앞마당
            # 거리 안으로 들어오고, 긴 줄이면 옆 본진 칸으로 넘어간다.
            eox = 1 if ex >= sx else -1
            eoy = 1 if ey >= sy else -1
            ax, ay = resource_anchor(cli, tileset_id, ex, ey,
                                     args.expansion_minerals,
                                     args.expansion_gas,
                                     eox, eoy, width, height,
                                     owner_xy=(sx, sy), all_starts=starts,
                                     pack=0.875)
            # 맞추는 거리가 길면 옆 본진 쪽으로 넘어가 개수가 갈린다.
            if math.hypot(ax - ex, ay - ey) > 12:
                snap_why["drift"] += 1
                return None
            own_s = math.dist((ax, ay), (sx, sy))
            if any(math.dist((ax, ay), st) <= own_s
                   for st in starts if st != (sx, sy)):
                snap_why["snapown"] += 1
                return None
            if any(math.hypot(ax - nx, ay - ny) < 12
                   for (nx, ny, _c, _s) in nat_placed):
                snap_why["nat"] += 1
                return None
            if any(math.hypot(ax - px, ay - py) < 16 for (px, py) in exp_pts):
                snap_why["exp"] += 1
                return None
            snap_why["ok"] += 1
            return ax, ay, eox, eoy

        for k in range(args.expansions):
            deg0, scale = spread[k % len(spread)]
            base_dist = max(args.natural_distance * scale,
                            args.natural_distance + 14)
            chosen = None
            sx0, sy0 = starts[0]
            for key in snap_why:
                snap_why[key] = 0

            fail_why = {"place": 0, "own": 0, "close": 0}

            def commit_ring(spots):
                """자원을 놓고, 칸이 겹치거나 옆 본진으로 가면 되돌려 False."""
                n_before = len(cli.units())

                def rollback():
                    for idx in sorted(
                            (u["index"] for u in cli.units()[n_before:]),
                            reverse=True):
                        cli.edit("unit", "remove", cli.path, str(idx))

                for (sx, sy), (ax, ay, eox, eoy) in spots:
                    _placed, _skip = scmap.place_base(
                        cli, ax, ay, owner=12,
                        minerals=args.expansion_minerals,
                        gas=args.expansion_gas,
                        out_x=eox, out_y=eoy,
                        facing=quadrant_facing(sx, sy, width, height),
                        width=width, height=height,
                        start_location=False, tileset_id=tileset_id,
                        pack=0.875)
                    if len(_placed) < args.expansion_minerals + args.expansion_gas - 2:
                        fail_why["place"] += 1
                        rollback()
                        return False
                fresh = cli.units()[n_before:]
                centers = []
                for (sx, sy), (ax, ay, _eox, _eoy) in spots:
                    pool = [u for u in fresh
                            if ("Mineral Field" in u["type_name"]
                                or "Vespene Geyser" in u["type_name"])
                            and math.hypot(u["x"] / 32 - ax, u["y"] / 32 - ay) < 12]
                    if len(pool) < args.expansion_minerals + args.expansion_gas - 2:
                        rollback()
                        return False
                    here = [(u["x"] // 32, u["y"] // 32) for u in pool]
                    if len(here) != len(set(here)):
                        rollback()
                        return False
                    gx = sum(p[0] for p in here) / len(here)
                    gy = sum(p[1] for p in here) / len(here)

                    def owned_at(px, py, sx=sx, sy=sy):
                        d0 = math.dist((px, py), (sx, sy))
                        return d0 >= far and all(
                            math.dist((px, py), st) > d0
                            for st in starts if st != (sx, sy))

                    if not owned_at(gx, gy):
                        vx, vy = sx - gx, sy - gy
                        L = math.hypot(vx, vy) or 1.0
                        moved = False
                        for step in range(1, 7):
                            ngx = gx + vx / L * step
                            ngy = gy + vy / L * step
                            dx, dy = round(ngx - gx), round(ngy - gy)
                            if dx == 0 and dy == 0 or not owned_at(gx + dx, gy + dy):
                                continue
                            pool_ids = {u["index"] for u in pool}
                            taken = set()
                            for o in cli.units():
                                if o["index"] in pool_ids:
                                    continue
                                name = o.get("type_name") or ""
                                if ("Mineral Field" not in name
                                        and "Vespene Geyser" not in name):
                                    continue
                                taken.add((o["x"] // 32, o["y"] // 32))
                            dest = [(u["x"] // 32 + dx, u["y"] // 32 + dy) for u in pool]
                            if len(dest) != len(set(dest)) or any(p in taken for p in dest):
                                continue
                            for u in pool:
                                cli.edit("unit", "move", cli.path, str(u["index"]),
                                         str(max(2, min(width - 3, u["x"] // 32 + dx))),
                                         str(max(2, min(height - 3, u["y"] // 32 + dy))),
                                         "--tiles")
                                u["x"] += dx * 32
                                u["y"] += dy * 32
                            here = dest
                            moved = True
                            break
                        if not moved:
                            fail_why["own"] += 1
                            rollback()
                            return False
                    centers.append((sum(p[0] for p in here) / len(here),
                                    sum(p[1] for p in here) / len(here)))
                if any(math.hypot(centers[a][0] - centers[b][0],
                                  centers[a][1] - centers[b][1]) < 10
                       for a in range(len(centers)) for b in range(a)):
                    fail_why["close"] += 1
                    rollback()
                    return False
                # 이미 있는 미네랄과 8타일 안이면 덩이가 하나로 합쳐진다.
                older = [(u["x"] // 32, u["y"] // 32)
                         for u in cli.units()[:n_before]
                         if "Mineral Field" in (u.get("type_name") or "")
                         or "Vespene Geyser" in (u.get("type_name") or "")]
                newer = [(u["x"] // 32, u["y"] // 32) for u in fresh
                         if "Mineral Field" in (u.get("type_name") or "")
                         or "Vespene Geyser" in (u.get("type_name") or "")]
                # 검증 덩이는 가로·세로 8칸 이내면 한 덩이로 합친다.
                if any(abs(a[0] - b[0]) <= 8 and abs(a[1] - b[1]) <= 8
                       for a in newer for b in older):
                    fail_why["close"] += 1
                    rollback()
                    return False
                return True

            # 첫 스타팅에서 자리를 고르고, 같은 회전으로 나머지에 옮긴다.
            # 각도만 각자 다시 계산하면 반올림이 어긋나 4인 한쪽만 떨어진다.
            # 계산상 맞아도 자원이 겹치거나 절벽에 걸리면 다음 각도를 본다.
            for extra in (45, 50, -25, -50, -125, 55, -15, -40, 15):
                if chosen:
                    break
                for dist in (36, 44, 48, 40, 56, 64, 72):
                    if dist < far:
                        continue
                    ang = (math.atan2(cy - sy0, cx - sx0)
                           + math.radians(deg0 + extra))
                    seed_x = sx0 + dist * math.cos(ang)
                    seed_y = sy0 + dist * math.sin(ang)
                    images = scmap.symmetric_points(
                        seed_x, seed_y, symmetry, len(starts), width, height)
                    spots = []
                    for (sx, sy), (ix, iy) in zip(starts, images):
                        hit = snap_expansion(int(round(ix)), int(round(iy)),
                                             sx, sy)
                        if hit is None:
                            spots = []
                            break
                        spots.append(((sx, sy), hit))
                    if len(spots) != len(starts):
                        continue
                    pts = [(ax, ay) for (_s, (ax, ay, _ox, _oy)) in spots]
                    if any(math.hypot(pts[a][0] - pts[b][0],
                                      pts[a][1] - pts[b][1]) < 18
                           for a in range(len(pts)) for b in range(a)):
                        snap_why["apart"] = snap_why.get("apart", 0) + 1
                        continue
                    if not commit_ring(spots):
                        continue
                    chosen = spots
                    break
            if not chosen:
                print(f"  !! 멀티 {k+1}: 겹치지 않는 자리가 없습니다 {fail_why} snap {snap_why}")
                continue
            exp_pts.extend((ax, ay) for (_s, (ax, ay, _ox, _oy)) in chosen)
            print(f"  멀티 {k+1} 앵커", [(ax, ay) for (ax, ay) in exp_pts[-len(starts):]])

    # 물길은 자원을 놓은 뒤에 빈 땅에만 칠한다. 가장자리에 먼저 두면
    # 그 칸을 못 짓게 만들어 변 확장 자리가 통째로 거절된다.
    if "Water" in types:
        water = types["Water"]
        ponds = scmap.symmetric_points(
            (width - 1) / 2.0, 14,
            symmetry, max(args.players, 2), width, height)
        taken = [(u["x"] / 32, u["y"] / 32) for u in cli.units()
                 if "Mineral Field" in u["type_name"]
                 or "Vespene Geyser" in u["type_name"]
                 or u["type"] == scmap.START_LOCATION]
        wst = []
        for (px, py) in ponds:
            for dy in range(-2, 3):
                for dx in range(-6, 7):
                    x = int(round(px)) + dx
                    y = int(round(py)) + dy
                    if not (4 <= x < width - 4 and 4 <= y < height - 4):
                        continue
                    if any((x - tx) * (x - tx) + (y - ty) * (y - ty) < 12 * 12
                           for (tx, ty) in taken):
                        continue
                    wst.append((x - (x % 2), y, water, 1))
        if wst:
            print(f"물을 {len(ponds)}곳에 놓습니다.")
            cli.isom_batch(wst)

    # 6) 가운데 지형 — 본진 언덕만 있으면 맵이 아니라 벌판이다.
    if shape is None and not args.no_center and not args.no_plateau:
        print("가운데 지형을 얹습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        # 한가운데 섬 하나 + 스타팅마다 같은 상대 위치의 능선 하나.
        # **여기도 네모로 그리지 않는다.** 본진 언덕만 고치고 이걸 두면
        # 가운데에 네모가 남아 형상 검사에 그대로 걸린다 (실제로 걸렸다).
        cli.isom_batch(plateau_strokes(int(cx), int(cy), 9, 6,
                                       high_terrain, width, height))
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
    if shape is None and args.features > 0:
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

    # 7) 지형지물 — 두뎃. 공식 맵 57개 중앙값이 135개다. 없으면 벌판이다.
    #
    # **세 가지를 지킨다. 셋 다 지적받고 고친 것이다.**
    #
    # (가) **자원 둘레에는 놓지 않는다.** 본진 미네랄 옆에 바위가 끼면
    #      일꾼 동선이 막히고 눈에 거슬린다. 자원·스타팅 둘레 다섯 칸을
    #      비운다.
    # (나) **그 자리 지형에 맞는 갈래만 쓴다.** 두뎃 갈래 이름은 지형
    #      종류 이름과 같다 (High Dirt·Grass·Rocky Ground…). 흙 두뎃을
    #      고지대에 올리면 안 어울린다.
    # (다) **걷기를 막는 것은 미리 걸러 낸다.** `data/doodad-walk.json`
    #      에 타일셋마다 재 두었다.
    if args.doodads is None:
        # **전체 중앙값을 쓰지 않는다.** 두뎃 수는 타일셋에 따라 Space 0
        # 에서 Badlands 208 까지 벌어진다 (밀리 183곳 전수). 전체
        # 중앙값 하나로 박아 두었더니 Badlands 맵이 34개만 받아,
        # "4x4 창의 83%가 지형 한두 가지뿐" 인 벌판이 됐다.
        args.doodads = corpus.doodad_count(corpus.load(), args.tileset,
                                           "melee", random.Random(args.seed))
        print(f"  두뎃 수를 {args.tileset} 밀리 실측에서 뽑았습니다: "
              f"{args.doodads}개")
    if args.doodads > 0:
        print(f"두뎃을 놓습니다 (목표 {args.doodads}개)...")
        safe = scmap.doodad_walk_table(tileset_id)
        gname = scmap.group_terrain_name(tileset_id)
        cat = cli.doodad_catalogue()
        picks = [d for d in cat
                 if d["w"] <= 4 and d["h"] <= 4
                 and not any(k in d["kind"]
                             for k in ("Cliff", "Bridges", "Wall", "Water"))
                 and (not safe or safe.get(d["id"], {}).get("walk"))]
        by_kind: dict[str, list] = {}
        for d in picks:
            by_kind.setdefault(d["kind"], []).append(d)

        # 비워 둘 자리 — 자원과 스타팅 둘레
        keep_clear = []
        for u in cli.units():
            t = u["type"]
            if t in scmap.MINERALS or t == scmap.VESPENE_GEYSER \
                    or t == scmap.START_LOCATION:
                keep_clear.append((u["x"] // 32 - 5, u["y"] // 32 - 5, 11, 11))

        def blocked(px, py, dw, dh):
            for (cx0, cy0, cw, ch) in keep_clear:
                if not (px + dw <= cx0 or cx0 + cw <= px
                        or py + dh <= cy0 or cy0 + ch <= py):
                    return True
            return False

        if not by_kind:
            print("  놓을 만한 두뎃이 없어 건너뜁니다")
        else:
            tile_rows = cli.tiles(0, 0, width, height)
            rng = random.Random(args.seed)
            placed = skipped_res = skipped_kind = 0
            tries = 0
            occupied: set[tuple[int, int]] = set()
            while placed < args.doodads and tries < args.doodads * 12:
                tries += 1
                bx = rng.randrange(4, width - 8)
                by = rng.randrange(4, height - 8)
                spots = scmap.symmetric_points(bx, by, symmetry, args.players,
                                               width, height)
                spots = [(int(round(px)), int(round(py))) for px, py in spots]
                if any(not (2 <= px < width - 6 and 2 <= py < height - 6)
                       for px, py in spots):
                    continue
                # 대칭 좌표는 그대로 두되, 칸마다 그 타일 그룹 이름의
                # 두뎃을 고른다. 자리들의 지형 이름이 다르다고 세트 전체를
                # 버리지 않는다.
                for (px, py) in spots:
                    # 갈래 이름이 비어 있으면 그 칸과 짝을 못 짓는다.
                    # 절벽·물 갈래는 램프가 따로 놓으므로 여기서 쓰지 않는다.
                    kind = gname.get(tile_rows[py][px] >> 4)
                    if not kind or any(k in kind for k in
                                       ("Cliff", "Bridges", "Wall", "Water")):
                        skipped_kind += 1
                        continue
                    pool = by_kind.get(kind)
                    if not pool:
                        skipped_kind += 1
                        continue
                    d = rng.choice(pool)
                    if d["kind"] != kind:
                        skipped_kind += 1
                        continue
                    if blocked(px, py, d["w"], d["h"]):
                        skipped_res += 1
                        continue
                    foot = [(px + dx, py + dy)
                            for dy in range(d["h"]) for dx in range(d["w"])]
                    if any(cell in occupied for cell in foot):
                        skipped_res += 1
                        continue
                    try:
                        # 두뎃은 **가운데 기준**으로 놓인다 — 왼위로 주면
                        # 제 크기의 절반만큼 밀린다
                        scmap.place_doodad(cli, d["id"], px, py,
                                           d["w"], d["h"])
                        occupied.update(foot)
                        placed += 1
                    except CliError:
                        break
            print(f"  두뎃 {placed}개 (자원 둘레라 건너뜀 {skipped_res}, "
                  f"지형이 안 맞아 건너뜀 {skipped_kind})")

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
        cx_mid, cy_mid = (width - 1) / 2.0, (height - 1) / 2.0
        for (sx, sy) in starts:
            # **좌우를 먼저 본다.** 공식 맵에서 램프가 붙은 본진 57곳 중
            # 왼쪽 26·오른쪽 23 이고 위아래는 8곳뿐이다. 위아래만 시도하면
            # 대개 자리를 못 찾는다.
            order = []
            order.append("right" if sx < cx_mid else "left")
            order.append("down" if sy < cy_mid else "up")
            order += [d for d in ("left", "right", "up", "down")
                      if d not in order]

            placed = None
            for direction in order:
                horizontal = direction in ("left", "right")
                step = 1 if direction in ("right", "down") else -1
                span = scmap.MAIN_RAMP_DISTANCE + 8
                if horizontal:
                    end = max(2, min(width - 8, sx + step * span))
                    edge = scmap.find_elevation_edge(cli, tileset_id, sy, sx,
                                                     end, vertical=False)
                else:
                    end = max(2, min(height - 8, sy + step * span))
                    edge = scmap.find_elevation_edge(cli, tileset_id, sx, sy,
                                                     end, vertical=True)
                if edge is None:
                    continue
                # 목표는 언덕 바깥 열네 칸. 맵 한가운데까지 요구하면 램프는
                # 멀쩡한데 먼 지형이 막았다는 이유로 퇴짜를 맞는다.
                if horizontal:
                    low = (max(2, min(width - 3, edge + step * 14)), sy)
                    fixed = sy - 3
                    fixed = max(0, min(height - 6, fixed))
                else:
                    low = (sx, max(2, min(height - 3, edge + step * 14)))
                    fixed = sx - 3
                    fixed -= fixed % 2          # 마름모 격자에 맞춘다
                    fixed = max(0, min(width - 6, fixed))
                cands = scmap.ramp_candidates(tileset_id, direction)
                got = scmap.place_ramp_checked(
                    cli, tileset_id, edge, fixed,
                    high_point=(sx, sy), low_point=low, direction=direction,
                    candidates=cands)
                if got:
                    did, rx, ry = got
                    wh = next(((e["w"], e["h"]) for e in cands if e["id"] == did), (6, 4))
                    ramp_at[(sx, sy)] = (rx, ry, direction, wh[0], wh[1])
                    print(f"  ({sx},{sy}) → {direction} 램프 ({rx},{ry}) "
                          f"두뎃 {did} [길찾기 통과]")
                    placed = got
                    break
            if placed is None:
                print(f"  ({sx},{sy}) 네 방향 모두 통하는 램프를 못 찾았습니다")

        if len(ramp_at) < len(starts):
            print(f"  램프 {len(ramp_at)}/{len(starts)}곳. "
                  f"못 낸 본진 언덕은 그대로 둔다.")

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
        # 자원과 그 앞 4×3 기지는 건드리지 않는다. 길을 내다 높이가
        # 갈라지면 미네랄은 Dirt, 가스는 High Dirt 가 된다.
        keep = set()
        for u in cli.units():
            if ("Mineral" not in u["type_name"]
                    and "Vespene" not in u["type_name"]
                    and u["type"] != scmap.START_LOCATION):
                continue
            ux, uy = u["x"] // 32, u["y"] // 32
            for dy in range(-3, 4):
                for dx in range(-3, 4):
                    keep.add((ux + dx, uy + dy))
        carve = []
        cx_m, cy_m = (width - 1) / 2.0, (height - 1) / 2.0
        for (sx, sy) in starts:
            # **램프 출구**에서 시작한다. 본진에서 몇 칸 떨어진 자리를 잡으면
            # 병목(언덕 바로 아래)을 건너뛰어 길이 이어지지 않는다.
            spot = ramp_at.get((sx, sy))
            if spot:
                rx, ry, direction = spot[0], spot[1], spot[2]
                ox, oy = rx + 3, ry + 3
                if direction == "down":    oy = ry + 7
                elif direction == "up":    oy = ry - 2
                elif direction == "right": ox = rx + 7
                else:                      ox = rx - 2
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
                    if (px, ty) in keep:
                        continue
                    # **램프 위는 절대 칠하지 않는다.** 칠하면 램프가 지워져
                    # 본진이 다시 갇힌다 — 길을 낼수록 더 막히는 꼴이 된다.
                    if any(rx0 - 1 <= px <= rx0 + 6 and ry0 - 1 <= ty <= ry0 + 6
                           for (rx0, ry0, *_rest) in ramp_at.values()):
                        continue
                    carve.append((px - (px % 2), ty, low_terrain))
        cli.isom_batch(carve)

    # 길을 내며 덮인 장식은 그림이 칸과 어긋난다. 램프는 남긴다.
    if args.doodads > 0:
        proc = subprocess.run(
            [cli.cli, "doodad", "check", cli.path, "--install", cli.install],
            capture_output=True, text=True)
        broken = [int(m.group(1)) for line in proc.stdout.splitlines()
                  if (m := re.match(r"^\s*(\d+)\s+타일", line))]
        if broken:
            ramp_ids = {e["id"] for direction in ("left", "right", "up", "down")
                        for e in scmap.ramp_candidates(tileset_id, direction)}
            by_index = {d["index"]: d["id"] for d in cli.doodads()}
            drop = [i for i in broken if by_index.get(i) not in ramp_ids]
            for idx in sorted(drop, reverse=True):
                cli.edit("doodad", "remove", cli.path, str(idx),
                         "--install", cli.install)
            if drop:
                print(f"  칸과 어긋난 두뎃 {len(drop)}개를 뺐습니다")

    # 램프·길이 자원 칸의 짓기 비트를 지우면 기본 에디터에서 못 놓는 칸이 된다.
    # 그 칸만 같은 높이의 짓기 가능 타일로 되돌린다. 맵 전체를 평지로 만들지 않는다.
    tiletbl = scmap.tileset_tiles(cli, tileset_id)
    fresh = cli.tiles(0, 0, width, height)

    def sample_tile(group: int, elev: int) -> int | None:
        for tid, prop in tiletbl.items():
            if tid >> 4 == group and prop[0] == elev and prop[1] and prop[2]:
                return tid
        for tid, prop in tiletbl.items():
            if tid >> 4 == group and prop[1] and prop[2]:
                return tid
        return None

    low_tile = sample_tile(low_terrain, 0)
    high_tile = sample_tile(high_terrain, 1) or sample_tile(high_terrain, 0)
    repaired = 0
    for u in cli.units():
        if u["type"] not in scmap.MINERALS and u["type"] != scmap.VESPENE_GEYSER:
            continue
        fw, fh = (4, 2) if u["type"] == scmap.VESPENE_GEYSER else (2, 1)
        for (xx, yy) in scmap._foot_cells(u["x"], u["y"], fw, fh):
            prop = scmap._cell_prop(fresh, tiletbl, xx, yy)
            if prop is not None and prop[1] and prop[2]:
                continue
            elev = prop[0] if prop is not None else 0
            tid = high_tile if elev >= 1 else low_tile
            if tid is None:
                continue
            cli.edit("terrain", "set", cli.path, str(xx), str(yy), str(tid))
            repaired += 1
    if repaired:
        print(f"  짓기 불가가 된 자원 칸 {repaired}개를 같은 높이로 되돌렸습니다")

    # 램프 양옆 절벽에는 코퍼스의 걷는 절벽 두뎃을 칸마다 붙인다.
    # 램프 두뎃과 절벽 선이 떨어져 보이지 않게 하는 자리다.
    walk_tab = scmap.doodad_walk_table(tileset_id)
    cliff_ids = [i for i, meta in walk_tab.items()
                 if meta.get("kind") == "Cliff" and meta.get("walk")
                 and meta.get("w", 9) <= 4 and meta.get("h", 9) <= 4]
    if cliff_ids and ramp_at:
        cid = cliff_ids[0]
        cw, ch = walk_tab[cid]["w"], walk_tab[cid]["h"]
        for (rx, ry, direction, rw, rh) in ramp_at.values():
            # 램프 칸 바로 옆에서 절벽 선을 이어 입구에 붙인다.
            flanks = []
            if direction in ("left", "right"):
                for s in range(0, 8, 2):
                    flanks.append((rx, ry - 2 - s))
                    flanks.append((rx, ry + rh + s))
            else:
                for s in range(0, 8, 2):
                    flanks.append((rx - 2 - s, ry))
                    flanks.append((rx + rw + s, ry))
            for (px, py) in flanks:
                if not (2 <= px < width - 6 and 2 <= py < height - 6):
                    continue
                try:
                    scmap.place_doodad(cli, cid, px, py, cw, ch)
                except CliError:
                    pass

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
