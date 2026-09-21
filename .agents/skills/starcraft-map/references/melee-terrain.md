# 밀리맵 지형 놓기

지형은 ISOM 브러시로 놓는다. 절벽과 경계가 자동으로 이어진다. 다만
**두 가지는 ISOM 으로 안 된다** — 램프와 대칭이다. 아래에 대신하는
방법을 적는다. 모두 실제로 해 보고 그려 확인한 것이다.

---

## 0. 얼마나 채워야 하는가 — 공식 맵 전수 실측

지형을 얼마나 놓아야 "맵" 인지는 감이 아니라 수로 잡는다. 공식 리그 맵
**57개 전수**를 재어 얻은 값이다 (`splash-cli tileset-groups` 로 타일마다
높이·걷기·짓기를 판정해 셌다).

| | 공식 밀리 (중앙값) | 범위 |
| --- | --- | --- |
| 걸을 수 있는 땅 | **79%** | 41 ~ 98% |
| 지을 수 있는 땅 | **53%** | 30 ~ 78% |
| 높은 땅 | **36%** | 0 ~ 84% |
| 쓰인 타일 그룹 | **484** | 87 ~ 870 |
| 두들 개수 | **135** | 0 ~ 1046 |

읽는 법:

- **막힌 땅이 5분의 1이다.** 절벽·물·장애물이 맵의 21%를 차지한다.
  전부 걸어 다닐 수 있는 맵은 맵이 아니라 벌판이다.
- **높은 땅이 3분의 1이다.** 본진 언덕 하나로는 어림없다. 가운데에도,
  멀티 자리에도 언덕이 있어야 한다.
- **타일 그룹 484개.** 한두 가지 지형으로만 칠하면 100개도 안 나온다.
  바닥 지형을 두세 가지 섞고 가장자리를 다르게 두어야 이 수에 닿는다.
- **두들 135개.** 두들을 아예 놓지 않은 맵은 57개 중 4개뿐이다.

유즈맵은 **완전히 다르다** (인기 유즈맵 93개 전수):

| | 유즈맵 (중앙값) |
| --- | --- |
| 걸을 수 있는 땅 | 65% |
| 높은 땅 | 16% |
| 쓰인 타일 그룹 | **52** |
| 두들 개수 | **0** (93개 중 68개가 0) |

유즈맵에 두들을 안 놓는 것은 잘못이 아니다 — 그쪽이 표준이다. 밀리맵의
잣대를 유즈맵에 들이대지 않는다.

**만든 뒤 반드시 재 본다:**

```sh
splash-cli tileset-groups "$SC_INSTALL" <타일셋>   # 타일 그룹별 높이·걷기
python3 scripts/verify_map.py <맵>                 # 지형 밀도까지 재 준다
```


## 1. 캔버스 — `new` 에 `--install` 을 꼭 준다

```sh
splash-cli new out.scx 128 128 4 --melee --install "$SC_INSTALL" --terrain Dirt
```

`--install` 을 빼면 **타일이 전부 0 으로 남는다.** 화면은 새까맣고,
그 위에서는 ISOM 브러시가 아무것도 놓지 못한다 — 빈 칸 위에는 절벽을
이을 수 없기 때문이다. 반드시 바닥 지형을 깔고 시작한다.

`--terrain` 에 줄 이름은 타일셋마다 다르다:

```sh
splash-cli terrain types out.scx --install "$SC_INSTALL"
```

정글이면 Water, Dirt, Mud, Jungle, Rocky Ground, Ruins, Raised Jungle,
Temple, High Dirt, High Jungle, High Ruins, High Raised Jungle,
High Temple 열세 가지다.

## 2. ISOM 브러시

```sh
splash-cli terrain isom <맵> <픽셀x> <픽셀y> <지형번호> [브러시] \
    --install "$SC_INSTALL" --in-place
```

- 좌표는 **픽셀**이다. 타일 좌표에 32를 곱한다.
- ISOM 은 타일 격자가 아니라 **마름모 격자** 위에서 움직인다. 가로는
  타일 두 칸이 마름모 한 칸이다. 그래서 면을 칠할 때 **가로는 2칸씩**
  건너뛴다. 1칸씩 가면 같은 자리를 두 번 칠할 뿐이다.
