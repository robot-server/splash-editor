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
QNUM = "Scanner Sweep"       # 문제 번호


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
    T = []
    add = T.append
    HUMANS = ",".join(f'"Player {p}"' for p in range(1, players + 1))
    nq = len(questions)

    # 하이퍼 트리거 — 없으면 발판 판정이 한 박자 늦는다
    add(scmap.hyper_trigger())

    add(f'''Trigger({HUMANS}){{
Conditions:
\tAlways();

Actions:
\tSet Deaths("Current Player", "{LIFE}", Set To, {lives});
\tSet Score("Current Player", Set To, 0, Custom);
\tDisplay Text Message(Always Display, "\\x04OX 퀴즈\\x02 — 문제 {nq}개. 목숨 \\x07{lives}개\\x02.");
\tDisplay Text Message(Always Display, "\\x03문제가 뜨면 {secs}초 안에 \\x04왼쪽 O\\x02 나 \\x06오른쪽 X\\x02 발판으로 옮기세요.");
\tSet Countdown Timer(Set To, {secs});
}}''')

    add('''Trigger("All players"){
Conditions:
\tAlways();

Actions:
\tLeader Board Points("\\x07맞힌 수", Custom);
\tPreserve Trigger();
}''')

    # 시계가 끝나면 다음 문제로
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
        # 문제를 띄운다 (번호가 바뀌는 순간 한 번)
        add(f'''Trigger("Player 8"){{
Conditions:
\tDeaths("Player 8", "{QNUM}", Exactly, {i});
\tSwitch("Switch {i}", not set);

Actions:
\tSet Switch("Switch {i}", set);
\tDisplay Text Message(Always Display, "\\x07{i}번.\\x02 {q}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
        # 판정 — 다음 문제로 넘어가는 순간, 틀린 발판에 있으면 목숨을 깎는다
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", Exactly, {i + 1});
\tBring("Current Player", "Any unit", "Pad {wrong}", At least, 1);

Actions:
\tSet Deaths("Current Player", "{LIFE}", Subtract, 1);
\tDisplay Text Message(Always Display, "\\x06{i}번 틀렸습니다.\\x02 답은 {ans} 입니다.");
\tPlay WAV("sound\\\\Misc\\\\PowerDown.wav", 500);
\tPreserve Trigger();
}}''')
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", Exactly, {i + 1});
\tBring("Current Player", "Any unit", "Pad {ans}", At least, 1);

Actions:
\tSet Score("Current Player", Add, 1, Custom);
\tDisplay Text Message(Always Display, "\\x07{i}번 정답!");
\tPreserve Trigger();
}}''')
        # 다음 문제로 넘어가면 대기 구역으로 되돌린다
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", Exactly, {i + 1});

Actions:
\tMove Unit("Current Player", "Any unit", All, "Pad O", "Lobby");
\tMove Unit("Current Player", "Any unit", All, "Pad X", "Lobby");
\tPreserve Trigger();
}}''')

    # 목숨이 다하면 진다
    add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Current Player", "{LIFE}", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "\\x06목숨이 다했습니다.");
\tDefeat();
}}''')
    # 끝까지 살아남으면 이긴다
    add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", At least, {nq + 1});
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
    ap.add_argument("--size", default="96x96")
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

    M, GAP = 4, 4
    lobby = (M, M, W - 2 * M, (H - 2 * M - GAP) // 2)
    pad_h = H - 2 * M - GAP - lobby[3]
    pad_w = (W - 2 * M - GAP) // 2
    pad_y = M + lobby[3] + GAP
    pad_o = (M, pad_y, pad_w, pad_h)
    pad_x = (M + pad_w + GAP, pad_y, pad_w, pad_h)

    print("벽을 세웁니다...")
    cli.edit("terrain", "fill", cli.path, "0", "0", str(W), str(H), str(VOID_TILE))
    fill = lambda r: cli.edit("terrain", "fill", cli.path,
                              str(r[0]), str(r[1]), str(r[2]), str(r[3]), str(floor))
    print("대기 구역과 발판 둘을 뚫습니다...")
    for r in (lobby, pad_o, pad_x):
        fill(r)
    # 대기 구역 → 발판 통로 (발판마다 따로 — 한번 고르면 건너갈 수 없게)
    for (px, _, pw, _) in (pad_o, pad_x):
        cli.edit("terrain", "fill", cli.path, str(px + pw // 2 - 3),
                 str(M + lobby[3]), "6", str(GAP), str(floor))

    print("바닥을 칠합니다...")
    scmap.paint_floor_mixed(cli, ts, rng, [lobby, pad_o, pad_x], groups)
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
        sx = lobby[0] + 3 + (p - 1) * (lobby[2] - 6) // max(1, a.players)
        cli.edit("location", "add", cli.path, str(sx), str(lobby[1] + 2),
                 str(sx + 3), str(lobby[1] + 5), "--tiles", "--name", f"P{p} Start")

    print("유닛을 놓습니다...")
    for p in range(1, a.players + 1):
        sx = lobby[0] + 4 + (p - 1) * (lobby[2] - 6) // max(1, a.players)
        cli.place(scmap.START_LOCATION, sx, lobby[1] + 3, owner=p)
        cli.place("Terran Civilian", sx, lobby[1] + 3, owner=p)
    cli.place(scmap.START_LOCATION, lobby[0] + 1, lobby[1] + 1, owner=8)
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

    print("트리거를 짭니다...")
    cli.apply_triggers(build_triggers(a.players, questions, a.lives, a.seconds))
    if a.name:
        cli.set_map_name(a.name)

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
