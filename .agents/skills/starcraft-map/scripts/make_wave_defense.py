#!/usr/bin/env python3
"""웨이브 디펜스 유즈맵 — **사각 디펜스와 다른 맵이다.**

앞서 웨이브 디펜스를 `make_square_defense.py` 로 만들고 있었다. 이름만
다른 같은 맵이었다. 실제로 두 장르는 짜임이 다르다.

| | 사각 디펜스 | **웨이브 디펜스** |
| --- | --- | --- |
| 자리 | 사람마다 **제 경기장** (벽으로 갈림) | **하나의 길을 같이** 지킨다 |
| 적 | 제 경기장 안에서 돈다 | **들머리 → 날머리**로 걸어간다 |
| 지는 것 | 제 경기장이 뚫리면 나만 진다 | **새면 모두의 목숨**이 준다 |
| 다투는 것 | 누가 오래 버티나 | 어느 길목을 누가 맡나 |

목숨이 하나니 순위표는 목숨이 될 수 없다. **잡은 수**로 건다 — 같이
지키되 누가 많이 잡았는지는 겨룬다.

실측 디펜스 78장: 유닛 중앙 294 · 트리거 187 · 로케이션 95.
`Order` 를 93% 가 쓴다 — 적을 길로 몰아 보내는 것이 이 장르의 뼈대다.
다만 **컴퓨터는 `Attack` 명령을 줘도 잘 안 움직인다.** `Patrol` 이
낫다 (docs/trigger/ai-scripts.md).

**길은 벽으로 감싼다.** 앞판은 맵 전체를 걷는 바닥으로 깔아 놓고 길을
색만 다르게 칠했다 — 그림으로는 길인데 게임에서는 벌판이었다. 다만
맵을 통째로 못 걷게 덮으면 66% 를 넘겨 튕기므로(docs/game/crashes.md),
**길 양옆 한 칸만** 막는다.

보기:
    python3 make_wave_defense.py out.scx --players 6 --waves 15
"""
from __future__ import annotations

import argparse
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scmap  # noqa: E402

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}

LANE = 13       # 길 너비 — 길과 길목 방이 맵에서 충분한 걷는 면적을 차지한다
WALLT = 1       # 길 양옆 벽 두께 — 128x128 Jungle 에서 불가 지형 41% (기존 59%)
LANE_MARGIN = 12  # 맵 가장자리에서 길까지
STOP_W, STOP_H = 12, 10   # 길목 방 (두뎃이 들어갈 만큼 넉넉히)

WAVE_TABLE = [
    ("Zerg Zergling", 6), ("Zerg Hydralisk", 5), ("Terran Marine", 6),
    ("Zerg Zergling", 9), ("Protoss Zealot", 5), ("Zerg Hydralisk", 7),
    ("Terran Firebat", 5), ("Zerg Ultralisk", 2), ("Protoss Dragoon", 4),
    ("Zerg Mutalisk", 5),
]
BOSS = ("Torrasque (Ultralisk)", 1)   # **줄여 쓰지 않는다** — 게임이 아는 이름 그대로

# 비콘 셋 — **하는 일이 서로 달라야 한다.** 이름만 다르고 다 체력만
# 고치던 판이 있었다.
SHOPS = [
    ("Terran Beacon",  "머린 4기",     100, "marine"),
    ("Protoss Beacon", "성큰 1기",     150, "sunken"),
    ("Zerg Beacon",    "체력 모두 회복", 80, "heal"),
]


def lane_turns(W: int) -> int:
    """굽이 수를 **맵 너비에서 셈한다.**

    굽이를 4로 못 박아 두었더니 128칸 맵에서 세로 다리 사이가 26칸씩
    벌어져, 그림의 절반이 아무도 안 가는 벌판이 됐다. 다리 하나가
    차지하는 폭은 길과 양옆 벽을 합쳐 15칸이니 그 간격으로 채운다.
    """
    span = W - 2 * LANE_MARGIN
    return max(3, round(span / (LANE + 2 * WALLT + 2)))


