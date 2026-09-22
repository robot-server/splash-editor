#!/usr/bin/env python3
"""**트리거와 배치를 함께 본다.**

사용자가 되풀이해 지적한 여섯 가지 중 넷은 플레이해야 아는 것이 아니라
**기하**다 — 사거리 밖, 동선이 너무 김, 맵을 꽉 채움, 회복이 없음.
지형·유닛 배치와 트리거를 따로 보면 안 잡히고, 같이 보면 잡힌다.

지금까지 유즈맵 검사는 트리거 글자열만 보았다. 여기서 둘을 잇는다.
"""
from __future__ import annotations

import collections
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli

# 무기 사거리 (타일). Liquipedia 와 카페 강좌로 확인한 값이다.
# **대공 전용은 지상 사거리를 None 으로 둔다** — 미사일 터렛을 디펜스
# 타워로 놓고 웨이브를 못 때리던 적이 있다.
GROUND_RANGE = {
    "Terran Marine": 4, "Terran Firebat": 1, "Terran Ghost": 7,
    "Terran Vulture": 5, "Terran Goliath": 5,
    "Terran Siege Tank (Tank Mode)": 7,
    "Terran Siege Tank (Siege Mode)": 12,
    "Terran Missile Turret": None,          # 대공 전용
    "Terran Bunker": 5,                     # 안에 유닛을 넣어야 쏜다
    "Protoss Zealot": 1, "Protoss Dragoon": 4,
    "Protoss Photon Cannon": 7,             # 파일런이 있어야 전력이 든다
    "Protoss Archon": 2, "Protoss Reaver": 8,
    "Zerg Zergling": 1, "Zerg Hydralisk": 4,
    "Zerg Ultralisk": 1, "Zerg Lurker": 6,
    "Zerg Sunken Colony": 7,                # 스스로 크립을 깐다
    "Zerg Spore Colony": None,              # 대공 전용
    "Jim Raynor (Marine)": 4,
    "Torrasque (Ultralisk)": 1,
}
# 전력·탄약이 딸린 것 — 놓기만 해서는 안 쏜다
NEEDS_SUPPORT = {
    "Protoss Photon Cannon": "파일런이 곁에 있어야 전력이 들어온다",
    "Terran Bunker": "안에 유닛을 넣어야 쏜다",
}
TILE = 32


