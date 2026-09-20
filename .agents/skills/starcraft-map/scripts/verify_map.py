#!/usr/bin/env python3
"""만든 맵을 재고 따진다 — 유효성, 대칭, 자원 형평, 종족 밸런스 기울기.

두 갈래로 본다.

1. **자리 밸런스** — 어느 스타팅을 받아도 손해보지 않는가. 대칭, 스타팅별
   자원 개수, 본진→앞마당 거리, 스타팅 사이 거리가 고른가.
2. **종족 밸런스** — 맵이 테란·저그·프로토스 중 누구 쪽으로 기울었는가.
   `references/melee-balance.md` 에 근거를 적어 둔 지렛대들을 재어
   기울기를 말한다. 자동 판정이 아니라 **눈금**이다 — 최종 판단은 사람이
   한다.

기준값은 공식 리그 맵 56개(투혼·서킷브레이커·폴리포이드·이클립스·
파이썬·로스트템플 등)를 재어 얻었다.

보기:
    python3 verify_map.py map.scx
    python3 verify_map.py map.scx --json
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError

# 공식 리그 맵 56개에서 잰 기준값.
REFERENCE = {
    "main_minerals": 9,          # 스타팅 229곳 중 144곳
    "main_gas": 1,               # 219곳
    "natural_minerals": 7,       # 93곳 (6개가 73곳으로 다음)
    "natural_gas": 1,            # 151곳 (없는 곳 78)
    "natural_distance": 28,      # 중앙값 28.3 타일
    "mineral_amount": 1500,      # 54/56 맵
    "gas_amount": 5000,          # 53/56 맵
    "clusters_per_start": 4.0,   # 중앙값
    "min_start_distance": {2: 122, 3: 102, 4: 87},  # 스타팅 수별 중앙값
}


def cluster_points(points, radius=8):
    groups = []
    for p in points:
        hit = [g for g in groups
               if any(abs(p[0] - q[0]) <= radius and abs(p[1] - q[1]) <= radius for q in g)]
        if not hit:
            groups.append([p])
        else:
            merged = [p]
            for g in hit:
                merged += g
                groups.remove(g)
            groups.append(merged)
    return groups


def point_symmetry(points, width, height, tol=4.0):
    if not points:
        return {}
    cx, cy = (width - 1) / 2.0, (height - 1) / 2.0

    def score(f):
        ok = 0
        for (x, y) in points:
            tx, ty = f(x, y)
            if any(abs(tx - qx) <= tol and abs(ty - qy) <= tol for (qx, qy) in points):
                ok += 1
        return round(100.0 * ok / len(points), 1)

    return {
        "rot180": score(lambda x, y: (2 * cx - x, 2 * cy - y)),
        "rot90": score(lambda x, y: (cx + (y - cy), cy - (x - cx))),
        "mirror_h": score(lambda x, y: (2 * cx - x, y)),
        "mirror_v": score(lambda x, y: (x, 2 * cy - y)),
    }


PLAYABLE_SLOTS = ("열림", "사람(게임)", "컴퓨터", "컴퓨터(게임)")


def read_players(cli: Cli):
    """플레이어 12칸을 읽는다. (번호, 종족, 슬롯) 목록."""
    out = cli.run("player", "list", cli.path)
    rows = []
    for line in out.splitlines():
        m = re.match(r"^\s*P\s*(\d+)\s+(\S+(?:\s\S+)?)\s+(\S+(?:\s\S+)?)", line)
        if m:
            rows.append({"player": int(m.group(1)),
                         "race": m.group(2).strip(),
                         "slot": m.group(3).strip()})
    return rows


def read_unit_flags(cli: Cli):
    """CHK 의 UNIT 구역을 직접 읽어 '이 값이 유효하다' 비트를 본다.

    자원량을 1500 으로 적어도 validFieldFlags 의 Resources 비트(0x10)가
    꺼져 있으면 게임과 에디터는 0 으로 본다. 실제로 겪었다.
    """
    import struct, tempfile
    with tempfile.NamedTemporaryFile(suffix=".chk", delete=False) as f:
        tmp = f.name
    try:
        cli.run("chk", cli.path, tmp)
        data = open(tmp, "rb").read()
    finally:
        try: os.unlink(tmp)
        except OSError: pass
    i = 0
    body = b""
    while i + 8 <= len(data):
        name = data[i:i+4]
        size = struct.unpack("<I", data[i+4:i+8])[0]
        if name == b"UNIT":
            body = data[i+8:i+8+size]
            break
        i += 8 + size
    fmt = "<IHHHHHHBBBBIHHII"
    out = []
    for k in range(len(body) // 36):
        (cid, x, y, ut, rel, vState, vField,
         owner, hp, sh, en, res, hangar, state, unused, link) = struct.unpack(
            fmt, body[k*36:(k+1)*36])
        out.append({"type": ut, "resource": res, "valid_field": vField})
    return out


def measure(cli: Cli) -> dict:
    info = cli.info()
    units = cli.units()
    width, height = info["width"], info["height"]

    starts_raw = [u for u in units if u["type"] == scmap.START_LOCATION]
    minerals = [u for u in units if u["type"] in scmap.MINERALS]
    geysers = [u for u in units if u["type"] == scmap.VESPENE_GEYSER]

    def tile(u):
        return (u["x"] // scmap.TILE, u["y"] // scmap.TILE)

    # 관측자(ob) 스타팅은 플레이어 스타팅 바로 옆에 있다. 가까우면 하나로 본다.
    starts = []
    for u in starts_raw:
        t = tile(u)
        if any(abs(t[0] - q[0]) <= 8 and abs(t[1] - q[1]) <= 8 for q, _ in starts):
            continue
        starts.append((t, u))

    clusters = []
    for g in cluster_points([tile(u) for u in minerals + geysers]):
        n_min = sum(1 for u in minerals if tile(u) in g)
        n_gas = sum(1 for u in geysers if tile(u) in g)
        clusters.append({
            "x": round(sum(p[0] for p in g) / len(g), 1),
            "y": round(sum(p[1] for p in g) / len(g), 1),
            "minerals": n_min, "gas": n_gas,
        })

    per_start = []
    for (sx, sy), u in starts:
        near = sorted(clusters, key=lambda c: (c["x"] - sx) ** 2 + (c["y"] - sy) ** 2)
        main = near[0] if near else None
        nat = near[1] if len(near) > 1 else None
        per_start.append({
            "owner": u["owner"], "x": sx, "y": sy,
            "main_minerals": main["minerals"] if main else 0,
            "main_gas": main["gas"] if main else 0,
            "natural_minerals": nat["minerals"] if nat else 0,
            "natural_gas": nat["gas"] if nat else 0,
            "natural_distance": (round(math.dist((sx, sy), (nat["x"], nat["y"])), 1)
                                 if nat else None),
            # 맵 구석에 얼마나 붙었는지 (0 = 한가운데, 1 = 완전 구석)
            "cornerness": round(max(abs(sx - (width - 1) / 2) / ((width - 1) / 2),
                                    abs(sy - (height - 1) / 2) / ((height - 1) / 2)), 2),
        })

    tiles = [t for (t, _) in starts]
    distances = sorted(round(math.dist(a, b), 1)
                       for i, a in enumerate(tiles) for b in tiles[i + 1:])

    amounts_min = [u["resource"] for u in minerals if u["resource"] is not None]
    amounts_gas = [u["resource"] for u in geysers if u["resource"] is not None]

    return {
        "file": os.path.basename(cli.path),
        "name": info["name"], "width": width, "height": height,
        "tileset": info["tileset"], "tileset_id": info["tileset_id"],
        "version": info["version"], "protected": info["protected"],
        "n_units": info["units"], "n_triggers": info["triggers"],
        "n_starts": len(starts),
        "n_start_units": len(starts_raw),
        "n_mineral_patches": len(minerals), "n_geysers": len(geysers),
        "n_clusters": len(clusters),
        "clusters_per_start": (round(len(clusters) / len(starts), 2) if starts else None),
        "mineral_amount": (statistics.mode(amounts_min) if amounts_min else None),
        "gas_amount": (statistics.mode(amounts_gas) if amounts_gas else None),
        "gasless_clusters": sum(1 for c in clusters if c["gas"] == 0),
        "per_start": per_start,
        "start_distances": distances,
        "symmetry": point_symmetry(tiles, width, height),
        "resource_symmetry": point_symmetry([(c["x"], c["y"]) for c in clusters],
                                            width, height, tol=5.0),
    }


def check_basics(cli: Cli, m: dict) -> list[tuple[str, str]]:
    """맵이 **열리기는 하는지**. 여기서 걸리면 밸런스는 따질 것도 없다."""
    out = []

    # 1) 스타팅이 있는가
    if m["n_start_units"] == 0:
        out.append(("!!", "스타팅 포인트(유닛 214)가 하나도 없습니다. "
                          "사람 플레이어마다 하나씩 있어야 게임이 시작됩니다."))

    # 2) 쓸 수 있는 슬롯 수와 스타팅 수가 맞는가
    players = read_players(cli)
    playable = [p for p in players
                if p["player"] <= 8 and p["slot"] in PLAYABLE_SLOTS]
    human = [p for p in playable if p["slot"] in ("열림", "사람(게임)")]
    computer = [p for p in playable if p["slot"] in ("컴퓨터", "컴퓨터(게임)")]
    # 스타팅은 **합치지 않은 원래 개수**로 센다. 관측자 판본을 가리려고
    # 가까운 것을 합치는데, 유즈맵은 사람들이 서로 붙어 시작하는 것이
    # 정상이라 그 잣대를 쓰면 안 된다.
    n_starts = m["n_start_units"]
    # 컴퓨터는 스타팅 없이도 트리거로 유닛을 받을 수 있다. 사람 수만 맞으면 된다.
    if n_starts and len(human) != n_starts:
        out.append(("!!", f"사람이 앉을 슬롯 {len(human)}개와 스타팅 {n_starts}개가 "
                          f"다릅니다. 스타팅 없는 자리를 받는 사람이 생깁니다."))
    else:
        out.append(("ok", f"사람 슬롯 {len(human)}개 = 스타팅 {n_starts}개"
                          f" (컴퓨터 {len(computer)})."))

    # 3) 확장자와 버전이 맞는가
    ext = os.path.splitext(cli.path)[1].lower()
    version = m["version"]
    if ext == ".scx" and "Brood War" not in version and "Remastered" not in version:
        out.append(("!!", f"확장자는 .scx 인데 버전이 {version} 입니다. "
                          f"브루드워 전용 유닛(럴커·메딕·커세어)을 쓸 수 없습니다."))
    elif ext == ".scm" and ("Brood War" in version or "Remastered" in version):
        out.append(("?", f"확장자는 .scm 인데 버전이 {version} 입니다."))
    else:
        out.append(("ok", f"버전 {version} — 확장자와 맞습니다."))

    # 4) 자원 유닛의 '유효' 비트가 켜져 있는가
    try:
        flags = read_unit_flags(cli)
    except Exception as e:
        flags = None
        out.append(("?", f"CHK 를 읽지 못해 자원 유효 비트를 못 봤습니다: {e}"))
    if flags:
        RESOURCES_BIT = 0x10
        res_units = [u for u in flags
                     if u["type"] in (176, 177, 178, 188)]
        bad = [u for u in res_units if not (u["valid_field"] & RESOURCES_BIT)]
        if bad:
            out.append(("!!", f"자원 유닛 {len(res_units)}개 중 {len(bad)}개가 "
                              f"'자원량 유효' 비트(0x10)가 꺼져 있습니다. "
                              f"남은 양을 적어도 게임과 에디터가 0 으로 봅니다."))
        elif res_units:
            out.append(("ok", f"자원 유닛 {len(res_units)}개 모두 자원량이 유효합니다."))

    # 5) 스타팅끼리 걸어서 닿는가 — 갇힌 본진은 맵을 못 쓰게 만든다
    starts = [(p["x"], p["y"]) for p in m["per_start"]]
    if len(starts) >= 2:
        try:
            grid = scmap.walk_grid(cli, m["tileset_id"], 0, 0, m["width"], m["height"])
            pts = []
            for (sx, sy) in starts:
                pt = scmap.nearest_walkable(grid, sx * 4 + 2, sy * 4 + 2, radius=40)
                pts.append(pt)
            if any(p is None for p in pts):
                out.append(("!!", "스타팅 자리에 걸을 수 있는 땅이 없습니다."))
            else:
                unreachable = []
                for i in range(1, len(pts)):
                    if not scmap.walk_reachable(grid, pts[0], pts[i]):
                        unreachable.append(i + 1)
                if unreachable:
                    out.append(("!!", f"1번 스타팅에서 {unreachable} 번 스타팅으로 "
                                      f"걸어갈 수 없습니다. 지상 유닛이 갇힙니다."))
                else:
                    out.append(("ok", f"스타팅 {len(pts)}곳이 모두 걸어서 이어집니다."))
        except Exception as e:
            out.append(("?", f"길찾기 검사를 못 했습니다: {e}"))

    # 6) 유즈맵인데 트리거가 없는가
    if m["n_triggers"] == 0:
        out.append(("?", "트리거가 없습니다. 유즈맵이라면 아무 일도 일어나지 않습니다."))

    # 7) 지형이 충분한가 (밀리 기준: 공식 57개 전수 중앙값)
    try:
        t = scmap.Terrain(cli, 0, 0, m["width"], m["height"], m["tileset_id"])
        total = m["width"] * m["height"]
        groups_used = {t.tiles[y][x] >> 4 for y in range(m["height"])
                       for x in range(m["width"])}
        walk = sum(1 for y in range(m["height"]) for x in range(m["width"])
                   if t.walkable(x, y))
        high = sum(1 for y in range(m["height"]) for x in range(m["width"])
                   if t.elevation(x, y) >= 1)
        wpct, hpct = 100*walk/total, 100*high/total
        out.append(("i", f"지형: 걷기 {wpct:.0f}% (공식 79%), 높은 땅 {hpct:.0f}% "
                         f"(공식 36%), 타일 그룹 {len(groups_used)} (공식 484)"))
        if len(groups_used) < 150 and m["n_starts"] >= 2:
            out.append(("?", "타일 그룹이 공식 맵의 3분의 1도 안 됩니다 — "
                             "지형이 거의 없는 벌판입니다."))
    except Exception as e:
        out.append(("?", f"지형을 못 쟀습니다: {e}"))

    return out


def check_fairness(m: dict) -> list[tuple[str, str]]:
    """자리 밸런스 — 어느 스타팅을 받아도 같은가."""
    out = []
    ps = m["per_start"]
    if m["n_starts"] < 2:
        out.append(("!!", f"스타팅이 {m['n_starts']}개입니다. 밀리맵은 2개 이상이어야 합니다."))
        return out

    for field, label in [("main_minerals", "본진 미네랄"), ("main_gas", "본진 가스"),
                         ("natural_minerals", "앞마당 미네랄"), ("natural_gas", "앞마당 가스")]:
        values = {p[field] for p in ps}
        if len(values) > 1:
            out.append(("!!", f"{label}이 스타팅마다 다릅니다: "
                              f"{[p[field] for p in ps]} — 자리에 따라 손해를 봅니다."))
        else:
            out.append(("ok", f"{label} {ps[0][field]}개로 모든 스타팅이 같습니다."))

    dists = [p["natural_distance"] for p in ps if p["natural_distance"] is not None]
    if dists and max(dists) - min(dists) > 4:
        out.append(("!!", f"본진→앞마당 거리가 고르지 않습니다: "
                          f"{min(dists):.0f}~{max(dists):.0f}타일."))
    elif dists:
        out.append(("ok", f"본진→앞마당 {statistics.median(dists):.0f}타일 "
                          f"(공식 맵 중앙값 {REFERENCE['natural_distance']})."))

    sym = m["symmetry"]
    best = max(sym, key=sym.get) if sym else None
    if best and sym[best] >= 99.9:
        out.append(("ok", f"스타팅이 {best} 대칭입니다."))
    else:
        out.append(("!!", f"완전 대칭인 축이 없습니다: {sym}. "
                          f"밀리맵은 대칭이 밸런스의 뿌리입니다."))

    rs = m["resource_symmetry"]
    if rs and max(rs.values()) < 90:
        out.append(("?", f"자원 덩이 배치가 대칭에서 벗어납니다: {rs}."))

    if m["n_starts"] >= 2 and m["start_distances"]:
        spread = m["start_distances"][-1] - m["start_distances"][0]
        if m["n_starts"] == 2 and spread > 1:
            out.append(("?", "2인용인데 스타팅 거리가 하나가 아닙니다."))

    if m["mineral_amount"] not in (None, REFERENCE["mineral_amount"]):
        out.append(("?", f"미네랄 양이 {m['mineral_amount']}입니다 "
                         f"(공식 맵은 대부분 {REFERENCE['mineral_amount']})."))
    if m["gas_amount"] not in (None, REFERENCE["gas_amount"]):
        out.append(("?", f"가스 양이 {m['gas_amount']}입니다 "
                         f"(공식 맵은 대부분 {REFERENCE['gas_amount']})."))
    return out


def check_race_balance(m: dict) -> list[tuple[str, str, str]]:
    """종족 밸런스 지렛대. (종족, 방향, 설명) 을 돌려준다.

    근거는 references/melee-balance.md 에 적어 두었다. 여기서 재는 것은
    수로 잴 수 있는 것뿐이다 — 입구 너비, 심시티 가능 여부, 우회로 개수
    같은 것은 지형을 걸어 봐야 알 수 있어 사람이 봐야 한다.
    """
    out = []
    ps = m["per_start"]
    if not ps:
        return out
    n = m["n_starts"]

    # 1) 본진 미네랄 개수 — 많으면 프로토스, 적으면 저그
    mm = ps[0]["main_minerals"]
    if mm >= 10:
        out.append(("프로토스", "+", f"본진 미네랄 {mm}개. 초반부터 부유해 질럿·드라군을 "
                                     "쭉쭉 뽑습니다. 저그는 라바가 모자라 남는 자원을 못 씁니다."))
    elif mm <= 7:
        out.append(("저그", "+", f"본진 미네랄 {mm}개로 가난합니다. 확장력이 좋은 저그가 "
                                 "유리하고, 본진·앞마당만으로 버티는 테란·토스가 불리합니다."))
    else:
        out.append(("-", "=", f"본진 미네랄 {mm}개 — 공식 맵 표준({REFERENCE['main_minerals']})입니다."))

    # 2) 앞마당 가스 — 없으면 테란
    ng = ps[0]["natural_gas"]
    if ng == 0:
        out.append(("테란", "+", "앞마당에 가스가 없습니다(본진+앞마당 1가스). 가스를 적게 쓰는 "
                                 "테란이 유리하고, 가스가 필요한 저그·토스가 불리합니다."))
    elif ng >= 2:
        out.append(("프로토스", "+", f"앞마당 가스 {ng}개. 하이템플러·리버 같은 고테크를 "
                                     "일찍 갑니다."))

    # 3) 가스 없는 멀티 비율 — 많으면 테란 쪽
    if m["n_clusters"]:
        ratio = m["gasless_clusters"] / m["n_clusters"]
        if ratio >= 0.5:
            out.append(("테란", "+", f"자원 덩이 {m['n_clusters']}곳 중 "
                                     f"{m['gasless_clusters']}곳이 미네랄 멀티입니다."))

    # 4) 멀티 개수 — 적고 풍족하면 프로토스, 많고 흩어지면 저그
    cps = m["clusters_per_start"]
    if cps is not None:
        if cps <= 2.5:
            out.append(("프로토스", "+", f"스타팅당 자원 덩이 {cps}곳. 지킬 멀티가 적어 "
                                         "기동성 낮은 프로토스에게 좋습니다."))
        elif cps >= 5.0:
            out.append(("저그", "+", f"스타팅당 자원 덩이 {cps}곳. 멀티가 많아 확장력이 좋은 "
                                     "저그가 유리합니다."))

    # 5) 러시 거리 — 스타팅 사이 최단 거리
    if m["start_distances"]:
        shortest = m["start_distances"][0]
        base = REFERENCE["min_start_distance"].get(n, 87)
        if shortest < base * 0.7:
            out.append(("테란/프로토스", "+", f"최단 러시 거리 {shortest:.0f}타일로 짧습니다"
                                              f"(같은 인원 공식 맵 중앙값 {base}). 전진 게이트·"
                                              "벙커링이 강해집니다. 너무 짧으면 4드론·하드코어 "
                                              "질럿을 막을 수 없어 맵이 망가집니다."))
        elif shortest > base * 1.3:
            out.append(("저그", "+", f"최단 러시 거리 {shortest:.0f}타일로 깁니다"
                                     f"(중앙값 {base}). 초반 압박이 약해져 저그가 편합니다."))

    # 6) 스타팅이 구석에 붙었는지 — 붙을수록 공중 침입 경로가 줄어 테란
    corner = statistics.mean(p["cornerness"] for p in ps)
    if corner >= 0.85:
        out.append(("테란", "+", f"스타팅이 맵 구석에 있습니다(구석도 {corner:.2f}). "
                                 "리콜·뮤탈 침입 경로가 적어 터렛 몇 개로 봉쇄됩니다."))
    elif corner <= 0.5:
        out.append(("저그/프로토스", "+", f"스타팅이 맵 안쪽에 있습니다(구석도 {corner:.2f}). "
                                          "여러 방향에서 들어올 수 있어 테란이 지키기 어렵습니다."))

    # 7) 인원수
    if n == 4:
        out.append(("테란", "+", "4인용입니다. 멀티가 늦은 테란도 스타팅 지역을 하나 먹을 수 "
                                 "있어 자원 격차가 줄어듭니다."))
    elif n == 2:
        out.append(("-", "=", "2인용입니다. 테란은 저그전에 좋지만 토스전 가스 러시에 약합니다."))

    return out


_CLI = [None]


def report(m: dict) -> int:
    print(f"== {m['name'] or m['file']} ==")
    print(f"  {m['width']}x{m['height']} {m['tileset']}  {m['version']}")
    print(f"  스타팅 {m['n_starts']}  자원덩이 {m['n_clusters']}"
          f" (미네랄 {m['n_mineral_patches']}, 가스 {m['n_geysers']})"
          f"  유닛 {m['n_units']}  트리거 {m['n_triggers']}")
    if m["start_distances"]:
        print(f"  스타팅 사이 거리: {m['start_distances']}")

    print("\n-- 기본 (열리는 맵인가) --")
    problems = 0
    for mark, text in check_basics(_CLI[0], m):
        if mark == "!!":
            problems += 1
        print(f"  [{mark:2}] {text}")

    print("\n-- 자리 밸런스 --")
    for mark, text in check_fairness(m):
        if mark == "!!":
            problems += 1
        print(f"  [{mark:2}] {text}")

    print("\n-- 종족 밸런스 기울기 --")
    lever = check_race_balance(m)
    if not lever:
        print("  (잴 것이 없습니다)")
    for race, direction, text in lever:
        tag = "=" if race == "-" else f"{race} 쪽"
        print(f"  [{tag}] {text}")
    print("\n  ※ 입구 너비, 심시티로 입구가 막히는지, 우회로 개수, 언덕에서 탱크가")
    print("     때릴 수 있는지는 수로 잴 수 없습니다. 그려 보고 사람이 판단하세요:")
    print("     python3 preview.py <맵> out.png")

    if problems:
        print(f"\n고쳐야 할 것 {problems}개.")
    else:
        print("\n자리 밸런스에 걸리는 것은 없습니다.")
    return 1 if problems else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="맵을 재고 밸런스를 따진다")
    ap.add_argument("map")
    ap.add_argument("--json", action="store_true", help="잰 값을 JSON 으로")
    ap.add_argument("--install", default=None)
    args = ap.parse_args(argv)

    cli = Cli(args.map, args.install)
    _CLI[0] = cli
    m = measure(cli)
    if args.json:
        m["basics"] = [{"mark": a, "text": b} for a, b in check_basics(cli, m)]
        m["fairness"] = [{"mark": a, "text": b} for a, b in check_fairness(m)]
        m["race_balance"] = [{"race": a, "dir": b, "text": c}
                             for a, b, c in check_race_balance(m)]
        print(json.dumps(m, ensure_ascii=False, indent=1))
        return 0
    return report(m)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(2)
