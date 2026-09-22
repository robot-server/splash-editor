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
import collections
import json
import math
import os
import re
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
import spatial

MAP_REVEALER = 101
TRIGGER_SEP_RE = "//-----------------------------------------------------------------//"
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


def named_locations(cli: Cli) -> int:
    """이름이 붙은 로케이션 수.

    `info` 가 내는 로케이션 수는 **슬롯 255개**라 늘 255다. `location
    list` 는 이름이 붙은 것만 내므로 그 줄을 센다.
    """
    try:
        out = cli.run("location", "list", cli.path)
    except Exception:
        return 0
    return sum(1 for line in out.splitlines() if re.match(r"^\s*\d+\s", line))


def measure(cli: Cli) -> dict:
    info = cli.info()
    units = cli.units()
    width, height = info["width"], info["height"]

    n_named = named_locations(cli)
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
        "n_locations_named": n_named,
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


def shape_metrics(cli: Cli, m: dict) -> dict:
    """지형의 **형상**을 잰다 — 슬롭을 실제로 가르는 두 수.

    타일 패치가 실제 맵 사전에 있는지 세는 방법(패치 커버리지)은 쓰지
    않는다. 재 보니 빈 흙 맵이 공식 맵보다 높은 점수를 받았다. 까닭은
    ISOM 평지가 그룹 짝(2↔3 등) 체커보드라, 흙만 칠하면 가장 흔한
    패치로 도배한 셈이 되기 때문이다.

    대신 형상을 본다. 실측으로 갈린다:

    | | 균일창 % | 고지덩이 채움 |
    | --- | --- | --- |
    | 공식 맵 | 3 ~ 18 | 0.40 ~ 0.75 |
    | 절차 생성 슬롭 | 69 ~ 91 | 0.90 ~ 0.91 |
    """
    out = {}
    try:
        t = scmap.Terrain(cli, 0, 0, m["width"], m["height"], m["tileset_id"])
        W, H = m["width"], m["height"]
        # (1) 4x4 창이 지형 한두 가지로만 이루어진 비율 = 평평한 벌판
        n = u = 0
        for y in range(0, H - 3, 2):
            for x in range(0, W - 3, 2):
                n += 1
                gs = {t.tiles[y + dy][x + dx] >> 4
                      for dy in range(4) for dx in range(4)}
                if len(gs) <= 2:
                    u += 1
        out["uniform_pct"] = 100.0 * u / n if n else 0.0

        # (2) 고지 덩이가 얼마나 직사각형인가. 1.0 = 완전 네모
        from collections import deque
        hi = [[1 if t.elevation(x, y) >= 1 else 0 for x in range(W)]
              for y in range(H)]
        seen = [[0] * W for _ in range(H)]
        fills = []
        for y in range(H):
            for x in range(W):
                if not hi[y][x] or seen[y][x]:
                    continue
                q = deque([(y, x)]); seen[y][x] = 1; cells = []
                while q:
                    cy, cx = q.popleft(); cells.append((cy, cx))
                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        ny, nx = cy + dy, cx + dx
                        if 0 <= ny < H and 0 <= nx < W and hi[ny][nx] \
                                and not seen[ny][nx]:
                            seen[ny][nx] = 1; q.append((ny, nx))
                if len(cells) < 60:
                    continue
                ys = [c[0] for c in cells]; xs = [c[1] for c in cells]
                area = (max(ys) - min(ys) + 1) * (max(xs) - min(xs) + 1)
                fills.append((len(cells), len(cells) / area))
        fills.sort(reverse=True)
        out["blob_fills"] = [round(f, 2) for _, f in fills[:5]]
    except Exception as e:
        out["error"] = str(e)
    return out


