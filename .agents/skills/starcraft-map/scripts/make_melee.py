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
import json
import math
import os
import random
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import melee_shape
import scmap
from scmap import TILE, Cli, CliError

# 타일셋 이름 → 번호, 그리고 그 타일셋에서 쓸 저지대·고지대 ISOM 지형 이름.
# 이름은 `splash-cli terrain types <맵> --install ...` 이 내는 것과 같다.
# Installation은 밀리 타일셋으로 제공하지 않는다: 설치본에서 일곱 ISOM
# 지형을 각각 생성해 MTXM walk/build 속성을 읽었을 때 buildable 타일이 없었다.
# 본진과 자원 포켓을 놓을 수 없어 시작 가능한 밀리맵이 되지 않는다.
TILESETS = {
    "badlands": (0, "Dirt", "Structure"),
    "space":    (1, "Platform", "Elevated Catwalk"),
    # Magma is a low ISOM class but does not provide ordinary resource/depot
    # placement in the generated start pockets. Use the buildable low dirt.
    "ashworld": (3, "Dirt", "High Dirt"),
    "jungle":   (4, "Jungle", "High Jungle"),
    # Tar and Ice are thematic ground classes, but the start-resource solver
    # requires a buildable floor across the full depot footprint.
    "desert":   (5, "Dirt", "High Sand Dunes"),
    "ice":      (6, "Snow", "High Dirt"),
    "twilight": (7, "Dirt", "High Crushed Rock"),
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
        if players != 4:
            raise CliError("rot90 대칭은 4인용에서만 됩니다.")
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
    # Keep patch coverage proportional to map area so a larger player-count
    # map does not end up with the same 18 ground-color islands as a 128 map.
    blob_count = max(18, round(18 * width * height / (128 * 128)))
    for _ in range(blob_count):
        bx, by = rng.randrange(width), rng.randrange(height)
        alt = rng.choice(alts)
        rad = rng.uniform(9.0, 16.0)
        for px, py in scmap.symmetric_points(bx, by, symmetry, players,
                                             width, height):
            blobs.append((px, py, alt, rad))
    # Preserve the full main-to-natural resource corridor. A radius of 18 only
    # protected the main mineral pocket; wide recolor blobs then changed
    # potential natural sites after the anchor solver had inspected them.
    guard = [(sx, sy, 30.0) for (sx, sy) in starts]
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
    ap.add_argument("--natural-distance", type=int, default=24,
                    help="본진에서 앞마당까지 타일 거리")
    ap.add_argument("--main-entry", choices=["auto", "flat", "ramp", "bridge"],
                    default="auto",
                    help="AI 선택 입구: 자동 램프, 평지, 램프 전용, 또는 검증된 Jungle 브리지 템플릿")
    ap.add_argument("--expansions", type=int, default=None,
                    help="스타팅마다 놓을 바깥 멀티 수")
    ap.add_argument("--expansion-minerals", type=int, default=7)
    ap.add_argument("--expansion-gas", type=int, default=1)
    ap.add_argument("--favor", default="even",
                    choices=["even", "terran", "zerg", "protoss"],
                    help="자원 배치를 어느 종족 쪽으로 기울일지. "
                         "지형까지 기울이지는 않는다 — references/melee-balance.md 참고")
    ap.add_argument("--no-center", action="store_true",
                    help="가운데 지형을 얹지 않는다")
    ap.add_argument("--features", type=int, default=4,
                    help="AI가 선택해 대칭으로 얹을 지형 덩이 수 (0~64)")
    ap.add_argument("--feature-area", type=int, default=28,
                    help="지형 덩이 하나의 목표 타일 면적(8~384)")
    ap.add_argument("--feature-terrain", action="append", default=[],
                    help="AI가 선택한 장식 지형 종류; 여러 지형을 반복 지정할 수 있음")
    ap.add_argument("--doodads", type=int, default=None,
                    help="배치할 두뎃 수. 지형·진입로·자원 접근을 확인하고 명시한다")
    ap.add_argument("--critters", type=int, default=0,
                    help="중립 크리쳐 수 (0~스타팅 수)")
    ap.add_argument("--critter-unit", default=None,
                    help="AI가 고른 중립 크리쳐 유닛명; --critters가 0보다 클 때 필요")
    ap.add_argument("--fight-terrain", default=None,
                    help="교전 구역용 걷기 가능·건축 불가 ISOM 지형 이름")
    ap.add_argument("--fight-zone", action="append", default=[],
                    help="AI가 고른 교전 구역 x,y,w,h (반복 가능)")
    ap.add_argument("--fight-pattern", choices=["pockets", "flanks", "ridge"],
                    default=None, help="교전 지형 배치 모양")
    ap.add_argument("--fight-density", type=int, default=0,
                    help="구역 내 목표 비율(%%)")
    ap.add_argument("--max-unbuildable-pct", type=int, default=30,
                    help="맵 전체 걷기 가능·건축 불가 지형 비율 상한 (0~45%%)")
    ap.add_argument("--seed", type=int, default=1, help="두뎃 자리 난수 씨앗")
    ap.add_argument("--plateau", action="store_true", default=True,
                    help="(기본) 본진을 고지대에 두고 넓은 저지대 출입 통로를 낸다")
    ap.add_argument("--no-plateau", action="store_true",
                    help="(옛 이름) 기본이 평지라 아무 일도 하지 않는다")
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true",
                    help="이미 있는 맵을 덮어쓴다. 기본은 거절한다 — 뼈대를 "
                         "다시 만들면 그 위에 쓴 트리거·유닛이 모두 사라진다")
    args = ap.parse_args(argv)
    if not 0 <= args.critters <= args.players:
        ap.error("--critters 는 0부터 --players 까지입니다")
    if args.critters and not args.critter_unit:
        ap.error("--critters를 쓰면 AI가 고른 --critter-unit을 지정하세요")
    if not 0 <= args.max_unbuildable_pct <= 45:
        ap.error("--max-unbuildable-pct 범위는 0~45입니다")
    if not 0 <= args.fight_density <= 70:
        ap.error("--fight-density 범위는 0~70%입니다")
    if args.fight_density and (not args.fight_terrain or not args.fight_zone or not args.fight_pattern):
        ap.error("교전 지형은 --fight-terrain, --fight-zone, --fight-pattern을 함께 지정하세요")
    if args.main_entry == "ramp" and args.no_plateau:
        ap.error("--main-entry ramp는 본진 고도 형태가 있을 때만 쓸 수 있습니다")

    # 뼈대 생성은 맵을 처음부터 다시 만든다. 작성해 둔 내용이 있으면
    # 통째로 날아간다. 실제로 그렇게 트리거를 날린 적이 있다.
    if os.path.exists(args.out) and not args.force:
        print(f"이미 있습니다: {args.out}\n"
              f"  뼈대를 다시 만들면 그 위에 쓴 내용이 모두 사라집니다.\n"
              f"  내용을 고치려면 trigger show/apply 로 트리거만 손보세요.\n"
              f"  정말 처음부터 다시 만들려면 --force 를 주세요.", file=sys.stderr)
        return 2

    width, height = args.size
    if not 64 <= width <= 256 or not 64 <= height <= 256:
        ap.error("스타크래프트 맵 크기는 가로·세로 각각 64~256 타일입니다")
    if not (2 <= args.players <= 8):
        ap.error("스타팅은 2~8 개입니다.")
    if args.expansions is None:
        args.expansions = (1 if args.players >= 6 or
                           (args.players >= 4 and min(width, height) < 144)
                           else 2)
    if not 0 <= args.expansions <= 5:
        ap.error("--expansions 범위는 0~5입니다")
    if args.main_minerals is not None and not 1 <= args.main_minerals <= 12:
        ap.error("--main-minerals 범위는 1~12입니다")
    if not 0 <= args.main_gas <= 2:
        ap.error("--main-gas 범위는 0~2입니다")
    if args.natural_minerals is not None and not 0 <= args.natural_minerals <= 12:
        ap.error("--natural-minerals 범위는 0~12입니다")
    if args.natural_gas is not None and not 0 <= args.natural_gas <= 2:
        ap.error("--natural-gas 범위는 0~2입니다")
    if not 12 <= args.natural_distance <= 48:
        ap.error("--natural-distance 범위는 12~48입니다")
    if not 0 <= args.expansion_minerals <= 12 or not 0 <= args.expansion_gas <= 2:
        ap.error("바깥 멀티 자원 범위는 미네랄 0~12, 가스 0~2입니다")
    if not 0 <= args.features <= 64:
        ap.error("--features 범위는 0~64입니다")
    if not 8 <= args.feature_area <= 384:
        ap.error("--feature-area 범위는 8~384입니다")

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
    if args.main_entry == "bridge":
        if args.tileset != "jungle" or args.players not in (2, 4) or symmetry not in ("rot180", "rot90"):
            ap.error("검증된 Bridge 패턴은 Jungle 2인 rot180 또는 4인 rot90 맵에서만 지원됩니다")
        if args.natural_minerals > 0:
            ap.error("검증된 Jungle Bridge 진입은 본진-앞마당 사이 공간을 쓰므로 --natural-minerals 0으로 선택하세요")
        if args.doodads:
            ap.error("Bridge 진입 템플릿은 진입 주변을 차지하므로 --doodads 0이어야 합니다")
    # 본진 미네랄은 스타팅에서 7타일 바깥이다. inset 10 이면 그 줄이
    # 맵 테두리와 절벽에 걸친다.
    if args.plateau and not args.no_plateau and args.inset < 25:
        args.inset = 25

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
    args.no_plateau = bool(args.no_plateau)
    shape = None
    if not args.no_plateau:
        print("본진·능선·확장 고도를 칠합니다...")
        shape = melee_shape.design_lanes(
            width, height, args.players, symmetry, starts,
            random.Random(args.seed))
        print(melee_shape.report(shape))
        # Recolor broad low-ground regions using this tileset's actual
        # walkable low-elevation terrain classes. A short Badlands/Jungle name
        # list left Space/Twilight/Ice with one terrain group and large empty
        # plains even though those tilesets offer several buildable grounds.
        terrain_specs = scmap.terrain_types_table(tileset_id)
        terrain_props = scmap.tileset_tiles(cli, tileset_id)
        buildable_low_groups = {tile >> 4 for tile, prop in terrain_props.items()
                                if prop[0] == 0 and prop[1] and prop[2]}
        alts = [meta["index"] for name, meta in terrain_specs.items()
                if name != "Water" and not name.lower().startswith("high")
                and "0" in meta.get("levels", {})
                and set(meta.get("groups", [])).issubset(buildable_low_groups)
                and meta["index"] != low_terrain]
        strokes = recolor_floor(
            shape.strokes(low_terrain, high_terrain),
            width, height, low_terrain, alts, random.Random(args.seed + 3),
            starts, symmetry, args.players)
        cli.isom_batch(strokes)
        feature_types=[]
        for name in args.feature_terrain:
            if name not in types:
                raise CliError(f"알 수 없는 --feature-terrain: {name}")
            feature_types.append(types[name])
        if not feature_types:
            feature_types=[high_terrain,*alts]
        if args.features:
            rng_features=random.Random(args.seed+7)
            pattern=scmap.organic_blob
            feature_strokes=[]
            for _ in range(args.features):
                sx=rng_features.randrange(8,width-8)
                sy=rng_features.randrange(8,height-8)
                terrain=feature_types[rng_features.randrange(len(feature_types))]
                area=max(8,int(args.feature_area*rng_features.uniform(0.8,1.2)))
                blob=pattern(rng_features,area,elongate=rng_features.uniform(0.6,1.8))
                spots=scmap.symmetric_points(sx,sy,symmetry,args.players,width,height)
                for turn,(px,py) in enumerate(spots):
                    px,py=int(round(px)),int(round(py))
                    for dx,dy in blob:
                        if symmetry in ("rot90","rot180"):
                            dx,dy=scmap.rotate_offset(dx,dy,turn)
                        elif symmetry=="horizontal" and turn%2:
                            dx=-dx
                        elif symmetry=="vertical" and turn%2:
                            dy=-dy
                        tx,ty=px+int(dx),py+int(dy)
                        if not (3<=tx<width-3 and 3<=ty<height-3):
                            continue
                        if (in_base_or_exit(tx,ty,starts,(width-1)/2,(height-1)/2)
                                or any(math.hypot(tx-sx,ty-sy)<30 for sx,sy in starts)):
                            continue
                        feature_strokes.append((tx-tx%2,ty,terrain))
            cli.isom_batch(feature_strokes)


    # 3) 스타팅 표시와 본진 자원
    print("본진 자원을 놓습니다...")
    cx_mid, cy_mid = (width - 1) / 2.0, (height - 1) / 2.0
    original_starts=starts[:]
    main_anchors=None
    sx0,sy0=original_starts[0]
    # Search one anchor and transform it to every player. Independent nearest
    # fit searches drifted differently at opposite corners, breaking start
    # symmetry and changing the apparent natural distance on two-player maps.
    for radius in range(13):
        offsets=[(dx,dy) for dy in range(-radius,radius+1)
                 for dx in range(-radius,radius+1)
                 if max(abs(dx),abs(dy))==radius]
        if radius==0: offsets=[(0,0)]
        for dx,dy in offsets:
            seed=(sx0+dx,sy0+dy)
            if not (8<=seed[0]<width-8 and 8<=seed[1]<height-8):
                continue
            points=scmap.symmetric_points(seed[0],seed[1],symmetry,len(starts),width,height)
            if len(points)!=len(starts):
                continue
            proposed=[(int(round(x)),int(round(y))) for x,y in points]
            ok=True
            for (sx,sy),(ax,ay) in zip(original_starts,proposed):
                out_x=-1 if sx<=cx_mid else 1
                out_y=-1 if sy<=cy_mid else 1
                if resource_anchor(cli,tileset_id,ax,ay,args.main_minerals,
                    args.main_gas,out_x,out_y,width,height)!=(ax,ay):
                    ok=False
                    break
            if ok:
                main_anchors=proposed
                break
        if main_anchors is not None:
            break
    if main_anchors is None:
        raise CliError("모든 본진에 자원·건물 footprint를 같은 대칭으로 놓을 자리가 없습니다. 맵 크기나 타일셋을 바꾸세요.")
    starts=main_anchors
    for i, (sx, sy) in enumerate(starts):
        out_x = -1 if sx <= cx_mid else 1
        out_y = -1 if sy <= cy_mid else 1
        _main_placed, _skip = scmap.place_base(cli, sx, sy, owner=i + 1,
                         minerals=args.main_minerals, gas=args.main_gas,
                         out_x=out_x, out_y=out_y, width=width, height=height,
                         tileset_id=tileset_id)
        if _skip:
            raise CliError(f"본진 {i+1} 자원 footprint가 지형/건물 조건을 통과하지 못했습니다: {_skip[:3]}")

    # 4) 앞마당 — 본진에서 가운데 쪽으로 한 걸음.
    nat_placed = []
    natural_sites_coords = []
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
        natural_sites_coords = [(nx,ny) for nx,ny,_ox,_oy in sites]

        # Keep natural resource footprints on the low center. A broad raised
        # rim makes the expansion read as a terrain pocket; leave the arc that
        # faces its nearest main open as a low, walkable entrance.
        if not args.no_plateau:
            rim=[]
            for i,(nx,ny,_ox,_oy) in enumerate(sites):
                sx,sy=starts[i]
                vx,vy=sx-nx,sy-ny
                length=math.hypot(vx,vy) or 1.0
                gate=math.atan2(vy,vx)
                for ty in range(max(4,ny-23),min(height-4,ny+24)):
                    for tx in range(max(4,nx-23),min(width-4,nx+24),2):
                        dx,dy=tx-nx,ty-ny
                        d=math.hypot(dx,dy)
                        da=abs((math.atan2(dy,dx)-gate+math.pi)%(2*math.pi)-math.pi)
                        if 18 <= d <= 21 and da > 0.42:
                            rim.append((tx,ty,high_terrain))
            cli.isom_batch(rim)
            gates=[]
            for i,(nx,ny,_ox,_oy) in enumerate(sites):
                sx,sy=starts[i]
                vx,vy=sx-nx,sy-ny
                length=math.hypot(vx,vy) or 1.0
                ux,uy=vx/length,vy/length
                for d in range(16,24):
                    px,py=int(round(nx+ux*d)),int(round(ny+uy*d))
                    for off in range(-5,6,2):
                        tx,ty=(px,py+off) if abs(ux)>abs(uy) else (px+off,py)
                        if 2<=tx<width-2 and 2<=ty<height-2:
                            gates.append((tx-tx%2,ty,low_terrain))
            cli.isom_batch(gates)

        # Keep each resource footprint and its town-hall pad on one buildable
        # low level. Rounded base hills can otherwise overlap a pocket found
        # on the pre-hill terrain map, leaving a mixed-elevation geyser pad.
        pocket_floors=[]
        for nx,ny,_ox,_oy in sites:
            for ty in range(max(2,ny-12),min(height-2,ny+13)):
                for tx in range(max(2,nx-12),min(width-2,nx+13),2):
                    if (tx-nx)**2+(ty-ny)**2 <= 12*12:
                        pocket_floors.append((tx-tx%2,ty,low_terrain))
        cli.isom_batch(pocket_floors)

        for i, (nx, ny, ox, oy) in enumerate(sites):
            _placed, _skip = scmap.place_base(cli, nx, ny, owner=12,
                             minerals=args.natural_minerals,
                             gas=args.natural_gas,
                             out_x=ox, out_y=oy,
                             width=width, height=height,
                             start_location=False, tileset_id=tileset_id)
            nat_placed.append((nx, ny, len(_placed), _skip))
            if _skip:
                raise CliError(f"앞마당 {i+1}의 요청 자원을 모두 놓을 수 없습니다: {_skip[:3]}")

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
                expected = (args.natural_minerals if kind == "Mineral Field"
                            else args.natural_gas)
                if min(counts) < expected:
                    raise CliError(f"앞마당 {label} 개수가 요청 수보다 적습니다: {counts}, 요청 {expected}")
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
        fallback_rings = []

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
                    if len(_placed) < args.expansion_minerals + args.expansion_gas:
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
                    if len(pool) < args.expansion_minerals + args.expansion_gas:
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

            # 첫 스타팅에서 자리를 스냅한 뒤 실제 스냅 좌표를 회전한다.
            # 각 시작지에서 독립 스냅하면 지형 경계의 한두 타일 차이로
            # 바깥 멀티가 비대칭이 된다. 변환된 정확 좌표가 모든 시작지에서
            # 자원 배치에 유효하지 않으면 다음 후보를 본다.
            for extra in (45, 50, -25, -50, -125, 55, -15, -40, 15):
                if chosen:
                    break
                for dist in (32, 34, 36, 40, 44, 48, 56, 64, 72):
                    if dist < far:
                        continue
                    ang = (math.atan2(cy - sy0, cx - sx0)
                           + math.radians(deg0 + extra))
                    seed_x = sx0 + dist * math.cos(ang)
                    seed_y = sy0 + dist * math.sin(ang)
                    images = scmap.symmetric_points(
                        seed_x, seed_y, symmetry, len(starts), width, height)
                    first = snap_expansion(int(round(images[0][0])),
                                           int(round(images[0][1])),
                                           starts[0][0], starts[0][1])
                    if first is None:
                        continue
                    snapped_images = scmap.symmetric_points(
                        first[0], first[1], symmetry, len(starts), width, height)
                    spots = [((starts[0][0], starts[0][1]), first)]
                    for i in range(1, len(starts)):
                        sx, sy = starts[i]
                        ix, iy = (int(round(v)) for v in snapped_images[i])
                        hit = snap_expansion(ix, iy, sx, sy)
                        if hit is None or hit[:2] != (ix, iy):
                            spots = []
                            break
                        spots.append(((sx, sy), hit))
                    if len(spots) != len(starts):
                        # Keep the old per-start fit as a last resort. Some
                        # ISOM rotations quantize differently, so an exact
                        # resource ring may not exist on every fair map.
                        fallback=[]
                        for (sx,sy),(ix,iy) in zip(starts,images):
                            hit=snap_expansion(int(round(ix)),int(round(iy)),sx,sy)
                            if hit is None:
                                fallback=[]
                                break
                            fallback.append(((sx,sy),hit))
                        if len(fallback)==len(starts):
                            fallback_rings.append(fallback)
                        continue
                    pts = [(ax, ay) for (_s, (ax, ay, _ox, _oy)) in spots]
                    if any(math.hypot(pts[a][0] - pts[b][0],
                                      pts[a][1] - pts[b][1]) < 18
                           for a in range(len(pts)) for b in range(a)):
                        snap_why["apart"] = snap_why.get("apart", 0) + 1
                        continue
                    if not commit_ring(spots):
                        fallback=[]
                        for (sx,sy),(ix,iy) in zip(starts,images):
                            hit=snap_expansion(int(round(ix)),int(round(iy)),sx,sy)
                            if hit is None:
                                fallback=[]
                                break
                            fallback.append(((sx,sy),hit))
                        if len(fallback)==len(starts):
                            fallback_rings.append(fallback)
                        continue
                    chosen = spots
                    break
            if not chosen:
                for fallback in fallback_rings:
                    if commit_ring(fallback):
                        chosen=fallback
                        print("  정확한 대칭 자원 자리가 없어 각 본진에서 검증한 근접 후보로 대체합니다")
                        break
            if not chosen:
                raise CliError(f"요청한 바깥 멀티 {k+1}/{args.expansions} 링을 모든 시작지에 공평하게 놓을 수 없습니다. {fail_why} snap {snap_why}. 맵 크기/멀티 수를 조정하세요.")
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

    # 6b) 지형 무늬 — 구역별 역할에 맞춰 고지대와 바닥 지형을 나눈다.
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

    # 7) 지형지물 — 두뎃은 의도적으로 지정된 경우에만 검토한다.
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
        # 두뎃은 경로·시야·자원 접근을 바꾸므로 자동 밀도 추정으로 배치하지 않는다.
        # 사용자가 목적에 맞는 수를 지정할 때만 배치한다.
        args.doodads = 0
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

    # 7b) 본진 램프 — AI 선택값과 맞는 실제 배치 후보만 쓴다.
    #
    #      먼저 내면 나중에 얹은 지형 덩이가 램프 바깥을 막아 본진이
    #      섬이 된다. 실제로 그렇게 갇힌 맵이 나왔다.
    #
    # 절벽 줄은 **높이**로 찾고, 찍은 뒤에는 **미니타일 길찾기**로 실제로
    # 통하는지 확인한다. 눈으로만 보고 판단하면 막힌 램프를 놓게 된다.
    ramp_at = {}
    bridge_at = {}
    bridge_plan = []
    bridge_patterns={}
    if args.main_entry == "bridge":
        pattern_path=os.path.normpath(os.path.join(os.path.dirname(__file__),"..","data","bridge-patterns.json"))
        with open(pattern_path,encoding="utf-8") as f:
            bridge_patterns={p["doodad_id"]:p for p in json.load(f)["patterns"]}

    if not args.no_plateau:
        cx0,cy0=(width-1)/2,(height-1)/2
        terrain = scmap.Terrain(cli,0,0,width,height,tileset_id)
        for sx,sy in starts:
            vx,vy=cx0-sx,cy0-sy
            length=math.hypot(vx,vy) or 1
            ux,uy=vx/length,vy/length
            direction = ("right" if ux > 0 else "left") if abs(ux)>abs(uy) else ("down" if uy > 0 else "up")
            horizontal = direction in ("left","right")
            if args.main_entry == "bridge":
                # The verified 14x10 Jungle template has diagonal land/water
                # endpoints. Orient its near end toward the start and its far
                # end toward the central low ground.
                px,py=int(round(sx+ux*12)),int(round(sy+uy*12))
                if ux>0 and uy>0:
                    did=269; pattern=bridge_patterns[did]; ox,oy=px,py
                elif ux<0 and uy<0:
                    did=268; pattern=bridge_patterns[did]; ox,oy=px-13,py-9
                elif ux<0 and uy>0:
                    did=268; pattern=bridge_patterns[did]; ox,oy=px-13,py
                else:
                    did=269; pattern=bridge_patterns[did]; ox,oy=px,py-9
                bw,bh=pattern["width"],pattern["height"]
                if not (1<=ox<width-bw-1 and 1<=oy<height-bh-1):
                    raise CliError(f"Bridge {did} placement for start ({sx},{sy}) is outside map")
                footprint={(ox+x,oy+y) for y in range(bh) for x in range(bw)}
                def touches_resource(u):
                    if u["type"]==scmap.START_LOCATION:
                        return (u["x"]//32,u["y"]//32) in footprint
                    if u["type"] in scmap.MINERALS:
                        fw,fh=2,1
                    elif u["type"]==scmap.VESPENE_GEYSER:
                        fw,fh=4,2
                    else:
                        return False
                    return any(cell in footprint for cell in scmap._foot_cells(u["x"],u["y"],fw,fh))
                if any(touches_resource(u) for u in cli.units()):
                    raise CliError(f"Bridge {did} overlaps a start or resource site")
                bridge_plan.append((sx,sy,ox,oy,bw,bh,did,pattern,ux,uy))
                bridge_at[(sx,sy)]=(ox,oy,bw,bh,did)
                # Open only the near approach; the template supplies the bank
                # and bridge deck across the water gap.
                approach=[]
                for dist in range(3,11):
                    qx,qy=int(round(sx+ux*dist)),int(round(sy+uy*dist))
                    for off in range(-5,6):
                        tx,ty=int(round(qx-uy*off)),int(round(qy+ux*off))
                        bridge_margin = 2
                        if (2<=tx<width-2 and 2<=ty<height-2
                                and not (ox-bridge_margin<=tx<ox+bw+bridge_margin
                                         and oy-bridge_margin<=ty<oy+bh+bridge_margin)):
                            approach.append((tx-tx%2,ty,low_terrain))
                cli.isom_batch(approach)
                print(f"  본진 ({sx},{sy}) Jungle Bridge {did} fits+보행 템플릿 적용")
                continue
            # Find the true high-to-low boundary along the center-facing axis.
            fixed = sy if horizontal else sx
            edge = None
            for d in range(8,23):
                px=int(round(sx+ux*d));py=int(round(sy+uy*d))
                tx,ty=(px,sy) if horizontal else (sx,py)
                nx,ny=(tx+(1 if ux>0 else -1),ty) if horizontal else (tx,ty+(1 if uy>0 else -1))
                if 0<=tx<width and 0<=ty<height and 0<=nx<width and 0<=ny<height:
                    if terrain.elevation(tx,ty)>=1 and terrain.elevation(nx,ny)==0:
                        edge = tx if horizontal else ty
                        fixed = sy if horizontal else sx
                        break
            ramp_placed = None
            if edge is not None and args.main_entry in ("auto", "ramp"):
                high_point=(edge-fixed if horizontal else fixed,
                            fixed if horizontal else edge)
                low_point=(high_point[0]+(3 if ux>0 else -3) if horizontal else high_point[0],
                           high_point[1] if horizontal else high_point[1]+(3 if uy>0 else -3))
                ramp_placed=scmap.place_ramp_checked(
                    cli,tileset_id,edge,fixed,high_point,low_point,direction,
                    candidates=[r for r in scmap.ramp_candidates(tileset_id,direction)
                                if r.get("walks") and r.get("kind")=="Cliff"
                                and r.get("walks_with")==high_name])
            if ramp_placed:
                did,rx,ry=ramp_placed
                meta=next(r for r in scmap.ramp_candidates(tileset_id,direction) if r["id"]==did)
                ramp_at[(sx,sy)]=(rx,ry,direction,meta["w"],meta["h"])
                print(f"  본진 ({sx},{sy}) {direction} 램프 {did} 배치 및 국소 보행 확인")
            elif args.main_entry != "ramp":
                # A flat passage is a valid entrance where no compatible ramp
                # can be placed and connected. Reopen a broad low-ground gate.
                cuts=[]
                for d in range(7,22):
                    px,py=int(round(sx+ux*d)),int(round(sy+uy*d))
                    for o in range(-7,8,2):
                        tx,ty=(px,py+o) if abs(ux)>abs(uy) else (px+o,py)
                        if 2<=tx<width-2 and 2<=ty<height-2:
                            cuts.append((tx-tx%2,ty,low_terrain))
                cli.isom_batch(cuts)
                reason = "설정한 평지 입구" if args.main_entry == "flat" else "검증된 램프가 없어 선택한 평지 대체 입구"
                print(f"  본진 ({sx},{sy}) {reason}")
            else:
                raise CliError(f"본진 ({sx},{sy})에 검증된 램프를 놓을 수 없습니다. auto 또는 flat으로 다시 선택하세요.")

    # Reopen each natural-pocket gate after center features, water, and doodad
    # decoration have been laid. Its endpoint is the nearest main ramp/causeway;
    # verify the tile-level walk graph instead of assuming the painted line works.
    if natural_sites_coords and not args.no_plateau:
        pocket_cuts=[]
        for i,(nx,ny) in enumerate(natural_sites_coords):
            sx,sy=starts[i]
            vx,vy=sx-nx,sy-ny
            length=math.hypot(vx,vy) or 1.0
            ux,uy=vx/length,vy/length
            for d in range(0,int(length)+5):
                px,py=int(round(nx+ux*d)),int(round(ny+uy*d))
                for off in range(-10,11):
                    tx,ty=int(round(px-uy*off)),int(round(py+ux*off))
                    if 2<=tx<width-2 and 2<=ty<height-2:
                        pocket_cuts.append((tx-tx%2,ty,low_terrain))
        cli.isom_batch(pocket_cuts)
        pocket_grid=scmap.walk_grid(cli,tileset_id,0,0,width,height)
        for i,(nx,ny) in enumerate(natural_sites_coords):
            sx,sy=starts[i]
            p0=scmap.nearest_walkable(pocket_grid,nx*4+2,ny*4+2,24)
            p1=scmap.nearest_walkable(pocket_grid,sx*4+2,sy*4+2,48)
            if p0 is None or p1 is None or not scmap.walk_reachable(pocket_grid,p0,p1):
                raise CliError(f"앞마당 ({nx},{ny})의 평지 입구가 본진과 연결되지 않았습니다")

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
                    if any(ox0 - 2 <= px <= ox0 + bw0 + 1 and
                           oy0 - 2 <= ty <= oy0 + bh0 + 1
                           for (ox0, oy0, bw0, bh0, _did) in bridge_at.values()):
                        continue
                    carve.append((px - (px % 2), ty, low_terrain))
        cli.isom_batch(carve)

    # 길을 내며 덮인 장식은 그림이 칸과 어긋난다. 램프는 남긴다.
    if args.doodads > 0 or bridge_at:
        proc = subprocess.run(
            [cli.cli, "doodad", "check", cli.path, "--install", cli.install],
            capture_output=True, text=True)
        broken = [int(m.group(1)) for line in proc.stdout.splitlines()
                  if (m := re.match(r"^\s*(\d+)\s+타일", line))]
        if bridge_at and broken:
            raise CliError(f"Bridge/두들 정렬 검사가 {len(broken)}개 오류를 찾았습니다")
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

    # 교전지에만 AI가 고른 비건축 지형을 섞는다. 전역 상한을 넘으면
    # 추가 도장을 줄여 다시 적용해 기지·멀티의 건축 여지를 보전한다.
    if args.fight_density > 0:
        types_by_name = cli.terrain_types()
        if args.fight_terrain not in types_by_name:
            raise CliError(f"알 수 없는 ISOM 지형명: {args.fight_terrain}")
        fight_id = types_by_name[args.fight_terrain]
        props = scmap.tileset_tiles(cli, tileset_id)
        groups = {t >> 4 for t, p in props.items() if p[1] and not p[2]}
        terrain_info = scmap.terrain_types_table(tileset_id).get(args.fight_terrain, {})
        valid_groups = groups.intersection(int(g) for g in terrain_info.get("groups", []))
        if not valid_groups:
            raise CliError("선택 지형에 걷기 가능·건축 불가 타일이 없습니다")
        zones = []
        for spec in args.fight_zone:
            try:
                z = tuple(int(v) for v in spec.split(","))
            except ValueError as e:
                raise CliError(f"잘못된 --fight-zone: {spec}") from e
            if len(z) != 4 or z[2] < 1 or z[3] < 1 or z[0] < 0 or z[1] < 0 or z[0]+z[2] > width or z[1]+z[3] > height:
                raise CliError(f"구역은 맵 안의 x,y,w,h여야 합니다: {spec}")
            zones.append(z)
        resource_blocks = [(u["x"]//32, u["y"]//32, 7) for u in cli.units()
                           if u["type"] in scmap.MINERALS or u["type"] == scmap.VESPENE_GEYSER]
        doodad_meta = {d["id"]: d for d in cli.doodad_catalogue()}
        doodad_blocks = []
        for d in cli.doodads():
            meta = doodad_meta.get(d["id"])
            if meta:
                ox, oy = scmap.doodad_topleft(d["x"], d["y"], meta["w"], meta["h"])
                doodad_blocks.append((ox, oy, meta["w"], meta["h"]))
        terrain_before = cli.tiles(0, 0, width, height)
        tile_props = scmap.tileset_tiles(cli, tileset_id)
        max_bad = int(width * height * args.max_unbuildable_pct / 100)
        baseline_bad = sum(1 for row in terrain_before for t in row if t in tile_props and tile_props[t][1] and not tile_props[t][2])
        if baseline_bad:
            if baseline_bad > max_bad:
                raise CliError(f"기본 지형부터 건축 불가 {baseline_bad*100/(width*height):.1f}%로 전역 상한을 넘습니다")
        selected_strokes = []
        for x, y, w, h in zones:
            # Central engagement zones intentionally cross the main routes:
            # this terrain is walkable. Preserve each base footprint, resource
            # approach, and entrance object, while allowing the AI-picked
            # center ground to be rough and non-buildable.
            eligible = [(tx, ty) for ty in range(y, y+h, 2) for tx in range(x, x+w, 2)
                        if not any(abs(tx-sx) <= 12 and abs(ty-sy) <= 9
                                   for sx,sy in starts)
                        and not any(abs(tx-px) <= radius and abs(ty-py) <= radius
                                    for px,py,radius in resource_blocks)
                        and not any(rx-4 <= tx <= rx+rw+4 and ry-4 <= ty <= ry+rh+4
                                    for rx, ry, _direction, rw, rh in ramp_at.values())
                        and not any(ox-2 <= tx <= ox+bw+1 and oy-2 <= ty <= oy+bh+1
                                    for ox,oy,bw,bh,_did in bridge_at.values())
                        and not any(ox-3 <= tx <= ox+dw+2 and oy-3 <= ty <= oy+dh+2
                                    for ox,oy,dw,dh in doodad_blocks)]
            rng = random.Random(args.seed + x * 37 + y * 101)
            rng.shuffle(eligible)
            take = min(len(eligible), int(len(eligible) * args.fight_density / 100))
            for tx, ty in eligible[:take]:
                if args.fight_pattern == "ridge" and (ty-y) % 8:
                    continue
                if args.fight_pattern == "flanks" and tx not in (x, x+w-2):
                    continue
                if args.fight_pattern == "pockets" and (tx % 12 > 4 or ty % 12 > 4):
                    continue
                selected_strokes.append((tx, ty, fight_id))

        # The AI specifies one fight-region shape; mirror its strokes and
        # bounds through the map symmetry so the terrain offer remains fair.
        transformed = set()
        for tx,ty,terrain in selected_strokes:
            for px,py in scmap.symmetric_points(tx,ty,symmetry,args.players,width,height):
                qx,qy=int(round(px)),int(round(py))
                qx-=qx%2
                if 0<=qx<width and 0<=qy<height:
                    transformed.add((qx,qy,terrain))
        strokes=sorted(transformed)

        # Snapshot every transformed zone before ISOM touches its neighbors;
        # this also makes the global-cap retry restore the entire symmetric set.
        zone_boxes=[]
        for x,y,w,h in zones:
            corners=((x,y),(x+w-1,y),(x,y+h-1),(x+w-1,y+h-1))
            transformed_corners=[scmap.symmetric_points(px,py,symmetry,args.players,width,height)
                                 for px,py in corners]
            ntrans=min(len(v) for v in transformed_corners)
            for i in range(ntrans):
                pts=[v[i] for v in transformed_corners]
                bx0=max(0,int(math.floor(min(p[0] for p in pts))))
                by0=max(0,int(math.floor(min(p[1] for p in pts))))
                bx1=min(width,int(math.ceil(max(p[0] for p in pts)))+1)
                by1=min(height,int(math.ceil(max(p[1] for p in pts)))+1)
                if bx1>bx0 and by1>by0:
                    box=(bx0,by0,bx1-bx0,by1-by0)
                    if box not in zone_boxes: zone_boxes.append(box)
        accepted = 0
        zone_cells={(tx,ty) for x,y,w,h in zone_boxes
                    for ty in range(y,y+h) for tx in range(x,x+w)}
        zone_base={(tx,ty):terrain_before[ty][tx] for tx,ty in zone_cells}
        cli.isom_batch(strokes)
        after = cli.tiles(0, 0, width, height)
        bad = sum(1 for row in after for t in row if t in tile_props and tile_props[t][1] and not tile_props[t][2])
        if bad <= max_bad:
            accepted = len(strokes)
            print(f"  교전 지형 {accepted} 도장; 전역 걷기·건축불가 {bad*100/(width*height):.1f}% / 상한 {args.max_unbuildable_pct}%")
        else:
            room = max(0, max_bad - baseline_bad)
            factor = room / max(1, bad-baseline_bad)
            take=int(len(selected_strokes)*factor)
            reduced_base=selected_strokes[:take]
            reduced_set=set()
            for tx,ty,terrain in reduced_base:
                for px,py in scmap.symmetric_points(tx,ty,symmetry,args.players,width,height):
                    qx,qy=int(round(px)),int(round(py)); qx-=qx%2
                    if 0<=qx<width and 0<=qy<height:
                        reduced_set.add((qx,qy,terrain))
            reduced=sorted(reduced_set)
            for bx,by,bw,bh in zone_boxes:
                rows=[[terrain_before[yy][xx] for xx in range(bx,bx+bw)]
                      for yy in range(by,by+bh)]
                cli.paste_tiles(bx,by,rows)
            cli.isom_batch(reduced)
            accepted = len(reduced)
            after = cli.tiles(0, 0, width, height)
            bad = sum(1 for row in after for t in row if t in tile_props and tile_props[t][1] and not tile_props[t][2])
            if bad > max_bad:
                raise CliError(f"교전 지형 적용 후 건축 불가 지형 {bad*100/(width*height):.1f}%가 상한 {args.max_unbuildable_pct}%를 넘습니다")
            print(f"  교전 지형을 {accepted} 도장으로 줄였습니다; 전역 {bad*100/(width*height):.1f}%")
        before_bad=sum(1 for tx,ty in zone_cells
                       if tile_props.get(terrain_before[ty][tx],(0,0,0))[1]
                       and not tile_props.get(terrain_before[ty][tx],(0,0,0))[2])
        after_bad=sum(1 for tx,ty in zone_cells
                      if tile_props.get(after[ty][tx],(0,0,0))[1]
                      and not tile_props.get(after[ty][tx],(0,0,0))[2])
        added=max(0,after_bad-before_bad)
        zone_pct=100*added/max(1,len(zone_cells))
        if args.fight_density and zone_pct < 8:
            raise CliError(f"교전 구역의 새 걷기 가능·건축 불가 지형이 {zone_pct:.1f}%뿐입니다. 구역/밀도를 늘리세요")
        print(f"  대칭 교전 구역 새 건축 불가 지형 {zone_pct:.1f}%")
        if not accepted and args.fight_density:
            print("  교전 지형은 전역 상한 때문에 넣지 않았습니다")

    # The fight-zone terrain is applied after entrance and decorative DD2s.
    # Confirm their rendered cells still match; remove only optional decor.
    if cli.doodads():
        proc=subprocess.run([cli.cli,"doodad","check",cli.path,"--install",cli.install],
                            capture_output=True,text=True)
        broken=[int(m.group(1)) for line in proc.stdout.splitlines()
                if (m:=re.match(r"^\s*(\d+)\s+타일",line))]
        if broken:
            ramp_ids={e["id"] for direction in ("left","right","up","down")
                      for e in scmap.ramp_candidates(tileset_id,direction)}
            by_index={d["index"]:d["id"] for d in cli.doodads()}
            if any(by_index.get(i) in ramp_ids for i in broken):
                raise CliError("교전 지형 적용 뒤 입구 램프 두들의 정렬이 깨졌습니다")
            for idx in sorted(broken,reverse=True):
                cli.edit("doodad","remove",cli.path,str(idx),"--install",cli.install)
            checked=subprocess.run([cli.cli,"doodad","check",cli.path,"--install",cli.install],
                                   capture_output=True,text=True)
            if checked.returncode:
                raise CliError("장식 두들을 제거한 뒤에도 doodad check가 통과하지 않았습니다")

    # The cap applies to the entire map, including the pre-existing terrain,
    # rather than only to the optional fight-zone strokes.
    final_tiles = cli.tiles(0, 0, width, height)
    final_props = scmap.tileset_tiles(cli, tileset_id)
    unbuildable = sum(1 for row in final_tiles for tile in row
                      if tile in final_props and final_props[tile][1]
                      and not final_props[tile][2])
    pct = 100.0 * unbuildable / max(1, width * height)
    if pct > args.max_unbuildable_pct:
        raise CliError(f"전체 맵의 걷기 가능·건축 불가 지형 {pct:.1f}%가 상한 {args.max_unbuildable_pct}%를 넘습니다")
    print(f"  전체 맵 걷기 가능·건축 불가 지형 {pct:.1f}% / 상한 {args.max_unbuildable_pct}%")

    # 지정 수 안에서 중립 크리쳐를 교전 공간 가장자리에 둔다.
    if args.critters:
        rng = random.Random(args.seed ^ 0xC17)
        blocked = [(u["x"]//32, u["y"]//32, 6) for u in cli.units()]
        placed = 0
        for _ in range(args.critters * 80):
            if placed >= args.critters:
                break
            tx, ty = rng.randrange(8, width-8), rng.randrange(8, height-8)
            if any(abs(tx-x) <= r and abs(ty-y) <= r for x,y,r in blocked):
                continue
            cli.place(args.critter_unit, tx, ty, owner=12)
            blocked.append((tx,ty,6))
            placed += 1
        print(f"  중립 크리쳐 {placed}/{args.critters}개 배치: {args.critter_unit}")
        if placed != args.critters:
            raise CliError(f"요청한 중립 크리쳐 {args.critters}개 중 {placed}개만 놓였습니다")

    # Recheck the final saved terrain from each actual start tile. Earlier
    # checks run before resource-footprint repairs and decorative cliff DDs,
    # which can alter a connection after it was reported as open.
    # Bridge templates are painted after terrain sculpting, since even a later
    # connectivity stroke can repaint a few MTXM cells under a valid DD2.
    for sx,sy,ox,oy,bw,bh,did,pattern,ux,uy in bridge_plan:
        cli.paste_tiles(ox,oy,pattern["tile_values"])
        bridge_cx,bridge_cy=scmap.doodad_anchor(ox,oy,bw,bh)
        if not cli.doodad_fits(did,bridge_cx,bridge_cy):
            raise CliError(f"Jungle Bridge {did} fails doodad fits at ({bridge_cx},{bridge_cy})")
        cli.paste_tiles(ox,oy,pattern["surface_values"])
        scmap.place_doodad(cli,did,ox,oy,bw,bh)
    if bridge_at:
        proc = subprocess.run(
            [cli.cli, "doodad", "check", cli.path, "--install", cli.install],
            capture_output=True, text=True)
        broken = [int(m.group(1)) for line in proc.stdout.splitlines()
                  if (m := re.match(r"^\s*(\d+)\s+타일", line))]
        if broken:
            raise CliError(f"Bridge 템플릿 정렬 검사가 {len(broken)}개 오류를 찾았습니다")

    final_grid = scmap.walk_grid(cli, tileset_id, 0, 0, width, height)
    final_starts = [scmap.nearest_walkable(final_grid, sx * 4 + 2, sy * 4 + 2,
                                           radius=8) for sx, sy in starts]
    if any(p is None for p in final_starts):
        raise CliError("최종 지형에서 스타팅 바로 옆의 보행 가능한 칸을 찾지 못했습니다")
    stranded = [(starts[i], p) for i, p in enumerate(final_starts)
                if not scmap.walk_reachable(final_grid, final_starts[0], p)]
    if stranded:
        if args.main_entry=="bridge":
            raise CliError("Bridge 템플릿 적용 뒤 최종 시작지 지상 경로가 끊겼습니다")
        # A locally valid ramp can still leave its base cut off after resource
        # tile repair. Drop only those failed ramp DDs and reopen that main's
        # entrance as a broad low-ground gate, then check the saved geometry.
        for (sx, sy), _point in stranded:
            ramp = ramp_at.get((sx, sy))
            if ramp:
                rx, ry, _direction, _rw, _rh = ramp
                doodad = next((d for d in cli.doodads()
                               if d["x"] == rx and d["y"] == ry), None)
                if doodad:
                    cli.edit("doodad", "remove", cli.path, str(doodad["index"]),
                             "--install", cli.install)
                ramp_at.pop((sx, sy), None)
            cx0, cy0 = (width - 1) / 2, (height - 1) / 2
            vx, vy = cx0 - sx, cy0 - sy
            length = math.hypot(vx, vy) or 1
            ux, uy = vx / length, vy / length
            cuts = []
            for dist in range(5, 24):
                px, py = int(round(sx + ux * dist)), int(round(sy + uy * dist))
                for off in range(-8, 9, 2):
                    tx, ty = ((px, py + off) if abs(ux) > abs(uy)
                              else (px + off, py))
                    if 2 <= tx < width - 2 and 2 <= ty < height - 2:
                        cuts.append((tx - tx % 2, ty, low_terrain))
            cli.isom_batch(cuts)
            print(f"  최종 경로가 끊긴 본진 ({sx},{sy})은 램프를 빼고 평지 입구로 복구")
        final_grid = scmap.walk_grid(cli, tileset_id, 0, 0, width, height)
        final_starts = [scmap.nearest_walkable(final_grid, sx * 4 + 2, sy * 4 + 2,
                                               radius=8) for sx, sy in starts]
    if any(p is None for p in final_starts) or any(
            not scmap.walk_reachable(final_grid, final_starts[0], p)
            for p in final_starts[1:]):
        raise CliError("최종 자원/장식 적용 뒤 스타팅 지상 경로가 끊겼습니다. 이 맵을 사용하지 말고 지형/입구 선택을 바꾸세요.")

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
