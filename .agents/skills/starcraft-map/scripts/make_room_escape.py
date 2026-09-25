#!/usr/bin/env python3
"""Room-by-room puzzle escape, separate from the generic chase route.

The corpus index contains 26 room-escape titles. The one inspected map whose
CHK could be read stores its trigger body with Set Memory; its puzzle mechanics
are not copied here. This non-EUD recipe uses documented Bring + Deaths
progression and a room/hub graph: collect three room seals in order, then reach
the exit. The locations and terrain signs are part of the puzzle UI.

    python3 make_room_escape.py out.scx --time-limit 900
"""
from __future__ import annotations

import argparse
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scmap
from scmap import CliError
import recipe_config as profile

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}
W = H = 128
# Start, central hub, three seal chambers, final exit chamber.
AREA_RECTS = [(4,48,32,32),(48,48,32,32),(48,6,32,32),(92,48,32,32),(48,90,32,32),(4,90,32,32)]


def trig(owner: str, conditions: list[str], actions: list[str]) -> str:
    return (f"Trigger({owner}){{\nConditions:\n" +
            "".join(f"\t{c};\n" for c in conditions) + "\nActions:\n" +
            "".join(f"\t{a};\n" for a in actions) + "}")


def carve_routes(cli, palette):
    # Foyer↔hub, hub↔north/east/south chambers, south chamber↔exit.
    palette.fill(cli, "path", 32, 59, 20, 10)
    palette.fill(cli, "path", 59, 36, 10, 20)
    palette.fill(cli, "path", 76, 59, 20, 10)
    palette.fill(cli, "path", 59, 76, 10, 20)
    palette.fill(cli, "path", 32, 101, 20, 10)


def main(argv=None):
    ap = argparse.ArgumentParser(description="AI 프로필 기반 방 순서 퍼즐 유즈맵")
    ap.add_argument("out")
    profile.add_profile_arguments(ap, "room_escape")
    ap.add_argument("--time-limit", type=int, default=None)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    cfg = profile.load_profile(a.config, "room_escape")
    a.time_limit = a.time_limit if a.time_limit is not None else cfg["rules"]["time_limit"]
    a.tileset, a.seed = cfg["map"]["tileset"], cfg["map"]["seed"]
    if tuple(cfg["map"]["size"]) != (W, H):
        raise CliError("현재 방 연결 템플릿은 128x128 프로필만 받습니다")
    if os.path.exists(a.out) and not a.force:
        ap.error(f"이미 있습니다: {a.out} (--force 로 덮어쓰기)")
    if a.time_limit < 120:
        ap.error("제한 시간은 120초 이상이어야 합니다")

    ts = TILESETS[a.tileset]
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    pal = scmap.Palette(cli, ts, random.Random(a.seed), "usemap")
    # Wall substrate first; finite room and corridor footprints stay walkable.
    pal.fill(cli, "wall", 0, 0, W, H)
    for x, y, w, h in AREA_RECTS:
        scmap.room(cli, pal, x, y, w, h, rim=1, wall=False)
    carve_routes(cli, pal)

    # Make the intended route graph an actual tile-walk path.
    grid = scmap.walk_grid(cli, ts, 0, 0, W, H)
    points = [(18, 64), (64, 64), (64, 22), (108, 64), (64, 106), (18, 106)]
    walk = [scmap.nearest_walkable(grid, x * 4 + 2, y * 4 + 2, 24)
            for x, y in points]
    if any(p is None for p in walk) or any(
            not scmap.walk_reachable(grid, walk[0], p) for p in walk[1:]):
        raise CliError("방과 출구 사이의 보행 경로가 끊겼습니다")

    scmap.setup_usemap_players(cli, 1, [8], race="terran")
    labels = cfg["labels"]["areas"]
    for i, (x, y, w, h) in enumerate(AREA_RECTS):
        cli.edit("location", "add", cli.path, str(x + 2), str(y + 2),
                 str(x + w - 2), str(y + h - 2), "--tiles", "--name", labels[i])
    cli.place(scmap.START_LOCATION, 18, 64, owner=1)
    player_unit = cfg["units"]["player"]
    token_unit = cfg["units"]["state_token"]
    cli.place(player_unit, 18, 64, owner=1)
    for i, (x, y, w, h) in enumerate(AREA_RECTS[2:5]):
        # The profile supplies visibly different beacon types and display labels.
        cli.place(cfg["units"]["seals"][i], x + w - 7, y + 6, owner=12)
    for x, y, w, h in (AREA_RECTS[2], AREA_RECTS[3], AREA_RECTS[4], AREA_RECTS[5]):
        scmap.pad(cli, pal, x + w // 2, y + h // 2, 5, 5)

    blocks = scmap.hyper_triggers("Player 8")
    blocks.append(trig('"All players"', ["Always()"], [
        f'Set Mission Objectives("{cfg["text"]["objectives"]}")']))
    blocks.append(trig('"Player 8"', ["Always()"], [
        f'Set Deaths("Player 11", "{token_unit}", Set To, 0)']))
    blocks.append(trig('"Player 1"', ["Always()"], [
        *profile.resource_actions(cfg, ["Player 1"]),
        f'Set Countdown Timer(Set To, {a.time_limit})',
        f'Display Text Message(Always Display, "{cfg["text"]["messages"]["start"]}")']))
    for i in range(3):
        blocks.append(trig('"Player 1"', [
            f'Bring("Player 1", "{player_unit}", "{labels[i+2]}", At least, 1)',
            f'Deaths("Player 11", "{token_unit}", Exactly, {i})'], [
            f'Set Deaths("Player 11", "{token_unit}", Set To, {i+1})',
            f'Display Text Message(Always Display, "{cfg["text"]["messages"][f"seal_{i+1}"]}")',
            'Play WAV("sound\\Misc\\Button.wav", 300)']))
    blocks.append(trig('"Player 1"', [
        f'Bring("Player 1", "{player_unit}", "{labels[5]}", At least, 1)',
        f'Deaths("Player 11", "{token_unit}", Exactly, 3)'], [
        f'Display Text Message(Always Display, "{cfg["text"]["messages"]["complete"]}")',
        'Victory()']))
    blocks.append(trig('"Player 1"', [
        'Countdown Timer(At most, 0)',
        f'Deaths("Player 11", "{token_unit}", At most, 2)'], [
        f'Display Text Message(Always Display, "{cfg["text"]["messages"]["timeout"]}")',
        'Defeat()']))
    cli.apply_triggers(scmap.TRIGGER_SEP.join(blocks))
    profile.apply_profile_metadata(cli, cfg, 1)
    print(f"\nCreated {a.out}: 3 ordered room states, connected room graph, "
          f"{len(blocks)} triggers")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
