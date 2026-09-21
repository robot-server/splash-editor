#!/usr/bin/env python3
"""사각 디펜스 유즈맵 — 지형·유닛·트리거까지 다 만든다.

유즈맵 지형은 자연 경관이 아니라 **직사각형 기하**다. 인기 디펜스 맵을
뜯어 보면 벽으로 두른 네모 경기장을 격자로 늘어놓은 것이 전부다.
능선과 절벽을 설계하는 밀리맵과 달리 이건 계산으로 정확히 만들 수 있다.

경기장 하나의 짜임 (인기 디펜스 맵에서 본뜸):

    ┌────────────────────┐   바깥 통로  : 적이 도는 길
    │ ╔════════════════╗ │   가운데 벽  : 걸어서 못 넘는다
    │ ║ ┌────────────┐ ║ │   가운데 섬  : 플레이어가 지키는 자리
    │ ║ │   플레이어  │ ║ │
    │ ║ └────────────┘ ║ │   적은 북서에서 나와 시계 방향으로 돌고,
    │ ╚════════════════╝ │   서쪽 출구에 닿으면 목숨이 하나 준다.
    └────────────────────┘

바닥 타일과 두뎃은 짐작하지 않는다 — `data/corpus.json` 의 타일셋별
실측 분포에서 뽑는다. 타일 값 하나로 도배하면 주기성 100% 가 되므로
바닥은 단색으로 반듯하게 둔다 — 유즈맵은 네모난 방이지 경관이 아니다.

보기:
    python3 make_square_defense.py out.scx --players 6 --waves 15
"""
from __future__ import annotations

import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus
import scmap
from scmap import Cli, CliError

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}

VOID_TILE = 0            # 걸을 수 없고 검게 그려진다 — 벽으로 쓴다

# 세는 데 쓰는 유닛. 맵에 한 번도 놓지 않으므로 죽음 수가 순수한 변수가
# 된다. **unused unit 은 쓰지 않는다** (허락을 받아야 하는 것이다).
WAVE_COUNTER = "Scanner Sweep"
LIFE_COUNTER = "Dark Swarm"
# 안내를 사람마다 한 번만 띄우기 위한 잠금. 스위치는 맵 전체에 하나뿐
# 이라 첫 사람만 걸리고 나머지는 지나가므로 플레이어별 죽음 수를 쓴다.
SEEN_COUNTER = "Protoss Scarab"

# 웨이브마다 나오는 것. 뒤로 갈수록 세진다.
WAVE_TABLE = [
    ("Zerg Zergling", 6), ("Zerg Zergling", 8), ("Terran Marine", 6),
    ("Zerg Hydralisk", 5), ("Terran Vulture", 5), ("Zerg Zergling", 14),
    ("Protoss Zealot", 6), ("Terran Goliath", 4), ("Zerg Hydralisk", 8),
    ("Protoss Dragoon", 5), ("Terran Siege Tank (Tank Mode)", 3),
    ("Zerg Ultralisk", 2), ("Protoss Archon", 2),
    ("Terran Battlecruiser", 1), ("Zerg Ultralisk", 4),
]
BOSS = ("Torrasque (Ultralisk)", 1)

# 스위치는 **임의 이름을 못 쓴다** — `Switch N` 번호만 받는다.
#   1..waves      : 그 웨이브를 이미 냈는가
#   waves+1       : 보스를 냈는가
#   waves+2..     : 경기장마다 이번 웨이브 보상을 이미 줬는가
# 상점 셋은 **서로 다른 일을 한다.** (이름, 값, 하는 일)
SHOPS = [("머린 4기", 120, "marine"),
         ("성큰 1기", 200, "sunken"),
         ("전체 수리", 80, "heal")]
UPGRADE_SHOPS = SHOPS          # 자리 잡는 쪽에서 쓰는 이름


# ---------------------------------------------------------------- 자리잡기

