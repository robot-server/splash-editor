#!/usr/bin/env python3
"""비대칭 숨바꼭질: 한 명이 술래, 나머지는 방을 옮겨 숨는다.

코퍼스 술래잡기 표본의 사람/술래 역할 분리, 준비 시간, 생존 제한시간,
플레이어별 숨는 위치, Bring 포획과 Deaths 상태 표시를 참고한다. 이 레시피는
Spider Mine 변환을 재사용하지 않고 민간인 상태 토큰으로 포획을 표시한다.

    python3 make_hide_seek.py out.scx --hiders 4 --prep 30 --survive 240
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
ROOMS = [
    (12, 12, 34, 30),
    (82, 12, 34, 30),
    (12, 86, 34, 30),
    (82, 86, 34, 30),
]
HALL = (46, 46, 36, 36)


def trig(owner: str, conditions: list[str], actions: list[str]) -> str:
    return (f"Trigger({owner}){{\nConditions:\n" +
            "".join(f"\t{c};\n" for c in conditions) + "\nActions:\n" +
            "".join(f"\t{a};\n" for a in actions) + "}")


def connect_rooms(cli, palette):
    # Cut four doorways through the room rims and join them to the hall.
    for x, y in ((43, 23), (78, 23), (43, 93), (78, 93)):
        palette.fill(cli, "path", x, y, 8, 7)
    palette.fill(cli, "path", 60, 29, 8, 21)
    palette.fill(cli, "path", 60, 78, 8, 21)
    palette.fill(cli, "path", 48, 58, 16, 8)
    palette.fill(cli, "path", 64, 58, 16, 8)


def main(argv=None):
    ap = argparse.ArgumentParser(description="AI 프로필 기반 비대칭 숨바꼭질 유즈맵")
    ap.add_argument("out")
    ap.add_argument("--hiders", type=int, default=None,
                    help="숨는 사람 수 (2~4); P1은 술래")
    profile.add_profile_arguments(ap, "hide_seek")
    ap.add_argument("--prep", type=int, default=None, help="프로필 준비 시간 대체")
    ap.add_argument("--survive", type=int, default=None,
                    help="생존 팀이 버틸 시간(초)")
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    cfg = profile.load_profile(a.config, "hide_seek")
    for key in ("hiders", "prep", "survive"):
        if getattr(a, key) is None: setattr(a, key, cfg["rules"][key])
    a.tileset, a.seed = cfg["map"]["tileset"], cfg["map"]["seed"]
    if tuple(cfg["map"]["size"]) != (W, H): raise CliError("hide seek profile requires 128x128")
    if os.path.exists(a.out) and not a.force:
        ap.error(f"이미 있습니다: {a.out} (--force 로 덮어쓰기)")
    if not 2 <= a.hiders <= 4:
        ap.error("숨는 사람은 2~4명입니다 (술래 한 명 포함 3~5명)")
    if a.prep < 5 or a.survive < 30:
        ap.error("준비 시간은 5초 이상, 생존 시간은 30초 이상이어야 합니다")

    humans = a.hiders + 1
    ts = TILESETS[a.tileset]
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    pal = scmap.Palette(cli, ts, random.Random(a.seed), "usemap")
    scmap.cover_map(cli, pal, W, H, margin=2)
    for x, y, w, h in ROOMS:
        scmap.room(cli, pal, x, y, w, h, rim=2, wall=True)
    scmap.room(cli, pal, *HALL, rim=2, wall=True)
    connect_rooms(cli, pal)

    # Reject a visually connected layout if the actual tile walk mask disagrees.
    grid = scmap.walk_grid(cli, ts, 0, 0, W, H)
    points = [(64, 64)] + [(x + w // 2, y + h // 2)
                           for x, y, w, h in ROOMS[:a.hiders]]
    walk = [scmap.nearest_walkable(grid, x * 4 + 2, y * 4 + 2, 24)
            for x, y in points]
    if any(p is None for p in walk) or any(
            not scmap.walk_reachable(grid, walk[0], p) for p in walk[1:]):
        raise CliError("술래 홀과 숨는 방 사이의 보행 경로가 연결되지 않았습니다")

    # Keep a free system-computer slot for hypers. Role diplomacy is set in
    # triggers because Force flags alone are not a runtime alliance guarantee.
    scmap.setup_usemap_players(cli, humans, [8], race="terran")
    location_specs = [(cfg["labels"]["hall"], *HALL)] + [
        (cfg["labels"]["rooms"][i], *r) for i, r in enumerate(ROOMS[:a.hiders])]
    for name, x, y, w, h in location_specs:
        cli.edit("location", "add", cli.path, str(x), str(y),
                 str(x + w), str(y + h), "--tiles", "--name", name)

    cli.place(scmap.START_LOCATION, 64, 64, owner=1)
    hunter_unit, hider_unit, token_unit = (cfg["units"][k] for k in ("hunter", "hider", "state_token"))
    cli.place(hunter_unit, 64, 64, owner=1)
    for p, (x, y, w, h) in enumerate(ROOMS[:a.hiders], start=2):
        px, py = x + w // 2, y + h // 2
        cli.place(scmap.START_LOCATION, px, py, owner=p)
        cli.place(hider_unit, px, py, owner=p)
        cli.place(cfg["units"]["room_marker"], x + w - 4, y + 3, owner=12)

    room_names = cfg["labels"]["rooms"][:a.hiders]
    hiders = [f"Player {p}" for p in range(2, humans + 1)]
    blocks = scmap.hyper_triggers("Player 8")
    messages = cfg["text"]["messages"]
    blocks.append(trig('"All players"', ["Always()"], [
        f'Set Mission Objectives("{cfg["text"]["objectives"]}")']))
    hunter_actions = [
        'Set Switch("Switch 1", clear)',
        *profile.resource_actions(cfg, ["Player 1"]),
        f'Set Countdown Timer(Set To, {a.prep})',
        f'Display Text Message(Always Display, "{messages["hunter_start"]}")',
        *[f'Set Alliance Status("{p}", Enemy)' for p in hiders]]
    blocks.append(trig('"Player 1"', ["Always()"], hunter_actions))
    for p in hiders:
        peers = [q for q in hiders if q != p]
        actions = [
            f'Set Deaths("Current Player", "{token_unit}", Set To, 0)',
            'Set Alliance Status("Player 1", Enemy)']
        actions.extend(f'Set Alliance Status("{q}", Allied Victory)'
                       for q in peers)
        actions.append(f'Display Text Message(Always Display, "{messages["hider_start"]}")')
        blocks.append(trig(f'"{p}"', ["Always()"], actions))

    blocks.append(trig('"Player 1"', [
        'Countdown Timer(At most, 0)', 'Switch("Switch 1", not set)'], [
        'Set Switch("Switch 1", set)',
        f'Set Countdown Timer(Set To, {a.survive})',
        f'Display Text Message(Always Display, "{messages["hunt_started"]}")']))

    # Room-level overlap is the capture rule. The human-owned death counter
    # consumes the interaction so simultaneous scans cannot capture twice.
    for p in hiders:
        for room in room_names:
            blocks.append(trig(f'"{p}"', [
                'Switch("Switch 1", set)',
                f'Bring("Player 1", "{hunter_unit}", "{room}", At least, 1)',
                f'Bring("Current Player", "{hider_unit}", "{room}", At least, 1)',
                f'Deaths("Current Player", "{token_unit}", Exactly, 0)'], [
                f'Remove Unit At Location("Current Player", "{hider_unit}", All, "{room}")',
                f'Set Deaths("Current Player", "{token_unit}", Set To, 1)',
                f'Display Text Message(Always Display, "{messages["caught"]}")',
                'Defeat()']))

    all_caught = [f'Deaths("{p}", "{token_unit}", At least, 1)' for p in hiders]
    blocks.append(trig('"Player 1"', ['Switch("Switch 1", set)', *all_caught], [
        f'Display Text Message(Always Display, "{messages["hunter_win"]}")',
        'Victory()']))
    for p in hiders:
        blocks.append(trig(f'"{p}"', [
            'Switch("Switch 1", set)', 'Countdown Timer(At most, 0)',
            f'Deaths("Current Player", "{token_unit}", Exactly, 0)',
            f'Command("Current Player", "{hider_unit}", At least, 1)'], [
            f'Display Text Message(Always Display, "{messages["hider_win"]}")',
            'Victory()']))
    blocks.append(trig('"Player 1"', [
        'Switch("Switch 1", set)',
        f'Command("Player 1", "{hunter_unit}", At most, 0)'], [
        f'Display Text Message(Always Display, "{messages["hunter_eliminated"]}")',
        'Defeat()']))
    cli.apply_triggers(scmap.TRIGGER_SEP.join(blocks))
    profile.apply_profile_metadata(cli, cfg, humans)
    print(f"\nCreated {a.out}: {humans} humans, 4 rooms, "
          f"{len(blocks)} triggers; walk graph connected")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
