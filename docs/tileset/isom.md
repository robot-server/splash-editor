# ISOM — 마름모 격자

지형을 **자연스럽게** 그리는 길이다. 타일 값을 직접 쓰는 사각형
방식과 달리, 절벽과 경계가 **저절로 이어진다.**

| | |
| --- | --- |
| **밀리맵** | **ISOM 만 쓴다** |
| **유즈맵** | **장르가 정한다** → [../usemap/terrain.md](../usemap/terrain.md) |

## 격자가 타일 격자가 아니다

ISOM 은 **마름모 격자** 위에서 움직인다. chkdraft 의 `Chk::IsomDiamond`
를 읽어 확인한 것:

| | |
| --- | --- |
| 쓸 수 있는 자리 | `(i + j) % 2 == 0` 인 `(i, j)` |
| 그 마름모의 타일 중심 | `(2i, j)` |
| 붓이 닿는 픽셀 | `(64i, 32j)` |
| 이웃 | 네 방향 모두 **`(±1, ±1)`** |
| 덮는 범위 | 반지름 약 **1.25 타일** |

**가로 두 칸이 세로 한 칸과 같은 한 걸음**이다. 그래서 가로 좌표는
짝수로 준다 — 홀수를 주면 격자에서 어긋난다.

```python
cli.isom(tile_x, tile_y, terrain)          # 한 번
cli.isom_batch([(x, y, terrain), ...])     # 여러 번 (빠르다)
scmap.isom_fill(cli, terrain, x, y, w, h)  # 네모를 채운다
scmap.isom_blob(cli, terrain, rng, cx, cy, area)   # 덩이 하나
```

`isom_fill` 이 짝수 좌표를 알아서 맞춘다.

## **rot90 은 대칭이 아니다**

마름모 격자는 가로와 세로의 자가 다르므로, **90도 회전이 격자를
그대로 보내지 않는다.** 4인용 맵을 90도 회전 대칭으로 만들 때
`terrain mirror rot90` 으로 베끼면 절벽 방향이 틀어진다.

**붓질 좌표를 돌려 다시 놓는다.** → [../melee/terrain.md](../melee/terrain.md)

## 지형 종류를 골라야 한다

ISOM 은 "이 지형으로 칠해라" 고 말하는 것이다. 타일 번호가 아니라
**지형 종류 번호**를 준다.

```sh
splash-cli terrain types 맵 --install "$SC_INSTALL"
```

```python
scmap.isom_terrain_ids(cli, tileset_id)
# {"low": [걷는 낮은 지형들], "high": [걷는 높은 지형들],
#  "blocked": [못 걷는 지형들], "names": {...}}
```

정글은 13종이 있고 **낮고 걷는 것 5 · 높고 걷는 것 7 · 못 걷는 것 1**
로 갈린다. 세 종류만 쓰면 바닥이 한 가지 색으로 남는다 —
실측 RPG 유즈맵의 타일 그룹 중앙값은 **248개**다.

## 바닥은 칠하고 시작한다

한동안 "바닥을 칠하지 말고 필요한 데만 놓아라" 는 조언을 따랐다가
지형이 헤링본 카펫처럼 되었다. **되돌렸다.**

바닥을 한 번 깔고 그 위에 덩이를 얹는다 (`scmap.isom_landscape`).
`terrain fill … 0` 으로 검게 덮는 것은 **하지 않는다** — 실측 유즈맵의
검은 칸 중앙값은 0.0% 다.

## 램프는 ISOM 이 아니다

`terrain types` 에 램프 항목이 없다. **램프는 두뎃**이다 →
[ramps.md](ramps.md).

## 사각형과 섞지 않는다

한 맵에서 ISOM 과 사각형 편집을 섞으면 ISOM 격자와 실제 타일이
어긋나, 나중에 붓을 한 번만 더 대도 지형이 뭉개진다. 타일을 직접 쓰는
`scatter_tile_variants` 같은 것은 **사각형으로 지은 맵에서만** 쓴다.

## 관련

- [README.md](README.md) — 타일셋 갈래
- [terrain-types.md](terrain-types.md) — 지형 종류와 고도
- [ramps.md](ramps.md) — 램프 두뎃
- [../melee/terrain.md](../melee/terrain.md) — 대칭과 실제로 놓는 법
- [../usemap/terrain.md](../usemap/terrain.md) — 장르가 정한다
