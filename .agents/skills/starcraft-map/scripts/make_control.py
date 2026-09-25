#!/usr/bin/env python3
"""컨트롤(전투) 유즈맵 — 가운데 싸움터에 스폰 주머니를 둘러 놓는다.

디펜스가 "막는" 고리라면 컨트롤은 "싸우는" 고리다. 되먹임이 훨씬 짧다 —
죽으면 바로 다시 받고, 잡으면 바로 점수가 오른다. 실측에서 컨트롤 맵의
트리거 수가 디펜스 다음으로 많은 까닭이다.

    ┌───────────────────────────┐
    │  ▣        싸움터        ▣ │   ▣ = 스폰 주머니 (플레이어마다)
    │        (장애물 몇 개)      │   가운데는 트여 있어야 싸움이 난다
    │  ▣                     ▣ │   주머니는 통로로 싸움터와 이어진다
    └───────────────────────────┘

점수는 게임이 세 주는 **죽인 수**를 쓴다 (`Leader Board Kills`,
`Kill` 조건). 직접 세려고 `Set Deaths` 를 쓰면 누가 잡았는지 못 가린다.

보기:
    python3 make_control.py out.scx --config recipe_profiles/your_control_profile.json
"""
from __future__ import annotations

import argparse
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError
import recipe_config as profile

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}
def pocket_spots(players, W, H, pocket, margin=3):
    """스폰 주머니를 가장자리에 고르게 돌려 놓는다."""
    cx, cy = W / 2.0, H / 2.0
    rx, ry = cx - pocket / 2 - margin, cy - pocket / 2 - margin
    out = []
    for i in range(players):
        a = -math.pi / 2 + 2 * math.pi * i / players
        x = int(cx + rx * math.cos(a) - pocket / 2)
        y = int(cy + ry * math.sin(a) - pocket / 2)
        out.append((max(margin, min(W - pocket - margin, x)),
                    max(margin, min(H - pocket - margin, y))))
    return out



