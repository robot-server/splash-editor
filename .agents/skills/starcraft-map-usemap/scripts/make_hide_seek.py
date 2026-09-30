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
W = H = 96


def lay_rooms(width: int, height: int):
    """작은 방을 격자로 깐다. 숨는 사람 수만큼의 큰 방 몇 개로 끝내지 않는다."""
    rw, rh, gap, margin = 11, 9, 3, 4
    cols = max(3, (width - 2 * margin + gap) // (rw + gap))
    rows = max(3, (height - 2 * margin + gap) // (rh + gap))
    rooms = [(margin + c * (rw + gap), margin + r * (rh + gap), rw, rh)
             for r in range(rows) for c in range(cols)]
    cx, cy = width / 2, height / 2
    hall = min(rooms, key=lambda room: abs(room[0] + room[2] / 2 - cx)
               + abs(room[1] + room[3] / 2 - cy))
    rooms.remove(hall)
    return rooms, hall


def trig(owner: str, conditions: list[str], actions: list[str]) -> str:
    return (f"Trigger({owner}){{\nConditions:\n" +
            "".join(f"\t{c};\n" for c in conditions) + "\nActions:\n" +
            "".join(f"\t{a};\n" for a in actions) + "}")


def connect_rooms(cli, palette, rooms, hall):
    """맞닿은 방 사이의 틈만 통로로 잇는다. 맵 끝 공터는 만들지 않는다."""
    cells = list(rooms) + [hall]
    rows = {}
    cols = {}
    for room in cells:
        rows.setdefault(room[1], []).append(room)
        cols.setdefault(room[0], []).append(room)
    for row in rows.values():
        row.sort()
        for left, right in zip(row, row[1:]):
            x0 = left[0] + left[2]
            palette.fill(cli, "path", x0, left[1] + left[3] // 2 - 1,
                         right[0] - x0, 3)
    for col in cols.values():
        col.sort(key=lambda room: room[1])
        for upper, lower in zip(col, col[1:]):
            y0 = upper[1] + upper[3]
            palette.fill(cli, "path", upper[0] + upper[2] // 2 - 1, y0,
                         3, lower[1] - y0)


def build_triggers(cfg, humans, prep, survive, hunter_unit, hider_unit, token_unit):
    room_names = cfg["labels"]["rooms"]
    hiders = [f"Player {p}" for p in range(2, humans + 1)]
    blocks = scmap.hyper_triggers("Player 8")
    messages = cfg["text"]["messages"]
    blocks.append(trig('"All players"', ["Always()"], [
        f'Set Mission Objectives("{cfg["text"]["objectives"]}")']))
    hunter_actions = [
        'Set Switch("Switch 1", clear)',
        *profile.resource_actions(cfg, ["Player 1"]),
        f'Set Countdown Timer(Set To, {prep})',
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
        f'Set Countdown Timer(Set To, {survive})',
        f'Display Text Message(Always Display, "{messages["hunt_started"]}")']))

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
    return blocks


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
        if getattr(a, key) is None:
            setattr(a, key, cfg["rules"][key])
    a.tileset, a.seed = cfg["map"]["tileset"], cfg["map"]["seed"]
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
    rooms, hall = lay_rooms(W, H)
    given = list(cfg["labels"]["rooms"])
    room_names = [given[i] if i < len(given) else f"숨는 칸 {i + 1}"
                  for i in range(len(rooms))]
    cfg["labels"]["rooms"] = room_names
    scmap.cover_map(cli, pal, W, H, margin=2)
    for x, y, w, h in rooms:
        scmap.room(cli, pal, x, y, w, h, rim=1, wall=True)
    scmap.room(cli, pal, *hall, rim=1, wall=True)
    connect_rooms(cli, pal, rooms, hall)
    # 예전에는 여기서 못 걷는 비율이 60%를 넘으면 길 옆을 무작위로 더
    # 뚫었다 — 66% 초과가 튕긴다는 근거로 세운 안전장치였는데, 그 근거가
    # 반증됐다(85x48 Space Platform/Brood War 205, 못 걷는 땅 80%가
    # 실제 게임에서 정상 진행됨, tools/mapgen/verify_map.py 0a) 참고).
    # 의도한 방 모양을 사후에 무작위로 뭉개던 걸 없앴다.

    grid = scmap.walk_grid(cli, ts, 0, 0, W, H)
    hx, hy = hall[0] + hall[2] // 2, hall[1] + hall[3] // 2
    points = [(hx, hy)] + [(x + w // 2, y + h // 2) for x, y, w, h in rooms]
    walk = [scmap.nearest_walkable(grid, x * 4 + 2, y * 4 + 2, 24)
            for x, y in points]
    if any(p is None for p in walk) or any(
            not scmap.walk_reachable(grid, walk[0], p) for p in walk[1:]):
        raise CliError("술래 홀과 숨는 방 사이의 보행 경로가 연결되지 않았습니다")

    scmap.setup_usemap_players(cli, humans, [8], race=profile.human_race(cfg),
                               computer_race=profile.human_race(cfg))
    location_specs = [(cfg["labels"]["hall"], *hall)] + [
        (room_names[i], *r) for i, r in enumerate(rooms)]
    for name, x, y, w, h in location_specs:
        cli.edit("location", "add", cli.path, str(x), str(y),
                 str(x + w), str(y + h), "--tiles", "--name", name)

    cli.place(scmap.START_LOCATION, hx, hy, owner=1)
    hunter_unit, hider_unit, token_unit = (cfg["units"][k] for k in ("hunter", "hider", "state_token"))
    cli.place(hunter_unit, hx, hy, owner=1)
    for p, (x, y, w, h) in enumerate(rooms[:a.hiders], start=2):
        px, py = x + w // 2, y + h // 2
        cli.place(scmap.START_LOCATION, px, py, owner=p)
        cli.place(hider_unit, px, py, owner=p)

    blocks = build_triggers(cfg, humans, a.prep, a.survive, hunter_unit, hider_unit, token_unit)
    zones = [(1, hall)] + [
        (p, room) for p, room in enumerate(rooms[:a.hiders], start=2)]
    scmap.apply_reveal(cli, "hide_seek", humans, zones=zones)
    cli.apply_triggers(scmap.TRIGGER_SEP.join(blocks))
    profile.apply_profile_metadata(cli, cfg, humans)
    print(f"\nCreated {a.out}: {humans} humans, {len(rooms)} rooms, "
          f"{len(blocks)} triggers; walk graph connected")
    scmap.assert_create_targets(cli)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
