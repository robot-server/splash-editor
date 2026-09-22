# 타일셋 — 게임이 이미 알려 주는 것

**여기 있는 것은 전부 설치본에서 읽은 값이다.** 맵에서 추정하거나 손으로
적은 것이 아니다. 잰 방법은 항목마다 적어 두었다.

맵을 만들 때 **여기서 먼저 확인한다.** 이 폴더에 있는 것을 짐작으로
대신하다가 같은 실수를 여러 번 했다.

## 여덟 타일셋

| 번호 | 이름 | 램프 | 비고 |
| --- | --- | --- | --- |
| 0 | Badlands | 있음 | 고지 문턱 2 |
| 1 | Space Platform | 있음 | **평지가 고도 1** (Platform), 고지 문턱 2 |
| 2 | Installation | 없음 | 실내. 고지대 자체가 없다 |
| 3 | Ashworld | 있음 | 고지 문턱 2 |
| 4 | Jungle | 있음 | 고지 문턱 1 |
| 5 | Desert | 있음 | 고지 문턱 1 |
| 6 | Ice | 있음 | 고지 문턱 1 |
| 7 | Twilight | 있음 | 고지 문턱 1 |

## 문서

| 글 | 무엇이 있나 |
| --- | --- |
| [terrain-types.md](terrain-types.md) | 지형 종류 → ISOM 번호·타일 그룹·고도. **고지 문턱이 타일셋마다 다르다** |
| [isom.md](isom.md) | ISOM 마름모 격자. 좌표, 이웃, 브러시, rot90 문제 |
| [ramps.md](ramps.md) | 램프. **램프는 두뎃이다** |
| [doodads.md](doodads.md) | 두뎃. 갈래는 지형 종류 이름과 같다. 걷기를 막는 것 |
| [sprites.md](sprites.md) | 스프라이트. 밀리맵 중립 건물, 튕기는 목록 |
| [disabled-units.md](disabled-units.md) | 유닛을 꾸밈으로. 건설 중인 건물 만들기, 맵 날리는 조합 |
| [colors.md](colors.md) | 타일 그룹의 실제 색. 눈으로 구분되는 지형을 고를 때 |

## 데이터 파일

기계가 읽는 것은 `.agents/skills/starcraft-map/data/` 에 있다.

| 파일 | 만든 스크립트 | 무엇 |
| --- | --- | --- |
| `terrain-types.json` | `measure_terrain_types.py` | 지형 종류마다 ISOM 으로 칠해 보고 잰 그룹·고도 |
| `tile-colors.json` | `measure_tile_colors.py` | 렌더러로 그려서 잰 그룹별 평균 RGB |
| `doodad-walk.json` | `measure_doodads.py` | 두뎃이 걷기를 막는가 |
| `ramps.json` | `measure_ramps.py` | 걸어서 검증한 램프 |

## 파이썬에서

```python
import scmap
scmap.tileset_tiles(cli, ts)      # 타일 → (고도, 걷기, 짓기, 램프, 걷기비트)
scmap.terrain_groups(cli, ts)     # 그룹 → 같은 것 (그룹 단위)
cli.terrain_types()               # 지형 종류 이름 → ISOM 번호
scmap.group_terrain_name(ts)      # 타일 그룹 → 지형 종류 이름
scmap.terrain_types_table(ts)     # 종류 → 그룹·고도 (잰 표)
scmap.doodad_walk_table(ts)       # 두뎃 번호 → 크기·갈래·걷기를 막는가
cli.doodad_catalogue()            # 두뎃 목록 (번호, 폭, 높이, 갈래)
```
