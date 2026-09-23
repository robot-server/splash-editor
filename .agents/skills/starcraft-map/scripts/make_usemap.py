#!/usr/bin/env python3
"""유즈맵 뼈대를 만든다 — 스타팅, 플레이어 슬롯, 지형, 로케이션, 트리거 고리.

유즈맵에서 다투는 것은 재미고, 재미는 **되먹임 고리**에서 나온다. 다만
고리를 짜기 전에 **맵이 열리기는 해야 한다.** 아래 셋을 빠뜨리면 게임이
아예 시작되지 않는다 — 실제로 빠뜨려 봤다.

1. **사람 플레이어마다 스타팅 포인트(유닛 214)가 있어야 한다.**
2. **플레이어 슬롯 수와 스타팅 수가 같아야 한다.** 남는 슬롯은 "사용 안 함".
3. **트리거가 적으로 쓰는 플레이어는 "컴퓨터" 여야 한다.** "열림" 으로
   두면 아무도 안 앉았을 때 그 유닛이 생기지 않는다.

보기:
    python3 make_usemap.py out.scx --genre defense --players 6 --name "디펜스"
"""
from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError

TILESETS = {"badlands": 0, "space": 1, "install": 2, "ashworld": 3,
            "jungle": 4, "desert": 5, "ice": 6, "twilight": 7}
BASE_TERRAIN = {0: "Dirt", 1: "Platform", 2: "Substructure", 3: "Magma",
                4: "Dirt", 5: "Tar", 6: "Ice", 7: "Dirt"}
HIGH_TERRAIN = {0: "High Dirt", 1: "High Platform", 2: "Substructure",
                3: "High Dirt", 4: "High Dirt", 5: "High Dirt",
                6: "High Snow", 7: "High Dirt"}

# 카운터로 쓸 자리. 플레이어 1~8 은 실제로 쓰이므로 건드리지 않는다.
COUNTER_PLAYER = "Player 11"
COUNTER_UNIT = "Spider Mine"       # trigger show 는 "Vulture Spider Mine" 로 낸다


def parse_size(text: str):
    parts = text.lower().replace(" ", "").split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("크기는 96x96 꼴로 줍니다")
    return int(parts[0]), int(parts[1])


def header(title: str) -> str:
    return f"\n//--------------- {title} ---------------//\n\n"


def common_triggers(name: str, objectives: str) -> str:
    return f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tSet Mission Objectives("{objectives}");
\tDisplay Text Message(Always Display, "\\x0F{name}\\r\\n\\x16게임 방법은 임무목표(F10 → J)를 보세요");
\tLeader Board Points("\\x1F점수", Kills);
\tLeaderboard Computer Players(disabled);
}}
'''


def defense_triggers(enemy: str, boss: str) -> str:
    """디펜스 — 웨이브가 오고, 막고, 보상받고, 다음 웨이브."""
    waves = [("Zerg Zergling", 8, "저글링 8"),
             ("Zerg Hydralisk", 8, "히드라리스크 8"),
             ("Zerg Lurker", 4, "럴커 4"),
             ("Zerg Ultralisk", 3, "울트라리스크 3"),
             ("Torrasque (Ultralisk)", 2, "\\x08보스 — 토라스크 2")]
    text = ""
    for i, (unit, count, label) in enumerate(waves, start=1):
        text += header(f"웨이브 {i}")
        reward = ""
        if i > 1:
            prev = 100 * (i - 1)
            reward = (f'\tSet Resources("All players", Add, {prev}, ore);\n'
                      f'\tDisplay Text Message(Always Display, '
                      f'"\\x07웨이브 {i-1} 막음! \\x04+{prev} 미네랄");\n')
        text += f'''Trigger("Player 1"){{
Conditions:
\tDeaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", Exactly, {i - 1});
\tCommand("{enemy}", "Men", Exactly, 0);
\tCountdown Timer(Exactly, 0);

Actions:
{reward}\tDisplay Text Message(Always Display, "\\x08웨이브 {i} \\x0F— {label}");
\tPlay WAV("sound\\\\Zerg\\\\Zergling\\\\ZLiRdy00.wav", 500);
\tCreate Unit("{enemy}", "{unit}", {count}, "Spawn");
\tOrder("{enemy}", "Men", "Spawn", "Goal", attack);
\tMinimap Ping("Spawn");
\tSet Deaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", Add, 1);
\tSet Countdown Timer(Set To, 45);
\tPreserve Trigger();
}}
'''
    text += header("끝맺음")
    text += f'''Trigger("All players"){{
Conditions:
\tDeaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", At least, {len(waves)});
\tCommand("{enemy}", "Men", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "\\x07모든 웨이브를 막았습니다!");
\tVictory();
}}