- 고지대를 칠하면 둘레에 절벽이 저절로 생긴다.

파이썬에서는 `scmap.Cli.isom(tile_x, tile_y, terrain)` 이 픽셀 변환을
대신 해 준다.

## 3. 램프 — ISOM 으로는 안 된다

지형 종류 표에 램프가 없다. 고지대를 좁고 길게 빼 봐도 절벽만 길어질
뿐 비탈이 생기지 않는다 (해 보고 그려 확인했다).

램프는 **VF4 의 램프 비트가 선 타일**로만 놓인다. 그 타일이 무엇인지는
이 명령으로 본다:

```sh
splash-cli tileset-ramps "$SC_INSTALL" 4     # 정글
```

램프 한 벌은 **연속한 타일 그룹 몇 줄 x 연속한 서브타일 몇 칸**의
직사각 블록이다:

```
tile(r, c) = (기준그룹 + r) * 16 + (기준서브 + c)
```

### 방향별 램프 표 — 전수 탐색으로 찾고 길찾기로 검증했다

**한 가지 기준값을 네 곳에 똑같이 찍으면 안 된다.** 공식 맵 57개의 램프를
둘레 높이로 갈라 보니 방향마다 다른 값을 쓴다 (투혼 네 본진이
`0x4383`/`0x4320`/`0x024c`/`0x61b2` 로 제각각이다).

공식 맵을 세는 것만으로는 "어느 값을 어디에 놓아야 통하는가" 가 안 나와서,
타일셋마다 **평지 위에 고지대 덩이 하나를 얹은 시험 맵**을 만들고 램프 비트가
선 (그룹, 서브) 을 모두 후보로 찍어 본 뒤 **미니타일 길찾기로 걸러냈다.**

| 타일셋 | 아래로 내려가는 램프 | 위로 올라가는 램프 |
| --- | --- | --- |
| 0 Badlands | `0x4A70` off −3 | `0x45D0` off −2 |
| 1 Space | `0x3D60` off 0 | `0x3D60` off −3 |
| 3 Ashworld | (못 찾음) | `0x5250` off −6 |
| 4 Jungle | `0x4300` off −3 | `0x4300` off −5 |
| 5 Desert | (못 찾음) | `0x35D0` off −6 |
| 6 Ice | `0x65A0` off −3 | `0x65A0` off −6 |
| 7 Twilight | `0x34D0` off 0 | `0x34E0` off −2 |

모두 6x6 블록이다. `off` 는 **고지대가 끝나는 줄**에서 블록을 몇 칸 밀지다.
Ashworld·Desert 의 "아래로" 는 시험 지형에서 통하는 자리를 못 찾았다 —
그쪽은 `place_ramp_checked` 가 다른 후보를 훑는다.

`scmap.RAMPS_BY_DIR` 에 들어 있고, `place_ramp_checked` 가 표의 off 를 먼저
써 보고 안 되면 위아래로 밀어 가며 다시 찾는다. **찍은 뒤에는 언제나
길찾기로 확인하고, 안 되면 되돌린다.**

### 눈으로 본 것은 검증이 아니다

램프가 이어졌는지는 **미니타일 단위로만** 알 수 있다. 게임은 타일당 4x4 칸
격자에서 길을 찾는다. "이 타일에 걸을 수 있는 칸이 하나라도 있는가" 로
판정하면 틀린다 — 그 잣대로 "정상" 이라 판단한 램프가 실제로는 위아래가
막혀 있었다.

```python
grid = scmap.walk_grid(cli, tileset_id, x, y, w, h)   # 4배 해상도 걷기 격자
a = scmap.nearest_walkable(grid, hx*4+2, hy*4+2)
b = scmap.nearest_walkable(grid, lx*4+2, ly*4+2)
scmap.walk_reachable(grid, a, b)                       # 진짜 답
```

걷기 비트는 `splash-cli tileset-groups` 의 마지막 칸(16진 16비트)에서 온다.

