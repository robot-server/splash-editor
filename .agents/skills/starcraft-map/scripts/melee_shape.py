#!/usr/bin/env python3
"""**밀리맵의 중간 스케일** — 고도 영역을 형태로 만든다.

`references/why-procedural-fails.md` 가 짚은 것: 슬롭은 국소도 전역도
아닌 **중간 스케일**에 산다. 국소 타일 접합은 ISOM 이 공짜로 해 주고,
전역 제약(스타팅 수·대칭·러시 거리)은 못 박으면 된다. 아무도 안 맡던
것이 "영역의 모양, 절벽선의 곡률" 이었다.

여기서 그 한 겹을 맡는다. 파는 방식은 셋이다.

1. **고도 마스크를 타일 해상도로 들고 있는다.** 마름모 격자에서 직접
   만들지 않는다. ISOM 마름모는 chkdraft 의 `IsomDiamond` 를 읽어 확인한
   바 **가로 4타일 × 세로 1타일, 줄마다 2타일 엇갈림**이다 (유효 좌표는
   `(x+y)%2==0`). 이 격자에서 설계하면 엇갈림 때문에 사람이 읽을 수 없고,
   타일 해상도에서 만들어 **가로 짝수 칸마다 붓질**하면 ISOM 솔버가
   알아서 절벽을 이어 준다. 대신 마스크의 특징이 **8타일보다 작으면
   양자화에 먹힌다** — 아래 2번이 그걸 막는다.

2. **곡률을 점수로 재지 않고 형태 연산으로 못 박는다.** 반지름 r 원판
   으로 **열고 닫는다**(opening + closing). 열기는 r 보다 얇은 돌출을
   지우고, 닫기는 r 보다 얇은 홈을 메운다. 그 결과 경계의 곡률 반경이
   어디서나 r 이상이 된다 — 사후 채점이 아니라 **생성 불변식**이다.
   네모난 언덕이 안 나오는 까닭도 같다: 시작을 원판의 합집합으로 하고
   모서리를 원판으로 다듬으므로 직각이 생길 데가 없다.

3. **램프 자리를 먼저 예약하고 얼린다.** 램프는 6x6 강체이고 위아래
   고도가 달라야 한다. 다 만든 뒤 끼워 넣으면 터진다. 자리를 먼저 찍어
   놓고 그 칸을 형태 연산에서 **제외**한다.

대칭은 씨앗 단계에서만 다룬다. 원판 합집합과 원판 형태 연산은 회전에
대해 교환법칙이 성립하므로, 씨앗이 대칭이면 **마스크는** 정확히 대칭이다.
`terrain mirror` 로 절벽을 베끼는 실수를 할 일이 없다.

> **다만 rot90 은 ISOM 격자의 정확한 대칭이 아니다.** 인접 마름모 사이
> 벡터가 `(±2, ±1)` 인데 90도 돌리면 `(±1, ±2)` 가 되어 같은 격자에
> 딱 맞지 않는다. 마스크가 대칭이어도 **양자화 뒤에 1~2 타일 어긋날 수
> 있다.** 타일 단위 완전 대칭이 필요하면 2인용(rot180)이 안전하다.
> 4인용을 만들려면 사분면마다 따로 양자화하고, 러시 거리와 초크 폭을
> 걸어서 다시 재 봐야 한다.

이 파일은 **형상만** 다룬다. 자원·유닛·트리거는 `make_melee.py` 몫이다.

    python3 melee_shape.py out.scx --players 4 --tileset jungle
"""
from __future__ import annotations

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError

LOW, HIGH = 0, 1

# ISOM 마름모 하나가 덮는 타일 (chkdraft `IsomDiamond` 에서 확인).
# 가로 특징이 이보다 작으면 양자화에 먹힌다.
ISOM_TILE_W, ISOM_TILE_H = 4, 1