Trigger("All players"){{
Conditions:
\tBring("{enemy}", "Men", "Goal", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x08목표를 뚫렸습니다");
\tDefeat();
}}
'''
    return text


def rpg_triggers(enemy: str, boss: str) -> str:
    """키우기 — 잡고, 경험치 쌓고, 승급한다."""
    tiers = [("Zerg Zergling", "Zerg Hydralisk", 10, "히드라리스크"),
             ("Zerg Hydralisk", "Zerg Lurker", 25, "럴커"),
             ("Zerg Lurker", "Zerg Ultralisk", 45, "울트라리스크")]
    # **`Kill(..., At least, 1)` 은 누적이다.** "지금까지 몇 기 죽였나" 라
    # 한 번 참이 되면 영원히 참이고, `Preserve` 와 함께 쓰면 매 주기
    # 경험치가 들어온다 (하이퍼 트리거를 쓰면 1초에 스물네 번). 앞판이
    # 그랬다. **킬 스코어를 깎아서** 잡은 만큼만 준다 — 킬 스코어는
    # 플레이어별로 쌓이므로 누가 잡았는지도 가려진다.
    #
    # 저글링 한 마리가 50점이다 (미네랄x2 + 가스x4, 영웅은 두 배).
    text = f'''Trigger("All players"){{
Conditions:
\tScore("Current Player", Kills, At least, 50);

Actions:
\tSet Score("Current Player", Subtract, 50, Kills);
\tSet Deaths("Current Player", "Terran Civilian", Add, 1);
\tPreserve Trigger();
}}
'''
    for old, new, need, label in tiers:
        text += header(f"승급 — {label}")
        text += f'''Trigger("All players"){{
Conditions:
\tDeaths("Current Player", "Terran Civilian", At least, {need});
\tCommand("Current Player", "{old}", At least, 1);

Actions:
\tSet Deaths("Current Player", "Terran Civilian", Subtract, {need});
\tDisplay Text Message(Always Display, "\\x07레벨 업! \\x0F{label}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tRemove Unit At Location("Current Player", "{old}", All, "Anywhere");
\tCreate Unit("Current Player", "{new}", 1, "Hero");
\tPreserve Trigger();
}}
'''
    text += header("사냥감 다시 채우기")
    text += f'''Trigger("Player 1"){{
Conditions:
\tCommand("{enemy}", "Men", At most, 2);

Actions:
\tCreate Unit("{enemy}", "Zerg Zergling", 6, "Spawn");
\tPreserve Trigger();
}}

//--------------- 보스 ---------------//

Trigger("Player 1"){{
Conditions:
\tCommand("{boss}", "Men", Exactly, 0);
\tElapsed Time(At least, 240);

Actions:
\tDisplay Text Message(Always Display, "\\x08보스가 나타났습니다!");
\tCreate Unit("{boss}", "Torrasque (Ultralisk)", 1, "Town");
\tMinimap Ping("Town");
\tPreserve Trigger();
}}

Trigger("All players"){{
Conditions:
\tKill("Current Player", "Torrasque (Ultralisk)", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07보스를 잡았습니다!");
\tVictory();
}}

Trigger("All players"){{
Conditions:
\tCommand("Current Player", "Men", Exactly, 0);
\tElapsed Time(At least, 30);

Actions:
\tDefeat();
}}
'''
    return text


def control_triggers(enemy: str, boss: str) -> str:
    """컨트롤 — 붙고, 이기면 점수, 다음 판."""
    return f'''Trigger("Player 1"){{
Conditions:
\tCommand("{enemy}", "Men", Exactly, 0);
\tElapsed Time(At least, 10);

Actions:
\tCreate Unit("{enemy}", "Terran Marine", 6, "Enemy Spawn");
\tOrder("{enemy}", "Men", "Enemy Spawn", "Arena", attack);
\tDisplay Text Message(Always Display, "\\x08다음 판 시작!");
\tPreserve Trigger();
}}

//--------------- 한 판 이김 ---------------//
//
// `Kill(..., At least, 6)` 은 누적이라 여섯 기를 잡은 뒤로는 계속 참이다.
// 그대로 두면 매 주기 +1점이 들어왔다. **킬 스코어를 깎아** 한 판에
// 한 번만 준다. 마린 여섯이면 600점이다 (마린 100점).

Trigger("All players"){{
Conditions:
\tScore("Current Player", Kills, At least, 600);

Actions:
\tSet Score("Current Player", Subtract, 600, Kills);
\tSet Deaths("Current Player", "Terran Civilian", Add, 1);
\tDisplay Text Message(Always Display, "\\x07이겼습니다! \\x04+1점");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}

//--------------- 졌을 때 다시 받기 ---------------//

Trigger("All players"){{
Conditions:
\tCommand("Current Player", "Men", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "\\x08졌습니다 — 다시 받습니다");
\tCreate Unit("Current Player", "Terran Marine", 6, "Arena");
\tPreserve Trigger();
}}
'''


def quiz_triggers(enemy: str, boss: str) -> str:
    """퀴즈 — 비콘을 밟아 답한다."""
    return '''Trigger("All players"){
Conditions:
\tBring("Current Player", "Men", "Answer O", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07O 를 골랐습니다");
\tSet Score("Current Player", Add, 1, Kills);
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tMove Unit("Current Player", "Men", All, "Answer O", "Lobby");
\tPreserve Trigger();
}

//--------------- X ---------------//

Trigger("All players"){
Conditions:
\tBring("Current Player", "Men", "Answer X", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x08X 를 골랐습니다");
\tMove Unit("Current Player", "Men", All, "Answer X", "Lobby");
\tPreserve Trigger();
}
'''


def escape_triggers(enemy: str, boss: str) -> str:
    """탈출 — 구역을 하나씩 통과한다."""
    return f'''Trigger("All players"){{
Conditions:
\tBring("Current Player", "Men", "Checkpoint", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07구간 통과!");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}

//--------------- 쫓아오는 것 ---------------//

Trigger("Player 1"){{
Conditions:
\tCommand("{enemy}", "Men", At most, 2);

Actions:
\tCreate Unit("{enemy}", "Zerg Zergling", 4, "Checkpoint");
\tOrder("{enemy}", "Men", "Checkpoint", "Goal", attack);
\tPreserve Trigger();
}}

//--------------- 끝맺음 ---------------//

Trigger("All players"){{
Conditions:
\tBring("Current Player", "Men", "Goal", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07탈출 성공!");
\tVictory();
}}

Trigger("All players"){{
Conditions:
\tCommand("Current Player", "Men", Exactly, 0);

Actions:
\tDefeat();
}}
'''


# 로케이션 자리는 맵 크기에 대한 비율로 둔다 (x0, y0, x1, y1).
GENRES = {
    "defense": {
        "triggers": defense_triggers,
        "locations": [("Spawn", .03, .45, .11, .55), ("Goal", .89, .45, .97, .55),
                      ("Build Zone", .38, .35, .62, .65)],
        "start_area": (.40, .40, .60, .60),
        "objectives": "\\x041. 길목에 유닛을 세워 웨이브를 막습니다\\r\\n\\x042. 웨이브마다 미네랄을 받습니다\\r\\n\\x043. 적이 목표에 닿으면 패배",
        "start_units": [("Terran SCV", 2)],
        "enemy_units": [("Zerg Zergling", 4), ("Zerg Hatchery", 1)],
        "description": "길목에 유닛을 세워 몰려오는 웨이브를 막습니다. 웨이브를 막을 때마다 미네랄을 받아 수비 유닛을 늘리세요. 적이 목표 지점에 닿으면 목숨이 줄어듭니다.",
        "size": (96, 96),
        "walls": [(.40, .12, .60, .38), (.40, .62, .60, .88)],
    },
    "rpg": {
        "triggers": rpg_triggers,
        "locations": [("Hero", .04, .04, .16, .16), ("Spawn", .42, .42, .58, .58),
                      ("Town", .80, .80, .94, .94)],
        "start_area": (.05, .05, .15, .15),
        "objectives": "\\x041. 사냥터의 적을 잡아 경험치를 모읍니다\\r\\n\\x042. 경험치가 차면 승급합니다\\r\\n\\x043. 보스를 잡으면 승리",
        "start_units": [("Zerg Zergling", 1)],
        "enemy_units": [("Zerg Zergling", 6), ("Terran Marine", 4)],
        "description": "저글링으로 시작해 사냥터의 적을 잡습니다. 경험치가 차면 히드라·럴커·울트라로 승급하고, 마지막에 보스를 잡으면 이깁니다.",
        "size": (128, 128),
        "walls": [(.24, .24, .36, .76), (.64, .24, .76, .76)],
    },
    "control": {
        "triggers": control_triggers,
        "locations": [("Arena", .30, .55, .70, .90), ("Enemy Spawn", .30, .06, .70, .30),
                      ("Lobby", .04, .04, .14, .14)],
        "start_area": (.35, .62, .65, .85),
        "objectives": "\\x041. 주어진 마린으로 싸워 이깁니다\\r\\n\\x042. 이기면 점수를 얻습니다",
        "start_units": [("Terran Marine", 6)],
        "enemy_units": [("Terran Marine", 6)],
        "description": "주어진 마린으로 상대 부대와 싸웁니다. 이기면 점수를 얻고 다음 판이 시작됩니다.",
        "size": (64, 64),
        "walls": [(.06, .38, .40, .48), (.60, .38, .94, .48)],
    },
    "quiz": {
        "triggers": quiz_triggers,
        "locations": [("Lobby", .35, .60, .65, .90), ("Answer O", .08, .10, .38, .40),
                      ("Answer X", .62, .10, .92, .40)],
        "start_area": (.38, .65, .62, .85),
        "objectives": "\\x041. 문제가 나오면 O 또는 X 로 걸어갑니다\\r\\n\\x042. 맞히면 1점",
        "start_units": [("Terran Civilian", 1)],
        "enemy_units": [],
        "description": "문제가 나오면 O 또는 X 비콘으로 걸어가 답합니다. 제한 시간 안에 답해야 하며, 가장 많이 맞힌 사람이 이깁니다.",
        "size": (64, 64),
        "walls": [(.44, .10, .56, .42)],
    },
    "escape": {
        "triggers": escape_triggers,
        "locations": [("Start", .03, .45, .13, .55), ("Checkpoint", .45, .45, .55, .55),
                      ("Goal", .88, .45, .97, .55)],
        "start_area": (.04, .46, .12, .54),
        "objectives": "\\x041. 쫓아오는 것을 피해 나아갑니다\\r\\n\\x042. 목표 지점에 닿으면 승리",
        "start_units": [("Terran Civilian", 1)],
        "enemy_units": [("Zerg Zergling", 4)],
        "description": "쫓아오는 것을 피해 구간을 하나씩 통과합니다. 목표 지점에 닿으면 탈출 성공입니다.",
        "size": (128, 128),
        "walls": [(.20, .10, .30, .42), (.20, .58, .30, .90),
                  (.65, .10, .75, .42), (.65, .58, .75, .90)],
    },
}


def main(argv=None):
    ap = argparse.ArgumentParser(description="유즈맵 뼈대를 만든다")
    ap.add_argument("out")
    ap.add_argument("--genre", default="defense", choices=sorted(GENRES))
    ap.add_argument("--players", type=int, default=6,
                    help="사람 플레이어 수 (1~6). 적·보스로 슬롯 둘을 더 쓴다")
    ap.add_argument("--size", type=parse_size, default=None)
    ap.add_argument("--tileset", default="jungle", choices=sorted(TILESETS))
    ap.add_argument("--name", default=None)
    ap.add_argument("--no-walls", action="store_true", help="지형 벽을 세우지 않는다")
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true",
                    help="이미 있는 맵을 덮어쓴다. 기본은 거절한다 — 뼈대를 "
                         "다시 만들면 그 위에 쓴 트리거·유닛이 모두 사라진다")
    args = ap.parse_args(argv)

    # 뼈대 생성은 맵을 처음부터 다시 만든다. 작성해 둔 내용이 있으면
    # 통째로 날아간다. 실제로 그렇게 트리거를 날린 적이 있다.
    if os.path.exists(args.out) and not args.force:
        print(f"이미 있습니다: {args.out}\n"
              f"  뼈대를 다시 만들면 그 위에 쓴 내용이 모두 사라집니다.\n"
              f"  내용을 고치려면 trigger show/apply 로 트리거만 손보세요.\n"
              f"  정말 처음부터 다시 만들려면 --force 를 주세요.", file=sys.stderr)
        return 2

    genre = GENRES[args.genre]
    width, height = args.size or genre["size"]
    tileset_id = TILESETS[args.tileset]
    name = args.name or f"{args.genre} 맵"

    # 사람 1~N, 적 N+1, 보스 N+2. 슬롯 여덟 칸 안에 들어가야 한다.
    if not (1 <= args.players <= 6):
        ap.error("사람 플레이어는 1~6 명입니다 (적·보스로 두 칸을 더 씁니다).")
    enemy_no, boss_no = args.players + 1, args.players + 2
    enemy, boss = f"Player {enemy_no}", f"Player {boss_no}"

    print(f"{args.genre} 유즈맵 {width}x{height} {args.tileset}")
    print(f"  사람 P1~P{args.players}, 적 {enemy}, 보스 {boss} (둘 다 컴퓨터)")

    cli = scmap.new_map(args.out, width, height, tileset_id,
                        terrain=BASE_TERRAIN[tileset_id], melee=False,
                        install=args.install)

    # 1) 플레이어 슬롯 — 스타팅 수와 맞추고, 적은 컴퓨터로
    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, args.players, [enemy_no, boss_no])

    # 유즈맵은 업그레이드를 고친다 — 실측 중앙 7가지, 90%가 전부
    # (docs/chk/anatomy.md). 밀리맵 비용·시간을 그대로 두면
    # 유즈맵 흐름에 안 맞는다.
    n_up = scmap.setup_usemap_upgrades(
        cli, a.players if hasattr(a, "players") else args.players,
        free_levels=0, max_level=3,
        mineral=75, gas=0, time=15)
    n_tech = scmap.setup_usemap_tech(cli, which=(0, 3, 5, 6),
        mineral=100, gas=0, time=15, available="all")
    print(f"  업그레이드 {n_up}가지 · 기술 {n_tech}가지를 유즈맵 값으로 정했습니다")

    # 2) 지형 — 아무것도 없으면 맵이 아니라 벌판이다
    if not args.no_walls and genre.get("walls"):
        types = cli.terrain_types()
        high_name = HIGH_TERRAIN[tileset_id]
        high = types.get(high_name) or next(
            (v for k, v in types.items() if k.lower().startswith("high")), None)
        if high is None:
            print("  고지대 지형이 없어 지형 벽은 건너뜁니다.")
        else:
            print(f"지형 벽을 세웁니다 ({high_name})...")
            for (rx0, ry0, rx1, ry1) in genre["walls"]:
                x0, x1 = int(rx0 * width), int(rx1 * width)
                y0, y1 = int(ry0 * height), int(ry1 * height)
                for ty in range(max(2, y0), min(height - 3, y1) + 1):
                    for tx in range(max(2, x0), min(width - 3, x1) + 1, 2):
                        cli.isom(tx, ty, high)

    # 3) 로케이션 — 트리거가 이름으로 가리킨다. 먼저 만들어야 한다.
    print("로케이션을 만듭니다...")
    for (loc, rx0, ry0, rx1, ry1) in genre["locations"]:
        cli.edit("location", "add", cli.path,
                 str(int(rx0 * width)), str(int(ry0 * height)),
                 str(int(rx1 * width)), str(int(ry1 * height)),
                 "--tiles", "--name", loc)

    # 4) 스타팅 포인트와 시작 유닛 — **스타팅이 없으면 게임이 시작되지 않는다**
    print("스타팅과 시작 유닛을 놓습니다...")
    ax0, ay0, ax1, ay1 = genre["start_area"]
    sx0, sy0 = int(ax0 * width), int(ay0 * height)
    sx1, sy1 = int(ax1 * width), int(ay1 * height)
    cols = max(1, min(args.players, 3))
    for p in range(1, args.players + 1):
        i = p - 1
        px = sx0 + 2 + (i % cols) * max(2, (sx1 - sx0 - 4) // max(1, cols - 1) if cols > 1 else 0)
        py = sy0 + 2 + (i // cols) * 4
        px = max(2, min(width - 3, px))
        py = max(2, min(height - 3, py))
        cli.place(scmap.START_LOCATION, px, py, owner=p)
        for unit, count in genre["start_units"]:
            for k in range(count):
                cli.place(unit, px + (k % 3), py + 1 + (k // 3), owner=p)

    # 4b) 컴퓨터 플레이어에게도 스타팅과 유닛을 준다.
    #
    #     인기 디펜스 맵 다섯 장을 재어 보니 **슬롯 여덟 곳 모두에 스타팅**이
    #     있고, 유닛을 가장 많이 가진 쪽이 컴퓨터였다 (147~212개). 컴퓨터가
    #     맵에 아무것도 없으면 트리거가 소환하기 전까지 존재하지 않는다.
    for k, pno in enumerate([enemy_no, boss_no]):
        ex = int((0.15 + 0.7 * k) * width)
        ey = int(0.12 * height)
        ex = max(3, min(width - 4, ex))
        cli.place(scmap.START_LOCATION, ex, ey, owner=pno)
        for unit, count in genre.get("enemy_units", []):
            for j in range(count):
                cli.place(unit, ex + (j % 4), ey + 2 + (j // 4), owner=pno)

    # 5) 시야 — 93개 중 59개 맵이 Map Revealer 를 쓴다
    print("시야를 엽니다...")
    cli.edit("scenario", "revealers", cli.path, "--owner", "1", "--spacing", "16")

    # 6) 트리거
    print("트리거를 넣습니다...")
    text = common_triggers(name, genre["objectives"])
    text += f'''
Trigger("Player 1"){{
Conditions:
\tAlways();

Actions:
\tSet Deaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", Set To, 0);
\tSet Countdown Timer(Set To, 15);
\tSet Resources("All players", Set To, 300, ore);
}}
'''
    text += header(f"{args.genre} 고리")
    text += genre["triggers"](enemy, boss)
    cli.apply_triggers(text)

    # **유닛 능력치를 유즈맵 값으로 정한다.**
    #
    # 실측 유즈맵 479장 중 473장(98%)이 유닛 설정을 고치고 중앙 90종을
    # 건드린다. 마린 체력 중앙값이 250 이다 — 원래 40 이니 여섯 배다.
    # 그대로 두면 유즈맵이 아니라 "스타 유닛으로 노는 맵" 이다
    # (docs/unit/settings.md).
    #
    # 트리거를 넣은 **뒤에** 부른다 — 맵에 실제로 나오는 유닛만 고치려면
    # 트리거가 무엇을 만드는지 알아야 한다.
    try:
        _used = scmap.units_in_play(cli, cli.trigger_text())
    except Exception:
        _used = None
    scmap.setup_usemap_units(cli, sorted(_used) if _used else None)


    cli.set_map_name(name, genre.get("description"))

    info = cli.info()
    print(f"\n만들었습니다: {args.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']}  {info['version']}")
    print(f"  유닛 {info['units']}  로케이션 {info['locations']}  트리거 {info['triggers']}")
    print(f"\n  python3 verify_map.py {args.out}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
