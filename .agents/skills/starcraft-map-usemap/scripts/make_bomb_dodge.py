#!/usr/bin/env python3
"""폭탄피하기 유즈맵 — 스테이지마다 다른 높이 층에 통로를 두는 계단식
코스를 스테이지로 나누고, 스테이지마다 폭탄 셀 격자 패턴이 반복된다.

코퍼스 표본(scmscx `r8gbbp63`, `yc766g76`)의 폭탄 트리거는 같은 로케이션에서
`Kill Unit At Location`으로 거기 있던 플레이어 유닛을 먼저 지우고(그 자리에
있었다면 이게 즉사 판정이다), `Create Unit`으로 폭발 표시 유닛을 놓은 뒤
바로 다시 `Kill Unit At Location`으로 그 유닛을 지운다 — 폭탄이 화면에
머무는 시간이 없다. 이 순서(킬→생성→킬)가 중요한 이유는, 생성 전에 그 자리
플레이어 유닛을 비워 두지 않으면 Create Unit이 옆 빈 칸으로 밀려나 판정
로케이션과 실제 생성 위치가 어긋나기 때문이다. `Wait`는 이 킬-생성-킬
묶음이 끝난 뒤 다음 박자로 넘어가기 전의 간격이지, 판정 전 텔레그래프가
아니다. 체크포인트는 `Bring`으로 통과를 감지해 다음 스테이지의 폭탄 루프
스위치를 켠다. 이 레시피는 이 체인을 그대로 재현하되, 코퍼스의 "소생하기"
Bring 조합 대신 `scmap.part_respawn`으로 대체 구현한다 — 코퍼스 원본 관찰과
이 레시피의 구현은 docs/usemap/genre-recipes.md에서 구분해 적는다.

스테이지 수·격자 크기·폭탄 박자(어느 칸이 같이 터지는지, 박자 사이 간격)는
전부 프로필이 정한다. 코드는 좌표 범위와 코스가 실제로 걸을 수 있는
지형인지만 검사한다.

    python3 make_bomb_dodge.py out.scx --config profile.json
"""
from __future__ import annotations

import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import CliError
import recipe_config as profile

TILESETS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
            "desert": 5, "ice": 6, "twilight": 7}
BASE_TERRAIN = {0: "Dirt", 1: "Platform", 3: "Magma", 4: "Dirt",
                5: "Tar", 6: "Ice", 7: "Dirt"}
GATE_W = 4  # 체크포인트 로케이션의 가로 폭 (타일)
MARGIN = 3
CELL_GAP = 0  # 폭탄 셀 사이 틈 (타일) — 0이면 셀이 서로 맞붙어 통로에 빈 칸이 없다
BAND_GAP = 4  # 이웃 층 밴드 "가장자리" 사이 최소 간격 (타일) — 벽으로 갈라져 보일 최소치

# 코퍼스(scmscx `r8gbbp63`, `yc766g76`)의 폭탄 셀은 2x2타일로 작고,
# Kill Unit At Location 이 그 Create Unit 과 정확히 같은 로케이션을 쓴다.
# 통로 폭도 코퍼스에서 1~3칸을 넘지 않는다("ㅁㅁㅁㅁㅁ" 한 줄, 많아야 석
# 줄). 이전 구현은 스테이지 구간 전체를 cols x rows 로 나눠 셀 하나가
# 통로 절반을 차지하는 거대한 사각형이 됐고(폭발 표시는 점 하나인데
# 죽는 범위는 기둥 하나), 코스도 굽이 없이 통짜 직선 띠 하나였으며 통로
# 폭 제한도 없었다. 지금은 셀 크기(2x2)와 통로 폭(rows, 최대 3)을 스키마가
# 강제하고, 스테이지마다 다른 높이 층으로 코스를 계단식으로 굽히며, 이웃
# 셀은 체크판으로 타일을 갈라 경계를 눈으로 읽게 한다 — 코퍼스의 정확한
# 재현은 아니지만 "직선에 통로가 넓고 죽는 범위가 안 맞는다"는 구체적
# 결함은 고친다.


CELL_PAD = 0  # 셀 격자와 체크포인트 게이트 사이 여백 (타일) — 0이면 맞닿는다


