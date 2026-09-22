#!/usr/bin/env python3
"""좀비 유즈맵 — **부품만으로 조립한다.**

이 파일은 새 장르를 부품으로 만들 수 있는지 보려고 쓴 것이다. 트리거를
직접 짜지 않고 `scmap.part_*` 를 이어 붙이기만 한다. 실제로 이 파일에는
트리거 글이 거의 없다 — 조립 순서와 맵 배치뿐이다.

좀비 맵의 알맹이는 **감염**이다 (실측 35장 중 91% 가 `Set Alliance
Status` 를 쓴다). 병력을 다 잃은 사람이 좀비 편으로 넘어가고, 넘어간
사람은 이제 남은 생존자를 쫓는다. 그래서 사람이 줄수록 좀비가 는다.

    ┌─────────────────────────────┐
    │  피난처   ·   벌판   ·  묘지 │
    └─────────────────────────────┘
      생존자 시작    싸움터    좀비가 나오는 곳

보기:
    python3 make_zombie.py out.scx --players 6 --survive 300
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

MARK = "Dark Swarm"          # 감염 표시 — 맵에 놓을 수 없는 스펠
SEEN = "Protoss Scarab"      # 안내를 한 번만 띄우기 위한 잠금
ZOMBIE_UNIT = "Zerg Zergling"
BOSS_ZOMBIE = "Torrasque (Ultralisk)"
SURVIVOR = "Terran Marine"

SHOPS = [("머린 4기", 100, "Terran Marine", 4),
         ("메딕 2기", 150, "Terran Medic", 2),
         ("파이어뱃 2기", 180, "Terran Firebat", 2)]


def build(a, enemy, boss_p, humans):
    """트리거를 **부품으로만** 조립한다."""
    T = []
    # 품질 바닥 — 하이퍼 트리거와 빈 슬롯 정리
    T += scmap.usemap_floor(a.players, enemy, min_players=1)

    T += scmap.part_intro(
        humans,
        [f"\\x04좀비\\x02 — \\x07{a.survive // 60}분\\x02 을 버티면 이깁니다.",
         "\\x03병력을 다 잃으면 감염되어 좀비가 됩니다.",
         "\\x03피난처 비콘에서 병력을 삽니다. 잡으면 돈이 들어옵니다."],
        ore=300,
        objectives=(f"\\x04좀비\\x02\\n\\x03- {a.survive // 60}분을 버틴다"
                    f"\\n- 병력을 다 잃으면 감염된다"
                    f"\\n- 피난처 비콘: 머린 100 · 메딕 150 · 파이어뱃 180"),
        counters={MARK: 0, SEEN: 0})

    T += scmap.part_leaderboard("\\x07잡은 수", kind="Kills")

    # 좀비는 계속 나오고 생존자를 쫓는다
    for k in range(a.waves):
        T += scmap.part_announce_once(
            humans, f'Elapsed Time(At least, {(k + 1) * a.survive // a.waves});',
            SEEN, k + 1,
            [f"\\x06좀비가 늘어납니다.\\x02 ({k + 1}/{a.waves})"])
    T.append(f'''Trigger("{enemy}"){{
Conditions:
\tBring("{enemy}", "{ZOMBIE_UNIT}", "Field", At most, {a.zombies // 2});

Actions:
\tCreate Unit("{enemy}", "{ZOMBIE_UNIT}", {a.zombies // 2}, "Graveyard");
\tPreserve Trigger();
}}''')
    # 좀비는 생존자를 **쫓아야** 한다 — patrol 이면 두 자리만 오가고,
    # move 면 도착해서 멈춘다. 놀이가 mode 를 정한다.
    T += scmap.part_patrol_path(enemy, ["Graveyard", "Field", "Shelter"], "attack")
    T += scmap.part_patrol_path(boss_p, ["Graveyard", "Field", "Shelter"], "attack")

    # 감염 — 이 장르의 알맹이
    T += scmap.part_infection(humans, enemy, MARK, ZOMBIE_UNIT, "Graveyard")

    # 피난처: 상점과 회복
    for i in range(a.players):
        p = f"Player {i + 1}"
        for k, (label, cost, unit, n) in enumerate(SHOPS):
            T += scmap.part_beacon_shop(
                p, f"Shop{k + 1}", cost,
                [f'\tCreate Unit("{p}", "{unit}", {n}, "P{i + 1} Home");'],
                label, push_to=f"P{i + 1} Home")
        T += scmap.part_heal_zone(p, "Shelter", cost=60,
                                  push_to=f"P{i + 1} Home")
        T.append(scmap.kill_bounty(p, 25, per_score=50))

    # 보스 좀비 — 절반쯤 지나면 나온다
    T.append(f'''Trigger("{boss_p}"){{
Conditions:
\tElapsed Time(At least, {a.survive // 2});
\tBring("{boss_p}", "Any unit", "Field", At most, 0);

Actions:
\tCreate Unit with Properties("{boss_p}", "{BOSS_ZOMBIE}", 1, "Graveyard", 1);
\tPreserve Trigger();
}}''')
    T += scmap.part_announce_once(
        humans, f'Elapsed Time(At least, {a.survive // 2});', SEEN,
        a.waves + 1, [f"\\x06보스 좀비가 나왔습니다."],
        wav="sound\\\\Zerg\\\\Ultra\\\\ZUlDth00.wav")

    # 버티면 이긴다
    T += scmap.part_survive_timer(humans, a.survive)
    return scmap.TRIGGER_SEP.join(T)


def main(argv=None):
    ap = argparse.ArgumentParser(description="좀비 유즈맵 (부품 조립)")
    ap.add_argument("out")
    ap.add_argument("--players", type=int, default=6)
    ap.add_argument("--survive", type=int, default=300, help="버틸 시간(초)")
    ap.add_argument("--waves", type=int, default=5, help="좀비가 느는 횟수")
    ap.add_argument("--zombies", type=int, default=24, help="벌판에 유지할 수")
    ap.add_argument("--size", default="128x128")
    ap.add_argument("--tileset", default="badlands", choices=sorted(TILESETS))
    ap.add_argument("--name", default="좀비")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    if not (2 <= a.players <= 6):
        ap.error("2~6명입니다 (좀비·보스로 슬롯 둘을 더 씁니다).")

    rng = random.Random(a.seed)
    W, H = (int(v) for v in a.size.lower().split("x"))
    ts = TILESETS[a.tileset]
    enemy_no, boss_no = a.players + 1, a.players + 2
    enemy, boss_p = f"Player {enemy_no}", f"Player {boss_no}"
    humans = [f"Player {p}" for p in range(1, a.players + 1)]

    print(f"좀비 {W}x{H} {a.tileset}, {a.players}명, {a.survive}초 버티기")
    print("  " + corpus.describe("usemap", a.tileset))
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)
    tiles = scmap.tileset_tiles(cli, ts)
    floor = next(t for g in corpus.pick_floor_groups(corpus.load(), ts, "usemap", 14)
                 for t in range(g * 16, g * 16 + 16)
                 if t in tiles and tiles[t][1] and tiles[t][2])

    # **필요한 만큼만 쓴다.** 피난처 · 벌판 · 묘지를 가로로 잇는다.
    SH_W, FIELD_W, GY_W, ROOM_H, GAP = 26, 44, 20, 40, 5
    used_w = SH_W + FIELD_W + GY_W + GAP * 2
    ox, oy = (W - used_w) // 2, (H - ROOM_H) // 2
    shelter = (ox, oy, SH_W, ROOM_H)
    field = (ox + SH_W + GAP, oy, FIELD_W, ROOM_H)
    grave = (ox + SH_W + GAP + FIELD_W + GAP, oy, GY_W, ROOM_H)

    print("벽을 세웁니다...")
    cli.edit("terrain", "fill", cli.path, "0", "0", str(W), str(H), str(VOID_TILE))
    fill = lambda r: cli.edit("terrain", "fill", cli.path, str(r[0]), str(r[1]),
                              str(r[2]), str(r[3]), str(floor))
    print(f"피난처 · 벌판 · 묘지를 뚫습니다 ({used_w}x{ROOM_H} 만 씁니다)...")
    for r in (shelter, field, grave):
        fill(r)
    # 이음 통로 — 좁게 내야 생존자가 버틸 자리가 생긴다
    for (a_, b_) in ((shelter, field), (field, grave)):
        cli.edit("terrain", "fill", cli.path, str(a_[0] + a_[2]),
                 str(oy + ROOM_H // 2 - 3), str(GAP), "6", str(floor))

    # 같은 지형 안의 **변종만** 흩는다. 그룹을 섞으면 얼룩덜룩한 무늬가
    # 생겨 네모난 방과 안 어울린다. 변종끼리는 게임 동작이 같아 밸런스에
    # 영향이 없다. 실제 사각 디펜스 맵도 타일 53~65종을 쓴다.
    n = scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)
    print(f"  바닥 변종 {n}칸")

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, a.players, [enemy_no, boss_no])

    print("로케이션을 놓습니다...")
    loc = lambda n, r: cli.edit("location", "add", cli.path, str(r[0]), str(r[1]),
                                str(r[0] + r[2]), str(r[1] + r[3]),
                                "--tiles", "--name", n)
    loc("Shelter", shelter); loc("Field", field); loc("Graveyard", grave)
    for i in range(a.players):
        hx = shelter[0] + 2 + (i % 2) * 11
        hy = shelter[1] + 3 + (i // 2) * 11
        cli.edit("location", "add", cli.path, str(hx), str(hy),
                 str(hx + 6), str(hy + 6), "--tiles", "--name", f"P{i + 1} Home")
    for k in range(len(SHOPS)):
        sx = shelter[0] + 3 + k * 7
        cli.edit("location", "add", cli.path, str(sx), str(shelter[1] + ROOM_H - 7),
                 str(sx + 4), str(shelter[1] + ROOM_H - 3),
                 "--tiles", "--name", f"Shop{k + 1}")

    print("유닛을 놓습니다...")
    for i in range(a.players):
        p = i + 1
        hx = shelter[0] + 4 + (i % 2) * 11
        hy = shelter[1] + 5 + (i // 2) * 11
        cli.place(scmap.START_LOCATION, hx, hy, owner=p)
        for k in range(6):
            cli.place(SURVIVOR, hx - 2 + k % 3, hy + k // 3, owner=p)
        cli.place("Terran Civilian", hx, hy + 3, owner=p)
    for k, (_l, _c, _u, _n) in enumerate(SHOPS):
        sx = shelter[0] + 5 + k * 7
        cli.place("Terran Beacon", sx, shelter[1] + ROOM_H - 5, owner=12)
        cli.place(("Terran Barracks", "Terran Academy",
                   "Terran Engineering Bay")[k], sx, shelter[1] + ROOM_H - 10,
                  owner=12)
    # 좀비와 보스도 스타팅이 있어야 슬롯이 산다
    cli.place(scmap.START_LOCATION, grave[0] + 3, grave[1] + 3, owner=enemy_no)
    cli.place(scmap.START_LOCATION, grave[0] + 3, grave[1] + ROOM_H - 4,
              owner=boss_no)
    for k in range(a.zombies // 2):
        cli.place(ZOMBIE_UNIT, grave[0] + 2 + k % 8,
                  grave[1] + 8 + (k // 8) * 3, owner=enemy_no)
    # 벌판에 가림막 — 트인 벌판이면 버틸 자리가 없다
    for k in range(10):
        cli.place("Protoss Pylon", field[0] + 6 + (k % 5) * 8,
                  field[1] + 8 + (k // 5) * 18, owner=12)

    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)

    print("브리핑을 짭니다...")
    cli.apply_briefing(scmap.briefing_text(
        [f"좀비가 묘지에서 몰려옵니다.",
         f"{a.survive // 60}분을 버티면 이깁니다.",
         "병력을 다 잃으면 감염되어 좀비 편이 됩니다.",
         "피난처 비콘에서 병력을 사고 체력을 채우세요."],
        objectives=f"{a.survive // 60}분을 버틴다", portrait=SURVIVOR))

    print("트리거를 짭니다 (부품 조립)...")
    cli.apply_triggers(build(a, enemy, boss_p, humans))
    if a.name:
        cli.set_map_name(a.name,
            f"{a.players}명이 {a.survive // 60}분을 버팁니다. 병력을 다 잃으면 "
            f"감염되어 좀비 편이 됩니다. 피난처 비콘에서 병력을 삽니다.")

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
