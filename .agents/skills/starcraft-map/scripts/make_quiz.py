#!/usr/bin/env python3
"""OX 퀴즈 유즈맵 — 문제를 띄우고 발판 위에서 답을 고른다.

디펜스가 "막는" 고리, 컨트롤이 "싸우는" 고리, 키우기가 "세지는" 고리라면
퀴즈는 **"맞히는"** 고리다. 한 문제가 10~15초로 끝나고 바로 결과가
나오므로 되먹임이 가장 짧다.

    ┌──────────────────────────────┐
    │          대기 구역            │   문제가 뜨면 O 나 X 로 건너간다
    ├──────────────┬───────────────┤
    │      O       │       X       │   제한 시간이 끝나면 판정한다
    └──────────────┴───────────────┘   틀린 쪽은 목숨이 하나 준다

**문제는 생성기에 박지 않는다.** `--questions` 로 파일을 받는다.
한 줄에 하나, `문제|O` 또는 `문제|X` 꼴이다.

    스타크래프트는 1998년에 나왔다|O
    저글링은 공중 유닛을 때릴 수 있다|X

보기:
    python3 make_quiz.py out.scx --players 6 --questions q.txt
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
VOID_TILE = 0
LIFE = "Dark Swarm"          # 맵에 한 번도 놓지 않는다 — 순수한 변수
QNUM = "Scanner Sweep"       # 문제 번호 (P8)
# 사람마다 따로 잠가야 해서 스위치가 아니라 플레이어별 죽음 수를 쓴다.
# 스위치는 맵 전체에 하나뿐이라 첫 사람만 걸리고 나머지는 지나간다.
SHOWN = "Protoss Scarab"     # 이 사람에게 몇 번까지 문제를 보여 줬나
JUDGED = "Protoss Interceptor"  # 이 사람의 몇 번까지 채점했나


def read_questions(path: str) -> list[tuple[str, str]]:
    out = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "|" not in line:
                raise CliError(f"{path}:{n} — '문제|O' 꼴이어야 합니다: {line}")
            q, a = line.rsplit("|", 1)
            a = a.strip().upper()
            if a not in ("O", "X"):
                raise CliError(f"{path}:{n} — 답은 O 나 X 여야 합니다: {a}")
            out.append((q.strip().replace('"', "'"), a))
    if not out:
        raise CliError(f"{path} 에 문제가 하나도 없습니다.")
    return out


def build_triggers(players, questions, lives, secs):
    """퀴즈 트리거.

    **두 가지를 지킨다.**

    1. `Display Text Message` 는 **그 트리거를 실행하는 플레이어에게만**
       보인다. 컴퓨터가 띄우면 아무도 못 본다 — 안내는 전부 사람이
       실행하는 트리거에 둔다.
    2. 하이퍼 트리거를 깔면 매 프레임 돈다. **조건이 한동안 계속 참인
       트리거에는 반드시 한 번만 걸리는 잠금**을 건다. 안 걸면 12초
       내내 목숨이 깎여 즉사한다.

    잠금은 사람마다 따로여야 하므로 스위치가 아니라 **플레이어별 죽음 수
    카운터**로 만든다 (스위치는 맵 전체에 하나뿐이다).
    """
    T = []
    add = T.append
    HUMANS = ",".join(f'"Player {p}"' for p in range(1, players + 1))
    nq = len(questions)

    T.extend(scmap.hyper_triggers("Player 8"))
    T.extend(scmap.absent_player_cleanup(players, "Player 8"))

    add(f'''Trigger({HUMANS}){{
Conditions:
\tAlways();

Actions:
\tSet Deaths("Current Player", "{LIFE}", Set To, {lives});
\tSet Deaths("Current Player", "{SHOWN}", Set To, 0);
\tSet Deaths("Current Player", "{JUDGED}", Set To, 0);
\tSet Score("Current Player", Set To, 0, Custom);
\tDisplay Text Message(Always Display, "\\x04OX 퀴즈\\x02 — 문제 {nq}개. 목숨 \\x07{lives}개\\x02.");
\tDisplay Text Message(Always Display, "\\x03문제가 뜨면 {secs}초 안에 \\x04왼쪽 O\\x03 나 \\x06오른쪽 X\\x03 발판으로 옮기세요.");
\tSet Mission Objectives("\\x04OX 퀴즈\\x02\\n\\x03- 문제 {nq}개, 한 문제에 {secs}초\\n- 왼쪽이 O, 오른쪽이 X 입니다\\n- 틀리면 목숨이 하나 줍니다 (목숨 {lives}개)\\n- 끝까지 살아남으면 이깁니다");
\tSet Countdown Timer(Set To, {secs});
}}''')

    add('''Trigger("All players"){
Conditions:
\tAlways();

Actions:
\tLeader Board Points("\\x07맞힌 수", Custom);
\tPreserve Trigger();
}''')

    # 문제 번호를 올리는 시계 — 컴퓨터가 돌린다 (안내는 하지 않는다)
    add(f'''Trigger("Player 8"){{
Conditions:
\tCountdown Timer(At most, 0);
\tDeaths("Player 8", "{QNUM}", At most, {nq});

Actions:
\tSet Deaths("Player 8", "{QNUM}", Add, 1);
\tSet Countdown Timer(Set To, {secs});
\tPreserve Trigger();
}}''')

    for i, (q, ans) in enumerate(questions, 1):
        wrong = "X" if ans == "O" else "O"
        # 문제 띄우기 — **사람이 실행한다.** 사람마다 한 번만.
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", Exactly, {i});
\tDeaths("Current Player", "{SHOWN}", At most, {i - 1});

Actions:
\tSet Deaths("Current Player", "{SHOWN}", Set To, {i});
\tDisplay Text Message(Always Display, "\\x07{i}번.\\x02 {q}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
        # 판정 — 다음 문제로 넘어가는 순간, 사람마다 **한 번만**.
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", At least, {i + 1});
\tDeaths("Current Player", "{JUDGED}", At most, {i - 1});
\tBring("Current Player", "Any unit", "Pad {ans}", At least, 1);

Actions:
\tSet Deaths("Current Player", "{JUDGED}", Set To, {i});
\tSet Score("Current Player", Add, 1, Custom);
\tDisplay Text Message(Always Display, "\\x07{i}번 정답!");
\tMove Unit("Current Player", "Men", All, "Pad {ans}", "Lobby");
\tPreserve Trigger();
}}''')
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", At least, {i + 1});
\tDeaths("Current Player", "{JUDGED}", At most, {i - 1});
\tBring("Current Player", "Any unit", "Pad {wrong}", At least, 1);

Actions:
\tSet Deaths("Current Player", "{JUDGED}", Set To, {i});
\tSet Deaths("Current Player", "{LIFE}", Subtract, 1);
\tDisplay Text Message(Always Display, "\\x06{i}번 틀렸습니다.\\x02 답은 {ans} 입니다.");
\tPlay WAV("sound\\\\Misc\\\\PowerDown.wav", 500);
\tMove Unit("Current Player", "Men", All, "Pad {wrong}", "Lobby");
\tPreserve Trigger();
}}''')
        # 어느 발판에도 없으면 안 고른 것 — 목숨을 깎고 넘어간다
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", At least, {i + 1});
\tDeaths("Current Player", "{JUDGED}", At most, {i - 1});

Actions:
\tSet Deaths("Current Player", "{JUDGED}", Set To, {i});
\tSet Deaths("Current Player", "{LIFE}", Subtract, 1);
\tDisplay Text Message(Always Display, "\\x06{i}번 — 고르지 않았습니다.\\x02 답은 {ans} 입니다.");
\tPreserve Trigger();
}}''')

    add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Current Player", "{LIFE}", At most, 0);

Actions:
\tDisplay Text Message(Always Display, "\\x06목숨이 다했습니다.");
\tDefeat();
}}''')
    add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", At least, {nq + 1});
\tDeaths("Current Player", "{JUDGED}", At least, {nq});
\tDeaths("Current Player", "{LIFE}", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "\\x07끝까지 살아남았습니다!");
\tVictory();
}}''')
    return scmap.TRIGGER_SEP.join(T)


def main(argv=None):
    ap = argparse.ArgumentParser(description="OX 퀴즈 유즈맵")
    ap.add_argument("out")
    ap.add_argument("--questions", required=True,
                    help="'문제|O' 꼴로 한 줄에 하나씩 적은 파일")
    ap.add_argument("--players", type=int, default=6)
    ap.add_argument("--lives", type=int, default=3)
    ap.add_argument("--seconds", type=int, default=12)
    ap.add_argument("--size", default="64x64")   # 대기 구역 + 발판 둘이면 충분하다
    ap.add_argument("--tileset", default="space", choices=sorted(TILESETS))
    ap.add_argument("--name", default="OX 퀴즈")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    if not (1 <= a.players <= 7):
        ap.error("1~7명입니다 (진행용으로 슬롯 하나를 더 씁니다).")

    questions = read_questions(a.questions)
    rng = random.Random(a.seed)
    W, H = (int(v) for v in a.size.lower().split("x"))
    ts = TILESETS[a.tileset]

    print(f"OX 퀴즈 {W}x{H} {a.tileset}, {a.players}명, 문제 {len(questions)}개")
    print("  " + corpus.describe("usemap", a.tileset))
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    groups = corpus.pick_floor_groups(corpus.load(), ts, "usemap", 14)
    tiles = scmap.tileset_tiles(cli, ts)
    floor = next(t for g in groups for t in range(g * 16, g * 16 + 16)
                 if t in tiles and tiles[t][1] and tiles[t][2])

    # **대기 구역을 따로 두지 않는다. O 와 X 사이가 대기 구역이다.**
    # 앞서는 위쪽에 넓은 대기 구역을 두고 발판마다 통로를 냈는데,
    # 동선이 서른 타일을 넘어 12초 안에 못 갔다. 가운데 띠에 서 있다가
    # 문제가 뜨면 왼쪽이나 오른쪽으로 몇 타일만 옮기면 된다.
    #
    #        ┌────────┬──────┬────────┐
    #        │   O    │ 대기 │   X    │
    #        └────────┴──────┴────────┘
    #
    # **필요한 만큼만 쓴다.** 맵이 크다고 다 쓸 이유도, 무조건 작게
    # 만들 이유도 없다. 이 놀이는 좌우로 몇 칸이면 닿아야 하므로 좁다.
    PAD_W, PAD_H, MID_W = 14, 13, 7
    used_w = PAD_W * 2 + MID_W
    used_h = PAD_H
    ox = (W - used_w) // 2
    oy = (H - used_h) // 2
    pad_o = (ox, oy, PAD_W, PAD_H)
    lobby = (ox + PAD_W, oy, MID_W, PAD_H)
    pad_x = (ox + PAD_W + MID_W, oy, PAD_W, PAD_H)

    print("벽을 세웁니다...")
    cli.edit("terrain", "fill", cli.path, "0", "0", str(W), str(H), str(VOID_TILE))
    fill = lambda r: cli.edit("terrain", "fill", cli.path,
                              str(r[0]), str(r[1]), str(r[2]), str(r[3]), str(floor))
    print(f"O · 대기 · X 를 나란히 뚫습니다 ({used_w}x{used_h} 만 씁니다)...")
    for r in (pad_o, lobby, pad_x):
        fill(r)

    # 같은 지형 안의 **변종만** 흩는다. 그룹을 섞으면 얼룩덜룩한 덩이
    # 무늬가 생겨 네모난 방과 안 어울린다 — 변종은 잔 알갱이만 남는다.
    # 실제 사각 디펜스 맵도 타일 53~65종을 쓴다.
    scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, a.players, [8])

    print("로케이션을 놓습니다...")
    loc = lambda n, r: cli.edit("location", "add", cli.path, str(r[0]), str(r[1]),
                                str(r[0] + r[2]), str(r[1] + r[3]),
                                "--tiles", "--name", n)
    loc("Lobby", lobby)
    loc("Pad O", pad_o)
    loc("Pad X", pad_x)
    for p in range(1, a.players + 1):
        sy = lobby[1] + 1 + (p - 1) * (lobby[3] - 3) // max(1, a.players)
        cli.edit("location", "add", cli.path, str(lobby[0] + 1), str(sy),
                 str(lobby[0] + lobby[2] - 1), str(sy + 2),
                 "--tiles", "--name", f"P{p} Start")

    print("유닛을 놓습니다...")
    for p in range(1, a.players + 1):
        sy = lobby[1] + 2 + (p - 1) * (lobby[3] - 3) // max(1, a.players)
        cli.place(scmap.START_LOCATION, lobby[0] + lobby[2] // 2, sy, owner=p)
        cli.place("Terran Civilian", lobby[0] + lobby[2] // 2, sy, owner=p)
    cli.place(scmap.START_LOCATION, lobby[0] + 1, lobby[1] + lobby[3] - 2, owner=8)
    # **발판에 O 와 X 를 실제로 그린다.** 표시가 없으면 어느 쪽이
    # 어느 쪽인지 알 수가 없다. 건물을 글자 모양으로 늘어놓는다.
    import math as _m
    for (r, mark) in ((pad_o, "O"), (pad_x, "X")):
        cx, cy = r[0] + r[2] // 2, r[1] + r[3] // 2
        rad = min(r[2], r[3]) // 3
        pts = []
        if mark == "O":
            for k in range(30):          # 성기면 원으로 안 읽힌다
                ang = 2 * _m.pi * k / 30
                pts.append((cx + int(round(rad * _m.cos(ang))),
                            cy + int(round(rad * 0.85 * _m.sin(ang)))))
        else:
            for t in range(-rad, rad + 1):
                pts.append((cx + t, cy + int(t * 0.85)))
                pts.append((cx + t, cy - int(t * 0.85)))
        for (px, py) in sorted(set(pts)):
            if r[0] + 1 <= px < r[0] + r[2] - 1 and r[1] + 1 <= py < r[1] + r[3] - 1:
                cli.place("Protoss Pylon", px, py, owner=12)
        cli.place("Terran Beacon", cx, r[1] + 2, owner=12)

    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)

    print("브리핑을 짭니다...")
    cli.apply_briefing(scmap.briefing_text(
        [f"OX 문제가 {len(questions)}개 나옵니다.",
         f"문제가 뜨면 {a.seconds}초 안에 발판으로 옮기세요.",
         "왼쪽이 O, 오른쪽이 X 입니다.",
         f"틀리거나 고르지 않으면 목숨이 하나 줍니다. 목숨은 {a.lives}개입니다."],
        objectives=f"{len(questions)}문제를 목숨 {a.lives}개로 버틴다",
        portrait="Terran Civilian"))

    print("트리거를 짭니다...")
    cli.apply_triggers(build_triggers(a.players, questions, a.lives, a.seconds))
    if a.name:
        cli.set_map_name(a.name,
            f"OX 문제 {len(questions)}개. 문제가 뜨면 {a.seconds}초 안에 "
            f"왼쪽 O 나 오른쪽 X 발판으로 옮기세요. 틀리면 목숨이 하나 줍니다"
            f"({a.lives}개). 끝까지 살아남으면 이깁니다.")

    info = cli.info()
    print(f"\n만들었습니다: {a.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']} {info['version']}")
    print(f"  유닛 {info['units']}  트리거 {info['triggers']}  문제 {len(questions)}개")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