def check_shape(cli: Cli, m: dict) -> list[tuple[str, str]]:
    """형상이 사람이 그린 것 같은가. 밀리맵에만 쓴다."""
    sm = shape_metrics(cli, m)
    if "error" in sm:
        return [("?", f"형상을 못 쟀습니다: {sm['error']}")]
    out = []
    u = sm["uniform_pct"]
    if u >= 60:
        out.append(("!!", f"4x4 창의 {u:.0f}%가 지형 한두 가지뿐입니다 "
                          f"(공식 맵 3~18%). 절벽·수풀 없는 벌판입니다 — "
                          f"절차 생성 슬롭이 늘 여기 걸립니다."))
    elif u >= 30:
        out.append(("?", f"4x4 창의 {u:.0f}%가 지형 한두 가지뿐입니다 "
                         f"(공식 맵 3~18%). 지형이 성깁니다."))
    else:
        out.append(("ok", f"지형이 고르게 섞여 있습니다 (균일 창 {u:.0f}%)."))
    f = sm.get("blob_fills") or []
    if f:
        worst = max(f)
        if worst >= 0.88:
            out.append(("!!", f"고지 덩이가 직사각형입니다 (채움 {worst:.2f}, "
                              f"공식 맵 0.40~0.75). 네모난 언덕은 사람이 "
                              f"그린 것으로 보이지 않습니다."))
        else:
            out.append(("ok", f"고지 덩이 모양이 네모나지 않습니다 "
                              f"(채움 {f})."))
    return out


def classify(m: dict) -> str:
    """밀리인가 유즈맵인가. 전수조사에서 쓴 것과 같은 잣대다.

    밀리맵에 유즈맵 잣대를, 유즈맵에 밀리맵 잣대를 들이대면 헛경고가
    쏟아진다 — 봉인된 디펜스 경기장을 "지상 유닛이 갇힘" 이라 하고,
    유즈맵에 종족 밸런스를 따지는 식이다.
    """
    if (m["n_triggers"] <= 12 and m.get("n_locations_named", 0) <= 3
            and m["n_units"] <= 400):
        return "melee"
    return "usemap"


