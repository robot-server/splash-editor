#!/usr/bin/env python3
"""좀비 유즈맵 — **부품만으로 조립한다.**

이 파일은 새 장르를 부품으로 만들 수 있는지 보려고 쓴 것이다. 트리거를
직접 짜지 않고 `scmap.part_*` 를 이어 붙이기만 한다. 실제로 이 파일에는
트리거 글이 거의 없다 — 조립 순서와 맵 배치뿐이다.

좀비 맵의 알맹이는 **감염**이다 (실측 35장 중 91% 가 `Set Alliance
Status` 를 쓴다). 병력을 다 잃은 사람이 좀비 편으로 넘어가고, 넘어간
사람은 이제 남은 생존자를 쫓는다. 그래서 사람이 줄수록 좀비가 는다.

    ┌─────────────────────────────┐
    │  피난처   ·   벌판   ·  묘지 │
    └─────────────────────────────┘
      생존자 시작    싸움터    좀비가 나오는 곳

보기:
    python3 make_zombie.py out.scx --config profile.json
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
def placed_zombies(cfg) -> int:
    """게임 시작 때 묘지에 깔아 두는 수. 보충 트리거의 초기 발급 횟수와 같다."""
    return cfg["rules"]["zombie_population"] // 2


def home_box(shelter, index, spacing):
    """생존자 거점의 타일 사각형 (x0, y0, x1, y1). 로케이션 추가와 같다."""
    hx = shelter[0] + 2 + (index % 2) * spacing
    hy = shelter[1] + 3 + (index // 2) * spacing
    return (hx, hy, hx + 6, hy + 6)


def heal_pad_box(field):
    """유료 회복 발판. 피난처 거점과 겹치지 않는 들판 한가운데다."""
    x = field[0] + field[2] // 2 - 2
    y = field[1] + field[3] // 2 - 2
    return (x, y, x + 4, y + 4)


def _rects_overlap(a, b) -> bool:
    return a[0] <= b[2] and b[0] <= a[2] and a[1] <= b[3] and b[1] <= a[3]


def build(cfg, enemy, boss_p, humans):
    rules,units,labels,msg=cfg["rules"],cfg["units"],cfg["labels"],cfg["text"]["messages"]
    players=rules["players"]
    zombie=units["zombie"]
    T=scmap.floor_triggers(players,enemy,min_players=1)
    intro=[line.format(minutes=rules["survive_seconds"]//60,players=players)
           for line in msg["intro"]]
    objectives=msg["objectives"].format(minutes=rules["survive_seconds"]//60,
        players=players,waves=rules["wave_count"],zombies=rules["zombie_population"])
    T += scmap.part_intro(humans,intro,ore=cfg["starting_resources"]["minerals"],
        gas=cfg["starting_resources"]["gas"],objectives=objectives,
        counters={units["infection_counter"]:0, units["wave_counter"]:0,
                  units["boss_seen"]:0})
    T += scmap.part_leaderboard(msg["leaderboard"],kind="Kills")
    for k in range(rules["wave_count"]):
        seconds=(k+1)*rules["survive_seconds"]//rules["wave_count"]
        notice=msg["wave"].format(number=k+1,total=rules["wave_count"],seconds=seconds)
        T += scmap.part_announce_once(humans,
            f'Elapsed Time(At least, {seconds});',units["wave_counter"],k+1,[notice])
    cap = rules["zombie_population"]
    lock = units["spawn_lock"]
    graveyard = labels["graveyard"]
    seeded = placed_zombies(cfg)
    # 이미 깔아 둔 수를 발급 횟수로 찍은 뒤에만 1기씩 보충한다.
    # 묘지 유닛이 들판으로 나가도 발급은 cap을 넘지 않는다.
    T.append(f'''Trigger("{enemy}"){{
Conditions:
\tDeaths("{enemy}", "{lock}", Exactly, 0);

Actions:
\tSet Deaths("{enemy}", "{lock}", Set To, {seeded});
}}''')
    T.append(f'''Trigger("{enemy}"){{
Conditions:
\tBring("{enemy}", "{zombie}", "{graveyard}", At most, {cap - 1});
\tDeaths("{enemy}", "{lock}", At least, 1);
\tDeaths("{enemy}", "{lock}", At most, {cap - 1});

Actions:
\tSet Deaths("{enemy}", "{lock}", Add, 1);
\tCreate Unit("{enemy}", "{zombie}", 1, "{graveyard}");
\tPreserve Trigger();
}}''')
    T += scmap.part_patrol_path(enemy,[labels["graveyard"],labels["field"],labels["shelter"]],"attack")
    T += scmap.part_patrol_path(boss_p,[labels["graveyard"],labels["field"],labels["shelter"]],"attack")
    T += scmap.part_infection(humans,enemy,units["infection_counter"],zombie,
        labels["graveyard"],msg["infected"],rules["infection_spawn_count"],lock)
    for p in range(1,players+1):
        who=f"Player {p}"
        home=f'{labels["home_prefix"]}{p} {labels["home_suffix"]}'
        for k,shop in enumerate(cfg["shops"]):
            offer_loc=f'{labels["shop_prefix"]}{k+1}'
            values={"player":who,"home":home,"shop":offer_loc,
                    "unit":shop["unit"],"count":shop["count"],"cost":shop["cost"]}
            effects=[f'\t{action.format_map(values)};' for action in shop["actions"]]
            T += scmap.part_beacon_shop(who,offer_loc,shop["cost"],effects,
                shop["receipt"],push_to=home)
        # 피난처 전체를 회복 칸으로 두고 그 안의 거점으로 밀면
        # Bring 이 계속 참이라 매 프레임 결제된다. 값이 있을 때만
        # 들판 발판에서 받고, 거점은 그 발판 밖에 둔다.
        if rules["heal_cost"]:
            T += scmap.part_heal_zone(who, labels["heal_pad"], cost=rules["heal_cost"],
                                       push_to=home)
        else:
            T += scmap.part_heal_zone(who, labels["shelter"], cost=0)
        T.append(scmap.kill_bounty(who,rules["bounty_per_kill"],per_score=rules["bounty_score_step"]))
    boss_time=rules["boss_seconds"]
    T.append(f'''Trigger("{boss_p}"){{
Conditions:
\tElapsed Time(At least, {boss_time});
\tDeaths("{boss_p}", "{lock}", Exactly, 0);

Actions:
\tSet Deaths("{boss_p}", "{lock}", Set To, 1);
\tCreate Unit with Properties("{boss_p}", "{units["boss"]}", {rules["boss_count"]}, "{graveyard}", 1);
\tPreserve Trigger();
}}''')
    boss_msg=msg["boss"].format(minutes=boss_time//60)
    # 웨이브 안내와 칸을 나누지 않으면, 보스가 마지막 웨이브보다 일찍
    # wave_count+1 을 찍어 그 뒤 안내의 At most 가 영영 거짓이 된다.
    T += scmap.part_announce_once(humans,
        f'Elapsed Time(At least, {boss_time});', units["boss_seen"], 1,
        [boss_msg], wav=msg.get("boss_wav", "sound\\Zerg\\Ultra\\ZUlDth00.wav"))
    T += scmap.part_survive_timer(humans,rules["survive_seconds"],msg=msg["survived"])
    return scmap.TRIGGER_SEP.join(T)


def main(argv=None):
    ap=argparse.ArgumentParser(description="AI-profiled infected-team survival Use Map")
    ap.add_argument("out")
    profile.add_profile_arguments(ap,"zombie")
    ap.add_argument("--install",default=None)
    ap.add_argument("--force",action="store_true")
    a=ap.parse_args(argv)
    cfg=profile.load_profile(a.config,"zombie")
    rules,units,labels=cfg["rules"],cfg["units"],cfg["labels"]
    players=rules["players"]
    if tuple(cfg["map"]["size"])[0] < 64 or tuple(cfg["map"]["size"])[1] < 64:
        ap.error("zombie shelter/field/grave layout requires at least 64x64")
    W,H=cfg["map"]["size"]
    ts=TILESETS[cfg["map"]["tileset"]]
    a.seed=cfg["map"]["seed"]
    enemy_no,boss_no=players+1,players+2
    enemy,boss_p=f"Player {enemy_no}",f"Player {boss_no}"
    humans=[f"Player {p}" for p in range(1,players+1)]
    if os.path.exists(a.out) and not a.force:
        ap.error("already exists (--force to overwrite)")
    rng=random.Random(a.seed)
    print(f"{cfg['map']['name']} {W}x{H} {cfg['map']['tileset']}, {players} players, {rules['survive_seconds']} seconds")
    cli=scmap.new_map(a.out,W,H,ts,terrain=None,melee=False,install=a.install)

    # The AI profile chooses the three areas' proportions and the map margins.
    margin,gap=rules["margin"],rules["gap"]
    inner_w=W-2*margin-2*gap
    shelter_w=max(rules["min_shelter_width"],int(inner_w*rules["shelter_ratio_pct"]/100))
    grave_w=max(rules["min_graveyard_width"],int(inner_w*rules["graveyard_ratio_pct"]/100))
    field_w=inner_w-shelter_w-grave_w
    room_h=H-2*margin
    if field_w<12: raise CliError("profile shelter/graveyard proportions leave too little walkable field")
    used_w=shelter_w+field_w+grave_w+gap*2
    ox,oy=(W-used_w)//2,(H-room_h)//2
    shelter=(ox,oy,shelter_w,room_h)
    field=(ox+shelter_w+gap,oy,field_w,room_h)
    grave=(ox+shelter_w+gap+field_w+gap,oy,grave_w,room_h)

    pal=scmap.Palette(cli,ts,rng,"usemap")
    scmap.cover_map(cli,pal,W,H,margin=2)
    for r in (shelter,field,grave): scmap.room(cli,pal,*r,rim=1,wall=True)
    for left,right in ((shelter,field),(field,grave)):
        pal.fill(cli,"path",left[0]+left[2],oy+room_h//2-3,gap,6)
    scmap.scatter_tile_variants(cli,ts,rng,chance=0.5)

    scmap.setup_usemap_players(cli,players,[enemy_no,boss_no],race=cfg["players"]["race"],
                               computer_race=cfg["players"]["enemy_race"])
    cli.edit("player","set",cli.path,str(enemy_no),"--race",cfg["players"]["enemy_race"],"--slot","computer")
    cli.edit("player","set",cli.path,str(boss_no),"--race",cfg["players"]["boss_race"],"--slot","computer")
    n_up,n_tech=profile.configure_progression(cli,cfg,players)
    print(f"  profile upgrades {n_up}, technologies {n_tech}")

    loc=lambda name,r: cli.edit("location","add",cli.path,str(r[0]),str(r[1]),str(r[0]+r[2]),str(r[1]+r[3]),"--tiles","--name",name)
    loc(labels["shelter"],shelter);loc(labels["field"],field);loc(labels["graveyard"],grave)
    for i in range(players):
        hx,hy,hx1,hy1=home_box(shelter,i,rules["home_spacing"])
        if _rects_overlap((hx,hy,hx1,hy1), heal_pad_box(field)):
            raise CliError("heal pad overlaps a survivor home; field is too small for this profile")
        home=f"{labels['home_prefix']}{i+1} {labels['home_suffix']}"
        cli.edit("location","add",cli.path,str(hx),str(hy),str(hx1),str(hy1),"--tiles","--name",home)
    px0,py0,px1,py1=heal_pad_box(field)
    cli.edit("location","add",cli.path,str(px0),str(py0),str(px1),str(py1),"--tiles","--name",labels["heal_pad"])
    # 치료는 구매가 아니다. 비콘 대신 이름 붙인 건물이 그 행동을 설명한다.
    cli.place("Terran Supply Depot", px0+1, py0+1, owner=12)
    cli.edit("unitdef", "set", cli.path, "Terran Supply Depot",
             "--name", cfg["unit_names"].get(units["heal_marker"], "치료"))
    if rules["heal_cost"]:
        scmap.place_price_mineral(cli, px0+3, py0+1, rules["heal_cost"])
    for k,shop in enumerate(cfg["shops"]):
        sx=shelter[0]+4+k*12
        by=shelter[1]+room_h-8
        name=f"{labels['shop_prefix']}{k+1}"
        cli.edit("location","add",cli.path,str(sx-1),str(by-1),str(sx+7),str(by+4),"--tiles","--name",name)

    for i in range(players):
        p=i+1;hx=shelter[0]+4+(i%2)*rules["home_spacing"];hy=shelter[1]+5+(i//2)*rules["home_spacing"]
        cli.place(scmap.START_LOCATION,hx,hy,owner=p)
        for k in range(rules["starting_survivors"]):
            cli.place(units["survivor"],hx-2+k%3,hy+k//3,owner=p)
        cli.place(units["shop_selection"],hx,hy+3,owner=p)
    for k,shop in enumerate(cfg["shops"]):
        sx=shelter[0]+4+k*12
        by=shelter[1]+room_h-8
        scmap.pad(cli,pal,sx,by,7,4)
        scmap.place_beacon_shop(
            cli, units["shop_beacon"], shop["actions"],
            shop.get("unit") or shop["marker"], sx, by, shop["cost"])
    cli.place(scmap.START_LOCATION,grave[0]+3,grave[1]+3,owner=enemy_no)
    cli.place(scmap.START_LOCATION,grave[0]+3,grave[1]+room_h-4,owner=boss_no)
    for k in range(placed_zombies(cfg)):
        cli.place(units["zombie"],grave[0]+2+k%8,grave[1]+8+(k//8)*3,owner=enemy_no)
    for k in range(rules["field_obstacles"]):
        cli.place(units["field_obstacle"],field[0]+6+(k%5)*8,field[1]+8+(k//5)*18,owner=12)

    names={}
    for shop in cfg["shops"]:
        preview = scmap.shop_preview_type(shop["actions"], shop.get("unit") or shop["marker"])
        names[preview] = shop.get("unit_name") or shop["marker_name"]
    name_cfg=dict(cfg);name_cfg["unit_names"]=scmap.merge_display_names(cfg.get("unit_names"), names)
    profile.apply_unit_names(cli,name_cfg)
    scmap.apply_reveal(cli, "zombie", players, shelter)
    clear=[(u["x"]//32-2,u["y"]//32-2,5,5) for u in cli.units()]
    decorated=scmap.decorate_rim(cli,ts,[shelter,field,grave],rng,keep_clear=clear)
    print(f"  border doodads {decorated}")

    cli.apply_triggers(build(cfg,enemy,boss_p,humans))
    profile.apply_profile_metadata(cli,cfg,players)
    info=cli.info()
    print(f"\nCreated {a.out}: {info['width']}x{info['height']} {info['tileset']}; {info['units']} units, {info['triggers']} triggers")
    scmap.assert_create_targets(cli)
    return 0


if __name__ == "__main__":
    try: sys.exit(main())
    except CliError as e:
        print(f"Error: {e}",file=sys.stderr);sys.exit(1)