def lane_points(W, H, turns=None):
    """**굽은 길 하나**를 만든다. 들머리 왼위 → 날머리 오른아래.

    곧은 길이면 길목이 하나뿐이라 재미가 없다. 굽이마다 길목이 생긴다.
    """
    turns = lane_turns(W) if turns is None else turns
    m = LANE_MARGIN
    xs = [m + int((W - 2 * m) * i / turns) for i in range(turns + 1)]
    pts = []
    for i in range(turns + 1):
        y = m if i % 2 == 0 else H - m
        pts.append((xs[i], y))
        if i < turns:
            pts.append((xs[i + 1], y))
    return pts


def paint_lane(cli, pal, pts, W, H):
    """길을 깔고 **양옆에 벽을 세운다.** 벽부터 세우고 길을 덮는다.

    깐 길 네모를 돌려준다 — 두뎃은 **걷기 경계 두 칸 안**에 몰려 있어야
    하고(실측 89%), 이 맵에서 경계는 방 테두리가 아니라 길 가장자리다.
    """
    segs = []
    for i in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        if y0 == y1:
            x, w = min(x0, x1), abs(x1 - x0) + 1
            segs.append(("h", x, y0, w))
        else:
            y, h = min(y0, y1), abs(y1 - y0) + 1
            segs.append(("v", x0, y, h))

    def clip(x, y, w, h):
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(W, x + w), min(H, y + h)
        return (x0, y0, x1 - x0, y1 - y0) if x1 > x0 and y1 > y0 else None

    half = LANE // 2
    laid = []
    for kind, a, b, n in segs:                      # 벽 먼저
        for side in (-1, 1):
            if kind == "h":
                oy = b + side * (half + 1) - (WALLT - 1 if side < 0 else 0)
                r = clip(a - half - WALLT, oy, n + 2 * (half + WALLT), WALLT)
            else:
                ox = a + side * (half + 1) - (WALLT - 1 if side < 0 else 0)
                r = clip(ox, b - half - WALLT, WALLT, n + 2 * (half + WALLT))
            if r:
                pal.fill(cli, "wall", *r)
    for kind, a, b, n in segs:                      # 길로 덮는다
        r = clip(a - half, b - half, n + 2 * half, LANE) if kind == "h" \
            else clip(a - half, b - half, LANE, n + 2 * half)
        if kind == "h":
            r = clip(a - half, b - half, n + 2 * half, LANE)
        else:
            r = clip(a - half, b - half, LANE, n + 2 * half)
        if r:
            pal.fill(cli, "path", *r)
            laid.append(r)
    return laid