def check_usemap(cli: Cli, m: dict) -> list[tuple[str, str]]:
    """유즈맵이 **실제로 굴러가는가**를 본다.

    **수를 사분위와 견주지 않는다.** 앞서 유닛·트리거·로케이션 수를 전체
    사분위와 대 보고 모자라면 표를 띄웠는데, 그건 "수치에 맞추기" 였다.
    퀴즈 맵의 유닛 중앙값은 156인데 전체 중앙값은 677이다 — 퀴즈를
    제대로 만들면 "유닛이 모자라다" 고 나오고, 디펜스에 쓸모없는 유닛을
    677개 채우면 통과한다. 그 검사가 다음 맵에 시키는 일이 바로
    references/why-procedural-fails.md 가 경고하는 것이다.

    대신 **틀리면 게임이 망가지는 것**만 본다. 전부 실제로 겪은 것이다.
    """
    out = []
    text = ""
    try:
        text = cli.trigger_text()
    except Exception as e:
        return [("?", f"트리거를 못 읽었습니다: {e}")]
    blocks = [b for b in text.split(TRIGGER_SEP_RE) if b.strip()]

    def owners(blk):
        mm = re.match(r'\s*Trigger\(([^)]*)\)', blk)
        return mm.group(1) if mm else ""

    humans = [p["player"] for p in read_players(cli)
              if p["slot"] in ("열림", "사람(게임)")]

    # 1) 사람 슬롯의 종족 — userselect 면 배치 유닛이 통째로 무시된다
    #
    #    **다만 늘 잘못은 아니다.** 컴까기처럼 밀리맵 컨셉을 늘린 유즈맵은
    #    사람이 본진을 짓고 종족을 골라야 하므로 일부러 쓴다. 그런 맵에서
    #    "본진 + 일꾼 4마리로 시작" 은 버그가 아니라 원하는 것이다.
    #
    #    가르는 기준은 장르 이름이 아니라 **그 슬롯에 유닛을 깔아 두었는가**
    #    다. 깔아 두었는데 선택 가능이면 그 유닛이 전부 사라진다.
    players = read_players(cli)
    bad_race = [l for l in players
                if l["slot"] in ("열림", "사람(게임)") and "선택" in l.get("race", "")]
    if bad_race:
        nums = set()
        for l in bad_race:
            try:
                nums.add(int(l.get("player") or l.get("no") or -1))
            except Exception:
                pass
        placed = collections.Counter()
        for u in cli.units():
            if u["owner"] in nums and u["type"] != scmap.START_LOCATION:
                placed[u["owner"]] += 1
        if placed:
            out.append(("!!", f"사람 슬롯 {sorted(placed)} 의 종족이 '선택 가능' "
                              f"인데 그 슬롯에 유닛을 {sum(placed.values())}기 "
                              f"깔아 두었습니다. 그 유닛은 **통째로 무시되고** "
                              f"본진 + 일꾼으로 시작합니다."))
        else:
            out.append(("?", f"사람 슬롯 {len(bad_race)}개의 종족이 '선택 가능' "
                             f"입니다. 깔아 둔 유닛이 없으니 밀리맵 컨셉을 늘린 "
                             f"맵(컴까기 등)이라면 맞습니다. 유닛을 받아서 노는 "
                             f"맵이라면 종족을 못 박으세요."))

    # 2) 사람마다 시야가 열려 있는가
    revealer_owners = {u["owner"] for u in cli.units()
                       if u["type"] == MAP_REVEALER}
    # 시야를 여는 길은 둘이다 — 그 플레이어 소유의 Map Revealer 를 깔거나,
    # `Run AI Script("Turn ON Shared Vision …")` 로 시야 가진 쪽과 나누거나.
    # 실측 563장: 리빌러 57% · AI 스크립트 72% · 둘 다 없는 맵 10%.
    # 한쪽만 보고 잡으면 멀쩡한 맵을 절반 가까이 걸러낸다.
    # 시야 공유 AI 스크립트는 네 글자 코드다: `+Vi<번호>` 가 켜기,
    # `-Vi<번호>` 가 끄기. 실제 맵은 이 꼴로 쓴다 — "Vision" 이라는
    # 글자를 찾으면 하나도 못 잡는다.
    shares_vision = bool(re.search(r'Run AI Script\w*\(\s*"[+-]Vi', text))
    missing = [p for p in humans if p not in revealer_owners]
    if missing and not shares_vision:
        out.append(("!!", f"{missing} 번 플레이어에게 Map Revealer 도 없고 "
                          f"시야를 나누는 AI 스크립트도 없습니다. 그 사람 화면은 "
                          f"깜깜합니다 — 세력 시야 공유만으로는 맵이 안 밝아집니다."))
    elif missing:
        out.append(("i", f"{missing} 번은 Map Revealer 가 없지만 AI 스크립트로 "
                         f"시야를 나눕니다."))

    # 3) 하이퍼 트리거의 주인이 **사람**이면 그 사람의 다른 Wait 가 먹통.
    #    사람 목록은 슬롯에서 읽는다 — 번호로 짐작하면 안 된다
    #    (4인 맵의 Player 5 는 컴퓨터다).
    for blk in blocks:
        if blk.count("Wait(0)") >= 20:
            ow = owners(blk)
            nums = [int(x) for x in re.findall(r'"Player (\d+)"', ow)]
            if "All players" in ow or any(p in humans for p in nums):
                out.append(("!!", f"하이퍼 트리거의 주인이 {ow} 입니다. 사람에게 "
                                  f"걸면 그 사람의 다른 웨이트 트리거가 전부 "
                                  f"먹통이 됩니다 — 컴퓨터에게 거세요."))
                break

    # 4) (뺐다) 승패에 Preserve 를 붙이는 것은 버그가 아니다.
    #    카페 강좌 확인: Defeat 은 "End scenario in defeat for current
    #    player" 라 그 플레이어의 시나리오가 끝나고 트리거가 다시 돌지
    #    않는다. 실제 인기 맵 30장 중 7장이 그렇게 쓴다. 거짓 양성이었다.

    # 5) Modify Unit ... 의 인자 차례 — **퍼센트가 먼저**다.
    #    실측에서 개수 자리는 거의 언제나 0(=전부)이다:
    #      (s,s,100,0,s) 634회 · (s,s,0,0,s) 223회 · (s,s,10,0,s) 88회
    #    개수가 큰 수면 퍼센트와 자리를 바꿔 쓴 것이다. 그렇게 쓰면
    #    회복이 아니라 체력을 그 퍼센트로 **깎는** 동작이 된다.
    suspect = []
    for kind, pct, cnt in re.findall(
            r'Modify Unit (Hit Points|Energy|Shield Points)'
            r'\([^)]*?,\s*(\d+)\s*,\s*(\d+)\s*,', text):
        if int(cnt) > 12 or int(pct) > 100:
            suspect.append(f"{kind}({pct}, {cnt})")
    if suspect:
        out.append(("!!", f"Modify Unit 의 인자 차례가 거꾸로인 것 같습니다: "
                          f"{', '.join(sorted(set(suspect))[:3])}. "
                          f"**퍼센트가 먼저**이고 개수 0 이 전부입니다 — "
                          f"실측에서 개수 자리는 거의 언제나 0 입니다. "
                          f"거꾸로 쓰면 회복이 아니라 체력을 깎습니다."))

    # 6) 비콘 상점에 밀어내기가 없으면 서 있는 동안 매 프레임 결제된다
    n = 0
    for b in blocks:
        if ("Bring(" in b and "Accumulate(" in b and "Preserve Trigger" in b
                and "Subtract" in b
                and "Move Unit" not in b and "Remove Unit" not in b
                and "Set Switch" not in b):
            n += 1
    if n:
        out.append(("!!", f"비콘 상점 {n}개에 밀어내기도 잠금도 없습니다. "
                          f"비콘 위에 서 있는 동안 매 프레임 결제됩니다."))

    # 7) 누적 조건으로 보상을 주면 첫 성공 뒤 계속 들어온다
    n = 0
    for b in blocks:
        head = b.split("Actions:")[0]
        body = b.split("Actions:")[-1]
        # 잠금으로 인정하는 것: 카운터를 깎거나(Subtract), 스위치를 잠그거나,
        # 죽음 수 잠금을 찍거나(Set To). 셋 다 없으면 계속 들어온다.
        locked = ("Subtract" in body or "Set Switch" in body
                  or re.search(r'Set Deaths\([^)]*Set To', body))
        if (re.search(r'\b(Kill|Deaths)\([^)]*At least', head)
                and "Preserve Trigger" in b
                and re.search(r'Set (Resources|Score)\([^)]*Add', body)
                and not locked):
            n += 1
    if n:
        out.append(("?", f"누적 조건({{Kill·Deaths At least}})으로 보상을 주는 "
                          f"트리거 {n}개에 카운터를 깎는 동작이 없습니다. "
                          f"첫 성공 뒤로 매 주기 보상이 들어옵니다."))

    # 8) 컴퓨터만 실행하는 안내는 아무도 못 본다
    n = 0
    for b in blocks:
        if "Display Text Message" not in b:
            continue
        ow = owners(b)
        if not ow or "All players" in ow:
            continue
        nums = [int(x) for x in re.findall(r'"Player (\d+)"', ow)]
        if nums and all(p not in humans for p in nums):
            n += 1
    if n:
        out.append(("!!", f"안내 트리거 {n}개를 컴퓨터만 실행합니다. "
                          f"Display Text Message 는 그 트리거를 실행하는 "
                          f"플레이어에게만 보입니다 — 아무도 못 봅니다."))

    # 9) **초기화하지 않은 카운터로 지는가.**
    #    `Deaths(카운터, Exactly, 0) -> Defeat` 인데 그 카운터를 어디서도
    #    `Set To` 로 찍지 않으면, 죽음 수는 처음에 0이므로 **시작하자마자
    #    진다.** 검사기를 통과하면서 0초에 지는 맵을 실제로 받았다.
    for blk in blocks:
        head = blk.split("Actions:")[0]
        body = blk.split("Actions:")[-1]
        if "Defeat();" not in body and "Victory();" not in body:
            continue
        for mm in re.finditer(r'Deaths\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,'
                              r'\s*(?:Exactly|At most)\s*,\s*0\s*\)', head):
            counter = mm.group(2)
            if not re.search(r'Set Deaths\([^)]*"' + re.escape(counter) +
                             r'"[^)]*Set To,\s*[1-9]', text):
                out.append(("!!", f'"{counter}" 죽음 수가 0이면 지는데, 그 값을 '
                                  f'어디서도 Set To 로 찍지 않습니다. 죽음 수는 '
                                  f'처음에 0이므로 **시작하자마자 집니다.**'))
                break

    # 10) **카운터로 쓰는 유닛이 맵에 실제로 있는가.**
    #     죽음 수를 변수로 쓰려면 그 유닛이 실제로 죽는 일이 없어야 한다.
    #     플레이어가 쓰는 유닛을 카운터로 삼으면 그게 죽을 때마다 값이
    #     틀어진다 — 목숨 카운터를 마린으로 잡아 마린이 죽으면 목숨이
    #     늘어나는 맵을 실제로 받았다.
    placed = collections.Counter()
    for u in cli.units():
        placed[u.get("type_name") or str(u["type"])] += 1
    #
    #     **`Subtract` 는 여기에 걸지 않는다.** 죽은 수를 하나씩 빼면서
    #     그만큼 보상하는 것은 오히려 권장하는 관용구다 (누적 조건으로
    #     무한 보상이 되는 것을 막는 방법이다). 변수로 쓰는 것은
    #     `Set To` 와 `Add` 다 — 그때만 실제 죽음이 값을 망친다.
    #     앞서 이 구분 없이 잡아 성한 키우기 맵을 틀렸다고 했다.
    used_as_var = set(re.findall(
        r'Set Deaths\(\s*"[^"]+"\s*,\s*"([^"]+)"\s*,\s*(?:Set To|Add)\b',
        text))
    dirty = sorted(c for c in used_as_var if placed.get(c))
    if dirty:
        out.append(("!!", f"죽음 수를 **변수로** 쓰는 유닛이 맵에 배치되어 "
                          f"있습니다: {dirty[:3]}. 그 유닛이 죽을 때마다 값이 "
                          f"틀어집니다. 놓을 수 없는 스펠류(Dark Swarm · "
                          f"Disruption Web · Scanner Sweep)를 쓰거나, "
                          f"scmap.MapResources.counter() 로 안전한 칸을 "
                          f"받아 쓰세요."))

    # 11) **잠금이 글자만 있고 실제로 안 읽히는가.**
    #     동작이 스위치나 죽음 수를 찍어도, 같은 트리거의 조건이 그것을
    #     읽지 않으면 잠금이 아니다. 글자만 보고 통과시킨 적이 있다.
    n = 0
    for blk in blocks:
        head = blk.split("Actions:")[0]
        body = blk.split("Actions:")[-1]
        if "Preserve Trigger" not in blk:
            continue
        if not re.search(r'Set (Resources|Score)\([^)]*Add', body):
            continue
        if not re.search(r'\b(Bring|Accumulate|Kill|Deaths)\(', head):
            continue
        # 동작이 찍는 잠금
        written = set(re.findall(r'Set Switch\(\s*"([^"]+)"', body))
        written |= set(re.findall(r'Set Deaths\(\s*"[^"]+"\s*,\s*"([^"]+)"[^)]*Set To', body))
        # 조건이 읽는 것
        readd = set(re.findall(r'Switch\(\s*"([^"]+)"', head))
        readd |= set(re.findall(r'Deaths\(\s*"[^"]+"\s*,\s*"([^"]+)"', head))
        consumed = "Subtract" in body or "Move Unit" in body or "Remove Unit" in body
        if not consumed and not (written & readd):
            n += 1
    if n:
        out.append(("?", f"보상 트리거 {n}개에 같은 트리거 안에서 도는 잠금이 "
                         f"보이지 않습니다. 다른 트리거가 잠글 수도 있으니 "
                         f"확인만 하세요 — 잠금이 정말 없으면 매 주기 들어옵니다."))

    # 12) **유닛을 통째로 지우면서 목숨은 하나만 깎는가.**
    for blk in blocks:
        body = blk.split("Actions:")[-1]
        if (re.search(r'Remove Unit At Location\([^)]*,\s*All\s*,', body)
                and re.search(r'Set Deaths\([^)]*Subtract,\s*1\s*\)', body)):
            out.append(("!!", "새어 나간 유닛을 All 로 통째 지우면서 목숨은 "
                              "하나만 깎습니다. 다섯이 새어도 목숨 하나입니다 — "
                              "한 기씩 지우세요."))
            break

    # 13) **매 프레임 안내 도배.**
    n = 0
    for blk in blocks:
        head = blk.split("Actions:")[0]
        body = blk.split("Actions:")[-1]
        if ("Preserve Trigger" not in blk
                or "Display Text Message" not in body):
            continue
        if re.search(r'\b(Switch|Countdown Timer)\(', head):
            continue
        written = set(re.findall(r'Set Switch\(\s*"([^"]+)"', body))
        written |= set(re.findall(r'Set Deaths\(\s*"[^"]+"\s*,\s*"([^"]+)"[^)]*Set To', body))
        readd = set(re.findall(r'Deaths\(\s*"[^"]+"\s*,\s*"([^"]+)"', head))
        readd |= set(re.findall(r'Switch\(\s*"([^"]+)"', head))
        if not (written & readd) and "Move Unit" not in body and "Remove" not in body:
            n += 1
    if n >= 3:
        out.append(("?", f"잠금 없이 되풀이되는 안내 트리거가 {n}개입니다. "
                         f"조건이 한동안 계속 참이면 매 프레임 도배됩니다."))

    # 14) **빈 슬롯 정리가 있는가.**
    if not re.search(r'Remove Unit\(\s*"Player \d+"\s*,\s*"Any unit"', text):
        out.append(("?", "들어오지 않은 자리를 치우는 트리거가 없습니다. "
                         "슬롯이 다 안 차면 빈 자리 유닛이 필드에 남아 "
                         "전멸 판정이 영영 참이 되지 않습니다."))

    # 15) **지형을 눈으로 본 결과를 검사로 굳힌다.**
    #     만든 맵 여섯 장을 실제 인기 유즈맵과 나란히 그려 보고서야
    #     알았다 — 트리거가 아니라 바닥이 달랐다. 그림을 안 그려도
    #     걸리도록 여기에 넣는다.
    #
    #     실측 인기 유즈맵 86장 (scmscx 내려받기 상위):
    #        1% 넘게 쓰는 타일 그룹   중앙 10개 (사분위 6~12)
    #        검은 칸(그룹 0)          중앙 0.0% (485장 중 404장이 1% 미만)
    #        비콘이 둘레와 다른 지형 위   624개 중 590개 = 94%
    try:
        W, H = m["width"], m["height"]
        g = cli.tiles(0, 0, W, H)
        cnt = collections.Counter(v >> 4 for r in g for v in r)
        void = 100.0 * cnt.get(0, 0) / (W * H)
        big = [k for k, n in cnt.items() if k and n >= W * H * 0.01]
        if void > 20:
            out.append(("!!", f"맵의 {void:.0f}% 가 **검은 칸**입니다. 실측 "
                              f"유즈맵 485장의 중앙값은 0.0% 이고 84% 가 "
                              f"1% 미만입니다. 못 걷게 막으려면 검게 뚫지 "
                              f"말고 **못 걷는 지형**(물·용암)을 까세요 — "
                              f"scmap.Palette 의 'wall' 몫."))
        elif void > 5:
            out.append(("?", f"검은 칸이 {void:.0f}% 입니다 (실측 중앙 0.0%). "
                             f"게임에서 맵에 구멍이 난 것처럼 보입니다."))
        if len(big) <= 2:
            out.append(("!!", f"1% 넘게 쓰는 타일 그룹이 {len(big)}개뿐입니다 "
                              f"(실측 중앙 10개, 아래 사분위 6개). 바닥을 한 "
                              f"가지로 깔면 어디가 길이고 어디가 발판인지 "
                              f"화면에서 안 읽힙니다."))
        # 비콘 발판
        BEACONS = {"Terran Beacon", "Protoss Beacon", "Zerg Beacon",
                   "Terran Flag Beacon", "Protoss Flag Beacon",
                   "Zerg Flag Beacon"}
        pads = same = 0
        for u in cli.units():
            if (u.get("type_name") or "") not in BEACONS:
                continue
            tx, ty = int(u["x"] // 32), int(u["y"] // 32)
            if not (6 <= tx < W - 6 and 6 <= ty < H - 6):
                continue
            here = g[ty][tx] >> 4
            away = [g[ty + dy][tx + dx] >> 4
                    for dx, dy in ((6, 0), (-6, 0), (0, 6), (0, -6))]
            away = [x for x in away if x]
            if not here or not away:
                continue
            pads += 1
            same += all(x == here for x in away)
        if pads and same * 2 > pads:
            out.append(("?", f"비콘 {pads}개 중 {same}개가 둘레와 **같은 지형** "
                             f"위에 있습니다. 실측에서는 624개 중 94% 가 따로 "
                             f"깐 발판 위였습니다 — 밟을 자리가 안 보입니다."))
    except Exception as e:
        out.append(("?", f"지형을 못 쟀습니다: {e}"))

    if not out:
        out.append(("ok", "유즈맵 바닥 검사를 모두 통과했습니다."))
    return out


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
    if n_starts and len(human) != n_starts and classify(m) == "melee":
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
                # 땅덩이(연결 성분)로 가른다. **이어지지 않는다고 결함이
                # 아니다** — 섬맵은 원래 그렇고, 실제 밀리맵 코퍼스에도
                # 스타팅마다 따로 노는 맵이 흔하다. 공수·수송으로 논다.
                comp = [0] * len(pts)
                c = 0
                for i in range(len(pts)):
                    if comp[i]:
                        continue
                    c += 1
                    comp[i] = c
                    for j in range(i + 1, len(pts)):
                        if not comp[j] and scmap.walk_reachable(grid, pts[i], pts[j]):
                            comp[j] = c
                sizes = sorted(comp.count(k) for k in range(1, c + 1))
                if c == 1:
                    out.append(("ok", f"스타팅 {len(pts)}곳이 모두 걸어서 이어집니다 "
                                      f"(뭍맵)."))
                elif classify(m) == "usemap":
                    out.append(("i", f"땅덩이 {c}개로 갈려 있습니다. 유즈맵이라면 "
                                     f"일부러 가른 것일 수 있습니다 (디펜스 경기장 등)."))
                elif c == len(pts):
                    out.append(("i", f"스타팅 {len(pts)}곳이 **모두 따로 있습니다 "
                                     f"— 섬맵**입니다. 공중·수송 없이는 못 만납니다. "
                                     f"드롭 견제와 공중 유닛이 강해지고, 초반 러시는 "
                                     f"사실상 사라집니다."))
                elif len(set(sizes)) == 1:
                    out.append(("i", f"땅덩이 {c}개에 스타팅이 {sizes[0]}곳씩 고르게 "
                                     f"있습니다 — 반섬맵입니다. 같은 덩이끼리는 지상으로 "
                                     f"싸우고 건너편은 공중·수송으로 갑니다."))
                else:
                    out.append(("!!", f"땅덩이마다 스타팅 수가 다릅니다 {sizes}. "
                                      f"같은 덩이에 여럿이 있는 쪽은 지상 러시를 "
                                      f"당하고 혼자 있는 쪽은 안 당합니다 — 자리에 "
                                      f"따라 게임이 달라집니다."))
        except Exception as e:
            out.append(("?", f"길찾기 검사를 못 했습니다: {e}"))

    # 유즈맵은 경기장을 일부러 봉인한다. 아래 검사는 밀리에만 쓴다.
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
        ref = " (밀리 실측 걷기 79%, 높은 땅 36%, 그룹 504)" if classify(m) == "melee" \
              else " (유즈 실측 그룹 150)"
        out.append(("i", f"지형: 걷기 {wpct:.0f}%, 높은 땅 {hpct:.0f}%, "
                         f"타일 그룹 {len(groups_used)}{ref}"))
        if len(groups_used) < 150 and m["n_starts"] >= 2 and classify(m) == "melee":
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
    print(f"  갈래: {'밀리맵' if classify(m) == 'melee' else '유즈맵'}")
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

    if classify(m) == "usemap":
        print("\n-- 유즈맵 (실측 329장과 견주어) --")
        for mark, text in check_usemap(_CLI[0], m):
            if mark == "!!":
                problems += 1
            print(f"  [{mark:2}] {text}")
        print("\n-- 배치와 트리거를 맞대어 --")
        try:
            sp = spatial.check(_CLI[0], m, _CLI[0].trigger_text())
        except Exception as e:
            sp = [("?", f"공간 검사를 못 했습니다: {e}")]
        if not sp:
            sp = [("i", "맞대어 볼 것이 없습니다 (경유지·제한 시간이 없습니다).")]
        for mark, textline in sp:
            if mark == "!!":
                problems += 1
            print(f"  [{mark:2}] {textline}")

        print("\n  ※ 밀리맵 잣대(대칭·종족 밸런스·스타팅 연결)는 건너뜁니다.")
        print("     유즈맵에서 그것들은 결함이 아닙니다.")
        print("     재미는 수로 못 잽니다. 그려 보고 돌려 보세요:")
        print("     python3 preview.py <맵> out.png")
        if problems:
            print(f"\n고쳐야 할 것 {problems}개.")
        else:
            print("\n걸리는 것은 없습니다.")
        return 1 if problems else 0

    print("\n-- 자리 밸런스 --")
    for mark, text in check_fairness(m):
        if mark == "!!":
            problems += 1
        print(f"  [{mark:2}] {text}")

    print("\n-- 지형 형상 --")
    for mark, text in check_shape(_CLI[0], m):
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
        m["kind"] = classify(m)
        m["basics"] = [{"mark": a, "text": b} for a, b in check_basics(cli, m)]
        if m["kind"] == "usemap":
            m["usemap"] = [{"mark": a, "text": b} for a, b in check_usemap(cli, m)]
        m["fairness"] = [{"mark": a, "text": b} for a, b in check_fairness(m)]
        m["shape"] = shape_metrics(cli, m)
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
