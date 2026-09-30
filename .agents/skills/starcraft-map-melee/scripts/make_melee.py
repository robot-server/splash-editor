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
    # "Structure"는 이름과 달리 build=0(못 지음)이다 — 실측으로 확인하고
    # 건설 가능한 "High Dirt"로 바꿨다(공식 확정 도장이 처음으로 이
    # 자리를 실측해 드러난 버그).
    "badlands": (0, "Dirt", "High Dirt"),
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


def _placed_items(cli) -> list[dict]:
    cli._role_table()
    items = []
    for unit in cli.units():
        name = unit["type_name"]
        if name == "Start Location":
            continue
        role = cli._roles.get(name, "ground")
        items.append({
            "index": unit["index"],
            "name": name,
            "tx": unit["x"] // 32,
            "ty": unit["y"] // 32,
            "role": role,
            "foot": cli._feet.get(name, (1, 1)),
        })
    return items


def _terrain_prop(cli, tileset_id: int, width: int, height: int):
    props = scmap.tileset_tiles(cli, tileset_id)
    grid = cli.tiles(0, 0, width, height)

    def prop_at(tx, ty):
        if not (0 <= ty < len(grid) and 0 <= tx < len(grid[0])):
            return None
        return props.get(grid[ty][tx])

    return prop_at


def _paint_floor_under(cli, tileset_id: int, width: int, height: int,
                       items: list[dict], terrain_id: int) -> int:
    """불법 발자국은 합법 칸으로 옮기거나 isometric 붓으로 다시 칠한다."""
    import verify_map
    props = scmap.tileset_tiles(cli, tileset_id)
    grid = cli.tiles(0, 0, width, height)

    def prop_at(tx, ty):
        if not (0 <= ty < len(grid) and 0 <= tx < len(grid[0])):
            return None
        return props.get(grid[ty][tx])

    occupied = set()
    for item in items:
        if item["role"] == "resource":
            occupied.update(scmap.footprint_cells(item["tx"], item["ty"], *item["foot"]))
    cluster_height = {}
    for cluster in verify_map._resource_clusters(
            [item for item in items if item["role"] == "resource"]):
        heights = []
        for item in cluster:
            for x, y in scmap.footprint_cells(item["tx"], item["ty"], *item["foot"]):
                prop = prop_at(x, y)
                if prop and prop[1] and prop[2]:
                    heights.append(prop[0])
        elev = max(set(heights), key=heights.count) if heights else None
        for item in cluster:
            cluster_height[item["index"]] = elev
    moved = 0
    strokes = []
    for item in items:
        if item["role"] not in ("resource", "building", "ground"):
            continue
        own = set(scmap.footprint_cells(item["tx"], item["ty"], *item["foot"]))
        remedy = scmap.footprint_remedy(
            item["tx"], item["ty"], *item["foot"], prop_at, width, height,
            terrain_id, build=item["role"] != "ground",
            blocked=occupied - own,
            prefer=cluster_height.get(item["index"]))
        if remedy["action"] == "move":
            tx, ty = remedy["at"]
            cli.edit("unit", "move", cli.path, str(item["index"]),
                     str(tx), str(ty), "--tiles")
            moved += 1
        elif remedy["action"] == "isom":
            strokes.extend(remedy["strokes"])
    items = _placed_items(cli)
    grid = cli.tiles(0, 0, width, height)

    def prop_at(tx, ty):
        if not (0 <= ty < len(grid) and 0 <= tx < len(grid[0])):
            return None
        return props.get(grid[ty][tx])

    for cluster in verify_map._resource_clusters(
            [item for item in items if item["role"] == "resource"]):
        cells = []
        for item in cluster:
            cells.extend(c for c in scmap.footprint_cells(
                item["tx"], item["ty"], *item["foot"])
                if 0 <= c[1] < height and 0 <= c[0] < width)
        if not cells:
            continue
        here = [prop_at(x, y) for x, y in cells]
        if any(prop is None or not prop[1] or not prop[2] for prop in here):
            continue
        heights = {prop[0] for prop in here if prop is not None}
        if len(heights) != 1:
            continue
        elev = next(iter(heights))
        blocked = set(cells)
        cx = sum(item["tx"] for item in cluster) // len(cluster)
        cy = sum(item["ty"] for item in cluster) // len(cluster)
        if scmap.find_townhall(prop_at, blocked, cx, cy, cells, elev):
            continue
        for oy in range(-8, 9):
            for ox in range(-8, 9):
                origin_x, origin_y = cx + ox, cy + oy
                hx, hy = origin_x + 1.5, origin_y + 1.0
                if any(math.hypot(tx - hx, ty - hy) > 8 for tx, ty in cells):
                    continue
                placed = False
                for rect in scmap.townhall_rects(origin_x, origin_y):
                    if any(cell in blocked or not (0 <= cell[0] < width and 0 <= cell[1] < height)
                           for cell in rect):
                        continue
                    for x, y in rect:
                        strokes.append((x - x % 2, y, terrain_id, 1))
                    placed = True
                    break
                if placed:
                    break
            else:
                continue
            break
    if strokes:
        cli.isom_batch(strokes)
    if moved or strokes:
        print(f"  불법 발자국 {moved}기를 옮기고 isometric {len(strokes)}붓을 쳤습니다")
    return moved + len(strokes)


def restore_illegal_floors(cli, tileset_id: int, low_terrain: int,
                           width: int, height: int) -> None:
    """자원·건물·크리쳐 발자국이 불법이면 옮기거나 isometric으로 다시 칠한다."""
    items = _placed_items(cli)
    _paint_floor_under(cli, tileset_id, width, height, items, low_terrain)


