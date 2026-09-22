# 지형 종류 — ISOM 번호·고도·타일 그룹

**어떻게 쟀나.** `measure_terrain_types.py` 가 타일셋마다 지형 종류를
하나씩 빈 맵에 ISOM 으로 칠하고, 나온 타일의 그룹과 고도를 센다.
결과는 `data/terrain-types.json`.

## 고지 문턱은 타일셋마다 다르다

이걸 세 번 틀렸다. 한 숫자로 "고도 2 이상이 고지대" 라고 잡으면
Fighting Spirit·Lost Temple·Tau Cross·Luna 가 전부 **"고지대 0.0%"** 로
나온다. 램프가 있는 맵에 고지대가 없을 수는 없다.

| 타일셋 | 저지대 | 1단 고지 | 2단 고지 | **고지 문턱** |
| --- | --- | --- | --- | --- |
| Badlands · Ashworld | 고도 0 | **아예 없다** | High \* = 2 | **2** |
| Space Platform | Space · Low Platform = 0 | **Platform · Plating = 1 (평지!)** | High Platform = 2 | **2** |
| **Installation** | — | **Floor · Plating = 1 (실내 바닥, 평지!)** | = 2 | **없음으로 친다** |
| Jungle · Desert · Ice · Twilight | 고도 0 | **High \* = 1** | 드문 3단(High Temple 등) = 2 | **1** |

### 타일 수로 본 진짜 구조

`splash-cli tileset-tiles` 를 여덟 타일셋에 전수로 돌려 고도를 셌다.

| 타일셋 | 고도 0 | 고도 1 | 고도 2 |
| --- | ---: | ---: | ---: |
| badlands | 5039 | **0** | 2308 |
| ashworld | 2921 | **0** | 1206 |
| space | 4681 | 1461 | 1021 |
| installation | 1514 | 1025 | 560 |
| jungle | 5388 | 2134 | 370 |
| desert | 6334 | 2069 | 370 |
| ice | 5614 | 1628 | 346 |
| twilight | 6282 | 1972 | 346 |

**"타일셋마다 제각각" 이 아니다.** 두 갈래다.

| | 타일셋 | 뜻 |
| --- | --- | --- |
| **2계층 (0 / 2)** | badlands · ashworld | **고도 1 타일이 하나도 없다.** 문턱 2 는 고른 것이 아니라 **강제** |
| **3계층 (0 / 1 / 2)** | 나머지 여섯 | 문턱을 골라야 한다 |

그리고 3계층 중 **기준선이 0 이 아닌 것이 둘**이다.

| | 평지가 몇인가 |
| --- | --- |
| **Space Platform** | **Platform = 1** 이 평지 |
| **Installation** | **Floor · Plating = 1** 이 실내 바닥 |

> **"Space 만 특별하다" 로 읽으면 틀린다.** Installation 도 똑같이
> 기준선이 1 로 밀려 있다. 다만 실내라 고지대를 쓸 일이 거의 없어
> `HIGH_THRESHOLD` 에서 `None` 으로 둔다.

### 이름을 믿으면 안 된다

| 이름 | 실제 고도 |
| --- | --- |
| Twilight **`High Sunken Ground`** | **0** (High 인데 저지대) |
| Desert **`Sandy Sunken Pit`** | **1** (구덩이인데 High Dirt 와 같은 단) |

```python
HIGH_THRESHOLD = {0: 2, 1: 2, 2: None, 3: 2, 4: 1, 5: 1, 6: 1, 7: 1}
```

> **수가 이상하면 맵이 아니라 자를 의심한다.** 이름난 래더 맵이 검사에
> 떨어지면 검사가 틀린 것이다.

## 지형 종류마다 무엇이 나오나

`ISOM 번호` 는 `terrain isom <맵> <픽셀x> <픽셀y> <번호>` 에 넘기는 값이다.
`타일 그룹` 은 그 종류를 칠했을 때 MTXM 에 들어가는 그룹 (앞 넷만).

### badlands

| 지형 종류 | ISOM 번호 | 고도 | 타일 그룹 |
| --- | --- | --- | --- |
| Dirt | 2 | 0 | 2, 3 |
| High Dirt | 3 | 2 | 4, 5 |
| Mud | 4 | 0 | 20, 21 |
| Water | 5 | 0 | 6, 7 |
| Grass | 6 | 0 | 8, 9 |
| High Grass | 7 | 2 | 10, 11 |
| Asphalt | 14 | 0 | 16, 17 |
| Rocky Ground | 15 | 0 | 12, 13 |
| Structure | 18 | 2 | 18, 19 |

### space

| 지형 종류 | ISOM 번호 | 고도 | 타일 그룹 |
| --- | --- | --- | --- |
| Space | 2 | 0 | 2, 3 |
| Platform | 3 | 1 | 4, 5 |
| Plating | 4 | 1 | 6, 7 |
| High Platform | 5 | 2 | 8, 9 |
| High Plating | 6 | 2 | 10, 11 |
| Solar Array | 7 | 1 | 12, 13 |
| Low Platform | 8 | 0 | 14, 15 |
| Rusty Pit | 9 | 0 | 18, 19 |
| Elevated Catwalk | 10 | 2 | 20, 21 |
| Dark Platform | 11 | 1 | 16, 17 |

