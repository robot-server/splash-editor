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
    if not all(isinstance(k, str) and isinstance(v, str) and v
               for k, v in t["messages"].items()):
        raise CliError("every text.messages entry must contain non-empty text")
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
        labels = _need(cfg, "labels", dict, "root")
        if len(_need(labels, "rooms", list, "labels")) < rules["hiders"]:
            raise CliError("labels.rooms must have one configured label per hider room")
        _need(labels, "hall", str, "labels")
    elif genre == "room_escape":
        _need(rules, "time_limit", int, "rules")
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
        for i, offer in enumerate(offers):
            for k in ("unit", "marker", "location", "label", "receipt", "cost", "count"):
                if k not in offer:
                    raise CliError(f"offers[{i}].{k} is required")
            if any(not isinstance(offer[k], str) or not offer[k] for k in ("unit", "marker", "location", "label", "receipt")):
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
    if any(type(x) is not int or not 0 <= x < 44 for x in tech["which"]):
        raise CliError("technologies.which contains an invalid technology ID")
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
    cli.apply_briefing(briefing_text(
        cfg["text"]["briefing"], objectives=cfg["text"]["objectives"],
        portrait=cfg["text"]["portrait"],
        hold_ms=cfg["text"]["briefing_hold_ms"]))
    cli.set_map_name(cfg["map"]["name"], cfg["map"]["description"])
