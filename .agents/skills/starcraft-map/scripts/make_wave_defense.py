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
    python3 make_wave_defense.py out.scx --config recipe_profiles/your_wave_defense_profile.json
"""
from __future__ import annotations

import argparse
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scmap  # noqa: E402
import recipe_config as profile

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}

LANE = 13       # 길 너비 — 길과 길목 방이 맵에서 충분한 걷는 면적을 차지한다
WALLT = 1       # 길 양옆 벽 두께 — 128x128 Jungle 에서 불가 지형 41% (기존 59%)
LANE_MARGIN = 12  # 맵 가장자리에서 길까지
STOP_W, STOP_H = 12, 10   # 길목 방 (두뎃이 들어갈 만큼 넉넉히)

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

def build_triggers(cfg, enemy, boss_p, stops, ways, res):
    players=cfg["rules"]["players"]
    wave_defs=cfg["waves"][:cfg["rules"]["waves"]]
    shops=cfg["shops"]
    units,labels,msg=cfg["units"],cfg["labels"],cfg["text"]["messages"]
    lives=cfg["rules"]["shared_lives"]
    waves=cfg["rules"]["waves"]
    HUM=[f"Player {i}" for i in range(1,players+1)]
    all_h=", ".join(f'"{h}"' for h in HUM)
    WAVE=res.counter("wave progress")
    LIFE=res.counter("shared lives")
    FLASH=res.counter("leak message")
    SEEN=res.counter("announced wave")
    LOCK=res.counter("respawn lock")
    res.claim_countdown(enemy);res.claim_leaderboard("Kills")
    T=list(scmap.hyper_triggers(enemy))+scmap.absent_player_cleanup(players,enemy)
    shop_summary=" · ".join(f'{x["label"]} {x["cost"]}' for x in shops)
    opening=msg["intro"].format(players=players,waves=waves,lives=lives,shops=shop_summary)
    T.append(f'''Trigger({all_h}){{
Conditions:
	Always();

Actions:
	Set Resources("Current Player", Set To, {cfg["starting_resources"]["minerals"]}, ore);
	Set Resources("Current Player", Set To, {cfg["starting_resources"]["gas"]}, gas);
	Display Text Message(Always Display, "{opening}");
	Set Mission Objectives("{msg["objectives"].format(players=players,waves=waves,lives=lives,shops=shop_summary)}");
}}''')
    T.append(f'''Trigger("{enemy}"){{
Conditions:
	Always();

Actions:
	Set Deaths("{enemy}", "{LIFE}", Set To, {lives});
	Set Countdown Timer(Set To, {cfg["rules"]["initial_timer"]});
}}''')
    T += scmap.part_leaderboard(msg["leaderboard"],kind="Kills")
    T += scmap.part_wave_clock(enemy,WAVE,waves,seconds=cfg["rules"]["wave_interval_seconds"])
    for w,wave in enumerate(wave_defs,1):
        unit=wave["unit"]
        count=wave["count"]+(w-1)*cfg["rules"]["wave_growth"]
        sw=res.switch(f"wave {w} spawn")
        T.append(f'''Trigger("{enemy}"){{
Conditions:
	Deaths("{enemy}", "{WAVE}", Exactly, {w});
	Switch("Switch {sw}", not set);

Actions:
	Set Switch("Switch {sw}", set);
	Create Unit("{enemy}", "{unit}", {count}, "{labels["entry"]}");
	Preserve Trigger();
}}''')
        announcement=msg["wave"].format(number=w,total=waves,unit=unit,count=count)
        T+=scmap.part_announce_once(HUM,f'Deaths("{enemy}", "{WAVE}", Exactly, {w});',
            SEEN,w,[announcement],wav=cfg["text"].get("wave_wav","sound\\Misc\\Button.wav"))
    boss_switch=res.switch("boss spawn")
    boss_msg=msg["boss"].format(unit=units["boss"],name=labels["boss_name"])
    T.append(f'''Trigger("{boss_p}"){{
Conditions:
	Deaths("{enemy}", "{WAVE}", At least, {waves+1});
	Switch("Switch {boss_switch}", not set);

Actions:
	Set Switch("Switch {boss_switch}", set);
	Create Unit with Properties("{boss_p}", "{units["boss"]}", {cfg["rules"]["boss_count"]}, "{labels["entry"]}", 1);
	Preserve Trigger();
}}''')
    T+=scmap.part_announce_once(HUM,f'Deaths("{enemy}", "{WAVE}", At least, {waves+1});',
        SEEN,waves+1,[boss_msg],wav=cfg["text"].get("boss_wav","sound\\Misc\\PowerDown.wav"))
    T+=scmap.part_patrol_path(enemy,ways,cfg["rules"]["enemy_order"])
    T+=scmap.part_patrol_path(boss_p,ways,cfg["rules"]["enemy_order"])
    for who in (enemy,boss_p):
        T.append(f'''Trigger("{enemy}"){{
Conditions:
	Bring("{who}", "Any unit", "{labels["exit"]}", At least, 1);

Actions:
	Remove Unit At Location("{who}", "Any unit", 1, "{labels["exit"]}");
	Set Deaths("{enemy}", "{LIFE}", Subtract, 1);
	Set Deaths("{enemy}", "{FLASH}", Set To, 1);
	Minimap Ping("{labels["exit"]}");
	Preserve Trigger();
}}''')
    T.append(f'''Trigger({all_h}){{
Conditions:
	Deaths("{enemy}", "{FLASH}", At least, 1);

Actions:
	Display Text Message(Always Display, "{msg["leak"]}");
	Play WAV("sound\\Misc\\PowerDown.wav", 500);
	Preserve Trigger();
}}''')
    T.append(f'''Trigger("{enemy}"){{
Conditions:
	Deaths("{enemy}", "{FLASH}", At least, 1);

Actions:
	Set Deaths("{enemy}", "{FLASH}", Set To, 0);
	Preserve Trigger();
}}''')
    for i,stop in enumerate(stops):
        player=f"Player {i+1}";home=f'{labels["stop_prefix"]}{i+1}'
        for k,shop in enumerate(shops):
            shoploc=f'{home} {labels["shop_suffix"]}{k+1}'
            vals={"player":player,"home":home,"shop":shoploc,"count":shop.get("count",1),
                  "unit":shop.get("unit",""),"cost":shop["cost"]}
            eff=[f'\t{line.format_map(vals)};' for line in shop["actions"]]
            T+=scmap.part_beacon_shop(player,shoploc,shop["cost"],eff,shop["receipt"],push_to=home)
        T.append(scmap.kill_bounty(player,cfg["rules"]["bounty_per_kill"],
                                   per_score=cfg["rules"]["bounty_score_step"]))
        T+=scmap.part_respawn(player,units["starting_unit"],home,
             count=cfg["rules"]["starting_unit_count"],cooldown_counter=LOCK,
             guard=f'Deaths("{enemy}", "{LIFE}", At least, 1);')
    T.append(f'''Trigger({all_h}){{
Conditions:
	Deaths("{enemy}", "{LIFE}", Exactly, 0);

Actions:
	Display Text Message(Always Display, "{msg["defeat"]}");
	Defeat();
}}''')
    T+=scmap.part_win(HUM,[f'Switch("Switch {boss_switch}", set);',
        f'Deaths("{enemy}", "{LIFE}", At least, 1);',
        f'Command("{boss_p}", "Any unit", Exactly, 0);'],msg=msg["victory"])
    return T


# ---------------------------------------------------------------- 본체

def main(argv=None):
    ap=argparse.ArgumentParser(description="AI-profiled shared-lane wave defense")
    ap.add_argument("out")
    profile.add_profile_arguments(ap,"wave_defense")
    ap.add_argument("--install",default=None)
    ap.add_argument("--force",action="store_true")
    a=ap.parse_args(argv)
    cfg=profile.load_profile(a.config,"wave_defense")
    rules,shops,labels,units=cfg["rules"],cfg["shops"],cfg["labels"],cfg["units"]
    a.players=rules["players"];a.waves=rules["waves"]
    W,H=cfg["map"]["size"];a.size=f"{W}x{H}"
    a.tileset=cfg["map"]["tileset"];a.name=cfg["map"]["name"];a.seed=cfg["map"]["seed"]
    global LANE,WALLT,LANE_MARGIN,STOP_W,STOP_H
    LANE,WALLT,LANE_MARGIN,STOP_W,STOP_H=(rules[k] for k in ("lane_width","wall_thickness","lane_margin","stop_width","stop_height"))

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
    scmap.setup_usemap_players(cli,a.players,[enemy_no,boss_no],race=cfg["players"]["race"])
    cli.edit("player","set",cli.path,str(enemy_no),"--race",cfg["players"]["enemy_race"],"--slot","computer")
    cli.edit("player","set",cli.path,str(boss_no),"--race",cfg["players"]["boss_race"],"--slot","computer")
    n_up,n_tech=profile.configure_progression(cli,cfg,a.players)
    print(f"  profile upgrades {n_up} · technologies {n_tech}")

    print("로케이션을 놓습니다...")
    def loc(name, x, y, w, h):
        cli.edit("location", "add", cli.path, str(max(0, x)), str(max(0, y)),
                 str(min(W, x + w)), str(min(H, y + h)), "--tiles",
                 "--name", name)
    loc(labels["entry"], pts[0][0] - 4, pts[0][1] - 4, 8, 8)
    loc(labels["exit"], pts[-1][0] - 4, pts[-1][1] - 4, 8, 8)
    for i, (x, y) in enumerate(pts):
        loc(f"{labels['way_prefix']}{i + 1}", x - 3, y - 3, 6, 6)
    for i, (sx, sy, sw, sh) in enumerate(stops):
        loc(f"{labels['stop_prefix']}{i + 1}", sx, sy, sw, sh)
        for k in range(len(shops)):
            loc(f"{labels['stop_prefix']}{i + 1} {labels['shop_suffix']}{k + 1}", sx + 1 + k * 3, sy + 1, 2, 2)

    print("스타팅과 시작 유닛을 놓습니다...")
    for i in range(1, a.players + 1):
        sx, sy, sw, sh = stops[(i - 1) % len(stops)]
        cli.place(scmap.START_LOCATION, sx + sw // 2, sy + sh - 2, owner=i)
        for k in range(rules["starting_unit_count"]):
            cli.place(units["starting_unit"],sx+2+k,sy+sh-3,owner=i)
    cli.place(scmap.START_LOCATION, pts[0][0], pts[0][1], owner=enemy_no)
    cli.place(scmap.START_LOCATION, pts[0][0] + 2, pts[0][1] + 2, owner=boss_no)
    cli.place(units["enemy_entry_marker"],pts[0][0],pts[0][1],owner=12)

    print("비콘을 놓습니다 (하는 일이 셋 다 다릅니다)...")
    for i,(sx,sy,sw,sh) in enumerate(stops):
        for k,shop in enumerate(shops):
            bx,by=sx+1+k*3,sy+1
            cli.place(shop["beacon"],bx,by,owner=i+1)
            cli.place(shop["icon"],bx,by+3,owner=12)

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

    names={}
    for shop in shops:
        if shop["icon"] in names and names[shop["icon"]] != shop["icon_name"]:
            raise scmap.CliError(f"unit type {shop['icon']} has conflicting map display names")
        names[shop["icon"]]=shop["icon_name"]
    name_cfg=dict(cfg);name_cfg["unit_names"]={**names,**cfg.get("unit_names",{})}
    profile.apply_unit_names(cli,name_cfg)
    res=scmap.MapResources(in_play=scmap.units_in_play(cli))
    ways=[f"{labels['way_prefix']}{i+1}" for i in range(len(pts))]
    cli.apply_triggers(scmap.TRIGGER_SEP.join(build_triggers(cfg,enemy,boss_p,stops,ways,res)))
    profile.apply_profile_metadata(cli,cfg,a.players)

    info = cli.info()
    print(f"\n만들었습니다: {a.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']}  "
          f"유닛 {info['units']}  로케이션 {info['locations']}  "
          f"트리거 {info['triggers']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
