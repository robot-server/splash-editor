#!/usr/bin/env python3
"""유즈맵 뼈대를 만든다 — 지형, 로케이션, 시작 유닛, 장르에 맞는 트리거 고리.

유즈맵에서 다투는 것은 재미다. 재미는 **되먹임 고리**에서 나온다 —
하고, 바로 알려 주고, 보상하고, 다시 하고 싶게. 그래서 이 스크립트는
지형보다 **고리를 먼저** 깔아 준다.

여기서 나오는 것은 돌아가는 뼈대다. 내용(웨이브 구성, 보상 계단, 연출)은
이 위에 얹는다. `references/usemap-dopamine.md` 와
`references/trigger-recipes.md` 를 함께 본다.

보기:
    python3 make_usemap.py out.scx --genre defense --players 6 --name "디펜스"
    python3 make_usemap.py out.scx --genre rpg --players 4 --size 96x96
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

# 카운터로 쓸 자리. 쓰지 않는 플레이어와 맵에 안 나오는 유닛을 고른다.
# 어느 칸을 무엇에 쓰는지 적어 두지 않으면 금방 엉킨다.
COUNTER_PLAYER = "Player 8"
COUNTER_UNIT = "Spider Mine"       # trigger show 는 "Vulture Spider Mine" 로 낸다

MAP_REVEALER = 101                  # 93개 중 59개 맵이 쓴다. 사실상 필수


def parse_size(text: str):
    parts = text.lower().replace(" ", "").split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("크기는 96x96 꼴로 줍니다")
    return int(parts[0]), int(parts[1])


def header(title: str) -> str:
    return f"\n//--------------- {title} ---------------//\n\n"


def common_triggers(name: str, objectives: str) -> str:
    """어느 장르에나 들어가는 것 — 안내, 순위표, 패배."""
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


def defense_triggers() -> str:
    """디펜스 — 웨이브가 오고, 막고, 보상받고, 다음 웨이브.

    공식처럼 굳은 고리다. 웨이브 번호를 Deaths 카운터에 담고, 번호마다
    트리거 하나가 붙는다. 여기서는 3웨이브까지만 깔아 둔다 — 나머지는
    같은 꼴로 늘린다.
    """
    text = f'''Trigger("Player 1"){{
Conditions:
\tElapsed Time(At least, 20);
\tDeaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", At most, 2);
\tCommand("Player 7", "Men", Exactly, 0);

Actions:
\tSet Deaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", Add, 1);
\tSet Countdown Timer(Set To, 30);
\tPreserve Trigger();
}}
'''
    waves = [("Zerg Zergling", 8, "\\x08웨이브 1 \\x0F— 저글링 8"),
             ("Zerg Hydralisk", 8, "\\x08웨이브 2 \\x0F— 히드라리스크 8"),
             ("Zerg Ultralisk", 3, "\\x08웨이브 3 \\x0F— 울트라리스크 3")]
    for i, (unit, count, msg) in enumerate(waves, start=1):
        text += header(f"웨이브 {i}")
        text += f'''Trigger("Player 1"){{
Conditions:
\tDeaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", Exactly, {i});

Actions:
\tDisplay Text Message(Always Display, "{msg}");
\tCreate Unit("Player 7", "{unit}", {count}, "Spawn");
\tOrder("Player 7", "Men", "Spawn", "Goal", attack);
\tMinimap Ping("Spawn");
\tPreserve Trigger();
}}
'''
        text += f'''Trigger("All players"){{
Conditions:
\tDeaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", Exactly, {i});
\tCommand("Player 7", "Men", Exactly, 0);
\tElapsed Time(At least, {20 + i * 30});

Actions:
\tSet Resources("Current Player", Add, {100 * i}, ore);
\tDisplay Text Message(Always Display, "\\x07웨이브 {i} 막음! \\x04+{100 * i} 미네랄");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}
'''
    text += header("끝맺음")
    text += f'''Trigger("All players"){{
Conditions:
\tDeaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", At least, {len(waves)});
\tCommand("Player 7", "Men", Exactly, 0);
\tElapsed Time(At least, {20 + (len(waves) + 1) * 30});

Actions:
\tDisplay Text Message(Always Display, "\\x07모든 웨이브를 막았습니다!");
\tVictory();
}}

Trigger("All players"){{
Conditions:
\tCommand("Current Player", "Men", Exactly, 0);

Actions:
\tDefeat();
}}
'''
    return text


def rpg_triggers() -> str:
    """키우기 — 잡고, 경험치 쌓고, 승급한다."""
    tiers = [("Zerg Zergling", "Zerg Hydralisk", 10, "히드라리스크"),
             ("Zerg Hydralisk", "Zerg Ultralisk", 25, "울트라리스크")]
    text = f'''Trigger("All players"){{
Conditions:
\tKill("Current Player", "Men", At least, 1);

Actions:
\tSet Deaths("Current Player", "Terran Civilian", Add, 1);
\tSet Score("Current Player", Add, 10, Kills);
\tPreserve Trigger();
}}
'''
    for old, new, need, label in tiers:
        text += header(f"승급 — {label}")
        text += f'''Trigger("All players"){{
Conditions:
\tDeaths("Current Player", "Terran Civilian", At least, {need});
\tBring("Current Player", "{old}", "Anywhere", At least, 1);

Actions:
\tSet Deaths("Current Player", "Terran Civilian", Subtract, {need});
\tDisplay Text Message(Always Display, "\\x07레벨 업! \\x0F{label}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tRemove Unit At Location("Current Player", "{old}", 1, "Hero");
\tCreate Unit("Current Player", "{new}", 1, "Hero");
\tPreserve Trigger();
}}
'''
    text += header("사냥감 다시 채우기")
    text += '''Trigger("Player 1"){
Conditions:
\tCommand("Player 7", "Men", At most, 2);

Actions:
\tCreate Unit("Player 7", "Zerg Zergling", 6, "Spawn");
\tPreserve Trigger();
}
'''
    return text


def control_triggers() -> str:
    """컨트롤 — 붙고, 이기면 점수, 다음 판."""
    return f'''Trigger("All players"){{
Conditions:
\tCommand("Current Player", "Men", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "\\x08졌습니다");
\tSet Deaths("Current Player", "Terran Civilian", Set To, 0);
\tPreserve Trigger();
}}

//--------------- 한 판 끝 ---------------//

Trigger("All players"){{
Conditions:
\tBring("Current Player", "Men", "Arena", At least, 1);
\tCommand("Player 7", "Men", Exactly, 0);

Actions:
\tSet Score("Current Player", Add, 1, Kills);
\tDisplay Text Message(Always Display, "\\x07이겼습니다! \\x04+1점");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tSet Deaths("{COUNTER_PLAYER}", "{COUNTER_UNIT}", Add, 1);
\tPreserve Trigger();
}}

//--------------- 다음 판 ---------------//

Trigger("Player 1"){{
Conditions:
\tCommand("Player 7", "Men", Exactly, 0);
\tElapsed Time(At least, 5);

Actions:
\tCreate Unit("Player 7", "Terran Marine", 6, "Enemy Spawn");
\tOrder("Player 7", "Men", "Enemy Spawn", "Arena", attack);
\tPreserve Trigger();
}}
'''


def quiz_triggers() -> str:
    """퀴즈 — 비콘을 밟아 답한다."""
    return '''Trigger("All players"){
Conditions:
\tBring("Current Player", "Men", "Answer O", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07정답!");
\tSet Score("Current Player", Add, 1, Kills);
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tMove Unit("Current Player", "Men", All, "Answer O", "Lobby");
\tPreserve Trigger();
}

//--------------- 오답 ---------------//

Trigger("All players"){
Conditions:
\tBring("Current Player", "Men", "Answer X", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x08땡!");
\tKill Unit At Location("Current Player", "Men", All, "Answer X");
\tPreserve Trigger();
}
'''


def escape_triggers() -> str:
    """탈출 — 구역을 하나씩 통과한다."""
    return f'''Trigger("All players"){{
Conditions:
\tBring("Current Player", "Men", "Checkpoint", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07구간 통과!");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tSet Deaths("Current Player", "Terran Civilian", Add, 1);
\tPreserve Trigger();
}}

//--------------- 도착 ---------------//

Trigger("All players"){{
Conditions:
\tBring("Current Player", "Men", "Goal", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07탈출 성공!");
\tVictory();
}}

//--------------- 실패 ---------------//

Trigger("All players"){{
Conditions:
\tCommand("Current Player", "Men", Exactly, 0);

Actions:
\tDefeat();
}}
'''


GENRES = {
    "defense": {
        "triggers": defense_triggers,
        "locations": ["Spawn", "Goal", "Build Zone"],
        "objectives": "\\x041. 유닛을 지어 길목을 막습니다\\r\\n\\x042. 웨이브를 모두 막으면 승리\\r\\n\\x043. 본진 유닛이 모두 죽으면 패배",
        "start_units": [("Terran SCV", 1), ("Terran Command Center", 1)],
        "size": (96, 96),
    },
    "rpg": {
        "triggers": rpg_triggers,
        "locations": ["Hero", "Spawn", "Town"],
        "objectives": "\\x041. 적을 잡아 경험치를 모읍니다\\r\\n\\x042. 경험치가 차면 더 센 유닛으로 승급합니다",
        "start_units": [("Zerg Zergling", 1)],
        "size": (128, 128),
    },
    "control": {
        "triggers": control_triggers,
        "locations": ["Arena", "Enemy Spawn", "Lobby"],
        "objectives": "\\x041. 주어진 유닛으로 싸워 이깁니다\\r\\n\\x042. 이기면 점수를 얻습니다",
        "start_units": [("Terran Marine", 6)],
        "size": (64, 64),
    },
    "quiz": {
        "triggers": quiz_triggers,
        "locations": ["Lobby", "Answer O", "Answer X"],
        "objectives": "\\x041. 문제를 읽습니다\\r\\n\\x042. O 또는 X 비콘으로 걸어가 답합니다",
        "start_units": [("Terran Civilian", 1)],
        "size": (64, 64),
    },
    "escape": {
        "triggers": escape_triggers,
        "locations": ["Start", "Checkpoint", "Goal"],
        "objectives": "\\x041. 쫓아오는 것을 피해 나아갑니다\\r\\n\\x042. 목표 지점에 닿으면 승리",
        "start_units": [("Terran Civilian", 1)],
        "size": (128, 128),
    },
}


def main(argv=None):
    ap = argparse.ArgumentParser(description="유즈맵 뼈대를 만든다")
    ap.add_argument("out")
    ap.add_argument("--genre", default="defense", choices=sorted(GENRES))
    ap.add_argument("--players", type=int, default=6, help="사람 플레이어 수 (1~8)")
    ap.add_argument("--size", type=parse_size, default=None,
                    help="기본값은 장르마다 다르다")
    ap.add_argument("--tileset", default="jungle", choices=sorted(TILESETS))
    ap.add_argument("--name", default=None)
    ap.add_argument("--no-revealer", action="store_true",
                    help="Map Revealer 를 깔지 않는다")
    ap.add_argument("--install", default=None)
    args = ap.parse_args(argv)

    genre = GENRES[args.genre]
    width, height = args.size or genre["size"]
    tileset_id = TILESETS[args.tileset]
    name = args.name or f"{args.genre} 맵"

    if not (1 <= args.players <= 8):
        ap.error("사람 플레이어는 1~8 명입니다.")

    print(f"{args.genre} 유즈맵 {width}x{height} {args.tileset}, 사람 {args.players}명")

    # 유즈맵은 melee 기본 트리거(자원 지급·승패)가 걸리적거린다. 넣지 않는다.
    cli = scmap.new_map(args.out, width, height, tileset_id,
                        terrain=BASE_TERRAIN[tileset_id], melee=False,
                        install=args.install)

    # 1) 로케이션 — 트리거가 이름으로 가리킨다. 먼저 만들어야 한다.
    print("로케이션을 만듭니다...")
    step = max(12, min(width, height) // 4)
    for i, loc in enumerate(genre["locations"]):
        x = 6 + (i % 3) * step
        y = 6 + (i // 3) * step
        cli.edit("location", "add", cli.path,
                 str(x), str(y), str(x + 8), str(y + 8), "--tiles", "--name", loc)

    # 2) 시작 유닛 — 사람마다 같은 자리 근처에
    print("시작 유닛을 놓습니다...")
    for p in range(1, args.players + 1):
        px = 8 + ((p - 1) % 4) * 10
        py = 8 + ((p - 1) // 4) * 10
        for unit, count in genre["start_units"]:
            for k in range(count):
                cli.place(unit, px + (k % 4), py + (k // 4), owner=p)

    # 3) Map Revealer — 93개 중 59개 맵이 쓴다. 시야가 막히면 아무것도 안 보인다.
    if not args.no_revealer:
        print("시야를 엽니다...")
        cli.edit("scenario", "revealers", cli.path, "--owner", "1", "--spacing", "16")

    # 4) 트리거
    print("트리거를 넣습니다...")
    text = common_triggers(name, genre["objectives"])
    text += header(f"{args.genre} 고리")
    text += genre["triggers"]()
    cli.apply_triggers(text)

    cli.set_map_name(name)

    info = cli.info()
    print(f"\n만들었습니다: {args.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']}  "
          f"유닛 {info['units']}  로케이션 {info['locations']}  "
          f"트리거 {info['triggers']}")
    print("\n다음으로:")
    print("  - 웨이브·보상·연출을 늘린다 (references/usemap-dopamine.md)")
    print(f"  - 트리거를 텍스트로 빼서 고친다:")
    print(f"      splash-cli trigger show {args.out} t.txt --install \"$SC_INSTALL\"")
    print(f"  - 그려 본다: python3 preview.py {args.out} look.png")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
