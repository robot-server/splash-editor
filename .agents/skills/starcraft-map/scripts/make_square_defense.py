#!/usr/bin/env python3
"""사각 디펜스 유즈맵 — 지형·유닛·트리거까지 다 만든다.

유즈맵 지형은 자연 경관이 아니라 **직사각형 기하**다. 인기 디펜스 맵을
뜯어 보면 벽으로 두른 네모 경기장을 격자로 늘어놓은 것이 전부다.
능선과 절벽을 설계하는 밀리맵과 달리 이건 계산으로 정확히 만들 수 있다.

경기장 하나의 짜임 (인기 디펜스 맵에서 본뜸):

    ┌────────────────────┐   바깥 통로  : 적이 도는 길
    │ ╔════════════════╗ │   가운데 벽  : 걸어서 못 넘는다
    │ ║ ┌────────────┐ ║ │   가운데 섬  : 플레이어가 지키는 자리
    │ ║ │   플레이어  │ ║ │
    │ ║ └────────────┘ ║ │   적은 북서에서 나와 시계 방향으로 돌고,
    │ ╚════════════════╝ │   서쪽 출구에 닿으면 목숨이 하나 준다.
    └────────────────────┘

바닥 타일은 타일셋 속성에서 걷기·건설 가능 여부를 확인해 고른다. 타일 값 하나로 도배하면 주기성 100% 가 되므로
바닥은 단색으로 반듯하게 둔다 — 이 경기장은 네모난 방 구조로 설계한다.

보기:
    python3 make_square_defense.py out.scx --config recipe_profiles/your_square_defense_profile.json
"""
from __future__ import annotations

import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError
import recipe_config as profile

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}

# 세는 데 쓰는 유닛. 맵에 한 번도 놓지 않으므로 죽음 수가 순수한 변수가
# 된다. **unused unit 은 쓰지 않는다** (허락을 받아야 하는 것이다).

# ---------------------------------------------------------------- 자리잡기

def layout(players, W, H, arena_w=38, arena_h=38, gap=3):
    """경기장을 격자로 늘어놓는다.

    **경기장 크기를 먼저 정하고 그만큼만 쓴다.** 맵 크기에 맞춰 늘리지
    않는다 — 참고한 실제 디펜스 맵의 경기장도 40칸 남짓이다. 남는 자리는
    검게 둔다. 다만 **무조건 작게 만들라는 뜻은 아니다.** 경기장 크기는
    통로 폭·사거리·웨이브 규모가 정한다.

    앞선 판에는 가운데에 공용 띠를 두었는데 아무도 갈 수 없는 죽은
    땅이었다 (경기장이 벽으로 봉인되어 있어 닿지 못한다).
    """
    cols = min(3, players)
    rows = (players + cols - 1) // cols
    used_w = cols * arena_w + (cols - 1) * gap
    used_h = rows * arena_h + (rows - 1) * gap
    if used_w > W - 4 or used_h > H - 4:
        raise CliError(
            f"경기장이 맵보다 큽니다 ({used_w}x{used_h} > {W}x{H}). "
            f"--size 를 키우거나 사람 수를 줄이세요.")
    ox, oy = (W - used_w) // 2, (H - used_h) // 2
    boxes = []
    for i in range(players):
        r, c = divmod(i, cols)
        boxes.append((ox + c * (arena_w + gap), oy + r * (arena_h + gap),
                      arena_w, arena_h))
    return boxes


def floor_tile(cli, tileset, rng):
    """그 타일셋에서 실제로 바닥으로 많이 쓰인 그룹의 타일을 고른다.

    "걷기·짓기 되는 첫 번째 타일"처럼 규칙으로 고르면 Ice 를 시켰는데
    흙바닥이 나온다. 실측 분포에서 뽑아야 그 타일셋처럼 보인다.
    """
    tiles = scmap.tileset_tiles(cli, tileset)
    groups = sorted({t >> 4 for t in tiles if t >= 32})
    terrain = scmap.terrain_groups(cli, tileset)
    levels = sorted({row[0] for row in terrain.values() if row[2]})
    for level in levels:
        for g in groups:
            row = terrain.get(g)
            if not row or row[0] != level or not (row[2] or (len(row) > 5 and row[5] == 0xFFFF)):
                continue
            good = [t for t in range(g * 16, g * 16 + 16)
                    if t in tiles and tiles[t][1] and tiles[t][2]]
            if good:
                return rng.choice(good), g
    for t, p in sorted(tiles.items()):          # 물러설 곳
        if t >= 16 and p[1] and p[2]:
            return t, t >> 4
    raise CliError("바닥으로 쓸 타일을 찾지 못했습니다.")


