#!/usr/bin/env python3
"""키우기(RPG) 유즈맵 — 마을에서 나가 구역을 하나씩 깨며 세진다.

디펜스가 "막는" 고리, 컨트롤이 "싸우는" 고리라면 키우기는 **"세지는"**
고리다. 되먹임이 길게 쌓인다 — 잡으면 돈, 돈으로 강화, 강해지면 다음
구역. 그래서 구역마다 눈에 보이는 문턱이 있어야 한다.

    ┌─────┐   ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐
    │ 마을 ├───┤ 1구역 ├─┤ 2구역 ├─┤ 3구역 ├─┤ 보스 │
    └─────┘   └──────┘ └──────┘ └──────┘ └──────┘
      상점      약함      보통     강함      끝

마을은 안전하다 — 몬스터가 안 나오고 죽으면 여기로 돌아온다.
구역은 통로로 이어져 있어 언제든 물러설 수 있다.

보기:
    python3 make_rpg.py out.scx --players 4 --zones 4
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

# 구역마다 나오는 것과 값. 뒤로 갈수록 세지고 비싸진다.
ZONES = [
    ("Zerg Zergling",   10, 20, "저글링 굴"),
    ("Zerg Hydralisk",   8, 45, "히드라 둥지"),
    ("Protoss Zealot",   6, 80, "질럿 성채"),
    ("Terran Goliath",   5, 140, "골리앗 기지"),
    ("Protoss Dragoon",  5, 220, "드라군 신전"),
]
BOSS = ("Torrasque (Ultralisk)", "토라스크")

HEAL_COST = 60                        # 구역 회복 발판 값
HERO = "Jim Raynor (Marine)"          # 실측 유즈맵 49% 가 영웅을 쓴다
SHOPS = [("Terran Engineering Bay", "공격력", 200),
         ("Terran Armory", "방어력", 250),
         ("Protoss Forge", "체력", 300)]


def build_triggers(players, nzones, enemy, boss_p, town_h):
    T = []
    add = T.append
    # **하이퍼 트리거를 맨 앞에.** 없으면 트리거가 1초에 한 번만 돌아
    # 비콘·스폰·판정이 모두 한 박자 늦는다. 유즈맵에 거의 필수다.
    T.extend(scmap.hyper_triggers(enemy))
    # 들어오지 않은 자리를 치운다
    T.extend(scmap.absent_player_cleanup(players, enemy))
    HUMANS = ",".join(f'"Player {p}"' for p in range(1, players + 1))

    add(f'''Trigger({HUMANS}){{
Conditions:
\tAlways();

Actions:
\tSet Resources("Current Player", Set To, 100, ore);
\tSet Score("Current Player", Set To, 0, Custom);
\tDisplay Text Message(Always Display, "\\x04키우기\\x02 — 마을 오른쪽으로 나가 구역을 하나씩 깨세요.");
\tDisplay Text Message(Always Display, "\\x03잡으면 돈이 들어옵니다. 마을 비콘에서 강화하세요. 죽으면 마을로 돌아옵니다.");
}}''')

    add(f'''Trigger("All players"){{
Conditions:
\tAlways();

Actions:
\tLeader Board Points("\\x07점수", Custom);
\tPreserve Trigger();
}}''')

    # 구역마다: 몬스터가 줄면 다시 채운다
    for z in range(nzones):
        unit, n, pay, label = (*ZONES[z % len(ZONES)][:3], ZONES[z % len(ZONES)][3])
        add(f'''Trigger("{enemy}"){{
Conditions:
\tBring("{enemy}", "{unit}", "Zone{z + 1}", At most, {max(1, n // 3)});

Actions:
\tCreate Unit("{enemy}", "{unit}", {n - max(1, n // 3)}, "Zone{z + 1} Spawn");
\tPreserve Trigger();
}}''')
        # 잡으면 돈 — **죽은 수를 소비한다.**
        # Kill(..., At least, 1) 은 누적 조건이라 Preserve 와 함께 쓰면
        # 첫 킬 뒤로 매 순회마다 돈이 들어온다 (무한 돈). 죽은 수를
        # 하나씩 빼면서 그만큼만 주는 것이 관용구다.
        add(f'''Trigger("{enemy}"){{
Conditions:
\tDeaths("{enemy}", "{unit}", At least, 1);

Actions:
\tSet Deaths("{enemy}", "{unit}", Subtract, 1);
\tSet Resources("All players", Add, {pay}, ore);
\tSet Score("All players", Add, {pay}, Custom);
\tPreserve Trigger();
}}''')
        # 구역에 처음 들어가면 알려 준다
        add(f'''Trigger({HUMANS}){{
Conditions:
\tBring("Current Player", "Any unit", "Zone{z + 1}", At least, 1);
\tSwitch("Switch {z + 1}", not set);

Actions:
\tSet Switch("Switch {z + 1}", set);
\tDisplay Text Message(Always Display, "\\x07{label}\\x02 — 한 마리에 \\x03{pay}원\\x02.");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')

    # 보스
    bu, bn = BOSS
    add(f'''Trigger("{boss_p}"){{
Conditions:
\tBring("{boss_p}", "{bu}", "Boss", At most, 0);
\tSwitch("Switch 20", not set);

Actions:
\tSet Switch("Switch 20", set);
\tCreate Unit with Properties("{boss_p}", "{bu}", 2, "Boss Spawn", 1);
\tPreserve Trigger();
}}''')
    add(f'''Trigger({HUMANS}){{
Conditions:
\tBring("{boss_p}", "Any unit", "Boss", At most, 0);
\tSwitch("Switch 20", set);

Actions:
\tDisplay Text Message(Always Display, "\\x07{bn} 를 잡았습니다!");
\tVictory();
}}''')

    # **회복 수단.** 없으면 한 번 깎인 체력이 죽을 때까지 그대로다.
    # 키우기 맵의 기본이다 — 마을은 공짜로 채워 주고, 밖에서는 돈을
    # 내고 채운다. 그래야 "돌아갈까 버틸까" 하는 판단이 생긴다.
    for p in range(1, players + 1):
        add(f'''Trigger("Player {p}"){{
Conditions:
\tBring("Player {p}", "Any unit", "Town", At least, 1);

Actions:
\tModify Unit Hit Points("Player {p}", "Any unit", 100, 0, "Town");
\tModify Unit Energy("Player {p}", "Any unit", 100, 0, "Town");
\tPreserve Trigger();
}}''')
        # 구역마다 회복 발판 — 값을 내고 그 자리에서 채운다
        for z in range(nzones):
            add(f'''Trigger("Player {p}"){{
Conditions:
\tBring("Player {p}", "Any unit", "Heal{z + 1}", At least, 1);
\tAccumulate("Player {p}", At least, {HEAL_COST}, ore);

Actions:
\tSet Resources("Player {p}", Subtract, {HEAL_COST}, ore);
\tModify Unit Hit Points("Player {p}", "Any unit", 100, 0, "Zone{z + 1}");
\tMove Unit("Player {p}", "Men", All, "Heal{z + 1}", "Zone{z + 1}");
\tDisplay Text Message(Always Display, "\\x03체력을 채웠습니다. \\x02-{HEAL_COST}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')

    # 마을로 되살리기 — 영웅이 없으면 새로 준다.
    # **Wait 로 시간을 끌지 않는다.** 한 플레이어는 동시에 웨이트
    # 트리거를 하나만 쓸 수 있어서, 여기서 4초를 끌면 그 플레이어의
    # 다른 트리거가 전부 뒤로 밀린다. 게다가 하이퍼 트리거와 부딪친다.
    for p in range(1, players + 1):
        add(f'''Trigger("Player {p}"){{
Conditions:
\tCommand("Player {p}", "Men", At most, 0);

Actions:
\tCreate Unit with Properties("Player {p}", "{HERO}", 1, "P{p} Home", 1);
\tDisplay Text Message(Always Display, "\\x06죽었습니다.\\x02 마을에서 다시 시작합니다.");
\tCenter View("P{p} Home");
\tPreserve Trigger();
}}''')
        # 마을 상점 — 비콘마다 다른 강화.
        # **산 뒤에 비콘 밖으로 밀어낸다** (하이퍼와 맞물린 연사 방지).
        for k, (bld, label, cost) in enumerate(SHOPS):
            add(f'''Trigger("Player {p}"){{
Conditions:
\tBring("Player {p}", "Any unit", "Shop{k + 1}", At least, 1);
\tAccumulate("Player {p}", At least, {cost}, ore);

Actions:
\tSet Resources("Player {p}", Subtract, {cost}, ore);
\tModify Unit Hit Points("Player {p}", "{HERO}", 100, 0, "Shop{k + 1}");
\tMove Unit("Player {p}", "Men", All, "Shop{k + 1}", "P{p} Home");
\tCreate Unit("Player {p}", "Terran Marine", 2, "P{p} Home");
\tSet Score("Player {p}", Add, 100, Custom);
\tDisplay Text Message(Always Display, "\\x03{label} 강화! \\x02-{cost}");
\tPlay WAV("sound\\\\Misc\\\\Button.wav", 300);
\tPreserve Trigger();
}}''')
    return "\n\n//-----------------------------------------------------------------//\n\n".join(T)


def main(argv=None):
    ap = argparse.ArgumentParser(description="키우기(RPG) 유즈맵")
    ap.add_argument("out")
    ap.add_argument("--players", type=int, default=4)
    ap.add_argument("--zones", type=int, default=4)
    ap.add_argument("--size", default="128x128")
    ap.add_argument("--tileset", default="jungle", choices=sorted(TILESETS))
    ap.add_argument("--name", default="키우기")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)

    if os.path.exists(a.out) and not a.force:
        print(f"이미 있습니다: {a.out} (--force)", file=sys.stderr)
        return 2
    if not (1 <= a.players <= 6):
        ap.error("1~6명입니다 (몬스터·보스로 슬롯 둘을 더 씁니다).")
    if not (2 <= a.zones <= 5):
        ap.error("구역은 2~5개입니다.")

    rng = random.Random(a.seed)
    W, H = (int(v) for v in a.size.lower().split("x"))
    ts = TILESETS[a.tileset]
    enemy_no, boss_no = a.players + 1, a.players + 2
    enemy, boss_p = f"Player {enemy_no}", f"Player {boss_no}"

    print(f"키우기 {W}x{H} {a.tileset}, {a.players}명, 구역 {a.zones}개")
    print("  " + corpus.describe("usemap", a.tileset))
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=False,
                        install=a.install)

    # **필요한 만큼만 쓴다.** 방 크기를 먼저 정하고 그만큼만 뚫는다.
    # 앞서는 맵을 다 채우려고 방을 늘렸는데 "검은 여백이 아깝다" 는
    # 잘못된 동기였다. 거꾸로 무조건 작게 만들 이유도 없다 — 방 크기는
    # 놀이가 정한다.
    #
    #   ┌────┐ ┌────┐ ┌────┐
    #   │마을├─┤1구역├─┤2구역│      한 줄에 세 칸, 넘치면 아래 줄로
    #   └────┘ └────┘ └──┬─┘       (줄바꿈은 세로 통로로 잇는다)
    #   ┌────┐ ┌────┐ ┌──┴─┐
    #   │보스│─┤4구역├─┤3구역│
    #   └────┘ └────┘ └────┘
    ROOM_W, ROOM_H, GAP = 26, 22, 4
    n_cells = a.zones + 2                       # 마을 + 구역들 + 보스
    cols = min(3, n_cells)
    rows = (n_cells + cols - 1) // cols
    used_w = cols * ROOM_W + (cols - 1) * GAP
    used_h = rows * ROOM_H + (rows - 1) * GAP
    if used_w > W - 4 or used_h > H - 4:
        raise CliError(f"방이 맵보다 큽니다 ({used_w}x{used_h} > {W}x{H}). "
                       f"--size 를 키우거나 --zones 를 줄이세요.")
    ox, oy = (W - used_w) // 2, (H - used_h) // 2
    cells = []
    for i in range(n_cells):
        r, c = divmod(i, cols)
        if r % 2:                               # 홀수 줄은 거꾸로 — 뱀
            c = cols - 1 - c
        cells.append((ox + c * (ROOM_W + GAP), oy + r * (ROOM_H + GAP),
                      ROOM_W, ROOM_H))
    band_y = cells[0][1]
    BAND_H = ROOM_H

    # **RPG 는 ISOM 으로 짓는다.** 실측 유즈맵 479장에서 RPG 의 ISOM
    # 다양도 중앙값은 0.445, 타일 그룹은 248개다 — 디펜스(0.000, 15개)
    # 와 정반대다. RPG 는 지형 자체가 콘텐츠라 네모난 방으로는 심심하다
    # (docs/usemap/terrain.md).
    #
    # 순서가 사각형 방식과 뒤집힌다. 다 덮고 방을 뚫는 것이 아니라,
    # **바닥을 먼저 깔고 그 위에 지형 덩이를 키운다.**
    pal = scmap.Palette(cli, ts, rng, "usemap")
    mode = scmap.terrain_mode("rpg")
    print(f"  지형: {pal.describe()}  ({mode})")

    if mode == "isom":
        print("지형을 ISOM 으로 짓습니다 (마을·구역·보스방 자리는 비웁니다)...")
        land = scmap.isom_landscape(cli, ts, rng, W, H,
                                    keep_clear=cells,
                                    walls=max(8, n_cells * 2),
                                    rises=max(14, n_cells * 4),
                                    patches=max(18, n_cells * 5),
                                    area=70)
        print(f"  바닥 얼룩 {len(land['patches'])}덩이 · "
              f"높은 땅 {len(land['rises'])}덩이 · "
              f"물 {len(land['walls'])}덩이 "
              f"(쓰는 지형 {len(land['low']) + len(land['high']) + len(land['blocked'])}종)")
        # 구역 사이를 잇는 길만 바닥으로 되돌린다 — 덩이가 길을 막았을
        # 수 있다. 여기서만 사각형을 쓴다(길은 길이어야 한다).
        for i in range(n_cells - 1):
            ax, ay, aw, ah = cells[i]
            bx_, by_, bw_, bh_ = cells[i + 1]
            if ay == by_:
                scmap.isom_fill(cli, land["base"],
                                min(ax + aw, bx_ + bw_), ay + ah // 2 - 3,
                                GAP, 6)
            else:
                scmap.isom_fill(cli, land["base"],
                                ax + aw // 2 - 3, ay + ah, 6, GAP)
    else:
        print("벽을 세웁니다...")
        scmap.cover_map(cli, pal, W, H)
        print(f"마을 1 + 구역 {a.zones} + 보스방 1 을 뚫습니다 "
              f"({used_w}x{used_h} 만 씁니다)...")
        for (x, y, w, h) in cells:
            scmap.room(cli, pal, x, y, w, h, rim=1)
        for i in range(n_cells - 1):
            ax, ay, aw, ah = cells[i]
            bx_, by_, bw_, bh_ = cells[i + 1]
            if ay == by_:
                pal.fill(cli, "path", min(ax + aw, bx_ + bw_),
                         ay + ah // 2 - 3, GAP, 6)
            else:
                pal.fill(cli, "path", ax + aw // 2 - 3, ay + ah, 6, GAP)

    # **한 맵에서 ISOM 과 사각형을 섞지 않는다.**
    #
    # `scatter_tile_variants` 는 타일 값을 직접 쓴다 — 사각형 편집이다.
    # ISOM 으로 지은 맵에 이것을 얹으면 ISOM 격자와 실제 타일이 어긋나,
    # 나중에 ISOM 붓을 한 번만 더 대도 지형이 뭉개진다. 사각형으로 지은
    # 맵에서만 쓴다.
    if mode != "isom":
        scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, a.players, [enemy_no, boss_no])

    # 유즈맵은 업그레이드를 고친다 — 실측 중앙 7가지, 90%가 전부
    # (docs/chk/anatomy.md). 밀리맵 비용·시간을 그대로 두면
    # 유즈맵 흐름에 안 맞는다.
    n_up = scmap.setup_usemap_upgrades(
        cli, a.players if hasattr(a, "players") else args.players,
        free_levels=0, max_level=3,
        mineral=100, gas=0, time=20)
    n_tech = scmap.setup_usemap_tech(cli, which=(0, 1, 2, 3, 5, 6, 7),
        mineral=150, gas=0, time=15, available="all")
    print(f"  업그레이드 {n_up}가지 · 기술 {n_tech}가지를 유즈맵 값으로 정했습니다")

    print("로케이션을 놓습니다...")
    loc = lambda nm, x0, y0, x1, y1: cli.edit(
        "location", "add", cli.path, str(x0), str(y0), str(x1), str(y1),
        "--tiles", "--name", nm)
    tx, ty, tw, th = cells[0]
    loc("Town", tx, ty, tx + tw, ty + th)
    for p in range(1, a.players + 1):
        hx = tx + 2 + ((p - 1) % 3) * 5
        hy = ty + 3 + ((p - 1) // 3) * 6
        loc(f"P{p} Home", hx, hy, hx + 4, hy + 4)
    for k in range(len(SHOPS)):
        sx = tx + 2 + k * 6
        loc(f"Shop{k + 1}", sx, ty + th - 8, sx + 4, ty + th - 4)
    for z in range(a.zones):
        zx, zy, zw, zh = cells[z + 1]
        loc(f"Zone{z + 1}", zx, zy, zx + zw, zy + zh)
        loc(f"Zone{z + 1} Spawn", zx + zw - 8, zy + 3, zx + zw - 2, zy + zh - 3)
        loc(f"Heal{z + 1}", zx + 2, zy + zh - 6, zx + 6, zy + zh - 2)
    bx, by, bw, bh = cells[-1]
    loc("Boss", bx, by, bx + bw, by + bh)
    loc("Boss Spawn", bx + bw // 2 - 3, by + bh // 2 - 3,
        bx + bw // 2 + 3, by + bh // 2 + 3)

    print("유닛을 놓습니다...")
    for p in range(1, a.players + 1):
        hx = tx + 4 + ((p - 1) % 3) * 5
        hy = ty + 5 + ((p - 1) // 3) * 6
        cli.place(scmap.START_LOCATION, hx, hy, owner=p)
        cli.place(HERO, hx, hy, owner=p)
        for k in range(3):
            cli.place("Terran Marine", hx - 1 + k, hy + 2, owner=p)
    for k, (bld, _, _) in enumerate(SHOPS):
        sx = tx + 4 + k * 6
        cli.place(bld, sx, ty + th - 11, owner=12)
        scmap.pad(cli, pal, sx, ty + th - 6, 3, 3)
        cli.place("Terran Beacon", sx, ty + th - 6, owner=12)
    # 구역마다 몬스터를 미리 깔아 둔다 (트리거가 채우기 전에도 보이게)
    for z in range(a.zones):
        zx, zy, zw, zh = cells[z + 1]
        unit, cnt = ZONES[z % len(ZONES)][0], ZONES[z % len(ZONES)][1]
        for k in range(cnt):
            mx = zx + 3 + (k % 6) * 2
            my = zy + 4 + (k // 6) * 3
            cli.place(unit, min(mx, zx + zw - 2), min(my, zy + zh - 2),
                      owner=enemy_no)
    # 구역마다 회복 발판 — 눈에 띄게 비콘과 건물을 함께 둔다
    for z in range(a.zones):
        zx, zy, zw, zh = cells[z + 1]
        scmap.pad(cli, pal, zx + 4, zy + zh - 4, 3, 3)
        cli.place("Terran Beacon", zx + 4, zy + zh - 4, owner=12)
        cli.place("Zerg Creep Colony", zx + 4, zy + zh - 8, owner=12)
    cli.place(scmap.START_LOCATION, cells[1][0] + 2, band_y + 2, owner=enemy_no)
    cli.place(scmap.START_LOCATION, bx + 2, by + 2, owner=boss_no)
    cli.place(BOSS[0], bx + bw // 2, by + bh // 2, owner=boss_no)

    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)


    # 방 테두리를 두뎃으로 꾸민다. 실측 유즈맵 76장 중 70장(92%)이
    # 두뎃 타일을 쓰고, 중앙 868칸이며 그 89%가 걷기 경계 두 칸 안에
    # 몰려 있다. 내 맵은 0칸이었다 — 그림으로 보고서야 알았다.
    # 걷기를 막는 두뎃은 `data/doodad-walk.json` 을 보고 걸러 낸다.
    print("방 테두리를 두뎃으로 꾸밉니다...")
    _clear = [(u["x"] // 32 - 2, u["y"] // 32 - 2, 5, 5) for u in cli.units()]
    _nd = scmap.decorate_rim(cli, ts, cells, rng, keep_clear=_clear)
    print(f"  두뎃 {_nd}개")

    print("브리핑을 짭니다...")
    cli.apply_briefing(scmap.briefing_text(
        ["마을에서 출발합니다. 마을은 안전합니다.",
         f"구역 {a.zones}곳을 차례로 깹니다. 뒤로 갈수록 셉니다.",
         "잡으면 돈이 들어옵니다. 마을 비콘에서 강화하세요.",
         f"마을에서는 공짜로, 구역 발판에서는 {HEAL_COST}원에 체력을 채웁니다.",
         "마지막 방의 보스를 잡으면 이깁니다."],
        objectives="구역을 깨고 보스를 잡는다",
        portrait=HERO))

    print("트리거를 짭니다...")
    cli.apply_triggers(build_triggers(a.players, a.zones, enemy, boss_p, th))
    if a.name:
        cli.set_map_name(a.name,
            f"마을에서 나가 구역 {a.zones}개를 깨고 보스를 잡으면 이깁니다. "
            f"잡으면 돈이 들어옵니다. 마을에서는 공짜로, 구역 발판에서는 "
            f"{HEAL_COST}원에 체력을 채웁니다. 죽으면 마을에서 다시 시작합니다.")

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
