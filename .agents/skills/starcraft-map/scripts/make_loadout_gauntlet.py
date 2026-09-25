#!/usr/bin/env python3
"""Shop-draft co-op gauntlet: recruit one unit, then clear profiled waves.

The shop transition is the documented Bring + Accumulate + state-token pattern:
shop marker and offer icon are visible together, and buying consumes the recruit
unit before charging and creating the selected unit. The drafting room feeds a
shared wave arena, making this a pre-match composition loop rather than the
persistent zone/economy loop in make_rpg.py.

    python3 make_loadout_gauntlet.py out.scx --config profile.json
"""
from __future__ import annotations
import argparse
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import recipe_config as profile
import scmap
from scmap import CliError

TILESETS = {"badlands":0,"space":1,"ashworld":3,"jungle":4,
            "desert":5,"ice":6,"twilight":7}
W = H = 128
DRAFT = (12, 8, 104, 44)
ARENA = (12, 62, 104, 54)
SHOP_CENTERS = [(28, 27), (64, 27), (100, 27), (46, 42)]
HERO_XS = [28, 52, 76, 100]
HERO_Y = 69
RECRUIT_XS = [18, 46, 74, 102]


def trig(owner: str, conditions: list[str], actions: list[str]) -> str:
    return (f"Trigger({owner}){{\nConditions:\n" +
            "".join(f"\t{x};\n" for x in conditions) + "\nActions:\n" +
            "".join(f"\t{x};\n" for x in actions) + "}")


