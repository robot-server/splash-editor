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
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import CliError
import recipe_config as profile

TILESETS = {"badlands": 0, "space": 1, "install": 2, "ashworld": 3,
            "jungle": 4, "desert": 5, "ice": 6, "twilight": 7}
BASE_TERRAIN = {0: "Dirt", 1: "Platform", 2: "Substructure", 3: "Magma",
                4: "Dirt", 5: "Tar", 6: "Ice", 7: "Dirt"}

# 경로 비율 (x0, y0, x1, y1). 표시 이름은 프로필 labels.
LOCATIONS = (
    ("start", .03, .45, .13, .55),
    ("checkpoint", .45, .45, .55, .55),
    ("goal", .88, .45, .97, .55),
)
START_AREA = (.04, .46, .12, .54)
WALLS = [(.20, .10, .30, .42), (.20, .58, .30, .90),
         (.65, .10, .75, .42), (.65, .58, .75, .90)]


def chase_lane_rect(width: int, height: int, target_walk: float = 0.46):
    """추격에 쓰는 가로 띠 (x, y, w, h).

    검문 틈(세로 42~58%)은 띠 안에 두고, 모서리는 띠 밖에 남겨 벽으로
    남긴다. 띠가 너무 좁으면 못 걷는 칸이 66%를 넘어 게임이 튕긴다.
    """
    gap0 = int(0.42 * height)
    gap1 = max(gap0 + 1, int(0.58 * height))
    need = int(target_walk * width * height)
    usable_w = max(1, width - 4)
    chokes = [(int(rx0 * width), int(ry0 * height), int(rx1 * width), int(ry1 * height))
              for rx0, ry0, rx1, ry1 in WALLS]
    y0, y1 = gap0, gap1
    for extra in range(height):
        y0 = max(2, gap0 - extra)
        y1 = min(height - 2, gap1 + extra)
        removed = 0
        for x0, wy0, x1, wy1 in chokes:
            iy0, iy1 = max(y0, wy0), min(y1, wy1)
            ix0, ix1 = max(2, x0), min(width - 2, x1)
            if iy1 > iy0 and ix1 > ix0:
                removed += (ix1 - ix0) * (iy1 - iy0)
        if usable_w * (y1 - y0) - removed >= need or (y0 <= 2 and y1 >= height - 2):
            break
    return 2, y0, usable_w, max(1, y1 - y0)


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
\tSet Switch("추격시작", set);
\tDisplay Text Message(Always Display, "{msg["checkpoint"]}");
\tPreserve Trigger();
}}

Trigger("{enemy}"){{
Conditions:
\tSwitch("추격시작", set);
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
    cli.edit("switch", "name", cli.path, "1", "추격시작")

    # 맵 전체를 걷는 흙으로 두면 검문소 밖 공터도 돌아다닌다.
    pal = scmap.Palette(cli, tileset_id, random.Random(cfg["map"]["seed"]), "usemap")
    scmap.cover_map(cli, pal, width, height, margin=2)
    lx, ly, lw, lh = chase_lane_rect(width, height)
    pal.fill(cli, "floor", lx, ly, lw, lh)
    if not args.no_walls:
        for rx0, ry0, rx1, ry1 in WALLS:
            x0, y0 = int(rx0 * width), int(ry0 * height)
            pal.fill(cli, "wall", x0, y0,
                     max(1, int(rx1 * width) - x0),
                     max(1, int(ry1 * height) - y0))
    grid = scmap.walk_grid(cli, tileset_id, 0, 0, width, height)

    def _near(rx, ry):
        return scmap.nearest_walkable(
            grid, int(rx * width) * 4 + 2, int(ry * height) * 4 + 2, 32)

    anchors = [_near(0.08, 0.50), _near(0.50, 0.50), _near(0.92, 0.50)]
    if any(p is None for p in anchors) or any(
            not scmap.walk_reachable(grid, anchors[0], p) for p in anchors[1:]):
        raise CliError("추격 길이 시작·검문소·도착을 잇지 않습니다")

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