def assert_placed_terrain(cli, tileset_id: int, width: int, height: int) -> None:
    """저장 직전에 자원·크리쳐·건물이 불법 칸에 남아 있으면 맵을 내지 않는다."""
    import verify_map
    prop_at = _terrain_prop(cli, tileset_id, width, height)
    items = _placed_items(cli)
    faults = verify_map.placement_faults(items, prop_at)
    if faults:
        raise CliError("지형 검사를 통과하지 못한 배치: " + ", ".join(faults[:4]))
    resources = [item for item in items if item["role"] == "resource"]
    if resources:
        clusters = verify_map._resource_clusters(resources)
        main_centers = [(u["x"] // 32, u["y"] // 32) for u in cli.units()
                        if u["type"] == scmap.START_LOCATION]
        base = verify_map.melee_base_faults(clusters, prop_at, main_centers)
        if base:
            raise CliError("기지·애드온 자리 검사 실패: " + ", ".join(base[:3]))


def place_legal_critters(cli, tileset_id: int, count: int, unit: str,
                         width: int, height: int, avoid: list[tuple[int, int]]) -> None:
    """걸을 수 있는 칸에만 중립 크리쳐를 놓는다."""
    if count <= 0:
        return
    props = scmap.tileset_tiles(cli, tileset_id)
    grid = cli.tiles(0, 0, width, height)
    rng = random.Random(0xC17)
    placed = 0
    avoid_set = set(avoid)
    for _ in range(count * 400):
        if placed >= count:
            break
        tx, ty = rng.randrange(4, width - 4), rng.randrange(4, height - 4)
        if any(max(abs(tx - ax), abs(ty - ay)) <= 6 for ax, ay in avoid_set):
            continue
        prop = props.get(grid[ty][tx])
        if not prop or not prop[1]:
            continue
        cli.place(unit, tx, ty, owner=12)
        avoid_set.add((tx, ty))
        placed += 1
    print(f"  중립 크리쳐 {placed}/{count}개 배치: {unit}")
    if placed != count:
        raise CliError(f"요청한 중립 크리쳐 {count}개 중 {placed}개만 걸을 수 있는 칸에 놓였습니다")


def _restamp_broken_ramps(cli, ramp_at):
    """타일과 어긋난 램프 두대드를 지우고 같은 자리에 다시 찍는다.

    램프 옆을 다른 붓이 지나가면 램프 밑 타일이 바뀌어 `doodad check`가
    어긋남으로 잡는다. 그걸 그냥 지우면 램프째 사라져 본진이 갇히므로,
    두대드만 다시 찍어 타일과 맞춘다.
    """
    proc = subprocess.run(
        [cli.cli, "doodad", "check", cli.path, "--install", cli.install],
        capture_output=True, text=True)
    broken = [int(m.group(1)) for line in proc.stdout.splitlines()
              if (m := re.match(r"^\s*(\d+)\s+타일", line))]
    if os.environ.get("ELEV_DEBUG"):
        print(f"    [ramp-restamp] broken={broken} ramp_at={dict(ramp_at)} out={proc.stdout[-120:]!r}")
    if not broken:
        return 0
    # `doodad list` 의 x,y 는 왼위가 아니라 **중심**(왼위 + 폭/2, 높이/2)이다.
    size = {(rx + rw // 2, ry + rh // 2): (rw, rh, rx, ry)
            for (rx, ry, _d, rw, rh) in ramp_at.values()}
    by_index = {d["index"]: d for d in cli.doodads()}
    fixed = 0
    for idx in sorted(broken, reverse=True):
        d = by_index.get(idx)
        if not d or (d["x"], d["y"]) not in size:
            continue
        rw, rh, rx, ry = size[(d["x"], d["y"])]
        cli.edit("doodad", "remove", cli.path, str(idx), "--install", cli.install)
        scmap.place_doodad(cli, d["id"], rx, ry, rw, rh)
        fixed += 1
    return fixed


def _conn_debug(cli, tileset_id, width, height, start0, label):
    """디버그 전용 — base1에서 미니타일 걷기로 도달 가능한 칸 수를 잰다."""
    if not os.environ.get("CONN_DEBUG"):
        return
    from collections import deque
    g = scmap.walk_grid(cli, tileset_id, 0, 0, width, height)
    p0 = scmap.nearest_walkable(g, start0[0]*4+2, start0[1]*4+2, radius=48)
    if p0 is None:
        print(f"  [conn-dbg] {label}: base1 근처 걸을 칸 없음")
        return
    seen = {p0}; q = deque([p0])
    H, W = len(g), len(g[0])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            nx, ny = x+dx, y+dy
            if 0<=nx<W and 0<=ny<H and g[ny][nx] and (nx,ny) not in seen:
                seen.add((nx,ny)); q.append((nx,ny))
    print(f"  [conn-dbg] {label}: base1 도달가능 미니타일수={len(seen)} (전체 {W*H})")


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
    ap.add_argument("--main-entry", choices=["ramp", "flat", "bridge"],
                    default="ramp",
                    help="(기본 ramp) 본진(고지대)-앞마당(평지) 사이 입구. "
                         "고리로 막지 않는다 — 램프 자체가 좁은 통로다")
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
    ap.add_argument("--doodads", type=int, default=None,
                    help="배치할 두대드 수. 지형·진입로·자원 접근을 확인하고 명시한다")
    ap.add_argument("--critters", type=int, default=0,
                    help="중립 크리쳐 수 (0~스타팅 수)")
    ap.add_argument("--critter-unit", default=None,
                    help="AI가 고른 중립 크리쳐 유닛명; --critters가 0보다 클 때 필요")
    ap.add_argument("--max-unbuildable-pct", type=int, default=30,
                    help="맵 전체 걷기 가능·건축 불가 지형 비율 상한 (0~45%%)")
    ap.add_argument("--seed", type=int, default=1, help="두대드 자리 난수 씨앗")
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
    # 다리 템플릿은 본진과 다리 사이에 앞마당을 두지 못한다.
    # 앞마당·가스·좁은 입구가 필요하므로 그 템플릿 대신 평지 고리를 쓴다.
    if args.main_entry == "bridge":
        print("  다리 템플릿은 앞마당을 넣을 수 없어 좁은 고리 입구로 바꿉니다.")
        args.main_entry = "flat"
    if args.main_gas < 1:
        args.main_gas = 1
    if args.natural_minerals < 1:
        args.natural_minerals = max(6, args.main_minerals - 1)
    if args.natural_gas < 1:
        args.natural_gas = 1
    if args.expansions and args.expansion_minerals < 1:
        args.expansion_minerals = 6
    if args.expansions and args.expansion_gas < 1:
        args.expansion_gas = 1
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

    # Plan 단계 반경 게이트 — 지형을 한 칸도 칠하기 전에 확인한다.
    # scmap.MIN_BASE_RADIUS 는 기지 4×3+애드온 2×2 가 겹치지 않는
    # 절대 하한이다. 이 밑으로 자리를 잡을 args.inset/args.natural_distance
    # 는 지형을 아무리 잘 그려도 애드온이 원천적으로 안 들어가므로,
    # 뒤에서 스타팅 좌표를 계산하기 전에 바로 거절한다.
    if args.inset < scmap.MIN_BASE_RADIUS:
        ap.error(f"--inset {args.inset}는 기지+애드온 최소 반경 "
                f"{scmap.MIN_BASE_RADIUS}칸보다 좁아 자리를 잡을 수 없습니다")
    if args.natural_distance < scmap.MIN_BASE_RADIUS * 2:
        ap.error(f"--natural-distance {args.natural_distance}는 본진·앞마당 두 최소 반경"
                f"({scmap.MIN_BASE_RADIUS}칸씩)을 합친 값보다 짧아 자리가 겹칩니다")
    print(f"  기지 자리 반경: 최소(기지+애드온) {scmap.MIN_BASE_RADIUS}칸, "
          f"목표(공식 밀리맵 실측) {scmap.TARGET_BASE_RADIUS}칸 — "
          f"본진 inset {args.inset}칸, 앞마당 거리 {args.natural_distance}칸으로 계획합니다")

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
        _conn_debug(cli, tileset_id, width, height, starts[0], "recolor_floor 직후")

    # 3) 스타팅 표시와 본진 자원
    # Plan(순수 기하) — 이미 확정된 `starts`를 그대로 쓴다. 지형에서
    # "맞는 자리를 찾는" 나선 탐색을 하지 않는다. 대신 이 앵커가 차지할
    # 칸(자원 9덩이+가스+기지 4×3+애드온 2×2)을 `scmap.base_pad_cells`로
    # 좌표만으로 계산해 Plan 데이터로 들고 있다가, 장식 지형을 다 칠한
    # 뒤 그 칸만 isom 붓으로 확정 도장을 찍는다.
    cx_mid, cy_mid = (width - 1) / 2.0, (height - 1) / 2.0
    original_starts = starts[:]
    main_pad_elev = high_terrain if (args.plateau and not args.no_plateau) else low_terrain
    main_pads = []
    for (sx, sy) in original_starts:
        out_x = -1 if sx <= cx_mid else 1
        out_y = -1 if sy <= cy_mid else 1
        main_pads.append(scmap.base_pad_cells(sx, sy, args.main_minerals,
                                              args.main_gas, out_x, out_y))
    main_pad_cells = {c for pad in main_pads for c in pad}
    print("본진 자리를 확정 도장으로 굳힙니다...")
    guarantee = [(x - x % 2, y, main_pad_elev, 1) for (x, y) in main_pad_cells
                if 0 <= x < width and 0 <= y < height]
    cli.isom_batch(guarantee)
    if os.environ.get("ELEV_DEBUG"):
        chk_props = scmap.tileset_tiles(cli, tileset_id)
        for (sx, sy) in original_starts:
            tid = cli.tiles(sx, sy, 1, 1)[0][0]
            print(f"  [elev-dbg] 확정도장 직후 ({sx},{sy}) tid={tid:#06x} prop={chk_props.get(tid)}")
    starts = original_starts
    print("본진 자원을 놓습니다...")
    for i, (sx, sy) in enumerate(starts):
        out_x = -1 if sx <= cx_mid else 1
        out_y = -1 if sy <= cy_mid else 1
        _main_placed, _skip = scmap.place_base(cli, sx, sy, owner=i + 1,
                         minerals=args.main_minerals, gas=args.main_gas,
                         out_x=out_x, out_y=out_y, width=width, height=height,
                         tileset_id=tileset_id)
        if _skip:
            raise CliError(f"본진 {i+1} 자원 footprint가 지형/건물 조건을 통과하지 못했습니다: {_skip[:3]}")
    if os.environ.get("ELEV_DEBUG"):
        chk_props = scmap.tileset_tiles(cli, tileset_id)
        for (sx, sy) in starts:
            tid = cli.tiles(sx, sy, 1, 1)[0][0]
            print(f"  [elev-dbg] 자원 배치 직후 ({sx},{sy}) tid={tid:#06x} prop={chk_props.get(tid)}")
    _conn_debug(cli, tileset_id, width, height, starts[0], "본진 자원 배치 직후")

    # 4) 앞마당 — 본진에서 가운데 쪽으로 한 걸음.
    nat_placed = []
    natural_sites_coords = []
    nat_pad_cells: set[tuple[int, int]] = set()
    if args.natural_minerals > 0:
        print("앞마당을 놓습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        # 정면(맵 중심)으로만 두면 3인 앞마당이 가운데에서 한 덩이가 된다.
        # 모든 스타팅에 같은 부호 각도를 더하면 회전 대칭은 유지된다.
        # 자원이 다 들어가는 자리를 고르고, 절벽이라 빠진 개수로 나머지
        # 앞마당을 깎지 않는다. 한 자리의 짧은 거리로 맞추지도 않는다.
        def natural_sites(ang: float, extra: int):
            """앞마당 후보를 순수 기하로만 고른다 — 지형을 한 칸도 보지 않는다.

            좌표는 본진 각·거리에서 바로 계산하고, 서로/본진/맵 가장자리와
            `scmap.TARGET_BASE_RADIUS`만큼 떨어졌는지만 확인한다. 실제
            건설 가능은 뒤에서 이 칸에 확정 도장을 찍어 보장된다 —
            여기서 지형을 뒤져 맞는 자리를 찾지 않는다.
            """
            sites = []
            margin = scmap.TARGET_BASE_RADIUS + 1
            for (sx, sy) in starts:
                base = math.atan2(cy - sy, cx - sx) + ang
                dist = args.natural_distance + extra
                tx = int(round(sx + dist * math.cos(base)))
                ty = int(round(sy + dist * math.sin(base)))
                if not (margin <= tx < width - margin and margin <= ty < height - margin):
                    return None
                if math.hypot(tx - sx, ty - sy) < scmap.TARGET_BASE_RADIUS * 2:
                    return None
                ox = 1 if tx >= sx else -1
                oy = 1 if ty >= sy else -1
                sites.append((tx, ty, ox, oy))
            for i in range(len(sites)):
                for j in range(i + 1, len(sites)):
                    if math.hypot(sites[i][0] - sites[j][0],
                                  sites[i][1] - sites[j][1]) < scmap.TARGET_BASE_RADIUS * 2:
                        return None
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
                for ty in range(max(4,ny-18),min(height-4,ny+19)):
                    for tx in range(max(4,nx-18),min(width-4,nx+19),2):
                        dx,dy=tx-nx,ty-ny
                        d=math.hypot(dx,dy)
                        da=abs((math.atan2(dy,dx)-gate+math.pi)%(2*math.pi)-math.pi)
                        # 13~16칸: `verify_map.choke_faults`가 실제로 재는
                        # 반지름 창(6~17)에 들어와야 좁은 입구로 인정된다.
                        # 이전 18~21은 그 창 밖이라 아무리 막아도 "안 좁음"
                        # 판정을 받았다 — 실제로 겪은 버그다.
                        if 13 <= d <= 16 and da > 0.42:
                            rim.append((tx,ty,high_terrain,1))
            cli.isom_batch(rim)
            gates=[]
            for i,(nx,ny,_ox,_oy) in enumerate(sites):
                sx,sy=starts[i]
                vx,vy=sx-nx,sy-ny
                length=math.hypot(vx,vy) or 1.0
                ux,uy=vx/length,vy/length
                for d in range(11,19):
                    px,py=int(round(nx+ux*d)),int(round(ny+uy*d))
                    for off in range(-5,6,2):
                        tx,ty=(px,py+off) if abs(ux)>abs(uy) else (px+off,py)
                        if 2<=tx<width-2 and 2<=ty<height-2:
                            gates.append((tx-tx%2,ty,low_terrain,1))
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

        # 확정 도장 — 앞마당 앵커가 실제로 자원 9덩이+가스+기지+애드온을
        # 받아들이도록, 좌표만으로 계산한 칸에 마지막으로 저지대를 굳힌다.
        nat_pad_cells: set[tuple[int, int]] = set()
        for (nx, ny, ox, oy) in sites:
            nat_pad_cells.update(scmap.base_pad_cells(
                nx, ny, args.natural_minerals, args.natural_gas, ox, oy))
        nat_guarantee = [(x - x % 2, y, low_terrain, 1) for (x, y) in nat_pad_cells
                        if 0 <= x < width and 0 <= y < height]
        cli.isom_batch(nat_guarantee)

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

    _conn_debug(cli, tileset_id, width, height, starts[0], "앞마당 배치 직후")

    # 5) 바깥 멀티 — 스타팅마다 같은 상대 위치에 놓아 대칭을 지킨다.
    exp_pts: list[tuple[int, int]] = []
    exp_pad_cells: set[tuple[int, int]] = set()
    if args.expansions > 0:
        print(f"바깥 멀티 {args.expansions}곳씩 놓습니다...")
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        # 스타팅에서 가운데를 보는 방향을 기준으로, 좌우로 벌려 놓는다.
        spread = [(-50, 1.55), (50, 1.55), (0, 2.1), (-70, 2.4), (70, 2.4)]
        # 한 슬롯의 각·거리는 스타팅 전부에 같이 적용한다. 일부만 놓으면
        # 4인 두 번째 멀티처럼 대칭이 깨지고, 못 놓는 자리는 빠진다.
        # 각 본진에서는 앞마당보다 멀어야 그 앞마당으로 집계되지 않는다.
        far = args.natural_distance + 4
        # Plan — 순수 기하로 후보를 고르고 서로/본진/앞마당과의 최소 간격만
        # 확인한다. 지형을 뒤져 실제로 자원이 들어갈 자리를 "찾지" 않는다.
        SPACING = scmap.TARGET_BASE_RADIUS * 2
        snap_why = {"edge": 0, "near": 0, "own": 0, "nat": 0, "exp": 0, "ok": 0}

        def snap_expansion(ex, ey, sx, sy):
            margin = scmap.TARGET_BASE_RADIUS + 1
            if not (margin <= ex < width - margin and margin <= ey < height - margin):
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
            # 자원 줄은 짧게(pack) 본진 반대편에 둔다.
            eox = 1 if ex >= sx else -1
            eoy = 1 if ey >= sy else -1
            if any(math.hypot(ex - nx, ey - ny) < SPACING
                   for (nx, ny, _c, _s) in nat_placed):
                snap_why["nat"] += 1
                return None
            if any(math.hypot(ex - px, ey - py) < SPACING for (px, py) in exp_pts):
                snap_why["exp"] += 1
                return None
            snap_why["ok"] += 1
            return ex, ey, eox, eoy

        for k in range(args.expansions):
            deg0, scale = spread[k % len(spread)]
            chosen = None
            sx0, sy0 = starts[0]
            for key in snap_why:
                snap_why[key] = 0

            # 첫 스타팅에서 자리를 고른 뒤 실제 좌표를 회전한다. 각 시작지에서
            # 독립으로 고르면 반올림 한두 타일 차이로 바깥 멀티가 비대칭이
            # 된다. 회전한 좌표가 모든 시작지에서 유효하지 않으면 다음
            # 후보를 본다 — 지형은 아직 한 칸도 보지 않는다.
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
                    spots = [((starts[0][0], starts[0][1]), first)]
                    for i in range(1, len(starts)):
                        sx, sy = starts[i]
                        ix, iy = (int(round(v)) for v in images[i])
                        hit = snap_expansion(ix, iy, sx, sy)
                        if hit is None:
                            spots = []
                            break
                        spots.append(((sx, sy), hit))
                    if len(spots) != len(starts):
                        continue
                    pts = [(ax, ay) for (_s, (ax, ay, _ox, _oy)) in spots]
                    if any(math.hypot(pts[a][0] - pts[b][0],
                                      pts[a][1] - pts[b][1]) < SPACING
                           for a in range(len(pts)) for b in range(a)):
                        continue
                    chosen = spots
                    break
            if not chosen:
                raise CliError(f"요청한 바깥 멀티 {k+1}/{args.expansions} 링을 모든 시작지에 공평하게 놓을 자리가 없습니다. snap {snap_why}. 맵 크기/멀티 수를 조정하세요.")

            # Terrain — 확정한 좌표에 확정 도장을 찍어 반드시 건설 가능하게
            # 만든 다음에야 자원을 놓는다.
            ring_pads = set()
            for (_s, (ax, ay, eox, eoy)) in chosen:
                ring_pads.update(scmap.base_pad_cells(
                    ax, ay, args.expansion_minerals, args.expansion_gas,
                    eox, eoy, pack=0.875))
            exp_pad_cells.update(ring_pads)
            exp_guarantee = [(x - x % 2, y, low_terrain, 1) for (x, y) in ring_pads
                            if 0 <= x < width and 0 <= y < height]
            cli.isom_batch(exp_guarantee)
            for (sx, sy), (ax, ay, eox, eoy) in chosen:
                _placed, _skip = scmap.place_base(
                    cli, ax, ay, owner=12,
                    minerals=args.expansion_minerals, gas=args.expansion_gas,
                    out_x=eox, out_y=eoy,
                    facing=quadrant_facing(sx, sy, width, height),
                    width=width, height=height,
                    start_location=False, tileset_id=tileset_id, pack=0.875)
                if len(_placed) < args.expansion_minerals + args.expansion_gas:
                    raise CliError(
                        f"바깥 멀티 {k+1} 확정 도장 이후에도 자원 배치가 실패했습니다: "
                        f"{_skip[:3]} — base_pad_cells 계산 버그입니다.")
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

    # 7) 지형지물 — 두대드는 의도적으로 지정된 경우에만 검토한다.
    #
    # **세 가지를 지킨다. 셋 다 지적받고 고친 것이다.**
    #
    # (가) **자원 둘레에는 놓지 않는다.** 본진 미네랄 옆에 바위가 끼면
    #      일꾼 동선이 막히고 눈에 거슬린다. 자원·스타팅 둘레 다섯 칸을
    #      비운다.
    # (나) **그 자리 지형에 맞는 갈래만 쓴다.** 두대드 갈래 이름은 지형
    #      종류 이름과 같다 (High Dirt·Grass·Rocky Ground…). 흙 두대드를
    #      고지대에 올리면 안 어울린다.
    # (다) **걷기를 막는 것은 미리 걸러 낸다.** `data/doodad-walk.json`
    #      에 타일셋마다 재 두었다.
    if args.doodads is None:
        # 두대드는 경로·시야·자원 접근을 바꾸므로 자동 밀도 추정으로 배치하지 않는다.
        # 사용자가 목적에 맞는 수를 지정할 때만 배치한다.
        args.doodads = 0
    if args.doodads > 0:
        print(f"두대드를 놓습니다 (목표 {args.doodads}개)...")
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
            print("  놓을 만한 두대드가 없어 건너뜁니다")
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
                # 두대드를 고른다. 자리들의 지형 이름이 다르다고 세트 전체를
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
                        # 두대드는 **가운데 기준**으로 놓인다 — 왼위로 주면
                        # 제 크기의 절반만큼 밀린다
                        scmap.place_doodad(cli, d["id"], px, py,
                                           d["w"], d["h"])
                        occupied.update(foot)
                        placed += 1
                    except CliError:
                        break
            print(f"  두대드 {placed}개 (자원 둘레라 건너뜀 {skipped_res}, "
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
        pattern_path=os.path.normpath(os.path.join(os.path.dirname(__file__),"..","..","..","..","tools","mapgen","data","bridge-patterns.json"))
        with open(pattern_path,encoding="utf-8") as f:
            bridge_patterns={p["doodad_id"]:p for p in json.load(f)["patterns"]}

    if not args.no_plateau:
        cx0,cy0=(width-1)/2,(height-1)/2
        for _start_i,(sx,sy) in enumerate(starts):
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
            # 램프 자리는 여기서 다시 찾지 않는다 — Plan 단계
            # (`melee_shape.design_lanes`)가 이미 `reserve_ramp`로 이
            # 시작의 램프 칸을 얼려 두었고, 그 좌표·방향이 `shape.ramps`에
            # 그대로 있다. Implement 단계는 그 계획을 그대로 실행만 한다.
            ramp_placed = None
            ramp_direction = direction
            if args.main_entry == "ramp":
                planned = shape.ramps[_start_i] if shape is not None else None
                if planned is None:
                    raise CliError(f"본진 ({sx},{sy})은 Plan 단계에서 램프 자리를 "
                                   f"얼리지 못했습니다(가장자리에 너무 가깝습니다).")
                rtx, rty, ramp_direction = planned
                if os.environ.get("ELEV_DEBUG"):
                    print(f"    [elev-dbg] 램프계획 start=({sx},{sy}) rtx={rtx} rty={rty} dir={ramp_direction}")
                horizontal_r = ramp_direction in ("left", "right")
                edge = rtx if horizontal_r else rty
                fixed = rty if horizontal_r else rtx
                # 붙이기 직전, `reserve_ramp`가 Plan 단계에서 얼렸던 것과
                # 같은 자리를 순정 고지/저지로 다시 확정 도장한다. 이걸
                # 빼 봤더니(더 "이론적으로 맞는" 접근이라 시도했다) 원래
                # 되던 조합까지 전부 램프가 안 붙었다 — 정확한 이유는
                # 못 밝혔지만, 실제로 이게 있어야 붙는다는 것만 확인했다.
                band = 16
                # 교차축 띠는 rtx/rty 하나만 기준으로 재지 않는다 — 각도로
                # 계산한 램프 앵커는 시작 지점(sx,sy)과 같은 줄에 있다는
                # 보장이 없다(예: (25,102)처럼 13칸 차이 나는 경우가 실제로
                # 있었다). 시작 지점까지 반드시 덮도록 띠를 늘린다 —
                # 안 늘리면 그 시작 지점 자체가 이 확정 도장 밖에 남아
                # 뒤에 다른 저지 변종으로 덮인다(실제로 겪은 버그다).
                if horizontal_r:
                    cross0 = min(sy, rty) - 8
                    cross_len = abs(sy - rty) + 16
                else:
                    cross0 = min(sx, rtx) - 8
                    cross_len = abs(sx - rtx) + 16
                if ramp_direction == "right":
                    hi_rect = (rtx-band, cross0, band, cross_len)
                    lo_rect = (rtx+6, cross0, band, cross_len)
                elif ramp_direction == "left":
                    hi_rect = (rtx+6, cross0, band, cross_len)
                    lo_rect = (rtx-band, cross0, band, cross_len)
                elif ramp_direction == "down":
                    hi_rect = (cross0, rty-band, cross_len, band)
                    lo_rect = (cross0, rty+6, cross_len, band)
                else:  # up
                    hi_rect = (cross0, rty+6, cross_len, band)
                    lo_rect = (cross0, rty-band, cross_len, band)
                clean = []
                for (rx0,ry0,rw,rh),terrain_id in ((hi_rect,high_terrain),(lo_rect,low_terrain)):
                    for yy in range(max(0,ry0),min(height,ry0+rh)):
                        for xx in range(max(0,rx0),min(width,rx0+rw),2):
                            clean.append((xx,yy,terrain_id,1))
                cli.isom_batch(clean)
                high_point=(sx, sy)
                low_point=(int(round(sx+ux*20)), int(round(sy+uy*20)))
                ramp_placed=scmap.place_ramp_checked(
                    cli,tileset_id,edge,fixed,high_point,low_point,ramp_direction,
                    candidates=[r for r in scmap.ramp_candidates(tileset_id,ramp_direction)
                                if r.get("walks") and r.get("kind")=="Cliff"
                                and r.get("walks_with")==high_name],
                    shifts=range(-8,9), avoid=main_pad_cells)
            if ramp_placed:
                did,rx,ry=ramp_placed
                meta=next(r for r in scmap.ramp_candidates(tileset_id,ramp_direction) if r["id"]==did)
                ramp_at[(sx,sy)]=(rx,ry,ramp_direction,meta["w"],meta["h"])
                print(f"  본진 ({sx},{sy}) {ramp_direction} 램프 {did} 배치 및 국소 보행 확인")
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
                raise CliError(f"본진 ({sx},{sy})에 검증된 램프를 놓을 수 없습니다.")
    if os.environ.get("ELEV_DEBUG"):
        chk_props = scmap.tileset_tiles(cli, tileset_id)
        for (sx, sy) in starts:
            tid = cli.tiles(sx, sy, 1, 1)[0][0]
            print(f"  [elev-dbg] 램프 배치 직후 ({sx},{sy}) tid={tid:#06x} prop={chk_props.get(tid)}")
    _conn_debug(cli, tileset_id, width, height, starts[0], "램프 배치 직후")

    # Reopen each natural-pocket gate after center features, water, and doodad
    # decoration have been laid. Its endpoint is the nearest main ramp/causeway;
    # verify the tile-level walk graph instead of assuming the painted line works.
    if natural_sites_coords and not args.no_plateau:
        # **본진 좌표까지 그대로 곧장 긋는다.** 램프 출구만 겨냥해 예약칸을
        # 지키는 방식도 시도해 봤는데, 이 넓은 통로(자연 지점→본진, 폭
        # 21칸)가 실제로는 지도 전체 연결성의 유일한 버팀목이었다 — 뒤의
        # "8) 연결성 복구" `carve`는 이것 없이는 스스로 못 뚫는다(재시도
        # 4번이 매번 똑같은 칸만 찍고 진전이 없는 것으로 실제 확인했다).
        # 본진이 저지대로 남는 건 감수하고, 뒤에서 예약칸만 다시 확정
        # 도장을 찍어 복구를 시도한다.
        pocket_cuts=[]
        for i,(nx,ny) in enumerate(natural_sites_coords):
            sx,sy=starts[i]
            # 통로는 램프 출구에서 끝낸다. 본진 좌표까지 관통시키면 램프
            # 두대드와 본진 언덕이 저지대로 덮여 본진이 평지가 된다.
            spot=ramp_at.get((sx,sy))
            keep_ramp=set()
            tgt_x,tgt_y,over=sx,sy,5
            if spot:
                rx,ry,rdir,rw,rh=spot
                tgt_x,tgt_y=rx+rw//2,ry+rh//2
                if rdir=="down":    tgt_y=ry+rh+1
                elif rdir=="up":    tgt_y=ry-2
                elif rdir=="right": tgt_x=rx+rw+1
                else:               tgt_x=rx-2
                over=0
                keep_ramp={(x,y) for x in range(rx-2,rx+rw+2) for y in range(ry-2,ry+rh+2)}
            vx,vy=tgt_x-nx,tgt_y-ny
            length=math.hypot(vx,vy) or 1.0
            ux,uy=vx/length,vy/length
            for d in range(0,int(length)+over):
                px,py=int(round(nx+ux*d)),int(round(ny+uy*d))
                for off in range(-10,11):
                    tx,ty=int(round(px-uy*off)),int(round(py+ux*off))
                    if 2<=tx<width-2 and 2<=ty<height-2:
                        ex=tx-tx%2
                        if spot:
                            if (ex,ty) in keep_ramp or (ex+1,ty) in keep_ramp:
                                continue
                            if (ex,ty) in main_pad_cells or (ex+1,ty) in main_pad_cells:
                                continue
                            if math.hypot(tx-sx,ty-sy)<=15:
                                continue
                        pocket_cuts.append((ex,ty,low_terrain))
        cli.isom_batch(pocket_cuts)

    # **실제로 확인한 사실**: 이 지형에서 전체 지도를 잇는 유일한 통로가
    # 본진 예약칸 그 자체를 지나가는 경우가 있다(연결성 복구 전에
    # 본진을 먼저 고지대로 막아 두면 carve를 아무리 넓혀도 연결이
    # 전혀 안 됐다). 그래서 여기서 미리 막지 않는다 — 연결이 확정된
    # 뒤에 본진만 되돌리는 시도를 한다(아래).

    # 8) 연결성 복구 — 갇힌 본진이 있으면 길을 낸다.
    #
    # 램프가 통해도 그 바깥을 지형 덩이가 막으면 본진이 섬이 된다.
    # 램프 하나만 보지 말고 **맵 전체에서** 스타팅끼리 닿는지 봐야 한다.
    if os.environ.get("ELEV_DEBUG"):
        chk_props_pk = scmap.tileset_tiles(cli, tileset_id)
        for (sx, sy) in starts:
            tid = cli.tiles(sx, sy, 1, 1)[0][0]
            print(f"  [elev-dbg] pocket_cuts 후 ({sx},{sy}) tid={tid:#06x} prop={chk_props_pk.get(tid)}")
    _conn_debug(cli, tileset_id, width, height, starts[0], "pocket_cuts 후")
    _n = _restamp_broken_ramps(cli, ramp_at)
    if _n:
        print(f"  통로를 낸 뒤 어긋난 램프 {_n}개를 다시 찍었습니다")
    print("연결성을 확인합니다...")
    if os.environ.get("SNAP_DIR"):
        import shutil
        shutil.copy(cli.path, os.path.join(os.environ["SNAP_DIR"], "precarve.scx"))
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
        for _si, (sx, sy) in enumerate(starts):
            # 이미 이어진 본진은 건드리지 않는다 — 전부 다시 파면 멀쩡하던
            # 램프 주변까지 깨져 [갇힘] 이 오히려 늘었다(실측).
            if _si != 0 and _si not in bad:
                continue
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
        if os.environ.get("ELEV_DEBUG"):
            chk_props5 = scmap.tileset_tiles(cli, tileset_id)
            for (sx, sy) in starts:
                tid = cli.tiles(sx, sy, 1, 1)[0][0]
                print(f"  [elev-dbg] carve(시도{attempt}) 후 ({sx},{sy}) tid={tid:#06x} prop={chk_props5.get(tid)}")

    # 길을 내며 덮인 장식은 그림이 칸과 어긋난다. 램프는 남긴다.
    if args.doodads > 0 or bridge_at:
        proc = subprocess.run(
            [cli.cli, "doodad", "check", cli.path, "--install", cli.install],
            capture_output=True, text=True)
        broken = [int(m.group(1)) for line in proc.stdout.splitlines()
                  if (m := re.match(r"^\s*(\d+)\s+타일", line))]
        if bridge_at and broken:
            raise CliError(f"Bridge/두대드 정렬 검사가 {len(broken)}개 오류를 찾았습니다")
        if broken:
            ramp_ids = {e["id"] for direction in ("left", "right", "up", "down")
                        for e in scmap.ramp_candidates(tileset_id, direction)}
            by_index = {d["index"]: d["id"] for d in cli.doodads()}
            drop = [i for i in broken if by_index.get(i) not in ramp_ids]
            for idx in sorted(drop, reverse=True):
                cli.edit("doodad", "remove", cli.path, str(idx),
                         "--install", cli.install)
            if drop:
                print(f"  칸과 어긋난 두대드 {len(drop)}개를 뺐습니다")

    # 램프·길이 자원 발자국을 못 짓게 만들면, 한 장 타일로 덮지 않는다.
    # 같은 높이의 합법 칸으로 옮기거나 isometric 붓으로 다시 칠한다.
    _paint_floor_under(cli, tileset_id, width, height, _placed_items(cli), low_terrain)
    print("  짓기 불가 자원 칸 terrain set 복구 0")

    # 램프 양옆 절벽에는 코퍼스의 걷는 절벽 두대드를 칸마다 붙인다.
    # 램프 두대드와 절벽 선이 떨어져 보이지 않게 하는 자리다.
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

    # The fight-zone terrain is applied after entrance and decorative DD2s.
    # Confirm their rendered cells still match; remove only optional decor.
    if cli.doodads():
        _n = _restamp_broken_ramps(cli, ramp_at)
        if _n:
            print(f"  마무리 전에 어긋난 램프 {_n}개를 다시 찍었습니다")
        proc=subprocess.run([cli.cli,"doodad","check",cli.path,"--install",cli.install],
                            capture_output=True,text=True)
        broken=[int(m.group(1)) for line in proc.stdout.splitlines()
                if (m:=re.match(r"^\s*(\d+)\s+타일",line))]
        if broken:
            # 교전 지형이 입구 램프 정렬을 깨면 그 두대드를 빼고, 고리 입구가 길을 남긴다.
            for idx in sorted(broken, reverse=True):
                cli.edit("doodad", "remove", cli.path, str(idx), "--install", cli.install)
            checked=subprocess.run([cli.cli,"doodad","check",cli.path,"--install",cli.install],
                                   capture_output=True,text=True)
            if checked.returncode:
                raise CliError("장식 두대드를 제거한 뒤에도 doodad check가 통과하지 않았습니다")

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

    if os.environ.get("SNAP_DIR"):
        import shutil
        shutil.copy(cli.path, os.path.join(os.environ["SNAP_DIR"], "prefinal.scx"))
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
                               if d["x"] == rx + _rw // 2 and d["y"] == ry + _rh // 2), None)
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
                for off in range(-1, 2):
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

    # 여기까지 오는 동안(자연 게이트 재개통·연결성 복구 폴백이 몇 차례고
    # 지나가며) 본진 자리 자체가 저지대로 깎였을 수 있다 — 실제로 겪은
    # 사고다("본진 뒤편이 아니라 본진 자체가 고지대여야 한다"). 연결이
    # 이미 확인된 지금, 본진 예약 칸만 다시 고지대로 확정 도장하고 한
    # 번 더 연결을 검사한다. **이 지형에서는 지도 전체를 잇는 유일한
    # 통로가 본진 예약칸 자체를 지나가는 경우가 실제로 있다** — 그럴
    # 때만(드물지 않다) 이 도장이 방금 뚫어 둔 길목을 막으므로, isom
    # 붓으로 반대 방향(저지대)으로 다시 찍어 되돌리고 조용히 넘어가지
    # 않고 알린다.
    if main_pad_elev == high_terrain:
        guarantee2 = [(x - x % 2, y, high_terrain, 1) for (x, y) in main_pad_cells
                     if 0 <= x < width and 0 <= y < height]
        cli.isom_batch(guarantee2)
        check_grid = scmap.walk_grid(cli, tileset_id, 0, 0, width, height)
        check_pts = [scmap.nearest_walkable(check_grid, sx * 4 + 2, sy * 4 + 2, radius=8)
                    for sx, sy in starts]
        if any(p is None for p in check_pts) or any(
                not scmap.walk_reachable(check_grid, check_pts[0], p) for p in check_pts[1:]):
            undo = [(x - x % 2, y, low_terrain, 1) for (x, y) in main_pad_cells
                   if 0 <= x < width and 0 <= y < height]
            cli.isom_batch(undo)
            print("  [주의] 본진 언덕을 다시 확정하면 연결이 끊겨 이번엔 포기합니다"
                  "(본진이 저지대로 남습니다 — 지형/입구 선택을 바꿔 다시 만드는 편이 낫습니다)")
        else:
            print("  본진 언덕을 다시 확정했습니다")

    # 입구를 고리로 막지 않는다. 본진은 고지대, 앞마당은 평지이고 그
    # 둘 사이에 놓은 램프 자체가 좁은 통로다 — 그 위에 또 벽 고리를
    # 두르는 건 이중 잠금이자 `why-procedural-fails.md`가 지적한 "지어낸
    # 벽"이다. `site_points`는 본진·앞마당 좌표로, 아래 좁은 입구 검사
    # 범위를 여기로만 좁히는 데 쓴다(바깥 멀티는 원래 트여 있다).
    site_points = [(int(sx), int(sy)) for sx, sy in starts]
    site_points += [(int(nx), int(ny)) for nx, ny, _c, _s in nat_placed]
    if os.environ.get("ELEV_DEBUG"):
        chk_props3 = scmap.tileset_tiles(cli, tileset_id)
        for (sx, sy) in starts:
            tid = cli.tiles(sx, sy, 1, 1)[0][0]
            print(f"  [elev-dbg] restore_illegal_floors 전 ({sx},{sy}) tid={tid:#06x} prop={chk_props3.get(tid)}")
    print("자원 발자국을 다시 맞춥니다...")
    restore_illegal_floors(cli, tileset_id, low_terrain, width, height)
    if os.environ.get("ELEV_DEBUG"):
        chk_props4 = scmap.tileset_tiles(cli, tileset_id)
        for (sx, sy) in starts:
            tid = cli.tiles(sx, sy, 1, 1)[0][0]
            print(f"  [elev-dbg] restore_illegal_floors 후 ({sx},{sy}) tid={tid:#06x} prop={chk_props4.get(tid)}")

    def _starts_connected() -> bool:
        grid = scmap.walk_grid(cli, tileset_id, 0, 0, width, height)
        points = [scmap.nearest_walkable(grid, sx * 4 + 2, sy * 4 + 2, radius=24)
                  for sx, sy in starts]
        if any(point is None for point in points):
            return False
        return all(scmap.walk_reachable(grid, points[0], point) for point in points[1:])

    if not _starts_connected():
        # 고리 틈이 미니타일로 안 이어지면, 가운데로 폭 3칸을 ISOM 붓질로 뚫는다.
        # (raw 타일을 직접 찍지 않는다 — 밀리 지형은 ISOM 만 허용한다.)
        props = scmap.tileset_tiles(cli, tileset_id)
        grid = cli.tiles(0, 0, width, height)
        cx_mid_i, cy_mid_i = width // 2, height // 2
        corridor = set()
        for sx, sy in starts:
            for x, y in scmap.segment_corridor(int(sx), int(sy), cx_mid_i, cy_mid_i, 2):
                if not (0 <= y < height and 0 <= x < width):
                    continue
                prop = props.get(grid[y][x])
                mask = (prop[4] & 0xFFFF) if prop is not None and len(prop) > 4 else 0
                if mask != 0xFFFF:
                    corridor.add((x - x % 2, y, low_terrain))
        cli.isom_batch(sorted(corridor))
        if not _starts_connected():
            raise CliError("입구 고리를 두른 뒤 스타팅 지상 경로가 끊겼습니다")

    avoid = [(u["x"] // 32, u["y"] // 32) for u in cli.units()]
    place_legal_critters(cli, tileset_id, args.critters, args.critter_unit or "",
                         width, height, avoid)
    _paint_floor_under(cli, tileset_id, width, height, _placed_items(cli), low_terrain)
    assert_placed_terrain(cli, tileset_id, width, height)
    import verify_map
    tile_grid = cli.tiles(0, 0, width, height)
    tile_props = scmap.tileset_tiles(cli, tileset_id)
    tile_walk = [[1 if (tile_props.get(tid) or (0, 0, 0))[1] else 0 for tid in row]
                 for row in tile_grid]
    clusters = verify_map._resource_clusters(
        [item for item in _placed_items(cli) if item["role"] == "resource"])
    centroids = [(sum(unit["tx"] for unit in cluster) // len(cluster),
                  sum(unit["ty"] for unit in cluster) // len(cluster))
                 for cluster in clusters]
    # 바깥 멀티는 고리로 좁히지 않기로 했으니(위 주석 참고) 좁은 입구
    # 검사도 본진·앞마당(site_points)만 본다 — 아니면 의도적으로 트인
    # 멀티가 매번 이 검사에 걸린다.
    centroids = [(cx, cy) for cx, cy in centroids
                if any(abs(cx - sx) <= 8 and abs(cy - sy) <= 8 for sx, sy in site_points)]
    chokes = verify_map.choke_faults(tile_walk, centroids)

    # 자원량·플레이어 슬롯은 좁은 입구 검사 결과와 무관하게 반드시
    # 끝맺는다. 예전에는 이 확인이 실패하면 CliError로 여기서 멈췄는데,
    # 그러면 뒤에 있는 `setup_melee_players`가 통째로 안 불려 남는
    # 슬롯이 "열림"으로 남고, 대기실에서 그 자리로 들어온 사람이 시작
    # 지점 없이 바로 패배하는 실제 사고가 났다. 좁은 입구 문제는 지형
    # 품질 경고로만 남기고, 맵을 쓸 수 없게 만들지는 않는다.
    if chokes:
        print("  [주의] 좁은 입구가 아닙니다: " + ", ".join(chokes[:3]))
    if os.environ.get("ELEV_DEBUG"):
        chk_props2 = scmap.tileset_tiles(cli, tileset_id)
        for (sx, sy) in starts:
            tid = cli.tiles(sx, sy, 1, 1)[0][0]
            print(f"  [elev-dbg] 최종 ({sx},{sy}) tid={tid:#06x} prop={chk_props2.get(tid)}")

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
    argv = sys.argv[1:]
    while True:
        try:
            sys.exit(main(argv))
        except CliError as e:
            text = str(e)
            if "바깥 멀티" in text and "--expansions" in argv:
                index = argv.index("--expansions")
                count = int(argv[index + 1])
                if count > 0:
                    argv = list(argv)
                    argv[index + 1] = str(count - 1)
                    print(f"  바깥 멀티를 {count - 1}곳으로 줄여 다시 만듭니다", flush=True)
                    continue
            print(f"오류: {e}", file=sys.stderr)
            sys.exit(1)