def layout(players, W, H, margin=2, gap=3):
    """경기장을 격자로 늘어놓는다.

    앞선 판에는 가운데에 공용 띠를 두었는데 **아무도 갈 수 없는 죽은
    땅**이었다 — 경기장이 벽으로 봉인되어 있어 닿지 못한다. 띠를 없애고
    그 자리를 경기장에 돌려주었다. 빈 땅을 남기지 않는다.
    """
    cols = min(3, players)
    rows = (players + cols - 1) // cols
    aw = (W - 2 * margin - (cols - 1) * gap) // cols
    ah = (H - 2 * margin - (rows - 1) * gap) // rows
    boxes = []
    for i in range(players):
        r, c = divmod(i, cols)
        boxes.append((margin + c * (aw + gap), margin + r * (ah + gap), aw, ah))
    return boxes



def floor_tile(cli, tileset, rng):
    """그 타일셋에서 실제로 바닥으로 많이 쓰인 그룹의 타일을 고른다.

    "걷기·짓기 되는 첫 번째 타일"처럼 규칙으로 고르면 Ice 를 시켰는데
    흙바닥이 나온다. 실측 분포에서 뽑아야 그 타일셋처럼 보인다.
    """
    c = corpus.load()
    tiles = scmap.tileset_tiles(cli, tileset)
    for g in corpus.pick_floor_groups(c, tileset, "usemap", 12):
        good = [t for t in range(g * 16, g * 16 + 16)
                if t in tiles and tiles[t][1] and tiles[t][2]]
        if good:
            return rng.choice(good), g
    for t, p in sorted(tiles.items()):          # 물러설 곳
        if t >= 16 and p[1] and p[2]:
            return t, t >> 4
    raise CliError("바닥으로 쓸 타일을 찾지 못했습니다.")


# ---------------------------------------------------------------- 트리거

def build_triggers(players, waves, enemy, boss_p, arena_names):
    """트리거를 글로 짠다. 하나하나 하는 일이 있어야 한다.

    **승리는 사람마다 따로 판정한다.** 여섯 경기장을 모두 검사하면
    빈 슬롯의 경기장에 생긴 보스를 아무도 잡을 수 없어 게임이 영영
    안 끝난다 (경기장끼리 벽으로 갈려 있으므로 대신 가 줄 수도 없다).
    """
    T = []
    add = T.append
    HUMANS = ",".join(f'"Player {p}"' for p in range(1, players + 1))

    T.extend(scmap.hyper_triggers(enemy))
    T.extend(scmap.absent_player_cleanup(players, enemy))

    add(f'''Trigger({HUMANS}){{
Conditions:
\tAlways();

Actions:
\tSet Resources("Current Player", Set To, 250, ore);
\tSet Deaths("Current Player", "{LIFE_COUNTER}", Set To, 20);
\tSet Score("Current Player", Set To, 20, Custom);
\tDisplay Text Message(Always Display, "\\x04사각 디펜스\\x02 — \\x07목숨 20\\x02. 적이 서쪽 출구에 닿으면 하나씩 줍니다.");
\tDisplay Text Message(Always Display, "\\x03가운데 섬에서 막으세요. 비콘: \\x07머린 120\\x03 · \\x07성큰 200\\x03 · \\x07회복 80\\x03.");
\tSet Mission Objectives("\\x04사각 디펜스\\x02\\n\\x03- 웨이브 {waves}개를 막고 보스를 잡으면 이깁니다\\n- 적이 서쪽 출구에 닿으면 목숨이 하나 줍니다 (20개)\\n- 비콘: 머린 120 · 성큰 200 · 회복 80\\n- 리더보드는 남은 목숨입니다");
\tSet Countdown Timer(Set To, 30);
}}''')

    # 리더보드로 남은 목숨을 늘 보여 준다 (안 보이면 방어 판단을 못 한다)
    add('''Trigger("All players"){
Conditions:
\tAlways();

Actions:
\tLeader Board Points("\\x07남은 목숨", Custom);
\tPreserve Trigger();
}''')

    # 웨이브 시계
    add(f'''Trigger("{enemy}"){{
Conditions:
\tCountdown Timer(At most, 0);
\tDeaths("{enemy}", "{WAVE_COUNTER}", At most, {waves});

Actions:
\tSet Deaths("{enemy}", "{WAVE_COUNTER}", Add, 1);
\tSet Countdown Timer(Set To, 35);
\tPreserve Trigger();
}}''')

    # 웨이브 스폰 — **사람이 들어온 경기장에만** 낸다
    for w in range(1, waves + 1):
        unit, n = WAVE_TABLE[(w - 1) % len(WAVE_TABLE)]
        n = n + (w - 1) // len(WAVE_TABLE) * 2
        for i, a in enumerate(arena_names):
            add(f'''Trigger("{enemy}"){{
Conditions:
\tDeaths("{enemy}", "{WAVE_COUNTER}", Exactly, {w});
\tDeaths("Player {i + 1}", "{scmap.PRESENCE_UNIT}", At least, 1);
\tSwitch("Switch {(w - 1) * 8 + i + 1}", not set);

Actions:
\tSet Switch("Switch {(w - 1) * 8 + i + 1}", set);
\tCreate Unit("{enemy}", "{unit}", {n}, "{a} Spawn");
\tPreserve Trigger();
}}''')
        # 안내는 **사람마다 한 번만.** 조건이 35초 내내 참이라
        # 잠금이 없으면 매 프레임 글과 소리가 도배된다.
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("{enemy}", "{WAVE_COUNTER}", Exactly, {w});
\tDeaths("Current Player", "{SEEN_COUNTER}", At most, {w - 1});