### installation

| 지형 종류 | ISOM 번호 | 고도 | 타일 그룹 |
| --- | --- | --- | --- |
| Substructure | 2 | 0 | 2, 3 |
| Floor | 3 | 1 | 6, 7 |
| Substructure Plating | 4 | 0 | 4, 5 |
| Plating | 5 | 1 | 10, 11 |
| Roof | 6 | 0 | 8, 9 |
| Bottomless Pit | 7 | 0 | 12, 13 |
| Substructure Panels | 8 | 0 | 14, 15 |

### ashworld

| 지형 종류 | ISOM 번호 | 고도 | 타일 그룹 |
| --- | --- | --- | --- |
| Dirt | 2 | 0 | 2, 3 |
| Lava | 3 | 0 | 4, 5 |
| High Dirt | 4 | 2 | 6, 7 |
| High Lava | 5 | 2 | 8, 9 |
| Shale | 6 | 0 | 10, 11 |
| High Shale | 7 | 2 | 12, 13 |
| Magma | 8 | 0 | 14, 15 |
| Broken Rock | 9 | 0 | 16, 17 |

### jungle

| 지형 종류 | ISOM 번호 | 고도 | 타일 그룹 |
| --- | --- | --- | --- |
| Dirt | 2 | 0 | 2, 3 |
| High Dirt | 3 | 1 | 4, 5 |
| Mud | 4 | 0 | 26, 27 |
| Water | 5 | 0 | 6, 7 |
| Jungle | 8 | 0 | 8, 9 |
| Raised Jungle | 9 | 1 | 12, 13 |
| High Jungle | 10 | 1 | 18, 19 |
| Ruins | 11 | 0 | 14, 15 |
| High Ruins | 12 | 1 | 20, 21 |
| High Raised Jungle | 13 | 2 | 22, 23 |
| Rocky Ground | 15 | 0 | 10, 11 |
| Temple | 16 | 1 | 16, 17 |
| High Temple | 17 | 2 | 24, 25 |

### desert

| 지형 종류 | ISOM 번호 | 고도 | 타일 그룹 |
| --- | --- | --- | --- |
| Dirt | 2 | 0 | 2, 3 |
| High Dirt | 3 | 1 | 4, 5 |
| Dried Mud | 4 | 0 | 26, 27 |
| Tar | 5 | 0 | 6, 7 |
| Sand Dunes | 8 | 0 | 8, 9 |
| Sandy Sunken Pit | 9 | 1 | 12, 13 |
| High Sand Dunes | 10 | 1 | 18, 19 |
| Crags | 11 | 0 | 14, 15 |
| High Crags | 12 | 1 | 20, 21 |
| High Sandy Sunken Pit | 13 | 2 | 22, 23 |
| Rocky Ground | 15 | 0 | 10, 11 |
| Compound | 16 | 1 | 16, 17 |
| High Compound | 17 | 2 | 24, 25 |

### ice

| 지형 종류 | ISOM 번호 | 고도 | 타일 그룹 |
| --- | --- | --- | --- |
| Snow | 2 | 0 | 2, 3 |
| High Snow | 3 | 1 | 4, 5 |
| Moguls | 4 | 0 | 26, 27 |
| Ice | 5 | 0 | 6, 7 |
| Dirt | 8 | 0 | 8, 9 |
| Water | 9 | 0 | 12, 13 |
| High Dirt | 10 | 1 | 18, 19 |
| Grass | 11 | 0 | 14, 15 |
| High Grass | 12 | 1 | 20, 21 |
| High Water | 13 | 1 | 22, 23 |
| Rocky Snow | 15 | 0 | 10, 11 |
| Outpost | 16 | 1 | 16, 17 |
| High Outpost | 17 | 2 | 24, 25 |

### twilight

| 지형 종류 | ISOM 번호 | 고도 | 타일 그룹 |
| --- | --- | --- | --- |
| Dirt | 2 | 0 | 2, 3 |
| High Dirt | 3 | 1 | 4, 5 |
| Mud | 4 | 0 | 26, 27 |
| Water | 5 | 0 | 6, 7 |
| Crushed Rock | 8 | 0 | 8, 9 |
| Sunken Ground | 9 | 0 | 12, 13 |
| High Crushed Rock | 10 | 1 | 18, 19 |
| Flagstones | 11 | 0 | 14, 15 |
| High Flagstones | 12 | 1 | 20, 21 |
| High Sunken Ground | 13 | 0 | 22, 23 |
| Crevices | 15 | 0 | 10, 11 |
| Basilica | 16 | 1 | 16, 17 |
| High Basilica | 17 | 2 | 24, 25 |

## 쓰는 법

```python
types = cli.terrain_types()          # 이름 → ISOM 번호
tt = scmap.terrain_types_table(ts)   # 이름 → {index, groups, levels}
gn = scmap.group_terrain_name(ts)    # 타일 그룹 → 이름  (두뎃 짝지을 때)
```

두뎃 갈래가 **이 이름과 정확히 같다**. High Dirt 바닥에는 갈래가
"High Dirt" 인 두뎃을 놓는다 — [doodads.md](doodads.md).
