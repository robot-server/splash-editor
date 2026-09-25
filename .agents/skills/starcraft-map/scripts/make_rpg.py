#!/usr/bin/env python3
"""키우기(RPG) 유즈맵 — 마을에서 나가 구역을 하나씩 깨며 세진다.

디펜스가 "막는" 고리, 컨트롤이 "싸우는" 고리라면 키우기는 **"세지는"**
고리다. 되먹임이 길게 쌓인다 — 잡으면 돈, 돈으로 강화, 강해지면 다음
구역. 그래서 구역마다 눈에 보이는 문턱이 있어야 한다.

    ┌─────┐   ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐
    │ 마을 ├───┤ 1구역 ├─┤ 2구역 ├─┤ 3구역 ├─┤ 보스 │
    └─────┘   └──────┘ └──────┘ └──────┘ └──────┘
      상점      약함      보통     강함      끝

마을은 안전하다 — 몬스터가 안 나오고 죽으면 여기로 돌아온다.
구역은 통로로 이어져 있어 언제든 물러설 수 있다.

보기:
    python3 make_rpg.py out.scx --config recipe_profiles/your_rpg_profile.json
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
def build_triggers(cfg, enemy, boss_p):
    players=cfg["rules"]["players"]
    zones=cfg["zones"][:cfg["rules"]["zones"]]
    shops=cfg["shops"]
    units=cfg["units"]
    labels=cfg["labels"]
    messages=cfg["text"]["messages"]
    heal_cost=cfg["rules"]["heal_cost"]
    humans=",".join(f'"Player {p}"' for p in range(1,players+1))
    T=list(scmap.hyper_triggers(enemy))+scmap.absent_player_cleanup(players,enemy)
    intro_actions=[
        f'Set Resources("Current Player", Set To, {cfg["starting_resources"]["minerals"]}, ore)',
        f'Set Resources("Current Player", Set To, {cfg["starting_resources"]["gas"]}, gas)',
        'Set Score("Current Player", Set To, 0, Custom)',
        *[f'Display Text Message(Always Display, "{line}")' for line in messages["intro"]]]
    T.append(f'''Trigger({humans}){{
Conditions:
\tAlways();

Actions:
{''.join(f"\t{x};\n" for x in intro_actions)}}}''')
    T.append(f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tLeader Board Points("{messages["leaderboard"]}", Custom);
\tPreserve Trigger();
}}''')

    for i,zone in enumerate(zones):
        unit,count,reward=zone["unit"],zone["count"],zone["reward"]
        loc=f'{labels["zone_prefix"]}{i+1}'
        spawn=f'{loc} {labels["zone_spawn_suffix"]}'
        threshold=max(1,count//3)
        T.append(f'''Trigger("{enemy}"){{
Conditions:
\tBring("{enemy}", "{unit}", "{loc}", At most, {threshold});

Actions:
\tCreate Unit("{enemy}", "{unit}", {count-threshold}, "{spawn}");
\tPreserve Trigger();
}}''')
        T.append(f'''Trigger("{enemy}"){{
Conditions:
\tDeaths("{enemy}", "{unit}", At least, 1);

Actions:
\tSet Deaths("{enemy}", "{unit}", Subtract, 1);
\tSet Resources("All players", Add, {reward}, ore);
\tSet Score("All players", Add, {reward}, Custom);
\tPreserve Trigger();
}}''')
        notice=messages["zone_enter"].format(label=zone["label"],reward=reward,unit=unit)
        T.append(f'''Trigger({humans}){{
Conditions:
\tBring("Current Player", "Any unit", "{loc}", At least, 1);
\tSwitch("Switch {i+1}", not set);

Actions:
\tSet Switch("Switch {i+1}", set);
\tDisplay Text Message(Always Display, "{notice}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')

    boss=units["boss"]
    boss_loc=labels["boss"]
    boss_spawn=labels["boss_spawn"]
    T.append(f'''Trigger("{boss_p}"){{
Conditions:
\tBring("{boss_p}", "{boss}", "{boss_loc}", At most, 0);
\tSwitch("{labels["boss_switch"]}", not set);

Actions:
\tSet Switch("{labels["boss_switch"]}", set);
\tCreate Unit with Properties("{boss_p}", "{boss}", {cfg["rules"]["boss_count"]}, "{boss_spawn}", 1);
\tPreserve Trigger();
}}''')
    T.append(f'''Trigger({humans}){{
Conditions:
\tBring("{boss_p}", "Any unit", "{boss_loc}", At most, 0);
\tSwitch("{labels["boss_switch"]}", set);

Actions:
\tDisplay Text Message(Always Display, "{messages["boss_win"]}");
\tVictory();
}}''')

    for p in range(1,players+1):
        home=f'{labels["home_prefix"]}{p} {labels["home_suffix"]}'
        T.append(f'''Trigger("Player {p}"){{
Conditions:
\tBring("Player {p}", "{units["hero"]}", "{labels["town"]}", At least, 1);

Actions:
\tModify Unit Hit Points("Player {p}", "{units["hero"]}", {cfg["rules"]["town_heal_percent"]}, 0, "{labels["town"]}");
\tModify Unit Energy("Player {p}", "{units["hero"]}", {cfg["rules"]["town_heal_percent"]}, 0, "{labels["town"]}");
\tPreserve Trigger();
}}''')
        if heal_cost:
            for i,zone in enumerate(zones):
                loc=f'{labels["zone_prefix"]}{i+1}'
                heal=f'{labels["heal_prefix"]}{i+1}'
                receipt=messages["zone_heal"].format(cost=heal_cost,label=zone["label"])
                T.append(f'''Trigger("Player {p}"){{
Conditions:
\tBring("Player {p}", "{units["hero"]}", "{heal}", At least, 1);
\tAccumulate("Player {p}", At least, {heal_cost}, ore);

Actions:
\tSet Resources("Player {p}", Subtract, {heal_cost}, ore);
\tModify Unit Hit Points("Player {p}", "{units["hero"]}", {cfg["rules"]["zone_heal_percent"]}, 0, "{loc}");
\tModify Unit Energy("Player {p}", "{units["hero"]}", {cfg["rules"]["zone_heal_percent"]}, 0, "{loc}");
\tMove Unit("Player {p}", "Men", All, "{heal}", "{loc}");
\tDisplay Text Message(Always Display, "{receipt}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
        T.append(f'''Trigger("Player {p}"){{
Conditions:
\tCommand("Player {p}", "Men", At most, 0);
\tDeaths("Player {p}", "{units["revive_lock"]}", Exactly, 0);

Actions:
\tSet Deaths("Player {p}", "{units["revive_lock"]}", Set To, 1);
\tCreate Unit with Properties("Player {p}", "{units["hero"]}", 1, "{home}", 1);
\tDisplay Text Message(Always Display, "{messages["revive"]}");
\tCenter View("{home}");
\tPreserve Trigger();
}}''')
        T.append(f'''Trigger("Player {p}"){{
Conditions:
\tCommand("Player {p}", "Men", At least, 1);
\tDeaths("Player {p}", "{units["revive_lock"]}", At least, 1);

Actions:
\tSet Deaths("Player {p}", "{units["revive_lock"]}", Set To, 0);
\tPreserve Trigger();
}}''')
        for i,shop in enumerate(shops):
            shoploc=f'{labels["shop_prefix"]}{i+1}'
            values={"player":f"Player {p}","hero":units["hero"],"home":home,"shop":shoploc,
                    "cost":shop["cost"],"count":shop.get("count",1)}
            effects=[line.format_map(values) for line in shop["actions"]]
            T.append(f'''Trigger("Player {p}"){{
Conditions:
\tBring("Player {p}", "{units["hero"]}", "{shoploc}", At least, 1);
\tAccumulate("Player {p}", At least, {shop["cost"]}, ore);

Actions:
\tSet Resources("Player {p}", Subtract, {shop["cost"]}, ore);
{''.join(f"\t{line};\n" for line in effects)}\tMove Unit("Player {p}", "Men", All, "{shoploc}", "{home}");
\tDisplay Text Message(Always Display, "{shop["receipt"]}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
    return scmap.TRIGGER_SEP.join(T)


def main(argv=None):
    ap = argparse.ArgumentParser(description="키우기(RPG) 유즈맵")
    ap.add_argument("out")
    profile.add_profile_arguments(ap,"rpg")
    ap.add_argument("--players",type=int)
    ap.add_argument("--zones",type=int)
    ap.add_argument("--install",default=None)
    ap.add_argument("--force",action="store_true")
    a=ap.parse_args(argv)
    cfg=profile.load_profile(a.config,"rpg")
    for arg,key in (("players","players"),("zones","zones")):
        if getattr(a,arg) is None:setattr(a,arg,cfg["rules"][key])
    cfg["rules"]["players"]=a.players
    cfg["rules"]["zones"]=a.zones
    W,H=cfg["map"]["size"]
    ts=TILESETS[cfg["map"]["tileset"]]
    a.seed=cfg["map"]["seed"]
    zones=cfg["zones"][:a.zones]
    shops=cfg["shops"]
    units=cfg["units"]
    labels=cfg["labels"]
    terrain_mode=cfg["rules"]["terrain_mode"]

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    rng = random.Random(a.seed)
    enemy_no, boss_no = a.players + 1, a.players + 2
    enemy, boss_p = f"Player {enemy_no}", f"Player {boss_no}"

    print(f"{cfg['map']['name']} {W}x{H} {cfg['map']['tileset']}, {a.players}명, 구역 {a.zones}개")
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    cli.edit("switch", "name", cli.path, str(cfg["rules"]["boss_switch_id"]),
             labels["boss_switch"])

    # **필요한 만큼만 쓴다.** 방 크기를 먼저 정하고 그만큼만 뚫는다.
    # 앞서는 맵을 다 채우려고 방을 늘렸는데 "검은 여백이 아깝다" 는
    # 잘못된 동기였다. 거꾸로 무조건 작게 만들 이유도 없다 — 방 크기는
    # 놀이가 정한다.
    #
    #   ┌────┐ ┌────┐ ┌────┐
    #   │마을├─┤1구역├─┤2구역│      한 줄에 세 칸, 넘치면 아래 줄로
    #   └────┘ └────┘ └──┬─┘       (줄바꿈은 세로 통로로 잇는다)
    #   ┌────┐ ┌────┐ ┌──┴─┐
    #   │보스│─┤4구역├─┤3구역│
    #   └────┘ └────┘ └────┘
    ROOM_W, ROOM_H, GAP = cfg["rules"]["room_width"],cfg["rules"]["room_height"],cfg["rules"]["room_gap"]
    n_cells = a.zones + 2                       # 마을 + 구역들 + 보스
    cols = min(3, n_cells)
    rows = (n_cells + cols - 1) // cols
    used_w = cols * ROOM_W + (cols - 1) * GAP
    used_h = rows * ROOM_H + (rows - 1) * GAP
    if used_w > W - 4 or used_h > H - 4:
        raise CliError(f"방이 맵보다 큽니다 ({used_w}x{used_h} > {W}x{H}). "
                       f"--size 를 키우거나 --zones 를 줄이세요.")
    ox, oy = (W - used_w) // 2, (H - used_h) // 2
    cells = []
    for i in range(n_cells):
        r, c = divmod(i, cols)
        if r % 2:                               # 홀수 줄은 거꾸로 — 뱀
            c = cols - 1 - c
        cells.append((ox + c * (ROOM_W + GAP), oy + r * (ROOM_H + GAP),
                      ROOM_W, ROOM_H))
    band_y = cells[0][1]
    BAND_H = ROOM_H

    # 이동 경관이 콘텐츠가 되는 이 생성기의 구역 연결은 ISOM으로 시작한다.
    #
    # 순서가 사각형 방식과 뒤집힌다. 다 덮고 방을 뚫는 것이 아니라,
    # **바닥을 먼저 깔고 그 위에 지형 덩이를 키운다.**
    pal = scmap.Palette(cli, ts, rng, "usemap")
    mode = terrain_mode
    print(f"  지형: {pal.describe()}  ({mode})")

    if mode == "isom":
        print("지형을 ISOM 으로 짓습니다 (마을·구역·보스방 자리는 비웁니다)...")
        land = scmap.isom_landscape(cli, ts, rng, W, H,
                                    keep_clear=cells,
                                    walls=max(8, n_cells * 2),
                                    rises=max(14, n_cells * 4),
                                    patches=max(18, n_cells * 5),
                                    area=70)
        print(f"  바닥 얼룩 {len(land['patches'])}덩이 · "
              f"높은 땅 {len(land['rises'])}덩이 · "
              f"물 {len(land['walls'])}덩이 "
              f"(쓰는 지형 {len(land['low']) + len(land['high']) + len(land['blocked'])}종)")
        # 구역 사이를 잇는 길만 바닥으로 되돌린다 — 덩이가 길을 막았을
        # 수 있다. 여기서만 사각형을 쓴다(길은 길이어야 한다).
        for i in range(n_cells - 1):
            ax, ay, aw, ah = cells[i]
            bx_, by_, bw_, bh_ = cells[i + 1]
            if ay == by_:
                scmap.isom_fill(cli, land["base"],
                                min(ax + aw, bx_ + bw_), ay + ah // 2 - 3,
                                GAP, 6)
            else:
                scmap.isom_fill(cli, land["base"],
                                ax + aw // 2 - 3, ay + ah, 6, GAP)
    else:
        print("벽을 세웁니다...")
        scmap.cover_map(cli, pal, W, H)
        print(f"마을 1 + 구역 {a.zones} + 보스방 1 을 뚫습니다 "
              f"({used_w}x{used_h} 만 씁니다)...")
        for (x, y, w, h) in cells:
            scmap.room(cli, pal, x, y, w, h, rim=1)
        for i in range(n_cells - 1):
            ax, ay, aw, ah = cells[i]
            bx_, by_, bw_, bh_ = cells[i + 1]
            if ay == by_:
                pal.fill(cli, "path", min(ax + aw, bx_ + bw_),
                         ay + ah // 2 - 3, GAP, 6)
            else:
                pal.fill(cli, "path", ax + aw // 2 - 3, ay + ah, 6, GAP)

    # **한 맵에서 ISOM 과 사각형을 섞지 않는다.**
    #
    # `scatter_tile_variants` 는 타일 값을 직접 쓴다 — 사각형 편집이다.
    # ISOM 으로 지은 맵에 이것을 얹으면 ISOM 격자와 실제 타일이 어긋나,
    # 나중에 ISOM 붓을 한 번만 더 대도 지형이 뭉개진다. 사각형으로 지은
    # 맵에서만 쓴다.
    if mode != "isom":
        scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli,a.players,[enemy_no,boss_no],race=cfg["players"]["race"])
    cli.edit("player","set",cli.path,str(enemy_no),"--race",cfg["players"]["enemy_race"],"--slot","computer")
    cli.edit("player","set",cli.path,str(boss_no),"--race",cfg["players"]["boss_race"],"--slot","computer")

    print("로케이션을 놓습니다...")
    loc = lambda nm, x0, y0, x1, y1: cli.edit(
        "location", "add", cli.path, str(x0), str(y0), str(x1), str(y1),
        "--tiles", "--name", nm)
    tx, ty, tw, th = cells[0]
    loc(labels["town"], tx, ty, tx + tw, ty + th)
    for p in range(1, a.players + 1):
        hx = tx + 2 + ((p - 1) % 3) * 5
        hy = ty + 3 + ((p - 1) // 3) * 6
        loc(f"{labels['home_prefix']}{p} {labels['home_suffix']}", hx, hy, hx + 4, hy + 4)
    for k,shop in enumerate(shops):
        sx = tx + 2 + k * 6
        loc(f"{labels['shop_prefix']}{k+1}", sx, ty + th - 8, sx + 4, ty + th - 4)
    for z,zone in enumerate(zones):
        zx, zy, zw, zh = cells[z + 1]
        zone_loc=f"{labels['zone_prefix']}{z+1}"
        spawn_loc=f"{zone_loc} {labels['zone_spawn_suffix']}"
        loc(zone_loc, zx, zy, zx + zw, zy + zh)
        loc(spawn_loc, zx + zw - 8, zy + 3, zx + zw - 2, zy + zh - 3)
        loc(f"{labels['heal_prefix']}{z + 1}", zx + 2, zy + zh - 6, zx + 6, zy + zh - 2)
    bx, by, bw, bh = cells[-1]
    loc(labels["boss"], bx, by, bx + bw, by + bh)
    loc(labels["boss_spawn"], bx + bw // 2 - 3, by + bh // 2 - 3,
        bx + bw // 2 + 3, by + bh // 2 + 3)

    print("유닛을 놓습니다...")
    for p in range(1, a.players + 1):
        hx = tx + 4 + ((p - 1) % 3) * 5
        hy = ty + 5 + ((p - 1) // 3) * 6
        cli.place(scmap.START_LOCATION, hx, hy, owner=p)
        cli.place(units["hero"], hx, hy, owner=p)
        for k in range(cfg["rules"]["companion_count"]):
            cli.place(units["companion"], hx - 1 + k, hy + 2, owner=p)
    for k,shop in enumerate(shops):
        sx = tx + 4 + k * 6
        cli.place(shop["marker"], sx, ty + th - 11, owner=12)
        scmap.pad(cli, pal, sx, ty + th - 6, 3, 3)
        cli.place(units["shop_beacon"], sx, ty + th - 6, owner=12)
    # 구역마다 몬스터를 미리 깔아 둔다 (트리거가 채우기 전에도 보이게)
    for z,zone in enumerate(zones):
        zx, zy, zw, zh = cells[z + 1]
        unit, cnt = zone["unit"],zone["count"]
        for k in range(cnt):
            mx = zx + 3 + (k % 6) * 2
            my = zy + 4 + (k // 6) * 3
            cli.place(unit, min(mx, zx + zw - 2), min(my, zy + zh - 2), owner=enemy_no)
        cli.place(zone["marker"],zx+2,zy+2,owner=12)
    # 구역마다 회복 발판 — 눈에 띄게 비콘과 건물을 함께 둔다
    for z,zone in enumerate(zones):
        zx, zy, zw, zh = cells[z + 1]
        scmap.pad(cli, pal, zx + 4, zy + zh - 4, 3, 3)
        cli.place(units["heal_marker"], zx + 4, zy + zh - 4, owner=12)
        cli.place(units["heal_building"], zx + 4, zy + zh - 8, owner=12)
    # Obstacles are part of the AI-authored arena layout. Keep them away from
    # room edges, heal pads, and each zone's spawn strip.
    obstacle_count = cfg["rules"]["field_obstacles"]
    obstacle_unit = units["field_obstacle"]
    obstacle_slots = []
    for z, (zx, zy, zw, zh) in enumerate(cells[1:-1]):
        for row in range(1, max(2, zh // 6)):
            for col in range(1, max(2, zw // 7)):
                px, py = zx + col * 6, zy + row * 6
                if px < zx + zw - 10 and py < zy + zh - 9:
                    obstacle_slots.append((px, py))
    rng.shuffle(obstacle_slots)
    if obstacle_count > len(obstacle_slots):
        raise CliError(f"field_obstacles={obstacle_count} exceeds safe zone slots ({len(obstacle_slots)})")
    for px, py in obstacle_slots[:obstacle_count]:
        cli.place(obstacle_unit, px, py, owner=12)
    cli.place(scmap.START_LOCATION, cells[1][0] + 2, band_y + 2, owner=enemy_no)
    cli.place(scmap.START_LOCATION, bx + 2, by + 2, owner=boss_no)
    cli.place(units["boss"], bx + bw // 2, by + bh // 2, owner=boss_no)

    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)


    # 방 테두리 두뎃은 실제 보행·시야·배치 검증 뒤 선택한다.
    # 두뎃 타일을 쓰고, 중앙 868칸이며 그 89%가 걷기 경계 두 칸 안에
    # 몰려 있다. 내 맵은 0칸이었다 — 그림으로 보고서야 알았다.
    # 걷기를 막는 두뎃은 `data/doodad-walk.json` 을 보고 걸러 낸다.
    print("방 테두리를 두뎃으로 꾸밉니다...")
    _clear = [(u["x"] // 32 - 2, u["y"] // 32 - 2, 5, 5) for u in cli.units()]
    _nd = scmap.decorate_rim(cli, ts, cells, rng, keep_clear=_clear)
    print(f"  두뎃 {_nd}개")

    name_map={}
    for collection,unit_key,name_key in ((shops,"marker","marker_name"),(zones,"marker","marker_name")):
        for item in collection:
            if item[unit_key] in name_map and name_map[item[unit_key]] != item[name_key]:
                raise CliError(f"{item[unit_key]} is assigned conflicting display names")
            name_map[item[unit_key]]=item[name_key]
    name_cfg=dict(cfg); name_cfg["unit_names"]={**name_map,**cfg.get("unit_names",{})}
    profile.apply_unit_names(cli,name_cfg)
    print("트리거를 짭니다...")
    cli.apply_triggers(build_triggers(cfg,enemy,boss_p))
    profile.apply_profile_metadata(cli,cfg,a.players)

    info = cli.info()
    print(f"\n만들었습니다: {a.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']} {info['version']}")
    print(f"  유닛 {info['units']}  트리거 {info['triggers']}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
