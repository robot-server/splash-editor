# 밀리맵 지형 놓기

지형은 ISOM 브러시로 놓는다. 절벽과 경계가 자동으로 이어진다. 다만
**두 가지는 ISOM 으로 안 된다** — 램프와 대칭이다. 아래에 대신하는
방법을 적는다. 모두 실제로 해 보고 그려 확인한 것이다.

---

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

공식 리그 맵 56개에서 쓰인 램프를 세어 가장 흔한 기준값을 골랐다
(`scmap.RAMPS`):

| 타일셋 | 기준값 | 크기 | 쓰인 곳 |
| --- | --- | --- | --- |
| 0 Badlands | `0x4AB0`, `0x4A50` | 6x6 | 7, 6 |
| 3 Ashworld | `0x4540`, `0x44E0` | 6x6 | 13, 11 |
| 4 Jungle | `0x4300`, `0x4360` | 6x6 | 21, 19 |
| 7 Twilight | `0x4040`, `0x4000` | 6x4 | 6, 4 |

놓는 법:

```python
scmap.place_ramp(cli, tile_x, tile_y, base=0x4300)
```

- `tile_y` 는 **고지대가 끝나고 절벽이 시작되는 줄**에 맞춘다.
- `tile_x` 는 **짝수**로 둔다. 마름모 격자가 타일 두 칸이라 홀수면
  어긋난다.
- 첫 줄은 6칸이 아니라 **4칸만** 찍는다. 공식 맵이 그렇게 놓는다 —
  오른쪽 위 두 칸은 둘레 지형을 그대로 둔다. `place_ramp` 가 알아서 한다.
- **램프 종류는 지형 짝에 맞춰 고른다.** `0x4300` 은 흙↔고지 흙 램프라
  고지대를 High Jungle 로 칠해 놓고 쓰면 재질이 어긋난다. 고지대를
  High Dirt 로 칠하면 맞는다.

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