### 램프 둘레는 비워 둔다

램프는 본진에서 나가는 유일한 길목이다. **자원을 램프 쪽에 놓으면
입구가 막힌다.** 미네랄과 가스는 램프 반대편(본진 안쪽)에 두고, 램프
앞뒤로 최소 3타일은 아무것도 놓지 않는다. 베스핀이 램프 위에 얹힌 적이
있다 — 자원 오프셋을 본진 방향에 따라 회전시키면서 램프 쪽을 비켜 가게
하지 않아서였다.

## 4. 대칭 — `terrain mirror` 로 절벽을 베끼지 않는다

`splash-cli terrain mirror` 는 **타일 값을 그대로 옮긴다.** 타일 그림에는
방향이 있어서(남향 절벽 타일은 옮겨도 남향이다) 베낀 쪽 절벽이 뒤집힌
채로 남는다. 걸을 수 있는 자리까지 어긋나 맵이 못 쓰게 된다.

평지 무늬를 베끼는 데에는 써도 되지만, **절벽이 든 지형에는 쓰지 않는다.**

대신 **같은 붓질을 회전 좌표에 다시 놓는다**:

```python
N = 128
def rot(x, y, k):          # 90도씩 k 번
    for _ in range(k):
        x, y = (N - 1 - y), x
    return x, y

strokes = [(x, y) for y in range(8, 18) for x in range(8, 24, 2)]
for k in range(4):                      # 네 귀퉁이
    for (x, y) in strokes:
        cli.isom(*rot(x, y, k), terrain=high_dirt)
```

이러면 ISOM 솔버가 각 자리에서 절벽을 새로 풀기 때문에 네 곳 모두
제대로 선다. `scmap.symmetric_points()` 가 rot90·rot180·좌우·상하·
방사 대칭 좌표를 만들어 준다.

대칭 고르기 (공식 맵 실측):

- **2인용 → rot180.** 잰 4개 모두 그랬고 거울 대칭은 하나도 없었다.
- **4인용 → rot90 (정사각형 맵) 또는 rot180.**
- **3·5·6인용 → 방사 대칭** (가운데를 축으로 고르게 돌린다).

## 5. 확인

숫자만 보고 끝내지 않는다. 지형은 눈으로 봐야 안다.

```sh
python3 scripts/preview.py out.scx look.png                 # 전체
python3 scripts/preview.py out.scx base.png --tiles 0 0 42 32  # 본진만
python3 scripts/verify_map.py out.scx
```

보는 것:

- 네 스타팅의 생김새가 같은가 (돌아간 모양이어야 한다).
- 램프가 고지대와 저지대를 실제로 잇는가. 이음매가 뜨지 않는가.
- 본진에서 앞마당까지, 앞마당에서 가운데까지 실제로 걸어갈 수 있는가.
- 가운데가 비어 있지 않은가 — 뼈대 상태라면 지형을 더 얹어야 한다.

## 6. 그 밖의 지형 명령

```sh
# 타일 하나 / 네모 칠하기 (ISOM 을 거치지 않는다 — 무늬용)
splash-cli terrain set <맵> <x> <y> <타일값> --in-place
splash-cli terrain fill <맵> <x> <y> <w> <h> <타일값> --in-place

# 지형을 파일로 베껴 두었다가 다른 자리에 붙이기
splash-cli terrain copy <맵> <x> <y> <w> <h> patch.tiles
splash-cli terrain paste <맵> <x> <y> patch.tiles --in-place

# 두들 (바위·나무 같은 장식). 지날 수 없는 것이 많아 길목에 쓰면 초크가 된다
splash-cli doodad list <맵> --catalogue --install "$SC_INSTALL"
splash-cli doodad place <맵> <두들번호> <타일x> <타일y> --install "$SC_INSTALL" --in-place
splash-cli doodad check <맵> --install "$SC_INSTALL"     # 어긋난 것 찾기
```

`.tiles` 는 간단한 텍스트다. 직접 만들어도 된다:

```
splash-tiles 1
<가로> <세로>
<16진 타일값을 빈칸으로 잇고, 줄마다 한 줄>
```
