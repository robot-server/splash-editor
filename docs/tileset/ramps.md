# 램프(경사로) — **램프는 두뎃이다**

## 한 줄 요약

램프를 생타일로 찍지 않는다. **두뎃으로 놓는다.** 방향마다 다른 두뎃이고,
어느 자리에 붙는지는 `dddata.bin` 의 `DoodadPlacibility` 가 안다.

## 어떻게 알았나

두 출처가 같은 말을 한다.

**게임 데이터** — `sc.h` 의 미니타일 표에 `Ramp = BIT_4` 가 있다.
그 표시가 붙은 타일을 모아 보니 **전부 타일 그룹 1024 이상**, 즉
두뎃 영역이었다.

**스타 에디터 아카데미** ([기초3] 밀리맵의 필수 요소) —

> 본진 입구와 맵 중앙부에도 **두뎃으로 언덕으로 올라가는 계단**을
> 만들어줍니다.

그리고 ([기초2] 지형을 발라보자 2) —

> 이런식으로 나무, **언덕 입구** 등의 두뎃을 배치할 수 있습니다.
> … Options - Doodads 에서 **Allow illegal placement(= 두뎃 배치 조건
> 무시)** 에 체크 해제된 상태라면 두뎃이 사라지긴 합니다.

즉 SCMDraft 는 배치 조건을 기본으로 **강제**한다. 사용자가 말한
"벽에 접합이 되는지도 StarEdit 에 이미 있다" 가 이 표다.

## 내가 틀렸던 것

`scmap.RAMPS_BY_DIR` 은 손으로 적은 표였고 세 군데가 틀렸다.

| 틀린 것 | 실제 |
| --- | --- |
| 생타일 덩이로 보고 `ramp_rows(base, w, h)` 로 찍음 | 두뎃이다. `doodad place` 로 놓는다 |
| **네 방향이 같은 세트**였다 (바탕 0x4a70 에서 행만 잘라 씀) | 방향마다 다른 두뎃이다 |
| "Space·Desert·Ice·Twilight 는 램프가 없다" | 넷 다 있다 (Desert 607 램프타일, Space 484) |

바탕값 0x4a70 은 그룹 1191 이다 — 애초에 **두뎃 번호를 생타일처럼**
쓰고 있었다. 우하단 하나를 네 방향에 다 붙였으니 세 방향이 깨진다.

## 어느 두뎃이 램프인가

`measure_ramps.py` 가 타일셋마다 두뎃을 하나씩 놓아 보고, **램프 표시가
붙은 타일이 나오는 것**을 고른다. 결과는 `data/ramps.json`.

| 타일셋 | 램프 두뎃 | 갈래 |
| --- | --- | --- |
| Desert | **86종** | Cliff 18, Compound 18, High Compound 18, (High) Sandy Sunken Pit 32 |
| Space Platform | **72종** | **Elevated Catwalk Ramps** 18, Platform Wall 20, Low Platform Wall 18, Rusty Pit Wall 16 |
| Ice | 54종 | Cliff 18, Outpost 18, High Outpost 18 |
| Twilight | 53종 | Cliff 18, Basilica 18, High Basilica 17 |
| Jungle | 51종 | Cliff 17, Temple Wall 17, High Temple Wall 17 |
| Badlands | 35종 | Cliff 17, Structure Wall 18 |
| Ashworld | 17종 | Cliff 17 |
| Installation | **1종** | Substructure Wall (실내라 고지대가 거의 없다) |

**여덟 타일셋 전부 램프가 있다.** 내 표는 "Space·Desert·Ice·Twilight 는
램프가 없다" 고 적어 두었는데, 정작 그 넷이 램프가 가장 많다.

갈래가 여럿인 까닭은 **고지대 종류마다 램프가 따로** 있기 때문이다 —
Ice 라면 Cliff(흙 절벽)·Outpost·High Outpost 로 셋. 올라가는 곳의 지형에
맞는 갈래를 골라야 한다.

크기는 `2x5`·`4x4`·`4x5`·`4x6`·`5x6`·`6x4`·`6x5`·`6x6`·`8x3`·`8x5`·
`8x6`·`8x7`·`10x6` 이 섞여 있다. **6x6 하나가 아니다.**
가장 흔한 것은 `6x5`(전체의 약 30%)와 `4x5`.

## 방향을 어떻게 가리나

쉬워 보이는 방법 셋이 다 실패했다. 기록해 둔다.

| 해 본 것 | 왜 안 되나 |
| --- | --- |
| 두뎃의 고도 무늬를 본다 | 평지에 놓으면 고도가 전부 0 이다. 절벽에 놓아야 경사가 생긴다 |
| 절벽에 놓고 "통하는가" 를 본다 | **어느 두뎃이든** 절벽 위에 놓으면 제 타일로 덮어써서 구멍이 난다. 후보 35종이 네 방향을 다 통과했다 |
| `doodad place` 가 거절하는지 본다 | 이 CLI 는 `DoodadPlacibility` 를 **강제하지 않는다**. High Grass 두뎃을 흙바닥에 놓아도 받는다 |

되는 방법: **절벽에 놓고 램프 미니타일이 몇 개 깔리는지 센다.**
방향이 맞으면 많이 깔리고 틀리면 거의 안 깔린다.

검증은 차이로 한다:

1. ISOM 으로 한쪽을 고지대로 올린다. **생타일로 채우면 절벽이 안 생겨**
   그냥 걸어 넘어간다 — 반드시 ISOM 이어야 한다.
2. 램프 없이 고지↔저지가 **막혀 있는지** 먼저 확인한다. 안 막혀 있으면
   그 시험은 아무것도 재지 못한다.
3. 램프를 놓고 통하는지 + 램프 미니타일 수를 본다.

## 쓰는 법

```python
import json, scmap
ramps = json.load(open("data/ramps.json"))["ramps"]["badlands"]
down = [r for r in ramps if r["dir"] == "down"]
cli.edit("doodad", "place", cli.path, str(down[0]["id"]), str(x), str(y),
         "--install", cli.install)
```

**놓는 차례가 있다.** ISOM 붓질을 다 끝낸 뒤에 램프를 놓고, 그 뒤로는
그 자리에 ISOM 을 다시 칠하지 않는다. ISOM 섹션은 직접 찍은 램프를
모르기 때문에 덮어쓴다.

## 대칭

SCMDraft 의 지형 대칭 목록에 **`Isom. Rot Symmetry`(마름모 지형 기준
회전 대칭)** 가 따로 있다. 마름모 격자가 90도 회전에 안 맞는 문제를
에디터가 이미 다루고 있다는 뜻이다 — [isom.md](isom.md) 참고.