def stop_boxes(pts, players, W, H):
    """길목 — 길에 **맞붙여** 낸다. 붙여야 벽에 문이 뚫린다.

    굽이를 앞에서부터 차례로 집으면 길목이 들머리 쪽에 몰려, 길의 뒷
    절반을 아무도 지키지 않게 된다 (여섯이 왼쪽 반에만 섰다). 굽이를
    **고르게 나눠** 집는다.
    """
    inner = pts[1:-1]
    if not inner:
        return []
    if players >= len(inner):
        picks = list(range(len(inner)))[:players]
    else:
        picks = [round(i * (len(inner) - 1) / (players - 1))
                 for i in range(players)] if players > 1 else [len(inner) // 2]
    half = LANE // 2
    out = []
    for idx in picks:
        x, y = inner[idx]
        sx = max(2, min(W - STOP_W - 2, x - STOP_W // 2))
        sy = y + half + 1 if y < H // 2 else y - half - 1 - STOP_H
        sy = max(2, min(H - STOP_H - 2, sy))
        if any(abs(sx - ox) < STOP_W and abs(sy - oy) < STOP_H
               for ox, oy, _, _ in out):
            continue
        out.append((sx, sy, STOP_W, STOP_H))
    return out


# ---------------------------------------------------------------- 트리거

def build_triggers(players, waves, enemy, boss_p, stops, ways, res):
    """웨이브 디펜스 트리거.

    **사각 디펜스와 다른 곳:** 목숨이 사람마다가 아니라 **하나**다.
    새면 모두가 잃는다. 그래서 승패도 한꺼번에 난다.

    목숨이 공용이라 깎는 일은 **적(컴퓨터)** 이 한다. 그런데
    `Display Text Message` 는 그 트리거를 실행하는 플레이어에게만
    보이므로, 컴퓨터가 깎고 **알림 칸**을 세우면 사람들이 그것을 보고
    각자 글을 띄운다. 그러지 않으면 아무도 새는 것을 모른다.
    """
    HUM = [f"Player {i}" for i in range(1, players + 1)]
    all_h = ", ".join(f'"{h}"' for h in HUM)
    WAVE = res.counter("웨이브")
    LIFE = res.counter("공용 목숨")
    FLASH = res.counter("샜다는 알림")
    SEEN = res.counter("본 웨이브")
    LOCK = res.counter("병력 재지급 잠금")
    res.claim_countdown(enemy)
    res.claim_leaderboard("Kills")

    T = []
    add = T.append

    # 바닥 가운데 **트리거로 되는 부분**만 여기서 쓴다. 종족·시야는
    # 본체에서 이미 했다 (`setup_usemap_players` · `reveal_for_all`).
    T += scmap.hyper_triggers(enemy)
    T += scmap.absent_player_cleanup(players, enemy)

    # 안내 — 비콘이 무엇을 하는지 **미리 적는다**
    shop_line = " · ".join(f"{lbl} {cost}" for _, lbl, cost, _ in SHOPS)
    add(f'''Trigger({all_h}){{
Conditions:
\tAlways();

Actions:
\tSet Resources("Current Player", Set To, 300, ore);
\tDisplay Text Message(Always Display, "\\x04웨이브 디펜스\\x02 — 길을 \\x07같이\\x02 지킵니다. 새면 \\x08모두\\x02의 목숨이 줍니다.");
\tDisplay Text Message(Always Display, "\\x03길목 비콘: \\x07{shop_line}\\x03. 순위표는 \\x07잡은 수\\x03입니다.");
\tSet Mission Objectives("\\x04웨이브 디펜스\\x02\\n\\x03- 적은 들머리에서 날머리로 걸어갑니다\\n- 길목 방에 자리를 잡고 막으세요\\n- 비콘: {shop_line}\\n- 공용 목숨 20개를 다 잃으면 함께 집니다\\n- 웨이브 {waves}개를 막고 보스를 잡으면 이깁니다");
}}''')

    # 공용 목숨·웨이브 칸을 세운다 (컴퓨터가 한 번)
    add(f'''Trigger("{enemy}"){{
Conditions:
\tAlways();

Actions:
\tSet Deaths("{enemy}", "{LIFE}", Set To, 20);
\tSet Countdown Timer(Set To, 30);
}}''')

    add(f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tLeader Board Kills("\\x07잡은 수", "Any unit");
\tPreserve Trigger();
}}''')

    T += scmap.part_wave_clock(enemy, WAVE, waves, seconds=35)

    # 웨이브 — 들머리에 낸다
    for w in range(1, waves + 1):
        unit, n = WAVE_TABLE[(w - 1) % len(WAVE_TABLE)]
        n += (w - 1) // len(WAVE_TABLE) * 2
        sw = res.switch(f"웨이브 {w} 스폰")
        add(f'''Trigger("{enemy}"){{
Conditions:
\tDeaths("{enemy}", "{WAVE}", Exactly, {w});
\tSwitch("Switch {sw}", not set);

Actions:
\tSet Switch("Switch {sw}", set);
\tCreate Unit("{enemy}", "{unit}", {n}, "Entry");
\tPreserve Trigger();
}}''')
        T += scmap.part_announce_once(
            HUM, f'Deaths("{enemy}", "{WAVE}", Exactly, {w});', SEEN, w,
            [f'\\x07웨이브 {w}\\x02 — {unit} x{n}'],
            wav="sound\\\\Misc\\\\Button.wav")

    # 보스
    bu, bn = BOSS
    bsw = res.switch("보스 스폰")
    add(f'''Trigger("{boss_p}"){{
Conditions:
\tDeaths("{enemy}", "{WAVE}", At least, {waves + 1});
\tSwitch("Switch {bsw}", not set);

Actions:
\tSet Switch("Switch {bsw}", set);
\tCreate Unit with Properties("{boss_p}", "{bu}", {bn}, "Entry", 1);
\tPreserve Trigger();
}}''')
    T += scmap.part_announce_once(
        HUM, f'Deaths("{enemy}", "{WAVE}", At least, {waves + 1});',
        SEEN, waves + 1,
        [f'\\x06보스\\x02 — {bu}. 이것만 잡으면 끝입니다.'],
        wav="sound\\\\Misc\\\\PowerDown.wav")

    # 길 안내 — 적과 보스 모두 길을 따라 간다
    for who in (enemy, boss_p):
        T += scmap.part_patrol_path(who, ways, "patrol")

    # 누수 — **컴퓨터가 한 기씩** 지우고 공용 목숨을 깎는다.
    # 통째로 지우면 다섯이 새어도 목숨이 하나만 준다.
    for who in (enemy, boss_p):
        add(f'''Trigger("{enemy}"){{
Conditions:
\tBring("{who}", "Any unit", "Exit", At least, 1);

Actions:
\tRemove Unit At Location("{who}", "Any unit", 1, "Exit");
\tSet Deaths("{enemy}", "{LIFE}", Subtract, 1);
\tSet Deaths("{enemy}", "{FLASH}", Set To, 1);
\tMinimap Ping("Exit");
\tPreserve Trigger();
}}''')

    # 샌 것을 사람들이 본다. 사람(1~6)이 먼저 돌고 적(7)이 뒤에 꺼서,
    # 한 번 샐 때마다 각자 한 줄씩 본다.
    add(f'''Trigger({all_h}){{
Conditions:
\tDeaths("{enemy}", "{FLASH}", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x06샜습니다!\\x02 공용 목숨이 하나 줄었습니다.");
\tPlay WAV("sound\\\\Misc\\\\PowerDown.wav", 500);
\tPreserve Trigger();
}}''')
    add(f'''Trigger("{enemy}"){{
Conditions:
\tDeaths("{enemy}", "{FLASH}", At least, 1);

Actions:
\tSet Deaths("{enemy}", "{FLASH}", Set To, 0);
\tPreserve Trigger();
}}''')

    # 길목마다 — 비콘 셋, 잡은 값, 병력 재지급
    for i in range(len(stops)):
        p = f"Player {i + 1}"
        home = f"Stop{i + 1}"
        for k, (_, label, cost, kind) in enumerate(SHOPS):
            if kind == "marine":
                eff = [f'\tCreate Unit("{p}", "Terran Marine", 4, "{home}");']
            elif kind == "sunken":
                eff = [f'\tCreate Unit("{p}", "Zerg Sunken Colony", 1, "{home}");']
            else:
                eff = [f'\tModify Unit Hit Points("{p}", "Men", 100, 0, "{home}");',
                       f'\tModify Unit Shield Points("{p}", "Men", 100, 0, "{home}");']
            T += scmap.part_beacon_shop(p, f"{home} Shop{k + 1}", cost,
                                        eff, label, push_to=home)
        add(scmap.kill_bounty(p, 25, per_score=50))
        T += scmap.part_respawn(p, "Terran Marine", home, count=4,
                                cooldown_counter=LOCK,
                                guard=f'Deaths("{enemy}", "{LIFE}", At least, 1);')

    # 패배 — 공용 목숨이 0
    add(f'''Trigger({all_h}){{
Conditions:
\tDeaths("{enemy}", "{LIFE}", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "\\x06목숨이 다했습니다.");
\tDefeat();
}}''')

    # 승리 — 보스가 사라졌다
    T += scmap.part_win(HUM, [
        f'Switch("Switch {bsw}", set);',
        f'Deaths("{enemy}", "{LIFE}", At least, 1);',
        f'Command("{boss_p}", "Any unit", Exactly, 0);'],
        msg="\\x07보스를 잡았습니다! 길을 지켰습니다.")
    return T


# ---------------------------------------------------------------- 본체

def main(argv=None):
    ap = argparse.ArgumentParser(description="웨이브 디펜스 유즈맵")
    ap.add_argument("out")
    ap.add_argument("--players", type=int, default=6)
    ap.add_argument("--waves", type=int, default=15)
    ap.add_argument("--size", default="128x128")
    ap.add_argument("--tileset", default="jungle", choices=sorted(TILESETS))
    ap.add_argument("--name", default="웨이브 디펜스")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out}\n  --force 를 주세요.")
        return 1
    if not (1 <= a.players <= 6):
        ap.error("사람은 1~6명입니다 (적·보스로 슬롯 둘을 더 씁니다).")

    W, H = (int(v) for v in a.size.lower().split("x"))
    ts = TILESETS[a.tileset]
    rng = random.Random(a.seed)
    enemy_no, boss_no = a.players + 1, a.players + 2
    enemy, boss_p = f"Player {enemy_no}", f"Player {boss_no}"

    print(f"웨이브 디펜스 {W}x{H} {a.tileset}, {a.players}명, {a.waves}웨이브")
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)

    pal = scmap.Palette(cli, ts, rng, "usemap")
    print(f"  지형: {pal.describe()}")

    # **맵 전체를 벽으로 덮고 길만 뚫는다.**
    #
    # 앞판은 거꾸로였다 — 걷는 바닥으로 다 깔고 길을 색만 다르게 칠했다.
    # 그림으로는 길인데 게임에서는 사방이 트인 벌판이라, 적이 길로 갈
    # 까닭도 사람이 길목에 설 까닭도 없었다. 걷는 자리의 39%가 길도
    # 방도 아닌 빈 벌판이었다 (실측).
    #
    # 그렇다고 벽으로 덮는 것이 언제나 옳지도 않다. **못 걷는 지형이
    # 66% 를 넘으면 튕긴다**(docs/game/crashes.md). 이전 폭(9)·두께(3)은
    # 128x128 Jungle 에서 59% 를 막았다. 길을 13칸, 벽을 1칸으로 바꾸면
    # 41% 가 막혀 여유가 생기고 벽 경로도 유지된다.
    pal.fill(cli, "wall", 0, 0, W, H)

    pts = lane_points(W, H)
    print(f"굽은 길 하나를 냅니다 (꺾임 {lane_turns(W)}번, 굽이 {len(pts)}곳, "
          f"양옆 벽 {WALLT}칸)...")
    lane_rects = paint_lane(cli, pal, pts, W, H)

    stops = stop_boxes(pts, a.players, W, H)
    print(f"길목 {len(stops)}곳을 냅니다...")
    for (sx, sy, sw, sh) in stops:
        scmap.room(cli, pal, sx, sy, sw, sh, rim=1)
    for i, (sx, sy, sw, sh) in enumerate(stops):        # 비콘 발판
        for k in range(3):
            scmap.pad(cli, pal, sx + 1 + k * 3, sy + 1, 2, 2)

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, a.players, [enemy_no, boss_no])
    n_up = scmap.setup_usemap_upgrades(cli, a.players, free_levels=0,
                                       max_level=3, mineral=75, gas=0, time=15)
    n_tech = scmap.setup_usemap_tech(cli, which=(0, 3, 5, 6), mineral=100,
                                     gas=0, time=15, available="all")
    print(f"  업그레이드 {n_up}가지 · 기술 {n_tech}가지")

    print("로케이션을 놓습니다...")
    def loc(name, x, y, w, h):
        cli.edit("location", "add", cli.path, str(max(0, x)), str(max(0, y)),
                 str(min(W, x + w)), str(min(H, y + h)), "--tiles",
                 "--name", name)
    loc("Entry", pts[0][0] - 4, pts[0][1] - 4, 8, 8)
    loc("Exit", pts[-1][0] - 4, pts[-1][1] - 4, 8, 8)
    for i, (x, y) in enumerate(pts):
        loc(f"Way{i + 1}", x - 3, y - 3, 6, 6)
    for i, (sx, sy, sw, sh) in enumerate(stops):
        loc(f"Stop{i + 1}", sx, sy, sw, sh)
        for k in range(3):
            loc(f"Stop{i + 1} Shop{k + 1}", sx + 1 + k * 3, sy + 1, 2, 2)

    print("스타팅과 시작 유닛을 놓습니다...")
    for i in range(1, a.players + 1):
        sx, sy, sw, sh = stops[(i - 1) % len(stops)]
        cli.place(scmap.START_LOCATION, sx + sw // 2, sy + sh - 2, owner=i)
        for k in range(4):
            cli.place("Terran Marine", sx + 2 + k, sy + sh - 3, owner=i)
    cli.place(scmap.START_LOCATION, pts[0][0], pts[0][1], owner=enemy_no)
    cli.place(scmap.START_LOCATION, pts[0][0] + 2, pts[0][1] + 2, owner=boss_no)

    print("비콘을 놓습니다 (하는 일이 셋 다 다릅니다)...")
    for i, (sx, sy, sw, sh) in enumerate(stops):
        for k, (beacon, _, _, _) in enumerate(SHOPS):
            cli.place(beacon, sx + 1 + k * 3, sy + 1, owner=i + 1)

    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)

    print("방 테두리를 두뎃으로 꾸밉니다...")
    # **길 가장자리에 놓는다.** 길목 방은 비콘·병력·스타팅으로 꽉 차서
    # 한 칸도 안 남았다 (0개가 나왔다). 두뎃이 몰려야 할 곳은 어차피
    # 걷기 경계 두 칸 안이고, 이 맵의 경계는 길 가장자리다.
    #
    # `Map Revealer` 는 비켜 둘 필요가 없다 — 날아다니는 것이라 지형과
    # 겹쳐도 그만인데, 16칸마다 깔려 있어 비켜 두면 맵이 통째로 막힌다.
    _clear = [(u["x"] // 32 - 2, u["y"] // 32 - 2, 5, 5) for u in cli.units()
              if u.get("type_name") != "Map Revealer"]
    nd = scmap.decorate_rim(cli, ts, lane_rects + list(stops), rng,
                            keep_clear=_clear)
    print(f"  두뎃 {nd}개")

    print("브리핑을 짭니다...")
    cli.apply_briefing(scmap.briefing_text(
        [f"길 하나를 {a.players}명이 같이 지킵니다.",
         f"적은 들머리에서 날머리까지 걸어갑니다. 웨이브는 {a.waves}개입니다.",
         "날머리에 닿으면 공용 목숨이 하나 줍니다. 목숨은 20개입니다.",
         "길목 방 비콘에서 " + " · ".join(f"{l} {c}" for _, l, c, _ in SHOPS) + " 에 삽니다.",
         "마지막 웨이브 뒤 보스를 잡으면 함께 이깁니다."],
        objectives=f"웨이브 {a.waves}개를 막고 보스를 잡는다",
        portrait="Terran Marine"))

    print("트리거를 짭니다...")
    res = scmap.MapResources(in_play=scmap.units_in_play(cli))
    ways = [f"Way{i + 1}" for i in range(len(pts))]
    T = build_triggers(a.players, a.waves, enemy, boss_p,
                       stops, ways, res)
    cli.apply_triggers(scmap.TRIGGER_SEP.join(T))
    print("  " + res.describe())

    try:
        used = scmap.units_in_play(cli, cli.trigger_text())
    except Exception:
        used = None

    if a.name:
        cli.set_map_name(a.name,
            f"{a.players}명이 굽은 길 하나를 같이 지킵니다. 적이 날머리에 닿으면 "
            f"공용 목숨이 하나 줍니다(20개). 길목 방 비콘에서 "
            + " · ".join(f"{l} {c}" for _, l, c, _ in SHOPS)
            + f" 에 삽니다. 웨이브 {a.waves}개를 막고 보스를 잡으면 이깁니다.")

    info = cli.info()
    print(f"\n만들었습니다: {a.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']}  "
          f"유닛 {info['units']}  로케이션 {info['locations']}  "
          f"트리거 {info['triggers']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
