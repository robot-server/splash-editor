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

질문·답, 문구, 플레이 수, 목숨, 제한 시간은 AI가 프로필 JSON에 쓴다.

보기:
    python3 make_quiz.py out.scx --config recipe_profiles/your_quiz_profile.json
"""
from __future__ import annotations

import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError
import recipe_config as profile

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}
def build_triggers(cfg, questions, labels):
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
    players=cfg["rules"]["players"]
    lives=cfg["rules"]["lives"]
    secs=cfg["rules"]["seconds"]
    units=cfg["units"]
    msg=cfg["text"]["messages"]
    LIFE,QNUM,SHOWN,JUDGED=(units[k] for k in ("life_counter","question_counter","shown_counter","judged_counter"))
    T = []
    add = T.append
    HUMANS = ",".join(f'"Player {p}"' for p in range(1, players + 1))
    nq = len(questions)
    # 문제 제한은 안내한 실제 초에 맞춘다. 카운트다운 칸에는 변환된 값이 보인다.
    timer_ticks = scmap.game_ticks(secs)

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
\tSet Resources("Current Player", Set To, {cfg["starting_resources"]["minerals"]}, ore);
\tSet Resources("Current Player", Set To, {cfg["starting_resources"]["gas"]}, gas);
\tDisplay Text Message(Always Display, "{msg["start"].format(questions=nq,lives=lives,seconds=secs)}");
\tDisplay Text Message(Always Display, "{msg["directions"].format(questions=nq,lives=lives,seconds=secs)}");
\tSet Mission Objectives("{cfg["text"]["objectives"]}");
\tSet Countdown Timer(Set To, {timer_ticks});
}}''')

    add(f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tLeader Board Points("{msg["leaderboard"]}", Custom);
\tPreserve Trigger();
}}''')

    # 문제 번호를 올리는 시계 — 컴퓨터가 돌린다 (안내는 하지 않는다)
    add(f'''Trigger("Player 8"){{
Conditions:
\tCountdown Timer(At most, 0);
\tDeaths("Player 8", "{QNUM}", At most, {nq});

Actions:
\tSet Deaths("Player 8", "{QNUM}", Add, 1);
\tSet Countdown Timer(Set To, {timer_ticks});
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
\tDisplay Text Message(Always Display, "{msg["question"].format(number=i,prompt=q,answer=ans)}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
        # 판정 — 다음 문제로 넘어가는 순간, 사람마다 **한 번만**.
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", At least, {i + 1});
\tDeaths("Current Player", "{JUDGED}", At most, {i - 1});
\tBring("Current Player", "{units["selection"]}", "{labels["pad_o"] if ans == "O" else labels["pad_x"]}", At least, 1);

Actions:
\tSet Deaths("Current Player", "{JUDGED}", Set To, {i});
\tSet Score("Current Player", Add, 1, Custom);
\tDisplay Text Message(Always Display, "{msg["correct"].format(number=i,answer=ans)}");
\tMove Unit("Current Player", "{units["selection"]}", All, "{labels["pad_o"] if ans == "O" else labels["pad_x"]}", "{labels["lobby"]}");
\tPreserve Trigger();
}}''')
        add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", At least, {i + 1});
\tDeaths("Current Player", "{JUDGED}", At most, {i - 1});
\tBring("Current Player", "{units["selection"]}", "{labels["pad_o"] if wrong == "O" else labels["pad_x"]}", At least, 1);

Actions:
\tSet Deaths("Current Player", "{JUDGED}", Set To, {i});
\tSet Deaths("Current Player", "{LIFE}", Subtract, 1);
\tDisplay Text Message(Always Display, "{msg["wrong"].format(number=i,answer=ans)}");
\tPlay WAV("sound\\\\Misc\\\\PowerDown.wav", 500);
\tMove Unit("Current Player", "{units["selection"]}", All, "{labels["pad_o"] if wrong == "O" else labels["pad_x"]}", "{labels["lobby"]}");
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
\tDisplay Text Message(Always Display, "{msg["no_answer"].format(number=i,answer=ans)}");
\tPreserve Trigger();
}}''')

    add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Current Player", "{LIFE}", At most, 0);

Actions:
\tDisplay Text Message(Always Display, "{msg["out_of_lives"]}");
\tDefeat();
}}''')
    add(f'''Trigger({HUMANS}){{
Conditions:
\tDeaths("Player 8", "{QNUM}", At least, {nq + 1});
\tDeaths("Current Player", "{JUDGED}", At least, {nq});
\tDeaths("Current Player", "{LIFE}", At least, 1);

Actions:
\tDisplay Text Message(Always Display, "{msg["survived"]}");
\tVictory();
}}''')
    return scmap.TRIGGER_SEP.join(T)


def main(argv=None):
    ap = argparse.ArgumentParser(description="AI profile-driven answer-platform quiz")
    ap.add_argument("out")
    profile.add_profile_arguments(ap,"quiz")
    ap.add_argument("--players",type=int)
    ap.add_argument("--lives",type=int)
    ap.add_argument("--seconds",type=int)
    ap.add_argument("--install",default=None)
    ap.add_argument("--force",action="store_true")
    a=ap.parse_args(argv)
    cfg=profile.load_profile(a.config,"quiz")
    for arg,key in (("players","players"),("lives","lives"),("seconds","seconds")):
        if getattr(a,arg) is None: setattr(a,arg,cfg["rules"][key])
        cfg["rules"][key]=getattr(a,arg)
    if not 1 <= a.players <= 7:
        ap.error("1~7명입니다 (진행용으로 슬롯 하나를 더 씁니다).")
    questions=[(q["prompt"].replace('"',"'").replace("\n"," "),q["answer"]) for q in cfg["questions"]]
    W,H=cfg["map"]["size"]
    a.seed=cfg["map"]["seed"]
    a.tileset=cfg["map"]["tileset"]
    labels=cfg["labels"]
    units=cfg["units"]
    rng=random.Random(a.seed)
    ts=TILESETS[a.tileset]

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    if not (1 <= a.players <= 7):
        ap.error("1~7명입니다 (진행용으로 슬롯 하나를 더 씁니다).")

    print(f"OX 퀴즈 {W}x{H} {a.tileset}, {a.players}명, 문제 {len(questions)}개")
    cli = scmap.new_map(a.out,W,H,ts,terrain=None,melee=False,install=a.install)
    profile.apply_unit_names(cli,cfg)

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
    # **맵 크기에 맞춰 늘린다.** 앞서 14x13 로 못 박아 두었더니 64x64
    # 맵에서 35x13 만 쓰고 나머지가 텅 빈 바닥이 되었다. 벽으로 덮어
    # 가릴 수도 없다 — 못 걷는 지형이 66% 를 넘으면 튕긴다.
    #
    # 좌우로 몇 칸이면 닿아야 하는 놀이라 발판은 지나치게 넓히지 않고,
    # 대신 **세로를 맵에 맞춰** 길게 뽑는다.
    MID_W = max(7, W // 8)
    PAD_W = max(14, (W - 2 * 3 - MID_W) // 2)
    PAD_H = max(13, H - 2 * 3)
    used_w = PAD_W * 2 + MID_W
    used_h = PAD_H
    ox = (W - used_w) // 2
    oy = (H - used_h) // 2
    pad_o = (ox, oy, PAD_W, PAD_H)
    lobby = (ox + PAD_W, oy, MID_W, PAD_H)
    pad_x = (ox + PAD_W + MID_W, oy, PAD_W, PAD_H)

    # 바닥·통로·테두리·발판·벽은 이동과 시각 안내 역할에 맞춰 구분한다.
    pal = scmap.Palette(cli, ts, rng, "usemap")
    print(f"  지형: {pal.describe()}")

    print("벽을 세웁니다...")
    scmap.cover_map(cli, pal, W, H, margin=2)
    print(f"O · 대기 · X 를 나란히 뚫습니다 ({used_w}x{used_h} 만 씁니다)...")
    # 발판 둘은 방으로, 가운데 대기 통로는 통로 지형으로 — 셋이 눈에
    # 따로 보여야 어디가 O 이고 어디가 기다리는 자리인지 읽힌다.
    for r in (pad_o, pad_x):
        scmap.room(cli, pal, r[0], r[1], r[2], r[3], rim=1, wall=True)
    pal.fill(cli, "path", lobby[0], lobby[1], lobby[2], lobby[3])

    # 같은 지형 안의 **변종만** 흩는다. 그룹을 섞으면 얼룩덜룩한 덩이
    # 무늬가 생겨 네모난 방과 안 어울린다 — 변종은 잔 알갱이만 남는다.
    # 실제 사각 디펜스 맵도 타일 53~65종을 쓴다.
    scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, a.players, [8], race=cfg["players"]["race"])

    print("로케이션을 놓습니다...")
    loc = lambda n, r: cli.edit("location", "add", cli.path, str(r[0]), str(r[1]),
                                str(r[0] + r[2]), str(r[1] + r[3]),
                                "--tiles", "--name", n)
    loc(labels["lobby"], lobby)
    loc(labels["pad_o"], pad_o)
    loc(labels["pad_x"], pad_x)
    for p in range(1, a.players + 1):
        sy = lobby[1] + 1 + (p - 1) * (lobby[3] - 3) // max(1, a.players)
        cli.edit("location", "add", cli.path, str(lobby[0] + 1), str(sy),
                 str(lobby[0] + lobby[2] - 1), str(sy + 2),
                 "--tiles", "--name", f"{labels["start_prefix"]} {p}")

    print("유닛을 놓습니다...")
    for p in range(1, a.players + 1):
        sy = lobby[1] + 2 + (p - 1) * (lobby[3] - 3) // max(1, a.players)
        cli.place(scmap.START_LOCATION, lobby[0] + lobby[2] // 2, sy, owner=p)
        cli.place(units["selection"], lobby[0] + lobby[2] // 2, sy, owner=p)
    # **발판에 O 와 X 를 지형으로 그린다.**
    #
    # 앞서 파일런을 글자 모양으로 늘어놓았다. 보이기는 했지만 **유닛이라
    # 길을 막아** 발판 안에서 걸어다니지를 못했다 — 열두 초 안에 옮겨야
    # 하는 놀이에서 치명적이다. 글자는 밟고 지나갈 수 있어야 한다.
    for (r, mark) in ((pad_o, "O"), (pad_x, "X")):
        n = scmap.stamp_glyph(cli, pal, mark, r[0], r[1], r[2], r[3],
                              role="pad", thick=2)
        print(f"  '{mark}' 를 지형 {n}칸으로 그렸습니다")

    # 비콘은 발판 **위쪽 끝**에 둔다. 글자 위에 겹치면 둘 다 안 읽힌다.
    for (r, mark) in ((pad_o, "O"), (pad_x, "X")):
        cx = r[0] + r[2] // 2
        scmap.pad(cli, pal, cx, r[1] + 1, 3, 2)
        cli.place(units["o_marker"] if mark == "O" else units["x_marker"], cx, r[1] + 1, owner=12)

    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)


    # 방 테두리 두뎃은 실제 보행·시야·배치 검증 뒤 선택한다.
    # 두뎃 타일을 쓰고, 중앙 868칸이며 그 89%가 걷기 경계 두 칸 안에
    # 몰려 있다. 내 맵은 0칸이었다 — 그림으로 보고서야 알았다.
    # 걷기를 막는 두뎃은 `data/doodad-walk.json` 을 보고 걸러 낸다.
    print("방 테두리를 두뎃으로 꾸밉니다...")
    _clear = [(u["x"] // 32 - 2, u["y"] // 32 - 2, 5, 5) for u in cli.units()]
    _nd = scmap.decorate_rim(cli, ts, [pad_o, pad_x], rng, keep_clear=_clear)
    print(f"  두뎃 {_nd}개")

    print("트리거를 짭니다...")
    cli.apply_triggers(build_triggers(cfg,questions,labels))
    profile.apply_profile_metadata(cli,cfg,a.players)

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