def band_layout(width: int, height: int, stages: int, stage_cfgs: list[dict]):
    """스테이지마다 다른 높이 층에 밴드를 두는 계단식 코스 좌표를 만든다.

    가로·세로 모두 콘텐츠(격자 크기)가 실제로 필요한 만큼만 잡는다 — 맵을
    임의 비율로 나눠 채우면 코스가 밋밋하게 넓어지거나(가로) 셀 하나가
    거대해진다(세로). 필요한 최소치보다 맵이 작으면 얼마가 모자란지 알려
    주는 CliError를 낸다. 밴드 두께는 그 스테이지 `cell` x `grid`(rows)로
    정해져 스테이지마다 통로 굵기가 달라진다.
    """
    def _grid_wh(s: int) -> tuple[int, int]:
        cols, rows = stage_cfgs[s]["grid"]
        cell_w, cell_h = stage_cfgs[s]["cell"]
        return (cols * cell_w + (cols - 1) * CELL_GAP,
                rows * cell_h + (rows - 1) * CELL_GAP)

    grid_wh = [_grid_wh(s) for s in range(stages)]
    # 밴드 두께는 격자 세로 길이 그대로다 — 셀 격자 바깥에 별도 통로
    # 배경을 깔지 않으므로(순수하게 셀로만 구성) 여백을 더하지 않는다.
    half_h_by_stage = [gh // 2 for _, gh in grid_wh]

    # 세로: 위/아래 두 층만 번갈아 쓴다. 통로 폭(rows)이 최대 3칸으로 좁아
    # 밴드 자체가 얇으므로(최대 12타일), 스테이지마다 새 층을 쓰면 맵
    # 바운딩 박스만 커지고 못 걷는 땅 비율이 오히려 나빠진다 — 층 두
    # 개를 재사용해 세로 낭비를 줄인다. 같은 층을 쓰는 스테이지 중 가장
    # 두꺼운 쪽에 맞춘다.
    top_stages = [s for s in range(stages) if s % 2 == 0]
    bot_stages = [s for s in range(stages) if s % 2 == 1]
    max_top = max((half_h_by_stage[s] for s in top_stages), default=0)
    max_bot = max((half_h_by_stage[s] for s in bot_stages), default=0)
    top_c = MARGIN + max_top
    bot_c = top_c + max_top + BAND_GAP + max_bot
    need_h = bot_c + max_bot + MARGIN
    if need_h > height:
        raise CliError(
            f"맵 세로 크기 {height}가 지그재그 밴드에 필요한 최소 {need_h}보다 작습니다 "
            f"(--size 를 키우거나 stages 의 grid/cell 을 줄이세요)")
    two_centers = [top_c, bot_c]
    centers = [two_centers[s % 2] for s in range(stages)]

    # 가로: 게이트 폭 + 격자 실제 길이 + 여백을 그대로 이어 붙인다(균등
    # 분할하지 않는다). 스테이지 격자가 짧으면 그 구간도 짧아진다.
    gate_half = GATE_W // 2
    gate_x = [MARGIN + gate_half]
    for s in range(stages):
        grid_w, _ = grid_wh[s]
        gate_x.append(gate_x[-1] + gate_half + grid_w + 2 * CELL_PAD + gate_half)
    need_w = gate_x[-1] + gate_half + MARGIN
    if need_w > width:
        raise CliError(
            f"맵 가로 크기 {width}가 코스에 필요한 최소 {need_w}보다 작습니다 "
            f"(--size 를 키우거나 stages 의 grid/cell 을 줄이세요)")
    lane_x0, lane_w = MARGIN, width - 2 * MARGIN

    bands = [(centers[s], half_h_by_stage[s]) for s in range(stages)]
    return lane_x0, lane_w, gate_x, bands


def stage_cells(seg_x0: int, seg_x1: int, center_y: float, half_h: int,
                cols: int, rows: int, cell_w: int, cell_h: int):
    """스테이지 구간 안에 정확히 cell_w x cell_h 타일짜리 폭탄 셀을 놓는다.

    셀 크기는 프로필이 정한 그대로다(비율로 나누지 않는다) — Create Unit
    으로 놓는 폭탄과 Kill Unit At Location 판정 범위가 같은 셀을 가리키게
    하기 위해서다. 격자가 구간에 안 들어가면 만들지 않고 바로 에러를 낸다.
    """
    grid_w = cols * cell_w + (cols - 1) * CELL_GAP
    grid_h = rows * cell_h + (rows - 1) * CELL_GAP
    seg_w = seg_x1 - seg_x0
    if grid_w > seg_w:
        raise CliError(
            f"스테이지 격자 가로 {grid_w}타일이 구간 폭 {seg_w}타일보다 큽니다 "
            f"(cols/cell_w 를 줄이거나 stages 수를 줄여 구간을 넓히세요)")
    x0 = seg_x0 + (seg_w - grid_w) // 2
    y0 = int(center_y - grid_h / 2)
    cells = []
    for r in range(rows):
        for c in range(cols):
            cx = x0 + c * (cell_w + CELL_GAP)
            cy = y0 + r * (cell_h + CELL_GAP)
            cells.append((cx, cy, cell_w, cell_h))
    return cells


def build_triggers(cfg, operator: str, gates: list[str],
                   stage_cell_locs: list[list[str]]) -> str:
    rules, units, labels = cfg["rules"], cfg["units"], cfg["labels"]
    msg = cfg["text"]["messages"]
    players = rules["players"]
    stages = rules["stages"]
    stage_counter, revive_lock = units["stage_counter"], units["revive_lock"]
    humans = [f"Player {p}" for p in range(1, players + 1)]
    HUMANS = ",".join(f'"{h}"' for h in humans)
    blocks = []

    # 시작 — 동맹과 안내만. 스테이지 1 스위치는 시작 게이트 체크포인트가
    # 켠다(아래) — Always()로 무조건 켜면 정적 검사가 "시작 배치만으로
    # 진행되는 트리거"로 잡고, 실제로도 체크포인트 스위치 번호가 하나씩
    # 밀려 스테이지 2를 건너뛰는 결과를 낳았다.
    blocks.append(f'''Trigger({HUMANS}){{
Conditions:
\tAlways();

Actions:
\tSet Alliance Status("{operator}", Ally);
\tDisplay Text Message(Always Display, "{msg["intro"]}");
\tSet Mission Objectives("{cfg["text"]["objectives"]}");
}}''')
    blocks.append(f'''Trigger("{operator}"){{
Conditions:
\tAlways();

Actions:
\tSet Alliance Status("All players", Ally);
}}''')
    blocks += scmap.part_ally_humans(humans)

    # 스테이지별 폭탄 루프 — 켜지면 영원히 반복한다(코퍼스도 끄지 않는다).
    for s in range(stages):
        st = cfg["stages"][s]
        cols, rows = st["grid"]
        cells = stage_cell_locs[s]
        # CHK 트리거는 액션을 최대 64개(0~63)만 담는다. 한 스테이지의
        # 폭탄 루프는 통째로 트리거 하나이므로, 박자마다 셀 수만큼
        # Kill(선점 제거) + Create + Kill(즉시 판정) 액션이 쌓인다.
        action_budget = 1 + sum(3 * len(beat["cells"]) + 1 for beat in st["sequence"])
        if action_budget > 64:
            raise CliError(
                f"stages[{s}]의 트리거 액션이 {action_budget}개로 64개 한도를 넘습니다 "
                f"(sequence의 cells 수나 beat 수를 줄이세요)")
        beats = []
        for beat in st["sequence"]:
            locs = [cells[i] for i in beat["cells"]]
            # 킬-생성-킬: Create Unit이 그 자리에 이미 있는 플레이어 유닛과
            # 겹치면 폭탄이 옆 빈 칸으로 밀려나 판정 자리가 어긋난다. 먼저
            # 그 자리의 플레이어를 죽여 비워 두고(이게 실제 "닿으면 즉사"
            # 판정이다), 그다음 폭탄을 만들고 바로 죽인다 — 폭탄이 화면에
            # 머무르는 시간이 없다(생성 즉시 삭제). Wait는 이 박자 전체가
            # 끝난 뒤, 다음 박자로 넘어가기 전의 간격이다.
            steps = "".join(
                f'\tKill Unit At Location("Players", "Any unit", All, "{loc}");\n'
                f'\tCreate Unit("{operator}", "{units["bomb"]}", 1, "{loc}");\n'
                f'\tKill Unit At Location("{operator}", "Any unit", All, "{loc}");\n'
                for loc in locs)
            beats.append(f'{steps}\tWait({beat["wait_ms"]});\n')
        blocks.append(f'''Trigger("{operator}"){{
Conditions:
\tSwitch("Stage {s + 1}", set);

Actions:
{"".join(beats)}\tPreserve Trigger();
}}''')

    # 체크포인트 — 한 번만 갱신하고, 그 구간(게이트 i)의 폭탄 스위치를
    # 켠다. i=0(시작 게이트)은 스테이지 1을 켜고, 이는 사람 시작 위치가
    # 그 게이트 로케이션 안이라 맵 시작과 거의 동시에 켜진다. 마지막
    # 게이트(목표)는 gates[:-1]에 들지 않으며 별도 승리 트리거가 처리한다.
    for i, gate in enumerate(gates[:-1]):
        actions = [f'Set Deaths("Current Player", "{stage_counter}", Set To, {i})']
        if i > 0:
            actions.append(f'Display Text Message(Always Display, "{msg["checkpoint"]}")')
        actions.append(f'Set Switch("Stage {i + 1}", set)')
        actions.append("Preserve Trigger()")
        blocks.append(f'''Trigger({HUMANS}){{
Conditions:
\tBring("Current Player", "{units["runner"]}", "{gate}", At least, 1);
\tDeaths("Current Player", "{stage_counter}", At most, {max(i - 1, 0)});

Actions:
{"".join(f"\t{a};\n" for a in actions)}}}''')

    # 목표 — 승리.
    blocks += scmap.part_win(humans, [
        f'Bring("Current Player", "{units["runner"]}", "{gates[-1]}", At least, 1);'],
        msg=msg["victory"])

    # 목숨 — 부활을 쓰면 마지막으로 지난 체크포인트에서 되살아난다.
    # 안 쓰면 한 번 죽는 순간 그 사람은 진다.
    if rules["revive"]:
        for p in range(1, players + 1):
            player = f"Player {p}"
            for i, gate in enumerate(gates[:-1]):
                blocks += scmap.part_respawn(
                    player, units["runner"], gate, msg["revive"], count=1,
                    cooldown_counter=revive_lock,
                    guard=f'Deaths("{player}", "{stage_counter}", Exactly, {i});')
    else:
        blocks.append(f'''Trigger({HUMANS}){{
Conditions:
\tCommand("Current Player", "{units["runner"]}", Exactly, 0);

Actions:
\tDisplay Text Message(Always Display, "{msg["defeat"]}");
\tDefeat();
}}''')

    return scmap.TRIGGER_SEP.join(blocks)


def main(argv=None):
    ap = argparse.ArgumentParser(description="AI 프로필 기반 폭탄피하기 유즈맵")
    ap.add_argument("out")
    profile.add_profile_arguments(ap, "bomb_dodge")
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args(argv)
    if os.path.exists(args.out) and not args.force:
        print(f"이미 있습니다: {args.out}", file=sys.stderr)
        return 2
    cfg = profile.load_profile(args.config, "bomb_dodge")
    rules = cfg["rules"]
    width, height = cfg["map"]["size"]
    tileset_id = TILESETS[cfg["map"]["tileset"]]
    players = rules["players"]
    stages = rules["stages"]
    operator_no = players + 1
    operator = f"Player {operator_no}"

    total_locations = (stages + 1) + sum(
        st["grid"][0] * st["grid"][1] for st in cfg["stages"][:stages])
    if total_locations > 250:
        raise CliError(
            f"로케이션 {total_locations}개는 맵 한도(255)에 너무 가깝습니다 "
            f"— 스테이지 grid 를 줄이세요")

    print(f"폭탄피하기 {width}x{height} {cfg['map']['tileset']}, "
          f"사람 {players}명, {stages}스테이지")

    cli = scmap.new_map(args.out, width, height, tileset_id,
                        terrain=BASE_TERRAIN.get(tileset_id), melee=False,
                        install=args.install)
    scmap.setup_usemap_players(cli, players, [operator_no],
                               race=cfg["players"]["race"],
                               computer_race=cfg["players"]["operator_race"])
    cli.edit("player", "set", cli.path, str(operator_no),
             "--race", cfg["players"]["operator_race"], "--slot", "computer")
    for s in range(1, stages + 1):
        cli.edit("switch", "name", cli.path, str(s), f"Stage {s}")

    # 안전지대(게이트/연결 통로)에 쓸 색은 체크판의 두 색(floor/pad)
    # 모두와 멀어야 셋이 다 구별된다. Palette 는 (floor,pad)·(floor,rim)·
    # (path,rim) 세 쌍만 거리를 보장하므로 (pad,rim)·(pad,path)는 가까울
    # 수 있다 — 안 걷는 그룹까지 뒤지면 벽 타일을 안전지대로 칠해 보행
    # 경로가 끊긴다(실제로 그렇게 했다가 걸렸다). 대신 시드 몇 개로
    # Palette 를 다시 뽑아 rim/path 가 floor·pad 둘 다에서 가장 먼
    # 조합을 고른다 — 넷 다 걷는 그룹이라는 보장은 유지된다.
    tile_colors = scmap._tile_colors(tileset_id)
    base_seed = cfg["map"]["seed"]
    best = None
    for tryseed in range(base_seed, base_seed + 12):
        cand = scmap.Palette(cli, tileset_id, random.Random(tryseed), "usemap")
        fc = tile_colors.get(cand.groups("floor")[0])
        pc = tile_colors.get(cand.groups("pad")[0])
        for role in ("rim", "path"):
            rc = tile_colors.get(cand.groups(role)[0])
            score = min(scmap._color_dist(rc, fc), scmap._color_dist(rc, pc))
            if best is None or score > best[0]:
                best = (score, cand, role)
    _, pal, safe_role = best

    def fill_safe(x: int, y: int, w: int, h: int) -> None:
        pal.fill(cli, safe_role, x, y, w, h)

    scmap.cover_map(cli, pal, width, height, margin=2)
    lane_x0, lane_w, gate_x, bands = band_layout(width, height, stages, cfg["stages"])

    # 스테이지 구간에는 별도 배경을 깔지 않는다 — 통로가 순수하게 폭탄
    # 셀(체크판)로만 구성되게 아래 "스테이지 폭탄 격자를 놓습니다" 에서
    # 칸마다 직접 칠한다. 여기서 먼저 칠하면 셀 사이에 안 쓰는 배경
    # 타일이 남아 "틈"처럼 보인다.
    gate_half = GATE_W // 2

    # 체크포인트 자리는 인접한 두 밴드를 모두 아우르는 세로 통로로 채운다
    # (양 끝은 자기 밴드만). 이 세로 통로가 지그재그의 꺾이는 구간이다.
    def _gate_span(i: int):
        if i == 0:
            c, h = bands[0]
            return c - h, c + h
        if i == stages:
            c, h = bands[-1]
            return c - h, c + h
        c0, h0 = bands[i - 1]
        c1, h1 = bands[i]
        return min(c0 - h0, c1 - h1), max(c0 + h0, c1 + h1)

    gate_spans = [_gate_span(i) for i in range(stages + 1)]

    # 시작 지점은 다른 게이트보다 훨씬 큰 방을 따로 잡는다. 이유 둘:
    # (1) 사람 수만큼 스타팅이 안 겹치게 들어갈 자리가 필요하고,
    # (2) Start Location 은 실제 건물을 안 놓아도 엔진이 그 자리에 종족
    # 본진 출입구가 있다고 가정해 검사한다 — 통로가 좁으면 "건물
    # 출입구가 막혔습니다" 가 뜬다(폭 6타일로 넓혀도 안 없어져서, 사람
    # 수에 맞춰 더 키운다). 그리드와 닿는 오른쪽 끝은 고정하고 왼쪽으로
    # 넓힌다 — 격자 시작 위치(gate_x[s]+gate_half)는 다른 스테이지 폭
    # 계산과 맞물려 있어 못 건드린다.
    occupants = players + 1  # 사람 전원 + 운영 플레이어
    pitch = 2  # 스폰 사이 간격(타일) — Marine 등 1타일 유닛이 안 겹친다
    cols_p = max(1, int(occupants ** 0.5 + 0.999))
    rows_p = -(-occupants // cols_p)
    room_w = max(GATE_W, cols_p * pitch + 2)
    room_h = max(int(2 * bands[0][1]), rows_p * pitch + 2)
    room_x1 = gate_x[0] + gate_half
    room_x0 = max(1, room_x1 - room_w)
    room_c = bands[0][0]
    room_y0 = max(1, int(room_c - room_h / 2))
    room_y1 = min(height - 1, room_y0 + room_h)
    gate_spans[0] = (room_y0, room_y1)

    # 게이트/연결 통로는 위에서 고른 safe_tile 로 칠해 폭탄 체크판
    # (floor/pad)과는 다른 세 번째 타일로 보이게 한다 — 여기는 폭탄이
    # 없는 안전지대라는 것을 화면에서 바로 읽을 수 있어야 한다.
    fill_safe(room_x0, room_y0, room_x1 - room_x0, room_y1 - room_y0)
    for x, (y0, y1) in zip(gate_x[1:], gate_spans[1:]):
        gx0 = max(lane_x0, x - GATE_W // 2)
        fill_safe(gx0, int(y0), GATE_W, int(y1 - y0))

    print("스테이지 폭탄 격자를 놓습니다...")
    stage_cell_locs = []
    for s in range(stages):
        st = cfg["stages"][s]
        cols, rows = st["grid"]
        cell_w, cell_h = st["cell"]
        center, half_h = bands[s]
        rects = stage_cells(gate_x[s] + gate_half, gate_x[s + 1] - gate_half,
                            center, half_h, cols, rows, cell_w, cell_h)
        names = []
        for i, (x, y, w, h) in enumerate(rects):
            r, c = divmod(i, cols)
            # 체크판: 이웃 칸(상하좌우)과 항상 다른 타일 몫을 쓴다 — 옆
            # 칸과의 경계를 화면에서 읽을 수 있어야 어디가 지금 안전한
            # 칸인지 판단할 수 있다. walk_grid 로 보행 경로를 확인하기
            # 전에 칠해야 한다 — 셀 배경이 이 fill 뿐이라, 먼저 안 칠하면
            # 스테이지 구간 전체가 여전히 못 걷는 벽으로 남는다.
            pal.fill(cli, "floor" if (r + c) % 2 == 0 else "pad", x, y, w, h)
            name = f"{cfg['labels']['bomb_prefix']}{s + 1}-{i + 1}"
            cli.edit("location", "add", cli.path, str(x), str(y),
                     str(x + w), str(y + h), "--tiles", "--name", name)
            names.append(name)
        stage_cell_locs.append(names)

    grid = scmap.walk_grid(cli, tileset_id, 0, 0, width, height)

    def _walk_at(x, y):
        return scmap.nearest_walkable(grid, int(x) * 4 + 2, int(y) * 4 + 2, 16)

    gate_names = ([cfg["labels"]["start"]]
                  + [f"{cfg['labels']['checkpoint_prefix']}{i}" for i in range(1, stages)]
                  + [cfg["labels"]["goal"]])
    gate_points = []
    for x, (y0, y1) in zip(gate_x, gate_spans):
        gy = (y0 + y1) / 2
        p = _walk_at(x, gy)
        if p is None:
            raise CliError(f"체크포인트 자리 ({x},{gy})가 걸을 수 있는 지형이 아닙니다")
        gate_points.append(p)
    if any(not scmap.walk_reachable(grid, gate_points[0], p) for p in gate_points[1:]):
        raise CliError("코스 시작부터 목표까지 보행 경로가 이어지지 않습니다")

    cli.edit("location", "add", cli.path, str(room_x0), str(room_y0),
             str(room_x1), str(room_y1), "--tiles", "--name", gate_names[0])
    for name, x, (y0, y1) in zip(gate_names[1:], gate_x[1:], gate_spans[1:]):
        gx0 = max(lane_x0, x - GATE_W // 2)
        cli.edit("location", "add", cli.path, str(gx0), str(int(y0)),
                 str(gx0 + GATE_W), str(int(y1)),
                 "--tiles", "--name", name)

    print("스타팅과 유닛을 놓습니다...")
    # 시작 방 안에 사람 전원 + 운영 플레이어를 pitch 간격 격자로 겹치지
    # 않게 놓는다. 오른쪽 끝(room_x1)은 스테이지 1 격자와 틈 없이
    # 맞붙어 있어서(CELL_PAD=0) 거기 붙여 놓으면 place() 의 합법 지형
    # 스냅이 격자 칸으로 넘어갈 수 있다 — 왼쪽부터 채운다.
    for k in range(occupants):
        gx, gy = k % cols_p, k // cols_p
        px = room_x0 + 1 + gx * pitch
        py = room_y0 + 1 + gy * pitch
        if k < players:
            p = k + 1
            cli.place(scmap.START_LOCATION, px, py, owner=p)
            cli.place(cfg["units"]["runner"], px, py, owner=p)
        else:
            cli.place(scmap.START_LOCATION, px, py, owner=operator_no)

    lane_y0 = min([int(y0) for y0, _ in gate_spans] + [int(c - h) for c, h in bands])
    lane_y1 = max([int(y1) for _, y1 in gate_spans] + [int(c + h) for c, h in bands])
    scmap.apply_reveal(cli, "bomb_dodge", players,
                       start=(lane_x0, lane_y0, lane_w, lane_y1 - lane_y0))

    gate_names_all = gate_names
    cli.apply_triggers(build_triggers(cfg, operator, gate_names_all, stage_cell_locs))
    profile.apply_profile_metadata(cli, cfg, players)

    info = cli.info()
    print(f"\n만들었습니다: {args.out}")
    print(f"  {info['width']}x{info['height']} {info['tileset']} {info['version']}")
    print(f"  유닛 {info['units']}  로케이션 {info['locations']}  트리거 {info['triggers']}")
    scmap.assert_create_targets(cli)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
