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

HERO = "Jim Raynor (Marine)"          # 실측 유즈맵 49% 가 영웅을 쓴다
SHOPS = [("Terran Engineering Bay", "공격력", 200),
         ("Terran Armory", "방어력", 250),
         ("Protoss Forge", "체력", 300)]


def build_triggers(players, nzones, enemy, boss_p, town_h):
    T = []
    add = T.append
    # **하이퍼 트리거를 맨 앞에.** 없으면 트리거가 1초에 한 번만 돌아
    # 비콘·스폰·판정이 모두 한 박자 늦는다. 유즈맵에 거의 필수다.
    add(scmap.hyper_trigger())
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
        # 마을 상점 — 비콘마다 다른 강화
        for k, (bld, label, cost) in enumerate(SHOPS):
            add(f'''Trigger("Player {p}"){{
Conditions:
\tBring("Player {p}", "Any unit", "Shop{k + 1}", At least, 1);
\tAccumulate("Player {p}", At least, {cost}, ore);

Actions:
\tSet Resources("Player {p}", Subtract, {cost}, ore);
\tModify Unit Hit Points("Player {p}", "{HERO}", 1, 100, "Shop{k + 1}");
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
    groups = corpus.pick_floor_groups(corpus.load(), ts, "usemap", 14)
    tiles = scmap.tileset_tiles(cli, ts)
    floor = next(t for g in groups for t in range(g * 16, g * 16 + 16)
                 if t in tiles and tiles[t][1] and tiles[t][2])

    # 마을 → 구역들 → 보스방을 **뱀처럼** 늘어놓아 맵을 채운다.
    # 가로 한 줄로만 놓으면 맵의 70% 가 검은 낭비가 된다 (실제로 그랬다).
    MARGIN, GAP = 3, 4
    n_cells = a.zones + 2                       # 마을 + 구역들 + 보스
    cols = 3 if n_cells > 4 else 2
    rows = (n_cells + cols - 1) // cols
    cw = (W - 2 * MARGIN - (cols - 1) * GAP) // cols
    chh = (H - 2 * MARGIN - (rows - 1) * GAP) // rows
    cells = []
    for i in range(n_cells):
        r, c = divmod(i, cols)
        if r % 2:                               # 홀수 줄은 거꾸로 — 뱀
            c = cols - 1 - c
        cells.append((MARGIN + c * (cw + GAP), MARGIN + r * (chh + GAP), cw, chh))
    band_y = cells[0][1]
    BAND_H = chh

    print("벽을 세웁니다...")
    cli.edit("terrain", "fill", cli.path, "0", "0", str(W), str(H), str(VOID_TILE))
    fill = lambda x, y, w, h, t: cli.edit(
        "terrain", "fill", cli.path, str(x), str(y), str(w), str(h), str(t))

    print(f"마을 1 + 구역 {a.zones} + 보스방 1 을 뚫습니다...")
    for (x, y, w, h) in cells:
        fill(x, y, w, h, floor)
    # 칸 사이 통로 — 좁게 내야 문턱이 생긴다
    for i in range(n_cells - 1):
        ax, ay, aw, ah = cells[i]
        bx_, by_, bw_, bh_ = cells[i + 1]
        if ay == by_:                           # 같은 줄 — 가로 통로
            x0 = min(ax + aw, bx_ + bw_)
            fill(x0, ay + ah // 2 - 3, GAP, 6, floor)
        else:                                   # 줄바꿈 — 세로 통로
            x0 = ax + aw // 2 - 3
            fill(x0, ay + ah, 6, GAP, floor)

    print("바닥을 칠합니다 (그룹을 섞어)...")
    n = scmap.paint_floor_mixed(cli, ts, rng, cells, groups)
    ch = scmap.scatter_tile_variants(cli, ts, rng, chance=0.5)
    print(f"  {n}칸 칠하고 변종 {ch}칸")

    print("플레이어 슬롯을 정합니다...")
    scmap.setup_usemap_players(cli, a.players, [enemy_no, boss_no])

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
    cli.place(scmap.START_LOCATION, cells[1][0] + 2, band_y + 2, owner=enemy_no)
    cli.place(scmap.START_LOCATION, bx + 2, by + 2, owner=boss_no)
    cli.place(BOSS[0], bx + bw // 2, by + bh // 2, owner=boss_no)

    print("시야를 엽니다...")
    scmap.reveal_for_all(cli, a.players)

    print("트리거를 짭니다...")
    cli.apply_triggers(build_triggers(a.players, a.zones, enemy, boss_p, th))
    if a.name:
        cli.set_map_name(a.name)

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