def main(argv=None):
    ap = argparse.ArgumentParser(description="AI 프로필 기반 선택형 협동 웨이브 방어")
    ap.add_argument("out")
    profile.add_profile_arguments(ap, "loadout_gauntlet")
    ap.add_argument("--install")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    cfg = profile.load_profile(a.config, "loadout_gauntlet")
    humans = cfg["rules"]["players"]
    offers, waves = cfg["offers"], cfg["waves"]
    labels = cfg["labels"]
    if not 1 <= humans <= 4 or not 2 <= len(offers) <= 4 or not 2 <= len(waves) <= 4:
        ap.error("players=1..4, offers=2..4, waves=2..4 이어야 합니다")
    if tuple(cfg["map"]["size"]) != (W,H):
        raise CliError("현재 draft/arena 공간 템플릿은 128x128 프로필만 받습니다")
    if os.path.exists(a.out) and not a.force:
        ap.error("이미 존재합니다 (--force로 덮어쓰기)")
    state = cfg["units"]["choice_state"]
    stage_state = cfg["units"]["wave_state"]
    notice_state = cfg["units"]["notice_state"]
    presence_counter = cfg["units"]["presence_counter"]
    recruit = cfg["units"]["recruit"]
    fallback = cfg["units"]["fallback"]
    reserved = {state, stage_state, notice_state, presence_counter, recruit, fallback, cfg["units"]["home_marker"]}
    if state == stage_state:
        raise CliError("choice_state and wave_state must be different death-counter unit types")
    custom_names={}
    for item in offers:
        for key in ("unit", "marker"):
            if item[key] in reserved:
                raise CliError(f"units.{key} must not reuse reserved counter/recruit unit {item[key]}")
        for unit,name in ((item["unit"],item["unit_name"]),
                          (item["marker"],item["marker_name"])):
            if unit in custom_names and custom_names[unit] != name:
                raise CliError(f"unit type {unit} is assigned conflicting map-wide display names")
            custom_names[unit]=name
    for wave in waves:
        if wave["unit"] in reserved:
            raise CliError("wave unit conflicts with reserved counter/recruit unit")
    safe_texts = [cfg["text"]["objectives"], *cfg["text"]["briefing"],
                  cfg["text"]["portrait"], *cfg["text"]["messages"].values(),
                  *(x[k] for x in offers for k in ("location","label","receipt","unit_name","marker_name")),
                  *(x["clear_message"] for x in waves)]
    if any('"' in x or "\n" in x or "\r" in x for x in safe_texts):
        raise CliError("trigger/briefing text may not contain a quote or line break")
    safe_names = [*cfg["units"].values(), labels["draft"], labels["arena"], labels["enemy_spawn"],
                  *labels["homes"], *labels["recruit_pads"],
                  *(x[k] for x in offers for k in ("unit","marker")),
                  *(x["unit"] for x in waves)]
    if any(not isinstance(x,str) or not x or '"' in x or "\n" in x or "\r" in x for x in safe_names):
        raise CliError("unit/location names must be non-empty trigger-safe strings")
    if len(cfg["launch_switch"]) > 31 or '"' in cfg["launch_switch"]:
        raise CliError("launch_switch must be an unquoted name of at most 31 characters")
    if not 5 <= cfg["rules"]["draft_seconds"] <= 86400 or not 30 <= cfg["rules"]["battle_seconds"] <= 86400:
        raise CliError("draft_seconds must be 5..86400 and battle_seconds 30..86400")

    ts = TILESETS[cfg["map"]["tileset"]]
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False, install=a.install)
    cli.edit("switch", "name", cli.path, "1", cfg["launch_switch"])
    # UNIS/UNIx names are type-wide; config validation forbids conflicting labels.
    profile.apply_unit_names(cli, {**cfg, "unit_names": custom_names})
    pal = scmap.Palette(cli, ts, random.Random(cfg["map"]["seed"]), "usemap")
    scmap.cover_map(cli, pal, W, H, margin=2)
    scmap.room(cli, pal, *DRAFT, rim=2, wall=True)
    scmap.room(cli, pal, *ARENA, rim=2, wall=True)
    # Broad gate between recruitment hall and the shared arena.
    pal.fill(cli, "path", 59, 50, 10, 16)
    # The battlefield has clear walkable lanes and a spacious enemy approach.
    for i in range(humans):
        x = HERO_XS[i]
        pal.fill(cli, "path", x-4, 64, 9, 38)
    pal.fill(cli, "path", 38, 91, 52, 9)

    grid = scmap.walk_grid(cli, ts, 0, 0, W, H)
    targets = [SHOP_CENTERS[i] for i in range(len(offers))] + [(x,HERO_Y) for x in HERO_XS[:humans]] + [(64,105)]
    walk = [scmap.nearest_walkable(grid,x*4+2,y*4+2,24) for x,y in targets]
    if any(p is None for p in walk) or any(not scmap.walk_reachable(grid,walk[0],p) for p in walk[1:]):
        raise CliError("shop, home, and arena are not connected by actual walkable tiles")

    enemy_player, system_player = humans+1, humans+2
    scmap.setup_usemap_players(cli, humans, [enemy_player,system_player], race=cfg["players"]["race"])
    cli.edit("player","set",cli.path,str(enemy_player),"--race",cfg["players"]["enemy_race"],"--slot","computer")
    cli.edit("player","set",cli.path,str(system_player),"--race",cfg["players"]["system_race"],"--slot","computer")

    def loc(name,x,y,w,h):
        cli.edit("location","add",cli.path,str(x),str(y),str(x+w),str(y+h),"--tiles","--name",name)
    loc(labels["draft"],*DRAFT)
    loc(labels["arena"],*ARENA)
    loc(labels["enemy_spawn"],52,99,24,12)
    for p in range(humans):
        loc(labels["homes"][p], HERO_XS[p]-5, HERO_Y-4, 10, 10)
        loc(labels["recruit_pads"][p], RECRUIT_XS[p]-2, 43, 4, 4)
    for i, offer in enumerate(offers):
        cx,cy=SHOP_CENTERS[i]
        loc(offer["location"],cx-4,cy-4,8,8)

    # Each player starts with the same mobile recruit token and an empty slot.
    for p in range(1,humans+1):
        x=RECRUIT_XS[p-1]
        cli.place(scmap.START_LOCATION,x,46,owner=p)
        cli.place(recruit,x,46,owner=p)
    for i, offer in enumerate(offers):
        cx,cy=SHOP_CENTERS[i]
        scmap.pad(cli,pal,cx,cy,7,7)
        cli.place("Terran Beacon",cx,cy,owner=12)
        cli.place(offer["marker"],cx,cy-4,owner=12)
    for p in range(humans):
        cli.place(cfg["units"]["home_marker"],HERO_XS[p],HERO_Y-2,owner=12)
    cli.place(scmap.START_LOCATION,64,103,owner=enemy_player)
    # Keep the first enemy formation visibly staged but neutral until drafting
    # closes. At launch the neutral preview is removed and a fresh P7 wave is
    # created into the reserved, open spawn footprint.
    for j in range(waves[0]["count"]):
        cli.place(waves[0]["unit"], 54+(j%8)*2, 101+(j//8)*2, owner=12)

    # Dress only room edges; keep units, purchase pads, and the center gate clear.
    keep_clear=[(u["x"]//32-2,u["y"]//32-2,5,5) for u in cli.units()]
    dressed=scmap.decorate_rim(cli,ts,[DRAFT,ARENA],
                               random.Random(cfg["map"]["seed"]^0x71DE),
                               keep_clear=keep_clear)
    print(f"  방 경계 장식 {dressed}개 (유닛/상점/웨이브 발판 제외)")
    blocks = scmap.hyper_triggers(f"Player {system_player}")
    blocks.extend(scmap.absent_player_cleanup(
        humans, f"Player {system_player}",
        count_slot=presence_counter))
    blocks.append(trig('"All players"',["Always()"],[
        f'Set Mission Objectives("{cfg["text"]["objectives"]}")']))
    start_actions = [f'Set Countdown Timer(Set To, {cfg["rules"]["draft_seconds"]})']
    start_actions += profile.resource_actions(cfg,[f"Player {p}" for p in range(1,humans+1)])
    blocks.append(trig(f'"Player {system_player}"',["Always()"],start_actions))

    # Human-varying diplomacy is explicit; a common force flag alone is not relied on.
    for p in range(1,humans+1):
        actions=[f'Set Alliance Status("Player {q}", Allied Victory)' for q in range(1,humans+1) if q!=p]
        if actions: blocks.append(trig(f'"Player {p}"',["Always()"],actions))
        for i,offer in enumerate(offers):
            blocks.append(trig(f'"Player {p}"',[
                f'Bring("Player {p}", "{recruit}", "{offer["location"]}", At least, 1)',
                f'Accumulate("Player {p}", At least, {offer["cost"]}, ore)',
                f'Deaths("Player {p}", "{state}", Exactly, 0)'],[
                f'Set Deaths("Player {p}", "{state}", Set To, {i+1})',
                f'Remove Unit At Location("Player {p}", "{recruit}", All, "{offer["location"]}")',
                f'Set Resources("Player {p}", Subtract, {offer["cost"]}, ore)',
                f'Create Unit("Player {p}", "{offer["unit"]}", {offer["count"]}, "{labels["homes"][p-1]}")',
                f'Display Text Message(Always Display, "{offer["receipt"]}")',
                'Play WAV("sound\\Misc\\Button.wav", 300)']))
        # No purchase before the draft clock runs out: recruit the profile-selected fallback.
        blocks.append(trig(f'"Player {p}"',[
            'Countdown Timer(At most, 0)',
            f'Deaths("Player {p}", "{state}", Exactly, 0)',
            f'Command("Player {p}", "{recruit}", At least, 1)'],[
            f'Set Deaths("Player {p}", "{state}", Set To, {len(offers)+1})',
            f'Remove Unit At Location("Player {p}", "{recruit}", All, "{labels["draft"]}")',
            f'Create Unit("Player {p}", "{fallback}", 1, "{labels["homes"][p-1]}")',
            f'Display Text Message(Always Display, "{cfg["text"]["messages"]["fallback"]}")']))
        # Chosen unit loss eliminates that slot; other teammates can continue.
        for i,offer in enumerate(offers):
            blocks.append(trig(f'"Player {p}"',[
                f'Deaths("Player {p}", "{state}", Exactly, {i+1})',
                f'Command("Player {p}", "{offer["unit"]}", At most, 0)',
                f'Switch("{cfg["launch_switch"]}", set)'],[
                'Defeat()']))
        blocks.append(trig(f'"Player {p}"',[
            f'Deaths("Player {p}", "{state}", Exactly, {len(offers)+1})',
            f'Command("Player {p}", "{fallback}", At most, 0)',
            f'Switch("{cfg["launch_switch"]}", set)'],['Defeat()']))

    ready=[f'Deaths("Player {p}", "{state}", At least, 1)' for p in range(1,humans+1)]
    blocks.append(trig(f'"Player {system_player}"',[
        *ready,f'Switch("{cfg["launch_switch"]}", not set)'],[
        f'Set Switch("{cfg["launch_switch"]}", set)',
        f'Set Deaths("Player {system_player}", "{stage_state}", Set To, 0)',
        f'Set Countdown Timer(Set To, {cfg["rules"]["battle_seconds"]})',
        f'Remove Unit At Location("Player 12", "{waves[0]["unit"]}", All, "{labels["enemy_spawn"]}")',
        f'Create Unit("Player {enemy_player}", "{waves[0]["unit"]}", {waves[0]["count"]}, "{labels["enemy_spawn"]}")',
        f'Order("Player {enemy_player}", "{waves[0]["unit"]}", "{labels["enemy_spawn"]}", "{labels["arena"]}", attack)']))
    for p in range(1,humans+1):
        blocks.append(trig(f'"Player {p}"',[
            f'Switch("{cfg["launch_switch"]}", set)',
            f'Deaths("Player {system_player}", "{stage_state}", Exactly, 0)',
            f'Deaths("Player {p}", "{notice_state}", Exactly, 0)'],[
            f'Set Deaths("Player {p}", "{notice_state}", Set To, 1)',
            f'Display Text Message(Always Display, "{cfg["text"]["messages"]["battle_start"]}")']))
    for i,wave in enumerate(waves):
        conds=[f'Switch("{cfg["launch_switch"]}", set)',f'Deaths("Player {system_player}", "{stage_state}", Exactly, {i})',
               f'Command("Player {enemy_player}", "{wave["unit"]}", At most, 0)']
        if i+1<len(waves):
            nxt=waves[i+1]
            actions=[f'Set Deaths("Player {system_player}", "{stage_state}", Set To, {i+1})',
                     f'Create Unit("Player {enemy_player}", "{nxt["unit"]}", {nxt["count"]}, "{labels["enemy_spawn"]}")',
                     f'Order("Player {enemy_player}", "{nxt["unit"]}", "{labels["enemy_spawn"]}", "{labels["arena"]}", attack)']
            blocks.append(trig(f'"Player {system_player}"',conds,actions))
            for p in range(1,humans+1):
                blocks.append(trig(f'"Player {p}"',[
                    f'Switch("{cfg["launch_switch"]}", set)',
                    f'Deaths("Player {system_player}", "{stage_state}", Exactly, {i+1})',
                    f'Deaths("Player {p}", "{notice_state}", Exactly, {i+1})'],[
                    f'Set Deaths("Player {p}", "{notice_state}", Set To, {i+2})',
                    f'Display Text Message(Always Display, "{wave["clear_message"]}")']))
        else:
            for p in range(1,humans+1):
                blocks.append(trig(f'"Player {p}"',conds,[
                    f'Display Text Message(Always Display, "{wave["clear_message"]}")',
                    f'Display Text Message(Always Display, "{cfg["text"]["messages"]["victory"]}")','Victory()']))
    for p in range(1,humans+1):
        blocks.append(trig(f'"Player {p}"',[
            f'Switch("{cfg["launch_switch"]}", set)','Countdown Timer(At most, 0)',
            f'Deaths("Player {system_player}", "{stage_state}", At most, {len(waves)-1})'],[
            f'Display Text Message(Always Display, "{cfg["text"]["messages"]["timeout"]}")','Defeat()']))

    scmap.reveal_for_all(cli, humans)
    cli.apply_triggers(scmap.TRIGGER_SEP.join(blocks))
    profile.apply_profile_metadata(cli,cfg,humans)
    print(f"\nCreated {a.out}: {humans} players, {len(offers)} draft offers, {len(waves)} waves, {len(blocks)} triggers")
    return 0


if __name__ == "__main__":
    try: sys.exit(main())
    except CliError as e:
        print(f"Error: {e}",file=sys.stderr); sys.exit(1)