# ---------------------------------------------------------------- 트리거

def build_triggers(cfg, enemy, boss_p, arena_names):
    rules,units,labels,msg=cfg["rules"],cfg["units"],cfg["labels"],cfg["text"]["messages"]
    players=rules["players"];waves=rules["waves"];wave_defs=cfg["waves"][:waves];shops=cfg["shops"]
    wave_state,life_state,seen_state,revive_lock=(units[k] for k in
        ("wave_counter","life_counter","seen_counter","revive_lock"))
    boss_switch=waves*players+1
    clear_switch=boss_switch+players
    HUMANS=",".join(f'"Player {p}"' for p in range(1,players+1))
    T=list(scmap.hyper_triggers(enemy))+scmap.absent_player_cleanup(players,enemy)
    shop_info=" · ".join(f'{s["label"]} {s["cost"]} ore' for s in shops)
    objective=msg["objectives"].format(waves=waves,lives=rules["shared_lives"],shops=shop_info)
    T.append(f'''Trigger({HUMANS}){{
Conditions:
\tAlways();

Actions:
\tSet Resources("Current Player", Set To, {cfg["starting_resources"]["minerals"]}, ore);
\tSet Resources("Current Player", Set To, {cfg["starting_resources"]["gas"]}, gas);
\tSet Deaths("Current Player", "{life_state}", Set To, {rules["shared_lives"]});
\tSet Score("Current Player", Set To, {rules["shared_lives"]}, Custom);
\tDisplay Text Message(Always Display, "{msg["intro"].format(players=players,waves=waves,lives=rules["shared_lives"])}");
\tSet Mission Objectives("{objective}");
\tSet Countdown Timer(Set To, {rules["initial_timer"]});
}}''')
    T.append(f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tLeader Board Points("{msg["leaderboard"]}", Custom);
\tPreserve Trigger();
}}''')
    T.append(f'''Trigger("{enemy}"){{
Conditions:
\tCountdown Timer(At most, 0);
\tDeaths("{enemy}", "{wave_state}", At most, {waves});

Actions:
\tSet Deaths("{enemy}", "{wave_state}", Add, 1);
\tSet Countdown Timer(Set To, {rules["wave_interval_seconds"]});
\tPreserve Trigger();
}}''')
    for w,wave in enumerate(wave_defs,1):
        count=wave["count"]+(w-1)*rules["wave_growth"]
        for i,arena in enumerate(arena_names):
            sw=(w-1)*players+i+1
            spawn_loc=f"{arena} {labels['spawn_suffix']}"
            T.append(f'''Trigger("{enemy}"){{
Conditions:
\tDeaths("{enemy}", "{wave_state}", Exactly, {w});
\tDeaths("Player {i+1}", "{scmap.PRESENCE_UNIT}", At least, 1);
\tSwitch("Switch {sw}", not set);

Actions:
\tSet Switch("Switch {sw}", set);
\tCreate Unit("{enemy}", "{wave["unit"]}", {count}, "{spawn_loc}");
\tPreserve Trigger();
}}''')
        seen_msg=msg["wave"].format(number=w,total=waves,unit=wave["unit"],count=count)
        T.append(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("{enemy}", "{wave_state}", Exactly, {w});
\tDeaths("Current Player", "{seen_state}", At most, {w-1});

Actions:
\tSet Deaths("Current Player", "{seen_state}", Set To, {w});
\tDisplay Text Message(Always Display, "{seen_msg}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
    for i,arena in enumerate(arena_names):
        spawn_loc=f"{arena} {labels['spawn_suffix']}"
        T.append(f'''Trigger("{boss_p}"){{
Conditions:
\tDeaths("{enemy}", "{wave_state}", At least, {waves+1});
\tDeaths("Player {i+1}", "{scmap.PRESENCE_UNIT}", At least, 1);
\tSwitch("Switch {boss_switch+i}", not set);

Actions:
\tSet Switch("Switch {boss_switch+i}", set);
\tCreate Unit with Properties("{boss_p}", "{units["boss"]}", {rules["boss_count"]}, "{spawn_loc}", 1);
\tPreserve Trigger();
}}''')
    boss_msg=msg["boss"].format(unit=units["boss"],name=labels["boss_name"])
    T.append(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("{enemy}", "{wave_state}", At least, {waves+1});
\tDeaths("Current Player", "{seen_state}", At most, {waves});

Actions:
\tSet Deaths("Current Player", "{seen_state}", Set To, {waves+1});
\tDisplay Text Message(Always Display, "{boss_msg}");
\tPlay WAV("sound\\\\Misc\\\\PowerDown.wav", 500);
\tPreserve Trigger();
}}''')
    for i,arena in enumerate(arena_names):
        player=f"Player {i+1}"
        spawn_loc=f"{arena} {labels['spawn_suffix']}"
        ne=f"{arena} {labels['ne_suffix']}"
        se=f"{arena} {labels['se_suffix']}"
        sw=f"{arena} {labels['sw_suffix']}"
        exit_loc=f"{arena} {labels['exit_suffix']}"
        center=f"{arena} {labels['center_suffix']}"
        all_loc=f"{arena} {labels['all_suffix']}"
        for owner in (enemy,boss_p):
            for start,end in ((spawn_loc,ne),(ne,se),(se,sw),(sw,exit_loc)):
                T.append(f'''Trigger("{owner}"){{
Conditions:
\tAlways();

Actions:
\tOrder("{owner}", "Any unit", "{start}", "{end}", attack);
\tPreserve Trigger();
}}''')
            T.append(f'''Trigger("{player}"){{
Conditions:
\tBring("{owner}", "Any unit", "{exit_loc}", At least, 1);

Actions:
\tRemove Unit At Location("{owner}", "Any unit", 1, "{exit_loc}");
\tSet Deaths("{player}", "{life_state}", Subtract, 1);
\tSet Score("{player}", Subtract, 1, Custom);
\tMinimap Ping("{exit_loc}");
\tPlay WAV("sound\\\\Misc\\\\PowerDown.wav", 500);
\tPreserve Trigger();
}}''')
        award_switch=clear_switch+i
        T.append(f'''Trigger("{player}"){{
Conditions:
\tBring("{enemy}", "Any unit", "{all_loc}", Exactly, 0);
\tDeaths("{enemy}", "{wave_state}", At least, 1);
\tSwitch("Switch {award_switch}", not set);

Actions:
\tSet Switch("Switch {award_switch}", set);
\tSet Resources("{player}", Add, {rules["wave_clear_ore"]}, ore);
\tDisplay Text Message(Always Display, "{msg["clear_arena"]}");
\tPreserve Trigger();
}}''')
        T.append(f'''Trigger("{player}"){{
Conditions:
\tBring("{enemy}", "Any unit", "{all_loc}", At least, 1);
\tSwitch("Switch {award_switch}", set);

Actions:
\tSet Switch("Switch {award_switch}", clear);
\tPreserve Trigger();
}}''')
        for k,shop in enumerate(shops):
            shoploc=f"{arena} {labels['shop_suffix']}{k+1}"
            vals={"player":player,"arena":arena,"center":center,
                  "shop":shoploc,"unit":shop.get("unit",""),"count":shop.get("count",1),
                  "cost":shop["cost"]}
            effects=[f'\t{x.format_map(vals)};' for x in shop["actions"]]
            T.append(f'''Trigger("{player}"){{
Conditions:
\tBring("{player}", "Men", "{shoploc}", At least, 1);
\tAccumulate("{player}", At least, {shop["cost"]}, ore);

Actions:
\tSet Resources("{player}", Subtract, {shop["cost"]}, ore);
{chr(10).join(effects)}
\tMove Unit("{player}", "Men", All, "{shoploc}", "{center}");
\tDisplay Text Message(Always Display, "{shop["receipt"]}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
        T+=scmap.part_respawn(player,units["starting_unit"],f"{arena} {labels['center_suffix']}",
            count=rules["starting_unit_count"],cooldown_counter=revive_lock,
            guard=f'Deaths("{player}", "{life_state}", At least, 1);')
        T.append(f'''Trigger("{player}"){{
Conditions:
\tDeaths("{player}", "{life_state}", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "{msg["defeat"]}");
\tDefeat();
}}''')
        T+=scmap.part_win([player],[
            f'Switch("Switch {boss_switch+i}", set);',
            f'Bring("{boss_p}", "Any unit", "{all_loc}", Exactly, 0);',
            f'Deaths("{player}", "{life_state}", At least, 1);'],msg=msg["win"])
    return scmap.TRIGGER_SEP.join(T)


# ---------------------------------------------------------------- 본체

def main(argv=None):
    ap=argparse.ArgumentParser(description="AI-profiled personal square defense")
    ap.add_argument("out")
    profile.add_profile_arguments(ap,"square_defense")
    ap.add_argument("--install",default=None)
    ap.add_argument("--force",action="store_true")
    a=ap.parse_args(argv)
    cfg=profile.load_profile(a.config,"square_defense")
    rules,units,labels,msg=cfg["rules"],cfg["units"],cfg["labels"],cfg["text"]["messages"]
    players=rules["players"];waves=rules["waves"];shops=cfg["shops"]
    W,H=cfg["map"]["size"];a.players=players;a.waves=waves
    a.tileset=cfg["map"]["tileset"];a.name=cfg["map"]["name"];a.seed=cfg["map"]["seed"]
    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    rng = random.Random(a.seed)
    ts = TILESETS[a.tileset]
    enemy_no, boss_no = a.players + 1, a.players + 2
    enemy, boss_p = f"Player {enemy_no}", f"Player {boss_no}"

    print(f"사각 디펜스 {W}x{H} {a.tileset}, 사람 {a.players}명, {a.waves}웨이브")

    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    # 바닥·통로·발판·벽을 **네 몫으로** 나눠 쓴다. 한 가지로 깔고 벽만
    # 검게 뚫던 앞판은 실측과 어긋났다 (실측 검은 칸 중앙 0.0%, 1% 넘게
    # 쓰는 그룹 중앙 10개 — 내 것은 검은 칸 55%, 그룹 1개였다).
    pal = scmap.Palette(cli, ts, rng, "usemap")
    print(f"  지형: {pal.describe()}")

    boxes = layout(a.players,W,H,arena_w=rules["arena_width"],arena_h=rules["arena_height"],gap=rules["arena_gap"])
    bx0 = min(b[0] for b in boxes); by0 = min(b[1] for b in boxes)
    bx1 = max(b[0] + b[2] for b in boxes); by1 = max(b[1] + b[3] for b in boxes)
    print(f"  경기장 {len(boxes)}개, 쓰는 자리 {bx1 - bx0}x{by1 - by0} "
          f"(맵은 {W}x{H} — 나머지는 못 걷는 지형)")
    # 벽 두께는 **사거리보다 얇아야 한다.** 마린 사거리가 4 타일이라
    # 벽이 4 면 섬 가장자리에 딱 붙어야 겨우 닿는다. 2 로 줄인다.
    RING,WALL=rules["ring_width"],rules["wall_width"]

    # 1) **맵 전체를 못 걷는 지형으로 덮는다.** 검은 칸이 아니다.
    print("벽을 세웁니다...")
    scmap.cover_map(cli, pal, W, H)

    # 2) 경기장: 통로 → 벽 고리 → 가운데 섬(테두리 두른 방)
    print(f"경기장 {len(boxes)}개를 뚫습니다 (통로/벽/섬)...")
    for (x, y, w, h) in boxes:
        pal.fill(cli, "path", x, y, w, h)                          # 바깥 통로
        pal.fill(cli, "wall", x + RING, y + RING,
                 w - 2 * RING, h - 2 * RING)                       # 벽 고리
        scmap.room(cli, pal, x + RING + WALL, y + RING + WALL,
                   w - 2 * (RING + WALL), h - 2 * (RING + WALL),
                   rim=1)                                          # 섬

    # 4) 플레이어 슬롯
    # 같은 지형 안의 **변종만** 흩는다. 그룹을 섞으면 얼룩덜룩한 덩이
    # 무늬가 생겨 네모난 방과 안 어울린다 — 변종은 잔 알갱이만 남는다.
    # 실제 사각 디펜스 맵도 타일 53~65종을 쓴다.
    scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli,a.players,[enemy_no,boss_no],race=cfg["players"]["race"])
    cli.edit("player","set",cli.path,str(enemy_no),"--race",cfg["players"]["enemy_race"],"--slot","computer")
    cli.edit("player","set",cli.path,str(boss_no),"--race",cfg["players"]["boss_race"],"--slot","computer")

    # 유즈맵은 업그레이드를 고친다 — 실측 중앙 7가지, 90%가 전부
    # (docs/chk/anatomy.md). 밀리맵 비용·시간을 그대로 두면
    # 유즈맵 흐름에 안 맞는다.
    n_up,n_tech=profile.configure_progression(cli,cfg,a.players)
    print(f"  profile upgrades {n_up} · technologies {n_tech}")

    # 5) 로케이션 — 하나하나 트리거가 쓴다
    print("로케이션을 놓습니다...")
    loc = lambda n, x0, y0, x1, y1: cli.edit(
        "location", "add", cli.path, str(x0), str(y0), str(x1), str(y1),
        "--tiles", "--name", n)
    arena_names=[]
    for i,(x,y,w,h) in enumerate(boxes):
        A=f"{labels['arena_prefix']}{i+1}"
        arena_names.append(A)
        C=RING-1
        loc(f"{A} {labels['spawn_suffix']}",x+1,y+1,x+1+C,y+1+C)
        loc(f"{A} {labels['ne_suffix']}",x+w-1-C,y+1,x+w-1,y+1+C)
        loc(f"{A} {labels['se_suffix']}",x+w-1-C,y+h-1-C,x+w-1,y+h-1)
        loc(f"{A} {labels['sw_suffix']}",x+1,y+h-1-C,x+1+C,y+h-1)
        loc(f"{A} {labels['exit_suffix']}",x+1,y+h//2-2,x+1+C,y+h//2+2)
        cx,cy=x+w//2,y+h//2
        loc(f"{A} {labels['center_suffix']}",cx-4,cy-4,cx+4,cy+4)
        for k in range(len(shops)):
            bx=cx-(len(shops)-1)*2+k*5
            loc(f"{A} {labels['shop_suffix']}{k+1}",bx,cy+8,bx+3,cy+11)
        loc(f"{A} {labels['all_suffix']}",x,y,x+w,y+h)

    print("프로필 유닛을 놓습니다...")
    icon_names={}
    for i,(x,y,w,h) in enumerate(boxes):
        p=i+1;cx,cy=x+w//2,y+h//2
        scmap.pad(cli,pal,cx,cy,5,5)
        cli.place(scmap.START_LOCATION,cx,cy,owner=p)
        for k in range(rules["starting_unit_count"]):
            cli.place(units["starting_unit"],x+RING+WALL+1+k,y+RING+WALL+1,owner=p)
        w_isl=w-2*(RING+WALL);h_isl=h-2*(RING+WALL)
        ix0,iy0=x+RING+WALL,y+RING+WALL
        cells=((ix0+2,iy0+2),(ix0+w_isl-3,iy0+2),
               (ix0+2,iy0+h_isl-3),(ix0+w_isl-3,iy0+h_isl-3))
        defenses=cfg["units"]["defenses"]
        for k,(px,py) in enumerate(cells):
            cli.place(defenses[k%len(defenses)],px,py,owner=p)
        cli.place(units["selection"],cx,cy+4,owner=p)
        A=arena_names[i]
        for k,shop in enumerate(shops):
            bx=cx-(len(shops)-1)*2+k*5
            by=cy+9
            scmap.pad(cli,pal,bx+1,by,3,3)
            cli.place(shop["beacon"],bx+1,by,owner=p)
            cli.place(shop["icon"],bx+1,by-3,owner=12)
            if shop["icon"] in icon_names and icon_names[shop["icon"]]!=shop["icon_name"]:
                raise CliError(f"unit type {shop['icon']} has conflicting display names")
            icon_names[shop["icon"]]=shop["icon_name"]
    # 적·보스도 스타팅이 있어야 슬롯이 산다. 첫 경기장 통로 구석에 둔다.
    bx, by, bw, bh = boxes[0]
    cli.place(scmap.START_LOCATION, bx + 2, by + bh // 2, owner=enemy_no)
    cli.place(scmap.START_LOCATION, bx + bw - 3, by + bh // 2, owner=boss_no)
    cli.place(units["enemy_entry_marker"],bx+3,by+bh//2,owner=12)

    # 7) 시야 (실측: 유즈맵 74% 가 Map Revealer 를 쓴다)
    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)

    # Apply AI-authored type labels before exposing the map.
    name_cfg=dict(cfg);name_cfg["unit_names"]={**icon_names,**cfg.get("unit_names",{})}
    profile.apply_unit_names(cli,name_cfg)
    cli.apply_triggers(build_triggers(cfg,enemy,boss_p,arena_names))
    profile.apply_profile_metadata(cli,cfg,a.players)

    info = cli.info()
    print(f"\n만들었습니다: {a.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']} {info['version']}")
    print(f"  유닛 {info['units']}  로케이션 {info['locations']}  "
          f"트리거 {info['triggers']}")
    print(f"\n  python3 preview.py {a.out} look.png   ← 반드시 그려 볼 것")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
