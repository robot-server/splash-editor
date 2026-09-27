#!/usr/bin/env python3
"""추격 체크포인트 유즈맵 골격.

공간은 시작-체크포인트-목표의 한 줄 경로이고, 컴퓨터가 체크포인트에서
추격 유닛을 낸다. 문구·유닛·인원·타이머·자원은 호출자가 넘긴 JSON만 쓴다.
디펜스·RPG·컨트롤·퀴즈는 각 make_*.py 가 담당한다.

    python3 make_usemap.py out.scx --config profile.json
"""
from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import CliError
import recipe_config as profile

TILESETS = {"badlands": 0, "space": 1, "install": 2, "ashworld": 3,
            "jungle": 4, "desert": 5, "ice": 6, "twilight": 7}
BASE_TERRAIN = {0: "Dirt", 1: "Platform", 2: "Substructure", 3: "Magma",
                4: "Dirt", 5: "Tar", 6: "Ice", 7: "Dirt"}
HIGH_TERRAIN = {0: "High Dirt", 1: "High Platform", 2: "Substructure",
                3: "High Dirt", 4: "High Dirt", 5: "High Dirt",
                6: "High Snow", 7: "High Dirt"}

# 경로 비율 (x0, y0, x1, y1). 표시 이름은 프로필 labels.
LOCATIONS = (
    ("start", .03, .45, .13, .55),
    ("checkpoint", .45, .45, .55, .55),
    ("goal", .88, .45, .97, .55),
)
START_AREA = (.04, .46, .12, .54)
WALLS = [(.20, .10, .30, .42), (.20, .58, .30, .90),
         (.65, .10, .75, .42), (.65, .58, .75, .90)]


def chase_triggers(cfg, enemy: str) -> str:
    labels = cfg["labels"]
    units = cfg["units"]
    msg = cfg["text"]["messages"]
    timer = cfg["rules"]["timer_seconds"]
    # 살아 있는 추격 수가 프로필 상한보다 적을 때만 한 기씩 채운다.
    # 한 번에 count 를 만들면 상한 이하인 동안 조건이 참이라 무리가 쌓인다.
    count = cfg["rules"]["pursuer_count"]
    below = count - 1
    runners = ",".join(f'"Player {p}"' for p in range(1, cfg["rules"]["players"] + 1))
    start, checkpoint, goal = labels["start"], labels["checkpoint"], labels["goal"]
    return f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tSet Mission Objectives("{cfg["text"]["objectives"]}");
\tDisplay Text Message(Always Display, "{msg["intro"]}");
\tSet Countdown Timer(Set To, {timer});
{chr(10).join(chr(9) + line + ";" for line in profile.resource_actions(cfg, ["All players"]))}
\tLeader Board Points("{msg["intro"]}", Kills);
\tLeaderboard Computer Players(disabled);
}}

Trigger("All players"){{
Conditions:
\tBring("Current Player", "{units["runner"]}", "{checkpoint}", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "{msg["checkpoint"]}");
\tPreserve Trigger();
}}

Trigger("{enemy}"){{
Conditions:
\tCommand("{enemy}", "{units["pursuer"]}", At most, {below});

Actions:
\tCreate Unit("{enemy}", "{units["pursuer"]}", 1, "{checkpoint}");
\tOrder("{enemy}", "{units["pursuer"]}", "{checkpoint}", "{goal}", attack);
\tDisplay Text Message(Always Display, "{msg["pursuit"]}");
\tPreserve Trigger();
}}

Trigger("All players"){{
Conditions:
\tBring("Current Player", "{units["runner"]}", "{goal}", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "{msg["victory"]}");
\tVictory();
}}

Trigger({runners}){{
Conditions:
\tCommand("Current Player", "{units["runner"]}", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "{msg["defeat"]}");
\tDefeat();
}}
'''


def main(argv=None):
    ap = argparse.ArgumentParser(description="추격 체크포인트 유즈맵 골격")
    ap.add_argument("out")
    profile.add_profile_arguments(ap, "chase")
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--no-walls", action="store_true")
    args = ap.parse_args(argv)
    if os.path.exists(args.out) and not args.force:
        print(f"이미 있습니다: {args.out}", file=sys.stderr)
        return 2
    cfg = profile.load_profile(args.config, "chase")
    width, height = cfg["map"]["size"]
    tileset_id = TILESETS[cfg["map"]["tileset"]]
    players = cfg["rules"]["players"]
    enemy_no = players + 1
    enemy = f"Player {enemy_no}"

    cli = scmap.new_map(args.out, width, height, tileset_id,
                        terrain=BASE_TERRAIN[tileset_id], melee=False,
                        install=args.install)
    scmap.setup_usemap_players(cli, players, [enemy_no], race=cfg["players"]["race"],
                               computer_race=cfg["players"]["enemy_race"])
    cli.edit("player", "set", cli.path, str(enemy_no),
             "--race", cfg["players"]["enemy_race"], "--slot", "computer")

    if not args.no_walls:
        types = cli.terrain_types()
        high_name = HIGH_TERRAIN[tileset_id]
        high = types.get(high_name) or next(
            (v for k, v in types.items() if k.lower().startswith("high")), None)
        if high is not None:
            for (rx0, ry0, rx1, ry1) in WALLS:
                x0, x1 = int(rx0 * width), int(rx1 * width)
                y0, y1 = int(ry0 * height), int(ry1 * height)
                for ty in range(max(2, y0), min(height - 3, y1) + 1):
                    for tx in range(max(2, x0), min(width - 3, x1) + 1, 2):
                        cli.isom(tx, ty, high)

    for key, rx0, ry0, rx1, ry1 in LOCATIONS:
        cli.edit("location", "add", cli.path,
                 str(int(rx0 * width)), str(int(ry0 * height)),
                 str(int(rx1 * width)), str(int(ry1 * height)),
                 "--tiles", "--name", cfg["labels"][key])

    ax0, ay0, ax1, ay1 = START_AREA
    sx0, sy0 = int(ax0 * width), int(ay0 * height)
    sx1, sy1 = int(ax1 * width), int(ay1 * height)
    cols = max(1, min(players, 3))
    for p in range(1, players + 1):
        i = p - 1
        px = sx0 + 2 + (i % cols) * max(2, (sx1 - sx0 - 4) // max(1, cols - 1) if cols > 1 else 0)
        py = sy0 + 2 + (i // cols) * 4
        px = max(2, min(width - 3, px))
        py = max(2, min(height - 3, py))
        cli.place(scmap.START_LOCATION, px, py, owner=p)
        cli.place(cfg["units"]["runner"], px, py + 1, owner=p)

    ex = max(3, min(width - 4, int(0.50 * width)))
    ey = max(3, min(height - 4, int(0.50 * height)))
    cli.place(scmap.START_LOCATION, ex, ey, owner=enemy_no)

    cli.apply_triggers(chase_triggers(cfg, enemy))
    profile.apply_profile_metadata(cli, cfg, players)
    print(f"만들었습니다: {args.out}")
    scmap.assert_create_targets(cli)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