Actions:
\tSet Deaths("Current Player", "{SEEN_COUNTER}", Set To, {w});
\tDisplay Text Message(Always Display, "\\x07웨이브 {w}\\x02 — {unit} x{n}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')

    # 보스 — 사람이 들어온 경기장에만
    bu, bn = BOSS
    for i, a in enumerate(arena_names):
        add(f'''Trigger("{boss_p}"){{
Conditions:
\tDeaths("{enemy}", "{WAVE_COUNTER}", At least, {waves + 1});
\tDeaths("Player {i + 1}", "{scmap.PRESENCE_UNIT}", At least, 1);
\tSwitch("Switch {240 + i}", not set);

Actions:
\tSet Switch("Switch {240 + i}", set);
\tCreate Unit with Properties("{boss_p}", "{bu}", 1, "{a} Spawn", 1);
\tPreserve Trigger();
}}''')

    # 보스 안내는 **사람이 띄운다.** Display Text Message 는 그 트리거를
    # 실행하는 플레이어에게만 보인다 — 컴퓨터가 띄우면 아무도 못 본다.
    add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("{enemy}", "{WAVE_COUNTER}", At least, {waves + 1});
\tDeaths("Current Player", "{SEEN_COUNTER}", At most, {waves});

Actions:
\tSet Deaths("Current Player", "{SEEN_COUNTER}", Set To, {waves + 1});
\tDisplay Text Message(Always Display, "\\x06보스\\x02 — {bu}. 이것만 잡으면 끝입니다.");
\tPlay WAV("sound\\\\Misc\\\\PowerDown.wav", 500);
\tPreserve Trigger();
}}''')

    # 경기장마다
    for i, a in enumerate(arena_names):
        p = f"Player {i + 1}"
        # 길 안내 — 적과 보스 모두 공격 이동으로
        for who in (enemy, boss_p):
            add(f'''Trigger("{who}"){{
Conditions:
\tAlways();

Actions:
\tOrder("{who}", "Any unit", "{a} Spawn", "{a} NE", attack);
\tOrder("{who}", "Any unit", "{a} NE", "{a} SE", attack);
\tOrder("{who}", "Any unit", "{a} SE", "{a} SW", attack);
\tOrder("{who}", "Any unit", "{a} SW", "{a} Exit", attack);
\tPreserve Trigger();
}}''')
        # **지킬 유닛을 묶지 않는다.** 앞서 Always + Preserve 로 가운데로
        # 보내는 명령을 넣었는데, 하이퍼 트리거와 맞물려 매 프레임 강제
        # 이동이 나가 플레이어가 유닛을 조작할 수 없었다. 섬은 벽으로
        # 갇혀 있어 어차피 쫓아 나갈 수 없으니 묶을 필요가 없다.
        # 누수 — **한 번에 한 기씩** 지우고 목숨 하나를 깎는다.
        # 통째로 지우면 다섯이 새어도 목숨이 하나만 준다.
        for who in (enemy, boss_p):
            add(f'''Trigger("{p}"){{
Conditions:
\tBring("{who}", "Any unit", "{a} Exit", At least, 1);

Actions:
\tRemove Unit At Location("{who}", "Any unit", 1, "{a} Exit");
\tSet Deaths("{p}", "{LIFE_COUNTER}", Subtract, 1);
\tSet Score("{p}", Subtract, 1, Custom);
\tMinimap Ping("{a} Exit");
\tPlay WAV("sound\\\\Misc\\\\PowerDown.wav", 500);
\tPreserve Trigger();
}}''')
        # 웨이브를 막으면 보상
        add(f'''Trigger("{p}"){{
Conditions:
\tBring("{enemy}", "Any unit", "{a} All", Exactly, 0);
\tDeaths("{enemy}", "{WAVE_COUNTER}", At least, 1);
\tSwitch("Switch {230 + i}", not set);

Actions:
\tSet Switch("Switch {230 + i}", set);
\tSet Resources("{p}", Add, 120, ore);
\tDisplay Text Message(Always Display, "\\x07웨이브를 막았습니다. \\x03+120");
\tPreserve Trigger();
}}''')
        add(f'''Trigger("{p}"){{
Conditions:
\tBring("{enemy}", "Any unit", "{a} All", At least, 1);
\tSwitch("Switch {230 + i}", set);

Actions:
\tSet Switch("Switch {230 + i}", clear);
\tPreserve Trigger();
}}''')
        # 상점 — **셋이 서로 다른 일을 한다.** 앞서는 셋 다 체력만
        # 고쳤는데, 이름이 거짓말이었고 병력을 잃으면 복구할 길도 없었다.
        for k, (label, cost, kind) in enumerate(SHOPS):
            if kind == "marine":
                body = (f'\tCreate Unit("{p}", "Terran Marine", 4, "{a} Center");')
            elif kind == "sunken":
                body = (f'\tCreate Unit("{p}", "Zerg Sunken Colony", 1, "{a} Center");')
            else:
                body = (f'\tModify Unit Hit Points("{p}", "Men", 100, 0, "{a} All");\n'
                        f'\tModify Unit Hit Points("{p}", "Buildings", 100, 0, "{a} All");')
            add(f'''Trigger("{p}"){{
Conditions:
\tBring("{p}", "Men", "{a} Shop{k + 1}", At least, 1);
\tAccumulate("{p}", At least, {cost}, ore);

Actions:
\tSet Resources("{p}", Subtract, {cost}, ore);
{body}
\tMove Unit("{p}", "Men", All, "{a} Shop{k + 1}", "{a} Center");
\tDisplay Text Message(Always Display, "\\x03{label} 구입! \\x02-{cost}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
        # 병력이 0이 되면 최소 병력을 다시 준다 (구경만 하다 지지 않게)
        add(f'''Trigger("{p}"){{
Conditions:
\tCommand("{p}", "Men", At most, 0);
\tDeaths("{p}", "{LIFE_COUNTER}", At least, 1);

Actions:
\tCreate Unit("{p}", "Terran Marine", 4, "{a} Center");
\tDisplay Text Message(Always Display, "\\x03병력이 다 죽어 새로 받았습니다.");
\tPreserve Trigger();
}}''')
        # 패배
        add(f'''Trigger("{p}"){{
Conditions:
\tDeaths("{p}", "{LIFE_COUNTER}", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "\\x06목숨이 다했습니다.");
\tDefeat();
}}''')
        # **개인 승리** — 내 경기장의 보스를 잡으면 나는 이긴다.
        add(f'''Trigger("{p}"){{
Conditions:
\tSwitch("Switch {240 + i}", set);
\tBring("{boss_p}", "Any unit", "{a} All", Exactly, 0);
\tDeaths("{p}", "{LIFE_COUNTER}", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07보스를 잡았습니다!");
\tVictory();
}}''')
    return scmap.TRIGGER_SEP.join(T)


# ---------------------------------------------------------------- 본체

def main(argv=None):
    ap = argparse.ArgumentParser(description="사각 디펜스 유즈맵")
    ap.add_argument("out")
    ap.add_argument("--players", type=int, default=6)
    ap.add_argument("--waves", type=int, default=15)
    ap.add_argument("--size", default="128x128")
    ap.add_argument("--tileset", default="ice", choices=sorted(TILESETS))
    ap.add_argument("--name", default="사각 디펜스")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    if not (1 <= a.players <= 6):
        ap.error("사람은 1~6명입니다 (적·보스로 슬롯 둘을 더 씁니다).")

    rng = random.Random(a.seed)
    W, H = (int(v) for v in a.size.lower().split("x"))
    ts = TILESETS[a.tileset]
    enemy_no, boss_no = a.players + 1, a.players + 2
    enemy, boss_p = f"Player {enemy_no}", f"Player {boss_no}"

    print(f"사각 디펜스 {W}x{H} {a.tileset}, 사람 {a.players}명, {a.waves}웨이브")
    print("  " + corpus.describe("usemap", a.tileset))

    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    tile, grp = floor_tile(cli, ts, rng)
    print(f"  바닥 타일 0x{tile:04x} (그룹 {grp}) — 실측 분포에서 골랐습니다")

    boxes = layout(a.players, W, H)
    # 벽 두께는 **사거리보다 얇아야 한다.** 마린 사거리가 4 타일이라
    # 벽이 4 면 섬 가장자리에 딱 붙어야 겨우 닿는다. 2 로 줄인다.
    RING, WALL = 5, 2

    # 1) 전부 벽으로 덮는다
    print("벽을 세웁니다...")
    cli.edit("terrain", "fill", cli.path, "0", "0", str(W), str(H), str(VOID_TILE))

    fill = lambda x, y, w, h, t: cli.edit(
        "terrain", "fill", cli.path, str(x), str(y), str(w), str(h), str(t))

    # 2) 경기장: 통로 → 벽 고리 → 가운데 섬
    print(f"경기장 {len(boxes)}개를 뚫습니다 (통로/벽/섬)...")
    for (x, y, w, h) in boxes:
        fill(x, y, w, h, tile)                                    # 통로까지 포함
        fill(x + RING, y + RING, w - 2 * RING, h - 2 * RING, VOID_TILE)   # 벽
        fill(x + RING + WALL, y + RING + WALL,
             w - 2 * (RING + WALL), h - 2 * (RING + WALL), tile)          # 섬

    # 3) 바닥에 결을 준다 — 그룹을 섞고 변종을 흩는다
    walk = []
    for (x, y, w, h) in boxes:
        walk.append((x, y, w, RING))                               # 위 통로
        walk.append((x, y + h - RING, w, RING))                    # 아래 통로
        walk.append((x, y + RING, RING, h - 2 * RING))             # 왼 통로
        walk.append((x + w - RING, y + RING, RING, h - 2 * RING))  # 오른 통로
        walk.append((x + RING + WALL, y + RING + WALL,
                     w - 2 * (RING + WALL), h - 2 * (RING + WALL)))  # 섬
    groups = corpus.pick_floor_groups(corpus.load(), ts, "usemap", 14)

    # 4) 플레이어 슬롯
    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, a.players, [enemy_no, boss_no])

    # 5) 로케이션 — 하나하나 트리거가 쓴다
    print("로케이션을 놓습니다...")
    loc = lambda n, x0, y0, x1, y1: cli.edit(
        "location", "add", cli.path, str(x0), str(y0), str(x1), str(y1),
        "--tiles", "--name", n)
    arena_names = []
    for i, (x, y, w, h) in enumerate(boxes):
        A = f"A{i + 1}"
        arena_names.append(A)
        C = RING - 1                       # 모서리 칸 크기
        loc(f"{A} Spawn", x + 1, y + 1, x + 1 + C, y + 1 + C)
        loc(f"{A} NE", x + w - 1 - C, y + 1, x + w - 1, y + 1 + C)
        loc(f"{A} SE", x + w - 1 - C, y + h - 1 - C, x + w - 1, y + h - 1)
        loc(f"{A} SW", x + 1, y + h - 1 - C, x + 1 + C, y + h - 1)
        loc(f"{A} Exit", x + 1, y + h // 2 - 2, x + 1 + C, y + h // 2 + 2)
        cx, cy = x + w // 2, y + h // 2
        loc(f"{A} Center", cx - 4, cy - 4, cx + 4, cy + 4)
        for k in range(len(UPGRADE_SHOPS)):
            bx = cx - 5 + k * 5
            loc(f"{A} Shop{k + 1}", bx, cy + 8, bx + 3, cy + 11)
        loc(f"{A} All", x, y, x + w, y + h)

    # 6) 유닛
    print("유닛을 놓습니다...")
    for i, (x, y, w, h) in enumerate(boxes):
        p = i + 1
        cx, cy = x + w // 2, y + h // 2
        cli.place(scmap.START_LOCATION, cx, cy, owner=p)
        # 시작 병력도 섬 가장자리에 붙여 둔다. 가운데 모아 두면
        # 사거리가 링에 닿지 않아 한 발도 못 쏜다.
        for k in range(6):
            cli.place("Terran Marine",
                      x + RING + WALL + 1 + k,
                      y + RING + WALL + 1, owner=p)
        # **미사일 터렛은 대공 전용이다.** 지상 웨이브를 못 때린다.
        #
        # 지상을 치는 것 중 **성큰 콜로니**를 쓴다 (사거리 7).
        #   - 포톤 캐논은 파일런이 있어야 전력이 들어온다. 없으면 안
        #     쏜다 — 실측에서도 캐논 쓰는 맵의 89% 가 파일런을 함께 둔다.
        #   - 벙커는 안에 마린을 넣어야 쏜다. 미리 놓은 벙커는 비어 있다.
        #   - 성큰은 **스스로 크립을 깔아서** 딸린 것이 없다. 유즈맵
        #     45% 가 쓰는 이유다.
        # 섬 가운데가 아니라 **가장자리를 따라** 놓아야 사거리가 닿는다.
        w_isl = w - 2 * (RING + WALL)
        h_isl = h - 2 * (RING + WALL)
        ix0, iy0 = x + RING + WALL, y + RING + WALL
        for (px, py) in ((ix0 + 2, iy0 + 2), (ix0 + w_isl - 3, iy0 + 2),
                         (ix0 + 2, iy0 + h_isl - 3),
                         (ix0 + w_isl - 3, iy0 + h_isl - 3)):
            cli.place("Zerg Sunken Colony", px, py, owner=p)
        cli.place("Terran Civilian", cx, cy + 4, owner=p)     # 비콘 밟을 말
        for k, (label, _cost, _kind) in enumerate(SHOPS):    # 상점 셋
            bx = cx - 5 + k * 5
            cli.place(("Terran Engineering Bay", "Zerg Creep Colony",
                       "Terran Academy")[k], bx + 1, cy + 6, owner=p)
            cli.place("Terran Beacon", bx + 1, cy + 9, owner=p)
    # 적·보스도 스타팅이 있어야 슬롯이 산다. 첫 경기장 통로 구석에 둔다.
    bx, by, bw, bh = boxes[0]
    cli.place(scmap.START_LOCATION, bx + 2, by + bh // 2, owner=enemy_no)
    cli.place(scmap.START_LOCATION, bx + bw - 3, by + bh // 2, owner=boss_no)

    # 7) 시야 (실측: 유즈맵 74% 가 Map Revealer 를 쓴다)
    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)

    # 8) 트리거
    print("트리거를 짭니다...")
    text = build_triggers(a.players, a.waves, enemy, boss_p, arena_names)
    cli.apply_triggers(text)

    if a.name:
        # **설명을 반드시 넣는다.** 안 넣으면 에디터 기본값
        # "Destroy all enemy buildings." 가 그대로 남아 맵과 어긋난다.
        cli.set_map_name(a.name,
            f"경기장 {a.players}개. 웨이브 {a.waves}개를 막고 보스를 잡으면 이깁니다. "
            f"적이 서쪽 출구에 닿으면 목숨이 하나 줍니다(20개). "
            f"가운데 섬 비콘에서 머린 120 · 성큰 200 · 수리 80 에 삽니다.")

    info = cli.info()
    print(f"\n만들었습니다: {a.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']} {info['version']}")
    print(f"  유닛 {info['units']}  로케이션 {info['locations']}  "
          f"트리거 {info['triggers']}")
    print(f"\n  python3 preview.py {a.out} look.png   ← 반드시 그려 볼 것")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