def build_triggers(cfg, arena_names):
    players=cfg["rules"]["players"]
    system_player=f"Player {players+1}"
    goal=cfg["rules"]["goal"]
    respawn_s=cfg["rules"]["respawn_seconds"]
    squads=cfg["squads"]
    starter=squads[cfg["starting_squad"]]
    money=cfg["rules"]["buy_cost"]
    bounty=cfg["rules"]["bounty_per_kill"]
    bounty_score=cfg["rules"]["bounty_score_step"]
    heal_cost=cfg["rules"]["heal_cost"]
    msg=cfg["text"]["messages"]
    T = []
    add = T.append
    # **하이퍼 트리거를 맨 앞에.** 없으면 트리거가 1초에 한 번만 돌아
    # 비콘·스폰·판정이 모두 한 박자 늦는다. 유즈맵에 거의 필수다.
    T.extend(scmap.hyper_triggers(system_player))
    # 들어오지 않은 자리를 치운다 (빈 주머니의 유닛이 남지 않게)
    T.extend(scmap.absent_player_cleanup(players, system_player))
    HUMANS = ",".join(f'"Player {p}"' for p in range(1, players + 1))

    add(f'''Trigger({HUMANS}){{
Conditions:
\tAlways();

Actions:
\tSet Resources("Current Player", Set To, {cfg["starting_resources"]["minerals"]}, ore);
\tSet Resources("Current Player", Set To, {cfg["starting_resources"]["gas"]}, gas);
\tDisplay Text Message(Always Display, "{msg["start"]}");
\tDisplay Text Message(Always Display, "{msg["controls"]}");
\tSet Countdown Timer(Set To, {respawn_s});
}}''')

    # 순위표 — 게임이 세 주는 죽인 수를 쓴다
    add(f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tLeader Board Kills("{msg["leaderboard"]}", "Any unit");
\tPreserve Trigger();
}}''')

    # 되살리기 — 주머니가 비면 채워 준다
    for i, a in enumerate(arena_names):
        p = f"Player {i + 1}"
        add(f'''Trigger("{p}"){{
Conditions:
\tCountdown Timer(At most, 0);
\tCommand("{p}", "Men", At most, 2);

Actions:
\tCreate Unit("{p}", "{starter["unit"]}", {starter["count"]}, "{a} {cfg["labels"]["spawn_suffix"]}");
\tDisplay Text Message(Always Display, "{msg["respawn"]}");
\tCenter View("{a} {cfg["labels"]["spawn_suffix"]}");
\tPreserve Trigger();
}}''')
        # 병력 고르기 — 비콘마다 다른 부대.
        # **산 뒤에 비콘 밖으로 밀어낸다** (하이퍼와 맞물린 연사 방지).
        for k, squad in enumerate(squads):
            add(f'''Trigger("{p}"){{
Conditions:
\tBring("{p}", "Any unit", "{a} {cfg["labels"]["buy_prefix"]}{k + 1}", At least, 1);
\tAccumulate("{p}", At least, {money}, ore);

Actions:
\tSet Resources("{p}", Subtract, {money}, ore);
\tCreate Unit("{p}", "{squad["unit"]}", {squad["count"]}, "{a} {cfg["labels"]["gate_suffix"]}");
\tMove Unit("{p}", "Men", All, "{a} {cfg["labels"]["buy_prefix"]}{k + 1}", "{a} {cfg["labels"]["spawn_suffix"]}");
\tDisplay Text Message(Always Display, "{squad["receipt"]}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
        # **킬마다 돈을 주지 않는다.** Kill(..., At least, 1) 은 누적
        # 조건이라 Preserve 와 함께 쓰면 첫 킬 뒤 매 순회마다 들어온다
        # (무한 돈). 죽은 수 소비 관용구는 "누가 잡았는지" 를 못 가리므로
        # 여기서는 시간 수입으로 준다. 누가 잘하는지는 순위표가 보여 준다.
        # 잡은 만큼만 준다 — **킬 스코어를 깎는 관용구.** 죽은 수를
        # 소비하는 방법과 달리 누가 잡았는지 가려진다.
        add(scmap.kill_bounty(p, bounty, per_score=bounty_score))
        # **제 주머니로 물러나면 공짜로 낫는다.** 안 그러면 한 번 깎인
        # 체력이 끝까지 그대로라 초반에 진 사람은 구경만 하게 된다
        # (실측 유즈맵 89%가 체력·에너지를 고쳐 준다). 값이 0이라
        # 밀어내기가 필요 없다 — 서 있어도 잃을 것이 없다.
        T.extend(scmap.part_heal_zone(p, f"{a} {cfg['labels']['spawn_suffix']}", cost=heal_cost))
        # 이김
        add(f'''Trigger("{p}"){{
Conditions:
\tKill("{p}", "Any unit", At least, {goal});

Actions:
\tDisplay Text Message(Always Display, "{msg["victory"]}");
\tVictory();
}}''')

    # 되살리기 시계
    add(f'''Trigger("All players"){{
Conditions:
\tCountdown Timer(At most, 0);

Actions:
\tSet Countdown Timer(Set To, {respawn_s});
\tPreserve Trigger();
}}''')
    return "\n\n//-----------------------------------------------------------------//\n\n".join(T)


def main(argv=None):
    ap = argparse.ArgumentParser(description="컨트롤(전투) 유즈맵")
    ap.add_argument("out")
    profile.add_profile_arguments(ap,"control")
    ap.add_argument("--players", type=int)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    cfg=profile.load_profile(a.config,"control")
    a.players=cfg["rules"]["players"]
    a.goal=cfg["rules"]["goal"]
    a.respawn=cfg["rules"]["respawn_seconds"]
    width,height=cfg["map"]["size"]
    a.size=f"{width}x{height}"
    a.tileset=cfg["map"]["tileset"]
    a.seed=cfg["map"]["seed"]

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    if not (2 <= a.players <= 8):
        ap.error("2~8명입니다.")

    rng = random.Random(a.seed)
    W, H = (int(v) for v in a.size.lower().split("x"))
    ts = TILESETS[a.tileset]
    POCKET = cfg["rules"]["pocket_size"]

    print(f"컨트롤 {W}x{H} {a.tileset}, {a.players}명, {a.goal}킬")
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    # 바닥·통로·테두리·발판·벽은 이동과 시각 안내 역할에 맞춰 구분한다.
    pal = scmap.Palette(cli, ts, rng, "usemap")
    print(f"  지형: {pal.describe()}")

    spots = pocket_spots(a.players, W, H, POCKET)
    cx, cy = W // 2, H // 2
    AR = min(W, H) // 2 - POCKET - 4          # 싸움터 반지름

    print("벽을 세웁니다...")
    scmap.cover_map(cli, pal, W, H)

    # 싸움터를 둥글게 뚫는다 (네모로 뚫으면 형상 검사에 걸린다).
    # 가장자리 한 칸은 테두리 지형으로 둘러 방처럼 읽히게 한다.
    print("싸움터를 뚫습니다...")
    grid = cli.tiles(0, 0, W, H)
    for y in range(H):
        for x in range(W):
            d = math.hypot(x - cx, (y - cy) * 1.15)
            wob = 3.0 * math.sin(math.atan2(y - cy, x - cx) * 3 + 1.1)
            if d < AR + wob - 1.5:
                grid[y][x] = pal.tile("floor")
            elif d < AR + wob:
                grid[y][x] = pal.tile("rim")
    cli.paste_tiles(0, 0, grid)

    print(f"스폰 주머니 {len(spots)}개와 통로를 뚫습니다...")
    regions = [(max(0, cx - AR - 4), max(0, cy - AR - 4),
                min(W, 2 * AR + 8), min(H, 2 * AR + 8))]
    for (px, py) in spots:
        scmap.room(cli, pal, px, py, POCKET, POCKET, rim=1)
        regions.append((px, py, POCKET, POCKET))
        # 주머니 → 싸움터 통로.
        #
        # **싸움터 안까지 그리면 안 된다.** 통로 지형이 바닥과 다른 색이
        # 되고 나서 알았다 — 여섯 통로가 가운데까지 뻗어 둥근 싸움터가
        # 여섯 조각 별 모양으로 갈렸다. 앞서는 통로와 바닥이 같은 타일
        # 이라 안 보였을 뿐 지형은 똑같이 잘못이었다.
        #
        # 테두리를 뚫고 들어가 **가장자리에서 멈춘다.**
        sx, sy = px + POCKET // 2, py + POCKET // 2
        steps = int(math.hypot(sx - cx, sy - cy))
        for t in range(steps + 1):
            gx = int(sx + (cx - sx) * t / max(1, steps))
            gy = int(sy + (cy - sy) * t / max(1, steps))
            if math.hypot(gx - cx, (gy - cy) * 1.15) < AR - 2:
                break
            # **주머니 안은 건드리지 않는다.** 방 가운데에서 시작해 그리니
            # 통로가 방을 가로질러 검은 골처럼 그려졌다. 테두리만 뚫고
            # 나간다.
            if (px + 1 <= gx - 3 and gx + 3 <= px + POCKET - 2
                    and py + 1 <= gy - 3 and gy + 3 <= py + POCKET - 2):
                continue
            pal.fill(cli, "path", max(0, gx - 3), max(0, gy - 3), 7, 7)


    # 같은 지형 안의 **변종만** 흩는다. 그룹을 섞으면 얼룩덜룩한 덩이
    # 무늬가 생겨 네모난 방과 안 어울린다 — 변종은 잔 알갱이만 남는다.
    # 실제 사각 디펜스 맵도 타일 53~65종을 쓴다.
    scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)

    print("플레이어 슬롯을 정합니다...")
    system_player=a.players+1
    scmap.setup_usemap_players(cli, a.players, [system_player], race=cfg["players"]["race"])
    if system_player > 8:
        cli.edit("player", "set", cli.path, str(system_player),
                 "--race", cfg["players"]["race"], "--slot", "computer")

    # 컨트롤 맵은 업그레이드를 **미리 다 해 둔다.** 파는 맵이 아니라
    # 순수하게 조작을 겨루는 맵이라, 업그레이드 차이가 나면 안 된다
    # (docs/chk/anatomy.md — 유즈맵은 업그레이드를 고친다).
    names={}
    for squad in cfg["squads"]:
        for unit,name in ((squad["unit"],squad["unit_name"]),
                          (squad["marker"],squad["marker_name"])):
            if unit in names and names[unit] != name:
                raise CliError(f"유닛 타입 {unit} 에 서로 다른 맵 이름이 지정됐습니다")
            names[unit]=name
    name_cfg=dict(cfg)
    name_cfg["unit_names"]=names
    profile.apply_unit_names(cli,name_cfg)
    print("로케이션을 놓습니다...")
    loc = lambda n, x0, y0, x1, y1: cli.edit(
        "location", "add", cli.path, str(x0), str(y0), str(x1), str(y1),
        "--tiles", "--name", n)
    names = []
    for i, (px, py) in enumerate(spots):
        A = f"{cfg['labels']['player_prefix']}{i + 1}"
        names.append(A)
        loc(f"{A} {cfg['labels']['spawn_suffix']}", px + 2, py + 2, px + 8, py + 8)
        loc(f"{A} {cfg['labels']['gate_suffix']}", px + POCKET // 2 - 3, py + POCKET - 6,
            px + POCKET // 2 + 3, py + POCKET - 1)
        for k in range(len(cfg["squads"])):
            bx = px + 2 + (k % 2) * 9
            by = py + 11 + (k // 2) * 5
            loc(f"{A} {cfg['labels']['buy_prefix']}{k + 1}", bx, by, bx + 4, by + 3)
        loc(f"{A} {cfg['labels']['all_suffix']}", px, py, px + POCKET, py + POCKET)
    loc(cfg["labels"]["arena"], cx - AR, cy - AR, cx + AR, cy + AR)

    print("유닛을 놓습니다...")
    for i, (px, py) in enumerate(spots):
        p = i + 1
        cli.place(scmap.START_LOCATION, px + 5, py + 5, owner=p)
        squad=cfg["squads"][cfg["starting_squad"]]
        for k in range(squad["count"]):
            cli.place(squad["unit"],px+3+k,py+8,owner=p)
        for k,squad in enumerate(cfg["squads"]):
            bx = px + 4 + (k % 2) * 9
            by = py + 12 + (k // 2) * 5
            scmap.pad(cli, pal, bx, by, 3, 3)
            cli.place(cfg["units"]["purchase_beacon"],bx,by,owner=p)
            cli.place(squad["marker"],bx,by-3,owner=12)
    # 싸움터 가운데 장애물 — 트인 벌판이면 컨트롤이 안 나온다
    for k in range(cfg["rules"]["obstacle_count"]):
        ang = 2 * math.pi * k / max(1,cfg["rules"]["obstacle_count"])
        ox = int(cx + AR * 0.45 * math.cos(ang))
        oy = int(cy + AR * 0.45 * math.sin(ang))
        cli.place(cfg["units"]["arena_obstacle"], ox, oy, owner=12)

    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)


    # 방 테두리 두뎃은 실제 보행·시야·배치 검증 뒤 선택한다.
    # 두뎃 타일을 쓰고, 중앙 868칸이며 그 89%가 걷기 경계 두 칸 안에
    # 몰려 있다. 내 맵은 0칸이었다 — 그림으로 보고서야 알았다.
    # 걷기를 막는 두뎃은 `data/doodad-walk.json` 을 보고 걸러 낸다.
    print("방 테두리를 두뎃으로 꾸밉니다...")
    _clear = [(u["x"] // 32 - 2, u["y"] // 32 - 2, 5, 5) for u in cli.units()]
    _nd = scmap.decorate_rim(cli, ts, [(px, py, POCKET, POCKET) for (px, py) in spots], rng, keep_clear=_clear)
    print(f"  두뎃 {_nd}개")

    print("트리거를 짭니다...")
    cli.apply_triggers(build_triggers(cfg,names))
    profile.apply_profile_metadata(cli,cfg,a.players)

    info = cli.info()
    print(f"\n만들었습니다: {a.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']} {info['version']}")
    print(f"  유닛 {info['units']}  트리거 {info['triggers']}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