class Field:
    """타일 해상도 고도 마스크. 0 = 저지대, 1 = 고지대."""

    def __init__(self, width: int, height: int, margin: int = 3):
        self.w, self.h = width, height
        self.margin = margin
        self.g = [[LOW] * width for _ in range(height)]
        self.frozen = [[False] * width for _ in range(height)]
        self._snap: list[tuple[int, int, int]] = []

    # -- 칠하기 ----------------------------------------------------------
    def disk(self, cx: float, cy: float, r: float, val: int = HIGH):
        """원판 하나. **모든 칠하기의 벽돌이다.**

        네모로 칠하지 않는다 — 시도 1 의 "네모난 언덕" 이 거기서 나왔다.
        """
        r2 = r * r
        y0, y1 = max(0, int(cy - r)), min(self.h - 1, int(cy + r))
        for y in range(y0, y1 + 1):
            dy = y - cy
            dx = math.sqrt(max(0.0, r2 - dy * dy))
            x0, x1 = max(0, int(cx - dx)), min(self.w - 1, int(cx + dx))
            row = self.g[y]
            for x in range(x0, x1 + 1):
                row[x] = val

    def path(self, pts, r: float, val: int = HIGH, step: float = 1.0):
        """점들을 잇는 띠. 원판을 촘촘히 겹쳐 그린다.

        선분을 직접 칠하면 끝이 각지고 이음매에 예각이 생긴다. 원판을
        겹치면 이음매 곡률도 r 이상이 보장된다.
        """
        for i in range(len(pts) - 1):
            (x0, y0), (x1, y1) = pts[i], pts[i + 1]
            d = math.hypot(x1 - x0, y1 - y0)
            n = max(1, int(d / step))
            for k in range(n + 1):
                t = k / n
                self.disk(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, val)

    def loop(self, cx: float, cy: float, base_r: float, r: float,
             harmonics, samples: int = 180, val: int = HIGH):
        """가운데를 두른 고리 모양 띠.

        `harmonics` 는 `(차수, 진폭, 위상)` 목록이다. 저차 성분만 쓰므로
        반지름이 천천히 변한다 — 이것도 곡률을 지키는 장치다.
        """
        pts = []
        for i in range(samples + 1):
            a = 2 * math.pi * i / samples
            rr = base_r
            for (k, amp, ph) in harmonics:
                rr += amp * math.sin(k * a + ph)
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        self.path(pts, r, val)

    # -- 형태 연산 -------------------------------------------------------
    @staticmethod
    def _disk_offsets(r: float):
        out = []
        rr = int(math.ceil(r))
        for dy in range(-rr, rr + 1):
            for dx in range(-rr, rr + 1):
                if dx * dx + dy * dy <= r * r:
                    out.append((dx, dy))
        return out

    def _morph(self, r: float, grow: bool):
        """팽창(grow) 또는 침식. 바깥은 저지대로 본다."""
        off = self._disk_offsets(r)
        src = [row[:] for row in self.g]
        want = HIGH if grow else LOW
        for y in range(self.h):
            row = self.g[y]
            for x in range(self.w):
                hit = False
                for (dx, dy) in off:
                    nx, ny = x + dx, y + dy
                    v = (src[ny][nx] if (0 <= ny < self.h and 0 <= nx < self.w)
                         else LOW)
                    if v == want:
                        hit = True
                        break
                row[x] = want if hit else (LOW if grow else HIGH)

    def open_close(self, r: float):
        """**최소 곡률 반경 r 을 못 박는다.**

        열기(침식→팽창)는 r 보다 얇은 돌출과 가는 목을 지운다.
        닫기(팽창→침식)는 r 보다 얇은 홈과 구멍을 메운다.
        둘을 거치면 경계의 곡률 반경이 어디서나 r 이상이다.

        얼린 칸은 연산 뒤에 되돌려 놓는다 — 램프 자리가 지워지면 안 된다.
        """
        self._morph(r, grow=False)     # 침식
        self._morph(r, grow=True)      # 팽창  → 열기
        self._morph(r, grow=True)      # 팽창
        self._morph(r, grow=False)     # 침식  → 닫기
        self._restore()

    def _restore(self):
        for (x, y, v) in self._snap:
            if 0 <= y < self.h and 0 <= x < self.w:
                self.g[y][x] = v

    def freeze_rect(self, x: int, y: int, w: int, h: int, val: int | None = None):
        """이 네모를 형태 연산에서 지킨다. `val` 을 주면 그 값으로 못 박는다."""
        for ty in range(max(0, y), min(self.h, y + h)):
            for tx in range(max(0, x), min(self.w, x + w)):
                if val is not None:
                    self.g[ty][tx] = val
                self.frozen[ty][tx] = True
                self._snap.append((tx, ty, self.g[ty][tx]))

    def clear_border(self):
        """맵 테두리는 저지대로 둔다. 절벽이 맵 밖으로 나가면 깨진다."""
        m = self.margin
        for y in range(self.h):
            for x in range(self.w):
                if x < m or y < m or x >= self.w - m or y >= self.h - m:
                    self.g[y][x] = LOW

    # -- 대칭 ------------------------------------------------------------
    def symmetric_seeds(self, pts, symmetry: str, count: int):
        """씨앗 하나를 대칭 자리로 퍼뜨린다.

        **씨앗에서만 대칭을 다룬다.** 원판 합집합과 원판 형태 연산은
        회전에 대해 교환되므로 씨앗이 대칭이면 결과도 정확히 대칭이다.
        타일을 옮겨 베끼면 절벽 방향이 뒤집힌다 — 그 실수를 할 데가 없다.
        """
        out = []
        for (x, y) in pts:
            out += scmap.symmetric_points(x, y, symmetry, count, self.w, self.h)
        return out

    # -- 재기 ------------------------------------------------------------
    def uniform_window_pct(self, win: int = 4) -> float:
        """4x4 창이 고도 한 가지로만 이루어진 비율.

        **`why-procedural-fails.md` 의 "공식 맵 3~18%" 와 견주면 안 된다.**
        그 값은 **타일 종류**로 잰 것이고 이건 고도로 잰 것이다. 처음에
        그걸 견주어 "공식 3~18 인데 내 것은 80" 이라고 적었는데 자가
        달랐다.

        **고지 문턱도 타일셋마다 다르다.** 세 번 틀리고 나서
        `measure_terrain_types.py` 로 칠해 보고 쟀다 — Badlands·Ashworld·
        Space 는 고도 2, Jungle·Desert·Ice·Twilight 는 1 이다. Space 는
        평지(Platform)가 고도 1 이라 1 로 잡으면 발판 전체가 고지가 된다.

        그 문턱으로 잰 공식·리그 밀리맵의 기준값:

            균일 창    중앙 65.0%   사분위 61.8~71.4
            고지대     중앙 36.4%   사분위 32.6~42.2
            bbox 채움  중앙 0.55    사분위 0.42~0.63
        """
        tot = same = 0
        for y in range(0, self.h - win + 1, 2):
            for x in range(0, self.w - win + 1, 2):
                vals = {self.g[y + dy][x + dx]
                        for dy in range(win) for dx in range(win)}
                tot += 1
                if len(vals) == 1:
                    same += 1
        return 100.0 * same / max(1, tot)

    def blobs(self):
        """고지 덩이들을 잇는 칸 묶음으로 준다."""
        from collections import deque
        seen = [[False] * self.w for _ in range(self.h)]
        out = []
        for y in range(self.h):
            for x in range(self.w):
                if self.g[y][x] != HIGH or seen[y][x]:
                    continue
                q = deque([(x, y)])
                seen[y][x] = True
                cells = []
                while q:
                    cx, cy = q.popleft()
                    cells.append((cx, cy))
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = cx + dx, cy + dy
                        if (0 <= ny < self.h and 0 <= nx < self.w
                                and self.g[ny][nx] == HIGH and not seen[ny][nx]):
                            seen[ny][nx] = True
                            q.append((nx, ny))
                out.append(cells)
        return out

    def bbox_fill(self):
        """덩이마다 외접 네모를 얼마나 채우나. 1.0 이면 완전한 직사각형.

        같은 자(고도)로 잰 공식 맵 125장의 사분위는 **0.36~0.73** 이다.
        1.0 에 가까우면 네모난 언덕이다.
        """
        out = []
        for cells in self.blobs():
            if len(cells) < 40:
                continue
            xs = [c[0] for c in cells]
            ys = [c[1] for c in cells]
            area = (max(xs) - min(xs) + 1) * (max(ys) - min(ys) + 1)
            out.append(len(cells) / area)
        return out

    def high_pct(self) -> float:
        n = sum(1 for row in self.g for v in row if v == HIGH)
        return 100.0 * n / (self.w * self.h)

    # -- 내보내기 --------------------------------------------------------
    def strokes(self, low_terrain: int, high_terrain: int, brush: int = 1):
        """ISOM 붓질 목록. **유효 마름모 자리에만** 찍는다.

        처음에 "가로 짝수 칸마다" 찍었는데 틀렸다. MappingCore 의
        `IsomDiamond` 는 `(i, j)` 에서 `(i+j)%2==0` 인 것만 유효하고,
        그 타일 평면 중심이 `(2i, j)` 다. 즉 **줄마다 엇갈린다**:

            j 짝수 → x = 0, 4, 8, 12, ...
            j 홀수 → x = 2, 6, 10, 14, ...

        짝수 칸마다 찍으면 같은 마름모를 두 번 찍고 엇갈림을 무시한다.
        인접 마름모 사이 벡터가 `(±2, ±1)` 이라는 뜻이지 `(2,1)` 직사각
        격자를 훑으라는 뜻이 아니다.

        **브러시는 1 로 못 박는다.** 큰 브러시는 경계를 예측하기 어렵게
        넓힌다 — 면을 채우는 데 쓸 것이 아니다.
        """
        out = []
        for j in range(self.h):
            for i in range(j & 1, self.w // 2, 2):
                x = 2 * i
                if x >= self.w:
                    continue
                out.append((x, j, high_terrain if self.g[j][x] == HIGH
                            else low_terrain, brush))
        return out

    # **저지대도 함께 칠한다 — 시험해 보고 이쪽이 맞았다.**
    #
    # 자문(grok)이 "바닥 지형은 칠하지 마라. 맵 생성 때 깔린 값은 ISOM
    # `Modified` 가 지워진 상태라 솔버가 자유롭게 쓸 수 있고, 다시 칠하면
    # 마름모가 잠겨 검은 구멍이 난다" 고 했다. 근거로 MappingCore 의
    # `setDiamondIsomValues` · `countNeighborMatches` 를 들었다.
    #
    # 그대로 해 봤다. **그림이 더 나빠졌다** — 고지대만 1,520번 칠하니
    # 맵 전체가 헤링본 카펫 무늬가 됐다. 낱개로 칠한 마름모가 이웃과
    # 맞물릴 상대가 없어 제각기 톱니로 처리된 것이다. 저지대까지 4,096번
    # 칠한 판은 고지 덩이가 제대로 나왔고 검은 칸도 0% 였다.
    #
    # 이론은 그럴듯했지만 재 보니 틀렸다. 검은 구멍 위험은 실제로 재서
    # 0% 였고(두 판 다), 무늬 쪽 손해가 훨씬 컸다. **자문 말도 재보고
    # 따른다.**
    #
    # 다만 함께 확인된 사실 둘은 지킨다:
    #   - 고지 집합은 **마름모 열 두 개(타일 4칸) 이상** 넓어야 한다.
    #     4이웃이 모두 `(dx±1, dy±1)` 이라 같은 열의 위아래는 이웃이
    #     아니다 — 폭 한 열은 서로 안 이어진 점들이다.
    #   - 붓질이 끝난 뒤에는 그 자리에 ISOM 을 다시 칠하지 않는다.

    # 마름모 중심 격자의 덮기 반경은 약 1.25 타일이다. 지키고 싶은 통로
    # 폭·목 폭은 **목표보다 2.5 타일 넓게** 잡아야 양자화에 안 먹힌다.
    QUANT_RADIUS = 1.25

    def ascii(self, step: int = 2) -> str:
        """사람이 읽을 그림. 렌더보다 이게 먼저다 — 두뎃이 눈을 속인다."""
        rows = []
        for y in range(0, self.h, step):
            rows.append("".join("#" if self.g[y][x] == HIGH else "."
                                for x in range(0, self.w, step)))
        return "\n".join(rows)


# ------------------------------------------------------------------ 램프

def reserve_ramp(field: Field, tx: int, ty: int, direction: str,
                 pad: int = 2) -> tuple[int, int]:
    """램프 자리를 예약한다. 돌려주는 것은 실제로 쓸 왼위 좌표.

    램프는 6x6 강체이고 **위아래 고도가 달라야** 한다. 다 만든 뒤 끼워
    넣으면 한쪽이 이미 저지대가 되어 터진다. 여기서 먼저:

      - 램프 네모를 저지대로 비워 둔다 (램프 타일이 그 자리를 채운다)
      - 램프 위쪽(고지대 쪽)을 고지대로 못 박는다
      - 램프 아래쪽(저지대 쪽)을 저지대로 못 박는다
      - 그 셋을 모두 **얼려** 형태 연산이 건드리지 못하게 한다

    `direction` 은 "down"(고지대가 위) · "up" · "left" · "right".
    """
    if direction not in ("down", "up", "left", "right"):
        raise CliError(f"램프 방향은 down·up·left·right 입니다: {direction!r}")
    # 램프 자체
    field.freeze_rect(tx, ty, 6, 6, LOW)
    if direction == "down":
        field.freeze_rect(tx - pad, ty - 4, 6 + 2 * pad, 4, HIGH)
        field.freeze_rect(tx - pad, ty + 6, 6 + 2 * pad, 4, LOW)
    elif direction == "up":
        field.freeze_rect(tx - pad, ty + 6, 6 + 2 * pad, 4, HIGH)
        field.freeze_rect(tx - pad, ty - 4, 6 + 2 * pad, 4, LOW)
    elif direction == "right":
        field.freeze_rect(tx - 4, ty - pad, 4, 6 + 2 * pad, HIGH)
        field.freeze_rect(tx + 6, ty - pad, 4, 6 + 2 * pad, LOW)
    else:
        field.freeze_rect(tx + 6, ty - pad, 4, 6 + 2 * pad, HIGH)
        field.freeze_rect(tx - 4, ty - pad, 4, 6 + 2 * pad, LOW)
    return tx, ty


# ------------------------------------------------------------------ 설계

def design(width: int, height: int, players: int, symmetry: str,
           starts, rng, min_radius: float = 3.0,
           ring=((0.28, 10.0, 0.0), (0.42, 11.0, 0.55)),
           notch: float = 2.6, per_mul: int = 2) -> Field:
    """**기능 그래프에서 고도 마스크를 만든다.**

    노드는 본진 언덕 · 센터 고지 · 바깥 확장이고, 그 사이 저지대가 길이다.

    **바닥이 이미 저지대다.** 처음 판에서 "본진에서 센터로 저지대 길을
    판다" 는 단계를 넣었더니 그 길이 본진 언덕과 센터 고지를 통째로
    지워, 고지대가 3% 만 남고 균일 창이 94% 가 되었다. 길은 아무것도
    안 해도 이미 저지대다. **저지대로 깎는 것은 고지 덩이에 목을 낼
    때만** 쓴다.

    순서가 중요하다. 램프를 먼저 예약해 얼리고, 원판으로 칠하고,
    마지막에 형태 연산으로 곡률을 못 박는다. 거꾸로 하면 형태 연산이
    램프 자리를 지운다.
    """
    f = Field(width, height)
    cx, cy = width / 2.0, height / 2.0
    R = min(width, height)

    # 1) 본진 언덕. 원판 둘을 겹쳐 완전한 원을 피한다 — 완전한 원도
    #    네모만큼이나 기계 티가 난다 (bbox 채움 0.79).
    for (sx, sy) in starts:
        ang = math.atan2(cy - sy, cx - sx)
        f.disk(sx, sy, 12.0)
        f.disk(sx + 8 * math.cos(ang + 0.9), sy + 8 * math.sin(ang + 0.9), 8.0)
        f.disk(sx + 9 * math.cos(ang - 1.1), sy + 9 * math.sin(ang - 1.1), 7.0)

    # 2) 센터 고지 — 덩이 여럿을 겹쳐 하나의 울퉁불퉁한 섬으로
    f.disk(cx, cy, 10.0)

    # 3) 고지 고리 — **사람 수와 무관하게 덮는 양이 같아야 한다.**
    #    처음에는 씨앗 몇 개에 대칭 복사를 걸었는데, 2인용(rot180)은
    #    복사가 둘뿐이라 고지대가 20% 밖에 안 됐다 (공식은 34~45%).
    #    한 사분면 안에 `per` 개를 고른 각도로 놓아, 사람 수가 몇이든
    #    한 바퀴에 같은 수가 돌게 한다.
    per = max(2, 8 // max(1, players)) * per_mul
    for frac, rad, phase in ring:
        for j in range(players):
            for t in range(per):
                ang = 2 * math.pi * (j / players + (t + phase) / (players * per))
                f.disk(cx + R * frac * math.cos(ang),
                       cy + R * frac * math.sin(ang), rad)

    # 4) 가운데 길과 만 — **여기서만** 저지대로 깎는다. 바닥은 이미
    #    저지대이므로 "길을 판다" 는 단계는 필요 없다 (그걸 넣었더니
    #    본진 언덕과 센터 고지가 통째로 지워졌다). 저지대 깎기는 통짜
    #    언덕에 목을 내고 둘레를 들쭉날쭉하게 하는 데만 쓴다.
    for j in range(players):
        for t in range(per):
            ang = 2 * math.pi * (j / players + (t + 0.5) / (players * per))
            f.path([(cx + R * 0.10 * math.cos(ang),
                     cy + R * 0.10 * math.sin(ang)),
                    (cx + R * 0.50 * math.cos(ang),
                     cy + R * 0.50 * math.sin(ang))], notch, LOW)

    # 5) 램프 — **형태 연산보다 먼저** 예약한다
    ramps = []
    for (sx, sy) in starts:
        ang = math.atan2(cy - sy, cx - sx)
        rx = int(sx + 12 * math.cos(ang)) - 3
        ry = int(sy + 12 * math.sin(ang)) - 3
        vertical = abs(math.sin(ang)) > abs(math.cos(ang))
        d = ("down" if vertical and sy < cy else
             "up" if vertical else
             "right" if sx < cx else "left")
        if 8 <= rx < width - 14 and 8 <= ry < height - 14:
            ramps.append(reserve_ramp(f, rx, ry, d))

    # 6) 곡률 불변식 — 열고 닫아 얇은 돌출과 홈을 없앤다
    f.open_close(min_radius)
    f.clear_border()
    f._restore()
    f.ramps = ramps
    return f


# 공식·리그 밀리맵 125장을 **고도로만** 재서 얻은 기준값.
# (scratchpad/elev_stat.py. 타일 종류로 잰 값과 섞어 쓰면 안 된다)
# 문턱은 타일셋마다 다르다 (`data/terrain-types.json` 을 칠해 보고 쟀다).
# Badlands·Ashworld·Space 는 고도 2, Jungle·Desert·Ice·Twilight 는 1.
HIGH_THRESHOLD = {0: 2, 1: 2, 2: 99, 3: 2, 4: 1, 5: 1, 6: 1, 7: 1}
OFFICIAL = {"uniform": (61.8, 71.4), "high_pct": (32.6, 42.2),
            "bbox_fill": (0.42, 0.63)}


def report(f: Field) -> str:
    fills = f.bbox_fill()
    u = f.uniform_window_pct()
    hp = f.high_pct()
    mark = lambda v, lo, hi: "ok" if lo <= v <= hi else "!!"
    out = [f"고지대 {hp:.0f}% [{mark(hp, *OFFICIAL['high_pct'])}] "
           f"(공식 사분위 {OFFICIAL['high_pct'][0]}~{OFFICIAL['high_pct'][1]})",
           f"균일 창 {u:.1f}% [{mark(u, *OFFICIAL['uniform'])}] "
           f"(공식 사분위 {OFFICIAL['uniform'][0]}~{OFFICIAL['uniform'][1]})"]
    if fills:
        srt = sorted(fills)
        med = srt[len(srt) // 2]
        out.append(f"덩이 {len(fills)}개 bbox 채움 "
                   f"{srt[0]:.2f}~{srt[-1]:.2f} 중앙 {med:.2f} "
                   f"[{mark(med, *OFFICIAL['bbox_fill'])}] "
                   f"(공식 사분위 {OFFICIAL['bbox_fill'][0]}~"
                   f"{OFFICIAL['bbox_fill'][1]})")
        # **최댓값으로 재지 않는다.** 공식 맵도 덩이 하나가 0.92 까지
        # 간다 (Benzene). 네모난 언덕은 덩이가 다 네모라 중앙값이 튄다.
    else:
        out.append("고지 덩이 없음")
    return "\n  ".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="밀리맵 고도 형상 층")
    ap.add_argument("out")
    ap.add_argument("--players", type=int, default=4)
    ap.add_argument("--size", default="128x128")
    ap.add_argument("--tileset", default="jungle",
                    choices=sorted(("badlands", "jungle", "ashworld",
                                    "desert", "ice", "twilight", "space")))
    ap.add_argument("--radius", type=float, default=3.0,
                    help="최소 곡률 반경 (타일)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--dry", action="store_true", help="그림만 찍고 안 만든다")
    ap.add_argument("--install")
    a = ap.parse_args(argv)

    import random
    rng = random.Random(a.seed)
    W, H = (int(v) for v in a.size.lower().split("x"))
    symmetry = "rot90" if a.players == 4 else "rot180"
    base = (W * 0.18, H * 0.18)
    starts = scmap.symmetric_points(base[0], base[1], symmetry,
                                    a.players, W, H)

    f = design(W, H, a.players, symmetry, starts, rng, a.radius)
    print(f"최소 곡률 반경 {a.radius} 타일, 램프 {len(f.ramps)}자리")
    print(report(f))
    print()
    print(f.ascii())
    if a.dry:
        return 0

    TS = {"badlands": 0, "space": 1, "ashworld": 3, "jungle": 4,
          "desert": 5, "ice": 6, "twilight": 7}
    ts = TS[a.tileset]
    cli = scmap.new_map(a.out, W, H, ts, terrain=None, melee=True,
                        install=a.install)
    types = cli.terrain_types()
    low = next((v for k, v in types.items()
                if k.lower() in ("dirt", "jungle", "ash", "desert", "snow",
                                 "ice", "twilight", "space platform")), None)
    high = next((v for k, v in types.items() if k.lower().startswith("high")),
                None)
    if low is None or high is None:
        raise CliError(f"저지대·고지대 지형 번호를 못 찾았습니다: "
                       f"{sorted(types)}")
    print(f"지형 번호: 저지대 {low}, 고지대 {high}")
    st = f.strokes(low, high)
    print(f"ISOM 붓질 {len(st):,}번 (저지대까지 함께 — 재 보고 정한 것)...")
    cli.isom_batch(st)
    print(f"만들었습니다: {a.out}")
    print(f"  python3 preview.py {a.out} look.png   ← 반드시 그려 볼 것")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


# --------------------------------------------------------- 실제 배치 설계
#
# **`design()` 은 지표를 통과하고 그림은 틀렸다.** 고리에 덩이를 고른
# 각도로 스물 몇 개 놓으니 고지대 37% · 균일 창 64% · bbox 0.51 로 셋 다
# 실측 대역에 들어왔는데, 그려 보니 **꽃잎 열여섯 장짜리 눈송이**였다.
# 맵으로 보이지 않는다. `why-procedural-fails.md` 의 시도 2 와 같은 병이고,
# 그 글이 "형상 두 지표는 바닥을 치는 검사이지 좋은 맵의 증명이 아니다"
# 라고 미리 적어 둔 그대로다.
#
# 병의 자리는 래스터화가 아니라 **설계**였다. 실제 맵의 고지대는 고른
# 각도로 흩어진 덩이가 아니라 **이름이 있는 몇 개의 자리**다 — 본진
# 언덕, 앞마당, 센터, 확장. 아래는 그걸 그대로 적은 것이다.

def design_lanes(width: int, height: int, players: int, symmetry: str,
                 starts, rng, min_radius: float = 3.0) -> Field:
    """이름 있는 자리로만 만든다 — 본진·앞마당·센터 능선·확장.

    덩이 스물 몇 개가 아니라 **자리 여섯 개**다. 지표는 나빠질 수 있다
    (경계 길이가 줄어 균일 창이 올라간다). 그래도 이쪽이 맵에 가깝다.
    지표가 대역에 들었다고 맵이 되는 것이 아니라는 것을 눈으로 확인한
    뒤에 쓴 함수다.
    """
    f = Field(width, height)
    cx, cy = width / 2.0, height / 2.0

    # 1) 본진 언덕. 램프가 나갈 쪽(센터)과 자원이 나가는 쪽(맵 가장자리)을
    #    둘 다 덮는다. 미네랄은 스타팅에서 7타일 바깥에 서므로, 그 줄이
    #    절벽 띠에 걸리면 일꾼이 못 붙는다. 반지름 13 만으로는 그 줄이
    #    언덕 끝에 걸린다.
    for (sx, sy) in starts:
        ang = math.atan2(cy - sy, cx - sx)
        ox = -1.0 if sx < cx else 1.0
        oy = -1.0 if sy < cy else 1.0
        f.disk(sx, sy, 16.0)
        f.disk(sx + ox * 9, sy + oy * 6, 12.0)
        f.disk(sx + 8 * math.cos(ang), sy + 8 * math.sin(ang), 10.0)

    # 2) 센터 능선 — 스타팅을 잇는 선의 **수직** 방향으로 가로지른다.
    #    이게 있어야 지상군이 돌아가야 하고 고지 점령 싸움이 생긴다.
    ax, ay = starts[0]
    perp = math.atan2(cy - ay, cx - ax) + math.pi / 2
    L = min(width, height) * 0.34
    f.path([(cx - L * math.cos(perp), cy - L * math.sin(perp)),
            (cx - L * 0.35 * math.cos(perp) + 6, cy - L * 0.35 * math.sin(perp)),
            (cx + L * 0.35 * math.cos(perp) - 6, cy + L * 0.35 * math.sin(perp)),
            (cx + L * math.cos(perp), cy + L * math.sin(perp))], 8.0)

    # 3) 능선에 목을 둘 낸다 — 통짜 능선은 맵을 두 쪽으로 갈라 버린다.
    for sign in (-1, 1):
        gx = cx + sign * L * 0.58 * math.cos(perp)
        gy = cy + sign * L * 0.58 * math.sin(perp)
        f.path([(gx - 9 * math.cos(perp + math.pi / 2),
                 gy - 9 * math.sin(perp + math.pi / 2)),
                (gx + 9 * math.cos(perp + math.pi / 2),
                 gy + 9 * math.sin(perp + math.pi / 2))], 4.0, LOW)

    # 4) 확장 언덕 — 맵 네 변 가운데. 작고 낮은 대. 확장 자리다.
    for (bx, by) in f.symmetric_seeds([(cx, height * 0.13)],
                                      symmetry, max(2, players)):
        f.disk(bx, by, 9.0)
        f.disk(bx + 6, by + 3, 7.0)

    # 5) 램프 — 형태 연산보다 먼저
    ramps = []
    for (sx, sy) in starts:
        ang = math.atan2(cy - sy, cx - sx)
        rx = int(sx + 13 * math.cos(ang)) - 3
        ry = int(sy + 13 * math.sin(ang)) - 3
        vertical = abs(math.sin(ang)) > abs(math.cos(ang))
        d = ("down" if vertical and sy < cy else "up" if vertical
             else "right" if sx < cx else "left")
        if 8 <= rx < width - 14 and 8 <= ry < height - 14:
            ramps.append(reserve_ramp(f, rx, ry, d))

    f.open_close(min_radius)
    f.clear_border()
    f._restore()
    f.ramps = ramps
    return f
