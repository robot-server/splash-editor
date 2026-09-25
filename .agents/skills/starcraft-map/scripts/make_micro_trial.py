#!/usr/bin/env python3
"""마이크로 실기 시험: 한 벌처로 단계별 적 편대를 처리한다.

코퍼스 벌처 컨트롤 표본은 플레이어의 벌처 생성, 시간 지정 적 웨이브,
적 유닛 Deaths 경계로 단계 완료를 판정하고 보상 뒤 다음 편대를 낸다.
이 레시피는 그 단계식 처치 고리를 4개 훈련실의 단일 플레이어 코스로
재구성한다. 기존 PvP 컨트롤 경기장과 달리 승리 조건은 정해진 실기 과제다.

    python3 make_micro_trial.py out.scx --stages 4 --time-limit 420
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
BAYS = [
    ("Bay 1", 5, 45, 27, 38),
    ("Bay 2", 35, 45, 27, 38),
    ("Bay 3", 65, 45, 27, 38),
    ("Bay 4", 95, 45, 27, 38),
]


def trig(owner: str, conditions: list[str], actions: list[str]) -> str:
    return (f'Trigger({owner}){{\nConditions:\n' +
            ''.join(f'\t{c};\n' for c in conditions) + '\nActions:\n' +
            ''.join(f'\t{a};\n' for a in actions) + '}')


def main(argv=None):
    ap = argparse.ArgumentParser(description="AI 프로필 기반 단계형 마이크로 유즈맵")
    ap.add_argument("out")
    profile.add_profile_arguments(ap, "micro_trial")
    ap.add_argument("--stages", type=int, default=None)
    ap.add_argument("--time-limit", type=int, default=None)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    cfg = profile.load_profile(a.config, "micro_trial")
    a.stages = a.stages if a.stages is not None else cfg["rules"]["stages"]
    a.time_limit = a.time_limit if a.time_limit is not None else cfg["rules"]["time_limit"]
    a.tileset, a.seed = cfg["map"]["tileset"], cfg["map"]["seed"]
    if tuple(cfg["map"]["size"]) != (W, H):
        raise CliError("현재 마이크로 시험실 배치는 128x128 프로필만 받습니다")
    if os.path.exists(a.out) and not a.force:
        ap.error(f"이미 있습니다: {a.out} (--force 로 덮어쓰기)")
    if not 2 <= a.stages <= 4:
        ap.error("stages 범위는 2~4입니다")
    if a.time_limit < 60:
        ap.error("제한 시간은 60초 이상이어야 합니다")

    ts = TILESETS[a.tileset]
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    pal = scmap.Palette(cli, ts, random.Random(a.seed), "usemap")
    scmap.cover_map(cli, pal, W, H, margin=2)
    for i, (_, x, y, w, h) in enumerate(BAYS[:a.stages]):
        scmap.room(cli, pal, x, y, w, h, rim=1, wall=True)
    # Each bay has a broad central doorway; the rooms are a connected course,
    # not isolated arenas. One tile of extra corridor keeps Vultures clear.
    for i in range(a.stages - 1):
        x = BAYS[i][1] + BAYS[i][3] - 1
        pal.fill(cli, "path", x, 59, 5, 10)
    starts = [(BAYS[0][1] + 5, 64)]
    targets = [(x + w // 2, y + h // 2) for _, x, y, w, h in BAYS[:a.stages]]
    grid = scmap.walk_grid(cli, ts, 0, 0, W, H)
    walk_points = [scmap.nearest_walkable(grid, x * 4 + 2, y * 4 + 2, 24)
                   for x, y in starts + targets]
    if any(p is None for p in walk_points) or any(
            not scmap.walk_reachable(grid, walk_points[0], p)
            for p in walk_points[1:]):
        raise CliError("훈련실이 입구와 연결되지 않았습니다")

    scmap.setup_usemap_players(cli, 1, [2, 8], race="terran")
    cli.edit("player", "set", cli.path, "2", "--race", cfg["players"]["enemy_race"], "--slot", "computer")
    cli.edit("player", "set", cli.path, "8", "--race", cfg["players"]["system_race"], "--slot", "computer")
    cli.edit("location", "add", cli.path, "5", "45", "32", "83",
             "--tiles", "--name", "Player Start")
    for i, (_, x, y, w, h) in enumerate(BAYS[:a.stages]):
        bay_name = cfg["labels"]["bays"][i]
        cli.edit("location", "add", cli.path, str(x + 2), str(y + 2),
                 str(x + w - 2), str(y + h - 2), "--tiles", "--name", bay_name)

    sx, sy = starts[0]
    cli.place(scmap.START_LOCATION, sx, sy, owner=1)
    player_unit = cfg["units"]["player"]
    state_token = cfg["units"]["state_token"]
    cli.place(player_unit, sx, sy, owner=1)
    # P2 has the enemy start marker and receives only the active wave.
    ex, ey = targets[0]
    cli.place(scmap.START_LOCATION, ex, ey, owner=2)

    blocks = scmap.hyper_triggers("Player 8")
    blocks.append(trig('"All players"', ["Always()"], [
        f'Set Mission Objectives("{cfg["text"]["objectives"]}")']))
    waves = cfg["waves"][:a.stages]
    if len(waves) < a.stages:
        raise CliError("프로필 waves 항목이 stages보다 적습니다")
    first_unit, first_count = waves[0]["unit"], waves[0]["count"]
    blocks.append(trig('"Player 1"', ["Always()"], [
        f'Set Deaths("Player 1", "{state_token}", Set To, 0)',
        *profile.resource_actions(cfg, ["Player 1"]),
        f'Set Countdown Timer(Set To, {a.time_limit})',
        f'Create Unit("Player 2", "{first_unit}", {first_count}, "{cfg["labels"]["bays"][0]}")',
        f'Order("Player 2", "{first_unit}", "{cfg["labels"]["bays"][0]}", "Player Start", attack)',
        f'Display Text Message(Always Display, "{cfg["text"]["messages"]["start"]}")']))

    for i in range(a.stages):
        wave = waves[i]
        enemy, count, message = wave["unit"], wave["count"], wave["clear_message"]
        conds = [f'Deaths("Player 1", "{state_token}", Exactly, {i})',
                 f'Command("Player 2", "{enemy}", At most, 0)']
        if i < a.stages - 1:
            next_enemy, next_count = waves[i + 1]["unit"], waves[i + 1]["count"]
            acts = [f'Set Deaths("Player 1", "{state_token}", Set To, {i + 1})',
                    f'Set Resources("Player 1", Add, {wave["reward"]}, ore)',
                    f'Display Text Message(Always Display, "{message}")',
                    f'Create Unit("Player 2", "{next_enemy}", {next_count}, "{cfg["labels"]["bays"][i+1]}")',
                    f'Order("Player 2", "{next_enemy}", "{cfg["labels"]["bays"][i+1]}", "Player Start", attack)']
        else:
            acts = [f'Set Deaths("Player 1", "{state_token}", Set To, {i + 1})',
                    f'Set Resources("Player 1", Add, {wave["reward"]}, ore)',
                    f'Display Text Message(Always Display, "{cfg["text"]["messages"]["complete"]}")',
                    'Victory()']
        blocks.append(trig('"Player 1"', conds, acts))
    blocks.append(trig('"Player 1"', [
        f'Countdown Timer(At most, 0)',
        f'Deaths("Player 1", "{state_token}", At most, {a.stages - 1})'], [
        f'Display Text Message(Always Display, "{cfg["text"]["messages"]["timeout"]}")',
        'Defeat()']))
    blocks.append(trig('"Player 1"', [
        f'Command("Player 1", "{player_unit}", At most, 0)',
        f'Deaths("Player 1", "{state_token}", At most, {a.stages - 1})'], [
        f'Display Text Message(Always Display, "{cfg["text"]["messages"]["unit_lost"]}")',
        'Defeat()']))
    cli.apply_triggers(scmap.TRIGGER_SEP.join(blocks))
    profile.apply_profile_metadata(cli, cfg, 1)
    print(f"\nCreated {a.out}: {a.stages} enemy waves, connected walk graph, "
          f"{len(blocks)} triggers")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