def _locations(cli: Cli) -> dict[str, tuple[float, float, int, int, int, int]]:
    """이름 → (가운데x, 가운데y, 왼, 위, 오른, 아래) 타일 단위."""
    out = {}
    try:
        text = cli.run("location", "list", cli.path)
    except Exception:
        return out
    for line in text.splitlines():
        m = re.match(r"^\s*\d+\s+\((\d+), (\d+)\) - \((\d+), (\d+)\)\s+\S+\s+(.+)$",
                     line)
        if not m:
            continue
        x0, y0, x1, y1 = (int(m.group(i)) // TILE for i in range(1, 5))
        out[m.group(5).strip()] = ((x0 + x1) / 2, (y0 + y1) / 2, x0, y0, x1, y1)
    return out


def _walk_distance(grid, a, b) -> int | None:
    """미니타일 격자에서 걸어간 칸 수. 못 가면 None."""
    from collections import deque
    H, W = len(grid), len(grid[0])
    sa = scmap.nearest_walkable(grid, int(a[0]) * 4 + 2, int(a[1]) * 4 + 2, radius=40)
    sb = scmap.nearest_walkable(grid, int(b[0]) * 4 + 2, int(b[1]) * 4 + 2, radius=40)
    if sa is None or sb is None:
        return None
    seen = [[False] * W for _ in range(H)]
    q = deque([(sa[1], sa[0], 0)])
    seen[sa[1]][sa[0]] = True
    while q:
        y, x, d = q.popleft()
        if (x, y) == (sb[0], sb[1]):
            return d // 4                    # 미니타일 → 타일
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < H and 0 <= nx < W and grid[ny][nx] and not seen[ny][nx]:
                seen[ny][nx] = True
                q.append((ny, nx, d + 1))
    return None


def check(cli: Cli, info: dict, text: str) -> list[tuple[str, str]]:
    """트리거가 말하는 것과 배치된 것을 맞대어 본다."""
    out = []
    locs = _locations(cli)
    units = cli.units()
    if not locs or not units:
        return out

    # --- 1) 방어 유닛이 적이 지나는 자리에 닿는가 -------------------
    # 트리거가 `Order(적, …, 경유지A, 경유지B, …)` 로 길을 지시하면
    # 그 경유지가 적이 밟는 자리다. 지킬 유닛에서 거기까지가 사거리보다
    # 멀면 한 발도 못 쏜다 — 벽을 사거리보다 두껍게 세워 그랬던 적이 있다.
    # 적은 경유지 **사이 통로를 따라** 걷는다. 경유지 중심까지만 재면
    # 성한 맵도 "사거리 밖" 이라고 나온다 — 선분까지의 거리를 본다.
    segs, boxes = [], []
    for m in re.finditer(r'Order\(\s*"[^"]+"\s*,\s*"[^"]+"\s*,\s*"([^"]+)"\s*,'
                         r'\s*"([^"]+)"', text):
        a, b = m.group(1), m.group(2)
        if a in locs and b in locs:
            segs.append((locs[a][:2], locs[b][:2]))
            boxes.append(locs[a][2:]); boxes.append(locs[b][2:])

    def dist_to_box(px, py, box):
        """경유지는 점이 아니라 **구역**이다. 그 안에 서 있으면 0.

        좀비·술래잡기처럼 **적이 나한테 오는** 놀이에서는 지킬 유닛이
        길 중간에서 멀어도 괜찮다 — 길의 끝이 곧 내가 선 자리다.
        중심점까지만 재면 그런 맵을 죄다 잘못 잡는다.
        """
        x0, y0, x1, y1 = box
        dx = max(x0 - px, 0, px - x1)
        dy = max(y0 - py, 0, py - y1)
        return math.hypot(dx, dy)

    def dist_to_seg(px, py, p1, p2):
        x1, y1 = p1; x2, y2 = p2
        dx, dy = x2 - x1, y2 - y1
        L2 = dx * dx + dy * dy
        if L2 == 0:
            return math.hypot(px - x1, py - y1)
        t = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / L2))
        return math.hypot(px - (x1 + t * dx), py - (y1 + t * dy))

    if segs:
        bad = []
        for u in units:
            name = u.get("type_name") or ""
            rng = GROUND_RANGE.get(name)
            if rng is None:
                continue                      # 모르는 유닛·대공 전용은 건너뛴다
            ux, uy = u["x"] / TILE, u["y"] / TILE
            near = min(min(dist_to_seg(ux, uy, a, b) for a, b in segs),
                       min(dist_to_box(ux, uy, bx) for bx in boxes))
            if near > rng + 1.5:
                bad.append((name, round(near), rng))
        if bad:
            worst = collections.Counter(b[0] for b in bad).most_common(3)
            far = max(bad, key=lambda b: b[1])
            out.append(("!!", f"지킬 유닛 {len(bad)}기가 적이 지나는 길에서 "
                              f"사거리 밖입니다. 가장 먼 것: {far[0]} — "
                              f"{far[1]}타일 떨어졌는데 사거리는 {far[2]}입니다. "
                              f"({', '.join(f'{n} {c}기' for n, c in worst)})"))
        else:
            out.append(("ok", f"적이 지나는 길 {len(segs)}구간 둘레의 유닛이 모두 "
                              f"사거리 안입니다."))

    # --- 2) 전력·탄약이 딸린 것을 그냥 놓았는가 ---------------------
    placed = collections.Counter(u.get("type_name") or "" for u in units)
    for name, why in NEEDS_SUPPORT.items():
        if not placed.get(name):
            continue
        if name == "Protoss Photon Cannon" and not placed.get("Protoss Pylon"):
            out.append(("!!", f"{name} 을 {placed[name]}기 놓았는데 파일런이 "
                              f"하나도 없습니다 — {why}. 전력이 없으면 안 쏩니다."))
        elif name == "Terran Bunker":
            out.append(("?", f"{name} 을 {placed[name]}기 놓았습니다 — {why}. "
                             f"트리거로 유닛을 넣어 주는지 확인하세요."))

    # --- 3) 제한 시간 안에 갈 수 있는가 ------------------------------
    # `Set Countdown Timer(Set To, N)` 이 있고 `Bring(…, 로케이션)` 으로
    # 자리를 고르게 하면, 시작 자리에서 그 자리까지 걸어갈 수 있어야 한다.
    # **아무 `Bring` 이나 재면 안 된다.** 카운트다운과 상관없는 상점
    # 비콘까지 재서, 다른 사람 경기장 상점까지 158타일이라고 성한 맵을
    # 틀렸다고 한 적이 있다. 카운트다운과 `Bring` 을 **같은 트리거의
    # 조건에서 함께 읽는** 것만 본다 — 그게 "시간 안에 저기로 가라" 다.
    secs = [int(x) for x in re.findall(
        r'Set Countdown Timer\(\s*Set To\s*,\s*(\d+)\s*\)', text)]
    if secs:
        limit = min(secs)
        choice = []
        for blk in re.split(r'(?=Trigger\()', text):
            head = blk.split("Actions:")[0]
            if "Countdown Timer(" not in head:
                continue
            choice += [n for n in re.findall(
                r'Bring\(\s*"[^"]+"\s*,\s*"[^"]+"\s*,\s*"([^"]+)"', head)
                if n in locs]
        choice = sorted(set(choice))
        starts = [(u["x"] / TILE, u["y"] / TILE) for u in units
                  if u["type"] == scmap.START_LOCATION]
        if choice and starts:
            try:
                ts = info["tileset_id"] & 7
                grid = scmap.walk_grid(cli, ts, 0, 0, info["width"], info["height"])
                worst = None
                for nm in choice:
                    for st in starts:
                        d = _walk_distance(grid, st, locs[nm][:2])
                        if d is not None and (worst is None or d > worst[0]):
                            worst = (d, nm)
                # 일꾼·시민 걸음으로 대략 초당 세 칸 남짓
                if worst and worst[0] > limit * 3:
                    out.append(("!!", f"제한 시간이 {limit}초인데 시작 자리에서 "
                                      f"'{worst[1]}' 까지 걸어서 {worst[0]}타일 "
                                      f"입니다. 시간 안에 못 갑니다 — 초당 세 칸 "
                                      f"남짓으로 잡으면 {worst[0] // 3}초가 듭니다."))
                elif worst:
                    out.append(("ok", f"제한 시간 {limit}초, 가장 먼 선택 자리까지 "
                                      f"{worst[0]}타일 (약 {worst[0] // 3}초)."))
            except Exception as e:
                out.append(("?", f"동선을 못 쟀습니다: {e}"))

    # --- 4) 싸움이 있는데 회복이 없는가 ------------------------------
    fights = bool(re.search(r'Create Unit\w*\([^)]*\)', text)) and \
        bool(re.search(r'\b(Kill|Bring)\(', text))
    heals = bool(re.search(r'Modify Unit (Hit Points|Shield Points)', text))
    revives = bool(re.search(r'Command\([^)]*"Men"[^)]*At most,\s*0', text))
    if fights and not heals and not revives:
        out.append(("?", "싸움은 있는데 **회복도 부활도 없습니다.** 한 번 깎인 "
                         "체력이 끝까지 그대로라 구경만 하다 지게 됩니다. "
                         "실측 유즈맵의 89%가 체력·에너지를 고쳐 줍니다."))

    return out
