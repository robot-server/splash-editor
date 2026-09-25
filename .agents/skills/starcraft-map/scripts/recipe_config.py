#!/usr/bin/env python3
"""Shared, required JSON profile for AI-authored Use Map recipe content.

Recipe scripts own the reusable space/trigger structure. The calling agent
supplies player-facing text, units, wave values, economy, upgrade and tech
choices in JSON so every generated map can get a deliberate, different setup.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import scmap
from scmap import CliError

TILESETS = {"badlands", "space", "ashworld", "jungle", "desert", "ice", "twilight"}


def _need(obj: dict, key: str, typ, where: str):
    if key not in obj or not isinstance(obj[key], typ):
        raise CliError(f"profile {where}.{key} must be {typ.__name__}")
    return obj[key]


def load_profile(path: str, genre: str) -> dict[str, Any]:
    try:
        cfg = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        raise CliError(f"cannot read JSON profile {path}: {e}") from e
    if not isinstance(cfg, dict) or cfg.get("genre") != genre:
        raise CliError(f"profile genre must be {genre!r}")

    m = _need(cfg, "map", dict, "root")
    for k in ("name", "description", "tileset"):
        _need(m, k, str, "map")
    _need(m, "seed", int, "map")
    size = _need(m, "size", list, "map")
    if len(size) != 2 or any(type(x) is not int or not 48 <= x <= 128 for x in size):
        raise CliError("map.size must be [width,height], each between 48 and 128 tiles")
    if m["tileset"] not in TILESETS:
        raise CliError(f"unsupported Use Map tileset: {m['tileset']}")

    t = _need(cfg, "text", dict, "root")
    _need(t, "objectives", str, "text")
    lines = _need(t, "briefing", list, "text")
    if not lines or any(not isinstance(x, str) for x in lines):
        raise CliError("text.briefing must be a non-empty list of strings")
    _need(t, "messages", dict, "text")
    for k,v in t["messages"].items():
        if not isinstance(k,str):
            raise CliError("text.messages keys must be strings")
        if genre in ("rpg","zombie") and k == "intro":
            if not isinstance(v,list) or not v or any(not isinstance(line,str) or not line for line in v):
                raise CliError("rpg text.messages.intro must be a list of non-empty strings")
        elif not isinstance(v,str) or not v:
            raise CliError(f"text.messages.{k} must be a non-empty string")
    _need(t, "portrait", str, "text")
    _need(t, "briefing_hold_ms", int, "text")
    if not 0 <= t["briefing_hold_ms"] <= 60000:
        raise CliError("text.briefing_hold_ms must be from 0 to 60000")

    res = _need(cfg, "starting_resources", dict, "root")
    for k in ("minerals", "gas"):
        if type(_need(res, k, int, "starting_resources")) is not int or res[k] < 0:
            raise CliError(f"starting_resources.{k} must be non-negative")

    units = _need(cfg, "units", dict, "root")
    for k, v in units.items():
        if not isinstance(k, str) or not (isinstance(v, str) and v or isinstance(v, list) and all(isinstance(x,str) and x for x in v)):
            raise CliError("units must map roles to unit names or a list of unit names")
    rules = _need(cfg, "rules", dict, "root")
    if genre == "micro_trial":
        for k in ("stages", "time_limit"):
            _need(rules, k, int, "rules")
        waves = _need(cfg, "waves", list, "root")
        if len(waves) < rules["stages"]:
            raise CliError("waves must have at least rules.stages entries")
        for i, wave in enumerate(waves):
            for k in ("unit", "count", "reward", "clear_message"):
                if k not in wave:
                    raise CliError(f"waves[{i}].{k} is required")
        players = _need(cfg, "players", dict, "root")
        _need(players, "enemy_race", str, "players")
        _need(players, "system_race", str, "players")
        if len(_need(_need(cfg, "labels", dict, "root"), "bays", list, "labels")) < rules["stages"]:
            raise CliError("labels.bays must name each active bay")
    elif genre == "hide_seek":
        for k in ("hiders", "prep", "survive"):
            _need(rules, k, int, "rules")
        _need(units, "room_marker", str, "units")
        _need(units, "room_marker_name", str, "units")
        _need(units, "state_token", str, "units")
        if units["state_token"] == units["room_marker"]:
            raise CliError("hide_seek state_token must be an unplaced unit distinct from room_marker")
        labels = _need(cfg, "labels", dict, "root")
        if len(_need(labels, "rooms", list, "labels")) < rules["hiders"]:
            raise CliError("labels.rooms must have one configured label per hider room")
        _need(labels, "hall", str, "labels")
    elif genre == "room_escape":
        _need(rules, "time_limit", int, "rules")
        seals = _need(units, "seals", list, "units")
        seal_names = _need(units, "seal_names", list, "units")
        state_token = _need(units, "state_token", str, "units")
        if len(seals) != 3 or len(seal_names) != 3 or any(not isinstance(s,str) or not s for s in seals+seal_names):
            raise CliError("units.seals and units.seal_names must each contain three non-empty strings")
        if state_token in seals:
            raise CliError("room_escape state_token must be unplaced and distinct from seal units")
        if len(_need(_need(cfg, "labels", dict, "root"), "areas", list, "labels")) != 6:
            raise CliError("labels.areas must name the six room locations")
    elif genre == "loadout_gauntlet":
        for k in ("players", "draft_seconds", "battle_seconds"):
            _need(rules, k, int, "rules")
        if not 5 <= rules["draft_seconds"] <= 86400 or not 30 <= rules["battle_seconds"] <= 86400:
            raise CliError("draft_seconds must be 5..86400 and battle_seconds 30..86400")
        offers = _need(cfg, "offers", list, "root")
        waves = _need(cfg, "waves", list, "root")
        if not 1 <= rules["players"] <= 4 or not 2 <= len(offers) <= 4 or not 2 <= len(waves) <= 4:
            raise CliError("loadout_gauntlet requires 1..4 players, 2..4 offers, and 2..4 waves")
        units = _need(cfg, "units", dict, "root")
        for k in ("recruit", "fallback", "choice_state", "wave_state", "notice_state", "presence_counter", "home_marker"):
            _need(units, k, str, "units")
        if len({units[k] for k in ("recruit", "fallback", "choice_state", "wave_state", "notice_state", "presence_counter", "home_marker")}) != 7:
            raise CliError("loadout state counters, recruit, fallback and home marker units must be distinct")
        for i, offer in enumerate(offers):
            for k in ("unit", "marker", "unit_name", "marker_name", "location", "label", "receipt", "cost", "count"):
                if k not in offer:
                    raise CliError(f"offers[{i}].{k} is required")
            if any(not isinstance(offer[k], str) or not offer[k] for k in ("unit", "marker", "unit_name", "marker_name", "location", "label", "receipt")):
                raise CliError(f"offers[{i}] display and unit fields must be non-empty strings")
            if type(offer["cost"]) is not int or not 1 <= offer["cost"] <= 100000 or type(offer["count"]) is not int or not 1 <= offer["count"] <= 8:
                raise CliError(f"offers[{i}] cost must be 1..100000 and count 1..8")
        for i, wave in enumerate(waves):
            for k in ("unit", "count", "clear_message"):
                if k not in wave:
                    raise CliError(f"waves[{i}].{k} is required")
            if not isinstance(wave["unit"], str) or not isinstance(wave["clear_message"], str):
                raise CliError(f"waves[{i}] unit and clear_message must be strings")
            if type(wave["count"]) is not int or not 1 <= wave["count"] <= 24:
                raise CliError(f"waves[{i}].count must be 1..24 for the reserved spawn footprint")
        labels = _need(cfg, "labels", dict, "root")
        for k in ("draft", "arena", "enemy_spawn"):
            _need(labels, k, str, "labels")
        for k in ("homes", "recruit_pads"):
            if len(_need(labels, k, list, "labels")) < rules["players"]:
                raise CliError(f"labels.{k} must provide one location per human player")
        players = _need(cfg, "players", dict, "root")
        for k in ("race", "enemy_race", "system_race"):
            _need(players, k, str, "players")
        _need(cfg, "launch_switch", str, "root")
        needed_messages = ("fallback", "battle_start", "victory", "timeout")
        if any(not isinstance(cfg["text"]["messages"].get(k), str) or not cfg["text"]["messages"][k] for k in needed_messages):
            raise CliError("loadout_gauntlet text.messages needs fallback, battle_start, victory, and timeout")
    elif genre == "control":
        for k in ("players", "goal", "respawn_seconds", "pocket_size", "buy_cost",
                  "bounty_per_kill", "bounty_score_step", "obstacle_count", "heal_cost"):
            _need(rules, k, int, "rules")
        if not 2 <= rules["players"] <= 8 or not 1 <= rules["goal"] <= 10000 or not 1 <= rules["respawn_seconds"] <= 3600:
            raise CliError("control players must be 2..8, goal 1..10000, respawn 1..3600")
        if not 12 <= rules["pocket_size"] <= 32 or not 1 <= rules["buy_cost"] <= 100000:
            raise CliError("control pocket_size must be 12..32 and buy_cost 1..100000")
        squads = _need(cfg, "squads", list, "root")
        if len(squads) < 2 or not 0 <= cfg.get("starting_squad", -1) < len(squads):
            raise CliError("control needs at least two squads and a valid starting_squad index")
        for i, squad in enumerate(squads):
            for k in ("unit", "count", "marker", "receipt", "unit_name", "marker_name"):
                if k not in squad:
                    raise CliError(f"squads[{i}].{k} is required")
            if any(not isinstance(squad[k],str) or not squad[k] for k in ("unit","marker","receipt","unit_name","marker_name")):
                raise CliError(f"squads[{i}] unit/marker/receipt/name fields must be non-empty strings")
            if type(squad["count"]) is not int or not 1 <= squad["count"] <= 24:
                raise CliError(f"squads[{i}].count must be 1..24")
        if type(rules["buy_cost"]) is not int or not 1 <= rules["buy_cost"] <= 100000:
            raise CliError("control buy_cost must be 1..100000")
        if not 0 <= rules["obstacle_count"] <= 48 or not 0 <= rules["heal_cost"] <= 100000:
            raise CliError("control obstacle_count must be 0..48 and heal_cost 0..100000")
        if not 0 <= rules["bounty_per_kill"] <= 100000 or not 1 <= rules["bounty_score_step"] <= 100000:
            raise CliError("control bounty values must be within 0..100000 and 1..100000")
        for k in ("arena_obstacle", "purchase_beacon"):
            _need(units,k,str,"units")
        labels=_need(cfg,"labels",dict,"root")
        for k in ("arena", "player_prefix", "spawn_suffix", "gate_suffix",
                  "all_suffix", "buy_prefix"):
            _need(labels,k,str,"labels")
        players=_need(cfg,"players",dict,"root")
        _need(players,"race",str,"players")
        msg = _need(t,"messages",dict,"text")
        if any(not isinstance(msg.get(k),str) or not msg[k] for k in ("start","controls","respawn","victory","leaderboard")):
            raise CliError("control text.messages needs start, controls, respawn, victory, leaderboard")
    elif genre == "quiz":
        for k in ("players","lives","seconds"):
            _need(rules,k,int,"rules")
        if not 1 <= rules["players"] <= 7 or not 1 <= rules["lives"] <= 99 or not 2 <= rules["seconds"] <= 180:
            raise CliError("quiz players=1..7, lives=1..99, seconds=2..180")
        questions=_need(cfg,"questions",list,"root")
        if not questions:
            raise CliError("quiz needs at least one AI-authored question")
        for i,q in enumerate(questions):
            if not isinstance(q,dict) or not isinstance(q.get("prompt"),str) or not q["prompt"] or q.get("answer") not in ("O","X"):
                raise CliError(f"questions[{i}] needs a prompt and answer O or X")
        units=_need(cfg,"units",dict,"root")
        for k in ("life_counter","question_counter","shown_counter","judged_counter","selection","o_marker","x_marker"):
            _need(units,k,str,"units")
        counters=[units[k] for k in ("life_counter","question_counter","shown_counter","judged_counter")]
        if len(set(counters)) != 4 or units["selection"] in counters:
            raise CliError("quiz counters must be distinct and may not reuse the selection unit")
        msg=_need(t,"messages",dict,"text")
        for k in ("start","directions","question","correct","wrong","no_answer","out_of_lives","survived","leaderboard"):
            if not isinstance(msg.get(k),str) or not msg[k]:
                raise CliError(f"quiz text.messages.{k} is required")
        _need(_need(cfg,"players",dict,"root"),"race",str,"players")
        labels=_need(cfg,"labels",dict,"root")
        for k in ("lobby","pad_o","pad_x","start_prefix"):
            _need(labels,k,str,"labels")
    elif genre == "rpg":
        for k in ("players","zones","heal_cost","bounty_per_kill","bounty_score_step","boss_count",
                  "room_width","room_height","room_gap","town_heal_percent","zone_heal_percent",
                  "companion_count","field_obstacles","boss_switch_id"):
            _need(rules,k,int,"rules")
        _need(rules,"terrain_mode",str,"rules")
        if not 1 <= rules["players"] <= 6 or not 2 <= rules["zones"] <= 5:
            raise CliError("rpg players must be 1..6 and zones 2..5")
        if not 0 <= rules["heal_cost"] <= 100000 or not 0 <= rules["boss_count"] <= 24:
            raise CliError("rpg heal_cost must be 0..100000 and boss_count 0..24")
        if not 1 <= rules["boss_switch_id"] <= 256 or rules["boss_switch_id"] <= rules["zones"]:
            raise CliError("rpg boss_switch_id must be 1..256 and outside zone notice switches")
        if not 1 <= rules["bounty_per_kill"] <= 100000 or not 1 <= rules["bounty_score_step"] <= 100000:
            raise CliError("rpg bounty values must be 1..100000")
        if not 20 <= rules["room_width"] <= 36 or not 18 <= rules["room_height"] <= 30 or not 3 <= rules["room_gap"] <= 10:
            raise CliError("rpg room geometry out of supported range")
        if rules["terrain_mode"] not in ("isom","rect"):
            raise CliError("rpg terrain_mode must be isom or rect")
        if not 1 <= rules["boss_count"] <= 24 or not 1 <= rules["companion_count"] <= 8 or not 0 <= rules["field_obstacles"] <= 30:
            raise CliError("rpg boss_count/companion_count/field_obstacles out of supported range")
        if not 0 <= rules["town_heal_percent"] <= 100 or not 0 <= rules["zone_heal_percent"] <= 100:
            raise CliError("rpg healing percents must be 0..100")
        zones=_need(cfg,"zones",list,"root")
        shops=_need(cfg,"shops",list,"root")
        if len(zones)<rules["zones"] or not 1 <= len(shops) <= 4:
            raise CliError("rpg needs enough zone definitions and at least one shop")
        for i,z in enumerate(zones[:rules["zones"]]):
            for k in ("unit","count","reward","label","marker","marker_name"):
                if k not in z: raise CliError(f"zones[{i}].{k} is required")
            if any(not isinstance(z[k],str) or not z[k] for k in ("unit","label","marker","marker_name")):
                raise CliError(f"zones[{i}] unit and display labels must be non-empty strings")
            if type(z["count"]) is not int or not 1 <= z["count"] <= 100 or type(z["reward"]) is not int or not 0 <= z["reward"] <= 100000:
                raise CliError(f"zones[{i}] count/reward outside supported range")
        for i,shop in enumerate(shops):
            for k in ("marker","marker_name","label","cost","receipt","actions"):
                if k not in shop: raise CliError(f"shops[{i}].{k} is required")
            if any(not isinstance(shop[k],str) or not shop[k] for k in ("marker","marker_name","label","receipt")):
                raise CliError(f"shops[{i}] marker/name/label/receipt must be non-empty strings")
            if type(shop["cost"]) is not int or not 0 <= shop["cost"] <= 100000 or not isinstance(shop["actions"],list) or not shop["actions"]:
                raise CliError(f"shops[{i}] needs a cost and one or more effect actions")
            if any(not isinstance(x,str) or "\n" in x or "\r" in x or ";" in x for x in shop["actions"]):
                raise CliError(f"shops[{i}].actions must be single safe trigger action lines")
        players=_need(cfg,"players",dict,"root")
        for k in ("race","enemy_race","boss_race"):_need(players,k,str,"players")
        for k in ("hero","companion","boss","revive_lock","shop_beacon","heal_marker","heal_building","field_obstacle"):
            _need(units,k,str,"units")
        messages=_need(t,"messages",dict,"text")
        for k in ("intro","zone_enter","boss_win","town_heal","zone_heal","revive"):
            if k not in messages: raise CliError(f"text.messages.{k} is required")
        if not isinstance(messages["intro"],list) or not messages["intro"]:
            raise CliError("text.messages.intro must be a non-empty list")
        labels=_need(cfg,"labels",dict,"root")
        for k in ("town","zone_prefix","zone_spawn_suffix","heal_prefix","boss","boss_spawn","home_prefix","home_suffix","shop_prefix","boss_switch"):
            _need(labels,k,str,"labels")
    elif genre == "zombie":
        for k in ("players","survive_seconds","wave_count","zombie_population","starting_survivors",
                  "boss_seconds","heal_cost","field_obstacles","infection_spawn_count",
                  "bounty_per_kill","bounty_score_step","margin","gap","min_shelter_width","min_graveyard_width","home_spacing","shop_spacing",
                  "shelter_ratio_pct","graveyard_ratio_pct"):
            _need(rules,k,int,"rules")
        if not 2 <= rules["players"] <= 6 or not 60 <= rules["survive_seconds"] <= 86400 or not 1 <= rules["wave_count"] <= 24:
            raise CliError("zombie players=2..6, survive_seconds=60..86400, wave_count=1..24")
        if not 2 <= rules["zombie_population"] <= 100 or not 1 <= rules["starting_survivors"] <= 24 or not 1 <= rules["infection_spawn_count"] <= 24:
            raise CliError("zombie population/survivor unit counts outside supported range")
        if not 0 <= rules["heal_cost"] <= 100000 or not 0 <= rules["field_obstacles"] <= 64 or not 1 <= rules["boss_seconds"] <= rules["survive_seconds"]:
            raise CliError("zombie costs, obstacle count, or boss time outside range")
        if not 0 <= rules["bounty_per_kill"] <= 100000 or not 1 <= rules["bounty_score_step"] <= 100000:
            raise CliError("zombie bounty values out of range")
        if not 2 <= rules["margin"] <= 16 or not 3 <= rules["gap"] <= 20 or not 20 <= rules["min_shelter_width"] <= 48 or not 16 <= rules["min_graveyard_width"] <= 48:
            raise CliError("zombie area margin/gap/minimum widths outside supported range")
        if not 25 <= rules["shelter_ratio_pct"] <= 55 or not 15 <= rules["graveyard_ratio_pct"] <= 40 or rules["shelter_ratio_pct"]+rules["graveyard_ratio_pct"]>80:
            raise CliError("zombie shelter/graveyard proportions must leave a walkable field")
        if not 6 <= rules["home_spacing"] <= 18 or not 5 <= rules["shop_spacing"] <= 12:
            raise CliError("zombie home/shop spacing out of range")
        players=_need(cfg,"players",dict,"root")
        for k in ("race","enemy_race","boss_race"):_need(players,k,str,"players")
        units=_need(cfg,"units",dict,"root")
        for k in ("survivor","zombie","boss","infection_counter","wave_counter","shop_selection","shop_beacon","heal_marker","heal_building","field_obstacle"):_need(units,k,str,"units")
        shops=_need(cfg,"shops",list,"root")
        if not shops or len(shops)>4:raise CliError("zombie requires 1..4 profiled shops")
        for i,shop in enumerate(shops):
            for k in ("marker","marker_name","unit","count","cost","receipt","actions"):
                if k not in shop:raise CliError(f"shops[{i}].{k} is required")
            if any(not isinstance(shop[k],str) or not shop[k] for k in ("marker","marker_name","unit","receipt")) or type(shop["count"]) is not int or not 1 <= shop["count"] <= 16 or type(shop["cost"]) is not int or not 0 <= shop["cost"] <= 100000 or not isinstance(shop["actions"],list) or not shop["actions"]:
                raise CliError(f"shops[{i}] marker, price, receipt and effect actions are required")
            if any(not isinstance(x,str) or "\n" in x or "\r" in x or ";" in x for x in shop["actions"]):
                raise CliError(f"shops[{i}].actions must be single trigger action lines")
        msg=_need(t,"messages",dict,"text")
        for k in ("intro","objectives","infected","wave","boss","survived","leaderboard","healed","revived"):
            if k not in msg:raise CliError(f"zombie text.messages.{k} is required")
        if not isinstance(msg["intro"],list) or any(not isinstance(x,str) for x in msg["intro"]):
            raise CliError("zombie text.messages.intro must be a string list")
        labels=_need(cfg,"labels",dict,"root")
        for k in ("shelter","field","graveyard","home_prefix","home_suffix","shop_prefix","wave_spawn"):_need(labels,k,str,"labels")
    elif genre == "wave_defense":
        for k in ("players","waves","lane_width","wall_thickness","lane_margin","stop_width","stop_height",
                  "shared_lives","initial_timer","wave_interval_seconds","wave_growth",
                  "starting_unit_count","bounty_per_kill","bounty_score_step","boss_count"):
            _need(rules,k,int,"rules")
        _need(rules,"enemy_order",str,"rules")
        if rules["enemy_order"] not in ("attack","patrol","move"):
            raise CliError("enemy_order must be attack, patrol, or move")
        if not 1 <= rules["players"] <= 6 or not 2 <= rules["waves"] <= 30 or not 9 <= rules["lane_width"] <= 21:
            raise CliError("wave_defense players=1..6, waves=2..30, lane_width=9..21")
        if not 1 <= rules["wall_thickness"] <= 3 or not 5 <= rules["lane_margin"] <= 24 or not 10 <= rules["stop_width"] <= 20 or not 8 <= rules["stop_height"] <= 18:
            raise CliError("wave_defense lane/stop geometry outside supported range")
        if not 1 <= rules["shared_lives"] <= 1000 or not 1 <= rules["initial_timer"] <= 3600 or not 5 <= rules["wave_interval_seconds"] <= 3600:
            raise CliError("wave_defense lives/timer values outside supported range")
        if not 0 <= rules["wave_growth"] <= 24 or not 1 <= rules["starting_unit_count"] <= 24 or not 0 <= rules["bounty_per_kill"] <= 100000 or not 1 <= rules["bounty_score_step"] <= 100000 or not 1 <= rules["boss_count"] <= 24:
            raise CliError("wave_defense growth/reward/count outside supported range")
        waves=_need(cfg,"waves",list,"root")
        if len(waves)<rules["waves"]:raise CliError("waves list must cover rules.waves")
        for i,wave in enumerate(waves):
            for k in ("unit","count"):
                if k not in wave:raise CliError(f"waves[{i}].{k} is required")
            if not isinstance(wave["unit"],str) or not wave["unit"] or type(wave["count"]) is not int or not 1 <= wave["count"] <= 200:
                raise CliError(f"waves[{i}] unit/count invalid")
        shops=_need(cfg,"shops",list,"root")
        if not 1 <= len(shops) <= 4:raise CliError("wave_defense requires 1..4 shops")
        if rules["stop_width"] < 3*len(shops)+2 or rules["stop_height"] < 3*len(shops)+3:
            raise CliError("stop_width/stop_height must leave room for every beacon and adjacent result-unit marker")
        for i,shop in enumerate(shops):
            for k in ("beacon","icon","icon_name","label","cost","receipt","actions"):
                if k not in shop:raise CliError(f"shops[{i}].{k} is required")
            if any(not isinstance(shop[k],str) or not shop[k] for k in ("beacon","icon","icon_name","label","receipt")) or type(shop["cost"]) is not int or not 0 <= shop["cost"] <= 100000 or not isinstance(shop["actions"],list) or not shop["actions"]:
                raise CliError(f"shops[{i}] needs a marker, cost, receipt, and effect action")
        players=_need(cfg,"players",dict,"root")
        for k in ("race","enemy_race","boss_race"):_need(players,k,str,"players")
        units=_need(cfg,"units",dict,"root")
        for k in ("boss","starting_unit","enemy_entry_marker"):_need(units,k,str,"units")
        labels=_need(cfg,"labels",dict,"root")
        for k in ("entry","exit","way_prefix","stop_prefix","shop_suffix","boss_name"):_need(labels,k,str,"labels")
        msg=_need(t,"messages",dict,"text")
        for k in ("intro","objectives","wave","leak","defeat","victory","leaderboard","boss"):
            if not isinstance(msg.get(k),str) or not msg[k]:raise CliError(f"wave_defense text.messages.{k} required")
    elif genre == "square_defense":
        for k in ("players","waves","arena_width","arena_height","arena_gap","ring_width","wall_width",
                  "shared_lives","initial_timer","wave_interval_seconds","wave_growth","starting_unit_count",
                  "bounty_per_kill","bounty_score_step","boss_count"):
            _need(rules,k,int,"rules")
        if not 1 <= rules["players"] <= 6 or not 2 <= rules["waves"] <= 30:
            raise CliError("square_defense players=1..6 and waves=2..30")
        if not 30 <= rules["arena_width"] <= 64 or not 30 <= rules["arena_height"] <= 64 or not 1 <= rules["arena_gap"] <= 8:
            raise CliError("square_defense arena size/gap outside supported range")
        if not 3 <= rules["ring_width"] <= 8 or not 1 <= rules["wall_width"] <= 4:
            raise CliError("square_defense ring/wall thickness outside supported range")
        if not 1 <= rules["shared_lives"] <= 1000 or not 1 <= rules["initial_timer"] <= 3600 or not 5 <= rules["wave_interval_seconds"] <= 3600:
            raise CliError("square_defense lives/timers outside supported range")
        waves=_need(cfg,"waves",list,"root")
        if len(waves)<rules["waves"]:raise CliError("waves list must cover rules.waves")
        for i,w in enumerate(waves):
            if not isinstance(w.get("unit"),str) or type(w.get("count")) is not int or not 1 <= w["count"] <= 100:
                raise CliError(f"waves[{i}] needs a unit and count 1..100")
        shops=_need(cfg,"shops",list,"root")
        if not 1 <= len(shops) <= 4:raise CliError("square_defense requires 1..4 shops")
        for i,shop in enumerate(shops):
            for k in ("beacon","icon","icon_name","label","cost","receipt","actions"):
                if k not in shop:raise CliError(f"shops[{i}].{k} required")
            if any(not isinstance(shop[k],str) or not shop[k] for k in ("beacon","icon","icon_name","label","receipt")) or type(shop["cost"]) is not int or not 0 <= shop["cost"] <= 100000 or not isinstance(shop["actions"],list) or not shop["actions"]:
                raise CliError(f"shops[{i}] needs valid icon, price, receipt and action")
        players=_need(cfg,"players",dict,"root")
        for k in ("race","enemy_race","boss_race"):_need(players,k,str,"players")
        units=_need(cfg,"units",dict,"root")
        for k in ("boss","starting_unit","enemy_entry_marker","wave_counter","life_counter","seen_counter","revive_lock"):_need(units,k,str,"units")
        labels=_need(cfg,"labels",dict,"root")
        for k in ("arena_prefix","spawn_suffix","ne_suffix","se_suffix","sw_suffix","exit_suffix","center_suffix","all_suffix","shop_suffix","boss_name"):_need(labels,k,str,"labels")
        messages=_need(t,"messages",dict,"text")
        for k in ("intro","objectives","leaderboard","wave","boss","leak","defeat","win","respawn","clear_arena"):
            if not isinstance(messages.get(k),str) or not messages[k]:raise CliError(f"square_defense text.messages.{k} required")

    # Beacon/result labels are type-wide UNIS names. The visible name beside a
    # beacon must agree with the name that the profile asks us to store.
    unit_names = cfg.get("unit_names", {})
    if not isinstance(unit_names, dict) or any(
            not isinstance(k, str) or not k or not isinstance(v, str) or not v
            for k, v in unit_names.items()):
        raise CliError("unit_names must map unit type names to non-empty labels")
    named_items = []
    if genre == "control":
        named_items = [(s[k], s[n]) for s in cfg["squads"]
                       for k, n in (("unit", "unit_name"), ("marker", "marker_name"))]
    elif genre == "loadout_gauntlet":
        named_items = [(o[k], o[n]) for o in cfg["offers"]
                       for k, n in (("unit", "unit_name"), ("marker", "marker_name"))]
    elif genre == "rpg":
        named_items = [(z["marker"], z["marker_name"]) for z in cfg["zones"]]
        named_items += [(s["marker"], s["marker_name"]) for s in cfg["shops"]]
    elif genre == "zombie":
        named_items = [(s["marker"], s["marker_name"]) for s in cfg["shops"]]
    elif genre in ("square_defense", "wave_defense"):
        named_items = [(s["icon"], s["icon_name"]) for s in cfg["shops"]]
    expected_names = {}
    for unit, label in named_items:
        if unit in expected_names and expected_names[unit] != label:
            raise CliError(f"unit type {unit} has conflicting beacon labels")
        expected_names[unit] = label
    for unit, label in expected_names.items():
        if unit in unit_names and unit_names[unit] != label:
            raise CliError(f"unit_names[{unit!r}] conflicts with the adjacent beacon label")

    up = _need(cfg, "upgrades", dict, "root")
    for k, ty in (("which", list), ("free_levels", int), ("max_level", int),
                  ("minerals", int), ("gas", int), ("time", int)):
        _need(up, k, ty, "upgrades")
    if not 0 <= up["free_levels"] <= up["max_level"] <= 3:
        raise CliError("upgrades levels must satisfy 0 <= free_levels <= max_level <= 3")
    if any(type(x) is not int or not 0 <= x < 61 for x in up["which"]):
        raise CliError("upgrades.which contains an invalid upgrade ID")

    tech = _need(cfg, "technologies", dict, "root")
    for k, ty in (("which", list), ("available", (list, str)),
                  ("researched", (list, str)), ("minerals", int),
                  ("gas", int), ("time", int), ("energy", int)):
        _need(tech, k, ty, "technologies")
    if not isinstance(tech["researched"], (list, str)):
        raise CliError("technologies.researched must be a player list or all/none")
    for key in ("available", "researched"):
        if isinstance(tech[key], list) and any(type(player) is not int or not 1 <= player <= 12
                                               for player in tech[key]):
            raise CliError(f"technologies.{key} player IDs must be from 1 to 12")
    if any(type(x) is not int or not 0 <= x < 44 for x in tech["which"]):
        raise CliError("technologies.which contains an invalid technology ID")

    unit_settings = _need(cfg, "unit_settings", dict, "root")
    unit_setting_fields = {"hp", "shields", "armor", "build_time", "minerals", "gas", "buildable"}
    limits = {"hp": (1, 65535), "shields": (0, 65535), "armor": (0, 255),
              "build_time": (0, 65535), "minerals": (0, 65535), "gas": (0, 65535)}
    for unit, settings in unit_settings.items():
        if not isinstance(unit, str) or not unit or not isinstance(settings, dict) or not settings:
            raise CliError("unit_settings must map unit names to non-empty settings")
        unknown = set(settings) - unit_setting_fields
        if unknown:
            raise CliError(f"unit_settings[{unit!r}] has unsupported fields: {sorted(unknown)}")
        for key, (lo, hi) in limits.items():
            if key in settings and (type(settings[key]) is not int or not lo <= settings[key] <= hi):
                raise CliError(f"unit_settings[{unit!r}].{key} must be {lo}..{hi}")
        if "buildable" in settings:
            value = settings["buildable"]
            if value not in ("all", "none") and (
                    not isinstance(value, list) or any(type(player) is not int or not 1 <= player <= 12
                                                       for player in value)):
                raise CliError(f"unit_settings[{unit!r}].buildable must be all, none, or player IDs 1..12")
    return cfg


def configure_progression(cli, cfg: dict, humans: int) -> tuple[int, int]:
    """Apply profile-selected upgrades and technologies; empty lists mean none."""
    up, te = cfg["upgrades"], cfg["technologies"]
    nu = scmap.setup_usemap_upgrades(
        cli, humans, which=up["which"], free_levels=up["free_levels"],
        max_level=up["max_level"], mineral=up["minerals"],
        gas=up["gas"], time=up["time"])
    nt = scmap.setup_usemap_tech(
        cli, which=te["which"], available=te["available"],
        researched=te["researched"], mineral=te["minerals"],
        gas=te["gas"], time=te["time"], energy=te["energy"])
    return nu, nt


def apply_unit_settings(cli, cfg: dict) -> int:
    """Apply AI-authored UNIS overrides while retaining installed unit defaults.

    Changing one UNIS field disables the global default for that unit type.
    Fill every other stat from this installation's units.dat before writing so
    omitted fields do not become zero-valued map overrides.
    """
    overrides = cfg["unit_settings"]
    if not overrides:
        return 0
    try:
        rows = json.loads(cli.run("unit-stats", cli.install, "--json"))
    except (ValueError, CliError) as e:
        raise CliError(f"설치본 유닛 기본 능력치를 읽지 못했습니다: {e}") from e
    by_name = {row.get("name"): row for row in rows.values()}
    fields = (("hp", "hp"), ("shields", "shields"), ("armor", "armor"),
              ("build_time", "build_time"), ("minerals", "minerals"), ("gas", "gas"))
    for unit, selected in overrides.items():
        base = by_name.get(unit)
        if base is None:
            raise CliError(f"unit_settings에 알 수 없는 유닛명이 있습니다: {unit}")
        merged = {key: selected.get(key, base[source]) for key, source in fields}
        args = ["unitdef", "set", cli.path, unit, "--default", "off"]
        for key, _source in fields:
            option = "--" + key.replace("_", "-")
            args.extend((option, str(merged[key])))
        if "buildable" in selected:
            table = selected["buildable"]
            args.extend(("--buildable", table if isinstance(table, str)
                         else ",".join(str(player) for player in table)))
            args.extend(("--uses-default", "none"))
        cli.edit(*args)
    return len(overrides)


def apply_unit_settings(cli, cfg: dict) -> int:
    """Apply AI-authored UNIS values, using the installed units.dat as base.

    UNIS overrides replace the full stat record, so omitted fields are copied
    from the target installation rather than left at empty map-table values.
    """
    overrides = cfg["unit_settings"]
    if not overrides:
        return 0
    try:
        defaults = json.loads(cli.run("unit-stats", cli.install, "--json"))
    except (ValueError, CliError) as e:
        raise CliError(f"설치본 유닛 기본 능력치를 읽지 못했습니다: {e}") from e
    by_name = {row.get("name"): row for row in defaults.values()}
    fields = (("hp", "hp"), ("shields", "shields"), ("armor", "armor"),
              ("build_time", "build_time"), ("minerals", "minerals"), ("gas", "gas"))
    applied = 0
    for unit, selected in overrides.items():
        base = by_name.get(unit)
        if base is None:
            raise CliError(f"unit_settings에 알 수 없는 유닛명이 있습니다: {unit}")
        merged = {key: selected.get(key, base[source]) for key, source in fields}
        args = ["unitdef", "set", cli.path, unit, "--default", "off"]
        for key, _source in fields:
            args.extend(("--hp" if key == "hp" else f"--{key.replace('_', '-')}", str(merged[key])))
        if "buildable" in selected:
            table = selected["buildable"]
            args.extend(("--buildable", table if isinstance(table, str)
                         else ",".join(str(player) for player in table)))
            args.extend(("--uses-default", "none"))
        cli.edit(*args)
        applied += 1
    return applied


def resource_actions(cfg: dict, players: list[str]) -> list[str]:
    values = cfg["starting_resources"]
    return [f'Set Resources("{p}", Set To, {values[key]}, {res})'
            for p in players for key, res in (("minerals", "ore"), ("gas", "gas"))]


def add_profile_arguments(ap, genre: str):
    ap.add_argument("--config", required=True,
                    help=f"AI 작성 {genre} 프로필 JSON (문구·유닛·수치 포함)")


def apply_profile_metadata(cli, cfg: dict, humans: int):
    """공통 게임 설정과 텍스트를 실제 맵에 쓴다."""
    from scmap import briefing_text
    cfg["text"]["briefing_hold_ms"] = cfg["text"].get("briefing_hold_ms", 1800)
    configure_progression(cli, cfg, humans)
    apply_unit_settings(cli, cfg)
    apply_unit_settings(cli, cfg)
    cli.apply_briefing(briefing_text(
        cfg["text"]["briefing"], objectives=cfg["text"]["objectives"],
        portrait=cfg["text"]["portrait"],
        hold_ms=cfg["text"]["briefing_hold_ms"]))
    cli.set_map_name(cfg["map"]["name"], cfg["map"]["description"])


def apply_unit_names(cli, cfg: dict):
    """Use profile-authored map-wide names; reject conflicting aliases."""
    names = cfg.get("unit_names", {})
    if not isinstance(names, dict):
        raise CliError("unit_names must map unit type names to display names")
    for unit, display in names.items():
        if not isinstance(unit, str) or not unit or not isinstance(display, str) or not display:
            raise CliError("unit_names keys and values must be non-empty strings")
        if any(c in display for c in ('"', "\n", "\r")):
            raise CliError(f"display name for {unit} may not contain quote/newline")
        cli.edit("unitdef", "set", cli.path, unit, "--name", display)
