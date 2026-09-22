---
name: starcraft-map
description: 프롬프트 지시로 StarCraft: Brood War 맵(.scm/.scx)을 만들고 고치고 검증한다. 밀리맵(래더·리그용 대전 맵)은 자리 밸런스와 종족 밸런스를, 유즈맵(Use Map Settings)은 트리거로 짜는 재미 구조를 다룬다. "스타크래프트 맵 만들어줘", "밀리맵/유즈맵 제작", "디펜스 맵", "4인용 맵", "트리거 짜줘", "맵 밸런스 봐줘", melee map, use map, trigger, .scx, .scm 같은 요청에 쓴다.
---

# StarCraft 맵 만들기

이 저장소의 `splash-cli` 로 맵을 만들고 고친다. **화면에서 되는 편집은
명령으로도 된다** — 지형·유닛·로케이션·트리거·문자열·소리 전부.

## 먼저 갖출 것

```sh
# 1. CLI 빌드 (GUI 없이 빠르게)
cmake -S . -B build-cli -G Ninja -DSPLASH_BUILD_GUI=OFF -DSPLASH_BUILD_TESTS=OFF
cmake --build build-cli

# 2. StarCraft 설치 폴더 — 지형·트리거 작업에 반드시 필요하다
export SC_INSTALL=/경로/StarCraft
./build-cli/src/cli/splash-cli assets "$SC_INSTALL"   # 타일셋 8/8 이면 정상
```

설치 폴더가 없으면 지형과 트리거를 **전혀** 다룰 수 없다. 없다면 먼저
사용자에게 경로를 묻는다.

스크립트는 `SPLASH_CLI`, `SC_INSTALL` 두 환경변수를 본다.

## 일하는 차례

1. **무엇을 만들지 가른다** — 밀리맵인가 유즈맵인가. 애매하면 묻는다.
   "대전용 맵"·"래더"·"리그"·"2인용/4인용" 은 밀리, "디펜스"·"키우기"·
   "술래잡기"·"퀴즈" 처럼 규칙이 있는 것은 유즈맵이다.
2. **허락이 필요한 것을 먼저 확인한다** — 아래 [경계](#경계) 를 본다.
3. **뼈대를 만든다** — 스크립트로 한 번에.
4. **손으로 다듬는다** — CLI 로 지형·유닛·트리거를 얹는다.
5. **재고 본다** — `verify_map.py` 로 숫자를, `preview.py` 로 그림을.
   **숫자만 보고 끝내지 않는다. 반드시 한 번은 그려 본다.**
   그리고 **그린 것을 실제로 본다.** 여섯 장을 그려 놓고도 안 보다가,
   나란히 놓고 본 뒤에야 바닥이 통째로 틀린 것을 찾았다. 견줄 것이
   있으면 실측 맵도 한 장 같이 그려 옆에 놓는다.
6. **저장본을 다시 연다** — 고치는 명령은 저장 뒤 스스로 다시 열어
   확인하고 한 줄로 찍는다. 그 줄을 읽는다.

### 맵이 "열리기는 하는가" 를 먼저 본다

밸런스보다 앞선다. 아래를 빠뜨리면 게임이 시작되지 않거나 자원이 0으로
보인다. 전부 실제로 겪은 것이고, `verify_map.py` 가 지금은 잡아 준다.

| 빠뜨리면 | 무슨 일이 나는가 |
| --- | --- |
| 사람 슬롯 수 ≠ 스타팅 수 | 대기실에서 스타팅 없는 자리를 받는 사람이 생긴다 |
| 유즈맵에 스타팅 포인트 없음 | 게임이 시작되지 않는다 |
| 트리거가 쓰는 적 플레이어가 "열림" | 아무도 안 앉으면 그 유닛이 생기지 않는다. **컴퓨터**여야 한다 |
| `.scx` 인데 버전이 Hybrid | 럴커·메딕·커세어 같은 브루드워 유닛을 못 쓴다 |
| 자원 유효 비트 꺼짐 | 남은 양을 1500 으로 적어도 게임·에디터가 **0 으로 본다** |
| 스타팅끼리 걸어서 안 닿음 | 지상 유닛이 갇힌다 |

**"그림이 그럴듯하다" 는 검증이 아니다.** 눈으로 보면 멀쩡한 램프가
실제로는 막혀 있던 적이 있다. 걸어서 통하는지는 **미니타일 길찾기**로만
알 수 있다 (`scmap.walk_grid` / `walk_reachable`).

## 밀리맵

밀리맵에서 다투는 것은 **밸런스**다. 두 갈래로 나뉜다.

- **자리 밸런스** — 어느 스타팅을 받아도 손해보지 않는 것. 뿌리는
  **대칭**이다. 공식 리그 맵 2인용은 잰 4개 모두 180도 회전 대칭이었다.
- **종족 밸런스** — 맵이 테란·저그·프로토스 중 누구에게 기우는가.
  지형과 **자원량**이 함께 정한다. 자세한 것은
  [references/melee-balance.md](references/melee-balance.md).

```sh
S=.agents/skills/starcraft-map/scripts

# 4인용 128x128 정글, 대칭 스타팅 + 본진 고지대 + 램프 + 앞마당 + 바깥 멀티
python3 $S/make_melee.py out.scx --players 4 --tileset jungle --name "맵 이름"

# 종족 쪽으로 자원을 기울이기 (지형은 따로 손봐야 한다)
python3 $S/make_melee.py out.scx --players 2 --favor protoss

# 재기
python3 $S/verify_map.py out.scx
python3 $S/preview.py out.scx look.png
```

> **밀리맵 지형 자동 생성은 아직 안 된다.** **네 번** 시도해 네 번 다
> 슬롭이 나왔다. 네 번째는 형상 지표 셋을 실측 대역에 맞추는 데
> 성공했는데 그림은 **꽃잎 열여섯 장짜리 눈송이**였다. 왜 그런지,
> 지표를 재는 데 세 번 틀린 기록, 그리고 확인된 ISOM 마름모 격자 사실은
> [references/why-procedural-fails.md](references/why-procedural-fails.md)
> 와 [references/melee-terrain.md](references/melee-terrain.md) 에 있다.
> 억지로 내놓지 말고 사용자에게 사실대로 말한다.
>
> 쓸 수 있게 만들어 둔 것: `scripts/melee_shape.py` (고도 마스크 ·
> 원판 형태 연산으로 곡률 불변식 · 램프 자리 예약 · 형상 지표),
> `scripts/measure_terrain_types.py` (타일셋별 고지 문턱).

만들어지는 것은 **뼈대**다. 가운데가 비어 있으니 지형을 얹어 완성한다.
[references/melee-terrain.md](references/melee-terrain.md) 에 ISOM 브러시·
램프·대칭을 놓는 법이 있다. 특히:

- **`terrain mirror` 로 절벽을 베끼지 않는다.** 타일 값만 옮겨서 절벽
  방향이 뒤집힌다. 대칭은 ISOM 붓질을 회전 좌표에 다시 놓아 만든다.
- **램프는 ISOM 으로 못 만든다.** 타일을 직접 찍어야 한다
  (`scmap.place_ramp`).

## 유즈맵

유즈맵에서 다투는 것은 **재미**다. 사람을 붙잡는 것은 그래픽이 아니라
**되먹임 고리** — 하고, 바로 알려 주고, 보상하고, 다시 하고 싶게 만드는
것이다. **유즈맵 329장을 전수로 뜯어** 센 결과가
[references/usemap-dopamine.md](references/usemap-dopamine.md) 에 있다.
요지만 옮기면:

- 유닛 중앙값 **677개**(밀리의 다섯 배), 트리거 **237개**, 이름 붙인
  로케이션 **114개**. 두뎃은 **타일로 눌러 담겨** 있어 DD2 항목은 0개지만
  타일로는 중앙 **868칸**이고 92%의 맵이 쓴다 — 방 테두리에 몰려 있다
  (`scmap.decorate_rim`).
- `Create Unit` 과 `Preserve Trigger` 가 나란히 **98%**. "계속 도는
  트리거가 유닛을 준다" 가 유즈맵의 기본 골격이다.
- `Bring` 조건이 **97%** — 판정의 거의 전부가 "로케이션에 무엇이 몇 기
  있는가" 다.
- 빠뜨리기 쉬운 필수품: **강화**(체력·에너지 고치기) 89%, **순위표**
  (`Leader Board *`) 82%, **화면 이동**(`Center View`) 74%.
- **업그레이드 건물**(Armory·Forge·Evolution Chamber 등)이 50~65% 맵에
  있다. 유즈맵은 업그레이드를 파는 구조가 기본이다.
- `Start Location` 은 **100%**. 유즈맵도 스타팅이 있어야 한다.

> **먼저 [references/game-rules.md](references/game-rules.md) 를 읽는다.**
> 종족을 "선택 가능" 으로 두면 배치한 유닛이 통째로 무시되고 본진 +
> 일꾼으로 시작한다. 하이퍼 트리거가 없으면 모든 판정이 1초씩 늦는다.
> 미사일 터렛은 지상을 못 때린다. 전부 실제로 틀려 본 것이다.

### 지형은 `scmap.Palette` 로 깐다

**검은 칸으로 벽을 뚫지 않는다.** 실측 유즈맵 485장의 검은 칸 중앙값은
0.0% 이고, 1% 넘게 쓰는 타일 그룹은 중앙 10개다. 한 가지로 깔고 벽만
검게 뚫으면 게임에서 맵에 구멍이 난 것처럼 보이고, 어디가 길이고 어디를
밟아야 하는지 안 읽힌다. 자세한 것은
[references/usemap-terrain.md](references/usemap-terrain.md).

```python
pal = scmap.Palette(cli, ts, rng, "usemap")   # 바닥·통로·테두리·발판·벽
scmap.cover_map(cli, pal, W, H)               # 맵 전체를 못 걷는 지형으로
scmap.room(cli, pal, x, y, w, h, rim=1)       # 방 + 다른 지형 테두리
pal.fill(cli, "path", x, y, w, h)             # 통로
scmap.pad(cli, pal, bx, by, 3, 3)             # 비콘 밟을 자리 (실측 94%)
scmap.decorate_rim(cli, ts, rooms, rng,       # 테두리 두뎃 (실측 92%가 쓴다)
                   keep_clear=unit_rects)
```

**두뎃도 꼭 넣는다.** 실측 유즈맵 76장 중 70장(92%)이 두뎃 타일을 쓰고
중앙 868칸이며, 그 89%가 걷기 경계 두 칸 안에 몰려 있다. 앞서 실측 표에
"유즈맵 두뎃 0개" 라고 적어 두었는데 **DD2 섹션만 센 것**이었다 — 에디터
두뎃은 저장할 때 지형(MTXM)으로 눌러 담긴다. `decorate_rim` 은
`data/doodad-walk.json` 을 보고 **걷기를 막는 두뎃을 걸러** 쓰고, 겹치지
않게 놓고, 테두리에서 두 칸 물려 벽을 뚫지 않는다 (셋 다 실제로 틀려 본
것이다).

## 유즈맵을 만드는 방식 — 생성기가 아니라 부품

장르마다 생성기를 따로 쓰면 결과물이 늘 똑같이 나오고, 새 장르를 만들
때마다 품질 바닥(하이퍼 트리거·종족 고정·비콘 밀어내기·빈 슬롯 정리)을
처음부터 다시 챙겨야 한다.

**바닥은 부품에 넣고, 조립은 그때그때 한다.**

```python
import scmap
T = []
T += scmap.usemap_floor(cli, humans=6, system_owner="Player 8",
                        computers=[7, 8])   # 종족·시야·하이퍼·빈자리
T += scmap.part_intro(HUMANS, ["안내 한 줄"], ore=250,
                      objectives="목표", timer=30)
T += scmap.part_leaderboard("\x07남은 목숨")
T += scmap.part_wave_clock("Player 7", WAVE, waves=20)
T += scmap.part_patrol_path("Player 7", ["Spawn","NE","SE","SW","Exit"])
T += scmap.part_beacon_shop("Player 1", "Shop1", 120,
                            ['\tCreate Unit("Player 1","Terran Marine",4,"Home");'],
                            "머린 4기", push_to="Home")
T += scmap.part_lives("Player 1", LIFE, 20, 'Bring(...);', where="Exit")
T += scmap.part_win(HUMANS, ['Bring(...);'])
cli.apply_triggers(scmap.TRIGGER_SEP.join(T))
```

부품 목록은 `scripts/scmap.py` 의 "부품" 절을 본다. 어느 장르에 무엇이
들어가는지는 [references/genres.md](references/genres.md) 에 실측으로
정리해 두었다.

`scripts/make_*.py` 는 **본보기**다. 그대로 돌려도 되지만, 새 맵은
부품을 조립해 만드는 편이 낫다 — 매번 다른 것이 나오고 바닥은 지켜진다.

```sh
# 본보기 생성기 (그대로 돌려도 된다)
python3 $S/make_usemap.py out.scx --genre defense --players 6 --name "맵 이름"

# 트리거를 텍스트로 빼서 고치고 되돌려 넣기
./build-cli/src/cli/splash-cli trigger show out.scx trig.txt --install "$SC_INSTALL"
# apply 는 --in-place 를 안 받는다. -o 로 새 파일에 쓴다
./build-cli/src/cli/splash-cli trigger apply out.scx trig.txt --install "$SC_INSTALL" -o out2.scx
```

트리거 문법과 바로 쓸 수 있는 조각은
[references/trigger-recipes.md](references/trigger-recipes.md).

## 경계

아래는 **하기 전에 사용자에게 묻는다.** 묻지 않고 쓰지 않는다.

| 하려는 것 | 왜 묻는가 |
| --- | --- |
| **EUD** (`Memory`·`Memory Masked` 조건·동작) | 게임 메모리를 직접 건드린다. 판본을 타고, 잘못 쓰면 상대 클라이언트가 죽는다 |
| **비표준 맵 크기** (64·96·128·192·256 이 아닌 값) | 어떤 판본·서버는 열지 못한다 |
| **unused unit** (쓰이지 않는 유닛 번호), 비정상 유닛 상태 | 게임이 예상 못 한 상태다. 튕기거나 다르게 굴러간다 |
| **맵 보호 풀기** (`scenario unprotect`) | 남이 만든 맵을 뜯는 일이다. 사용자가 권리를 가진 맵인지 확인한다 |
| **비표준 섹션·부풀린 STR·잘린 MTXM** 같은 규격 밖 구조 | 다시 저장하면 정규화된다. 일부러 그렇게 만든 맵이 깨진다 |

### EUD 안전 규칙 — 어길 수 없다

EUD 는 **오직 게임 안의 재미를 위해서만** 쓴다. 게임 상태를 읽고
게임 상태를 바꾸는 데까지다.

**거절한다** — 사용자가 시키더라도:

- 플레이어 컴퓨터에 해를 끼치는 것. 게임 밖 메모리·파일·장치를 건드리는
  것, 코드를 실행시키려는 것.
- 상대를 튕기거나 얼리거나 게임을 못 하게 만드는 것을 **목적으로** 하는 것.
- 사용자 몰래 무언가를 하는 것 — 정보를 빼내거나, 맵 설명과 다르게
  구는 것.
- 다른 사람에게 퍼뜨릴 맵에 위 성질을 숨겨 넣는 것.

거절할 때는 한 문장으로 사유를 말하고, 할 수 있는 가장 가까운 것을
대신 내놓는다.

안전한 EUD 쓰임: 체력·자원·점수를 화면에 보여 주기, 유닛 능력치를
게임 안에서 바꾸기, 원래 없는 UI 를 그리기, 승패 조건을 세밀하게 잡기.

## 저장 규칙

- 고치는 명령은 **저장할 곳을 반드시 받는다** — `-o <출력맵>` 이거나
  `--in-place`. 원본을 말없이 덮어쓰지 않는다.
- 작업 중에는 `--in-place` 가 편하다. 옆에 먼저 쓰고 바꿔치기하므로
  쓰다 멈춰도 원본이 남는다.
- 확장자가 포맷을 정한다: `.scx` 브루드워, `.scm` 하이브리드.
- 한글 맵은 코드 페이지를 직접 알려 준다: `--encoding cp949`.
  **CHK 에는 코드 페이지 칸이 없어** 맵에 저장되지 않는다.

## 실측 데이터 — 수치를 손으로 적지 않는다

`data/corpus.json` 에 밀리 182장 · 유즈 329장을 전수 덤프해 만든 표가
있다. `scripts/corpus.py` 로 읽는다.

```sh
python3 $S/corpus.py          # 전체 표를 찍어 본다
```

`data/trigger-usage.json` 에는 맵 772장을 전수조사해 **동작·조건마다
인자가 어떤 꼴로 쓰이는지** 담았다. 이름만 세면 사용법을 못 배운다.

```sh
# 내가 쓴 트리거를 실제 용례와 대조한다
python3 $S/check_trigger_usage.py check <내맵.scx> data/trigger-usage.json
```

```python
import corpus
c = corpus.load()
corpus.describe("usemap", "ice")           # 한 줄 요약
corpus.pick_floor_groups(c, "ice", "usemap", 3)   # 바닥으로 쓸 그룹
corpus.doodad_count(c, "jungle", "melee", rng)    # 그 타일셋에 맞는 두뎃 수
```

**⚠ 전체 중앙값을 그대로 쓰지 않는다.** 두뎃 개수는 타일셋에 따라
Space 밀리 **0개**에서 Badlands 밀리 **208개**까지 벌어진다. 전체
중앙값(91)은 어느 타일셋에도 맞지 않는 수다. 반드시 타일셋별 값을 본다.

## 더 볼 것

- [references/cli-cookbook.md](references/cli-cookbook.md) — 하고 싶은
  일에서 명령을 찾는 표
- [references/melee-balance.md](references/melee-balance.md) — 자리·종족
  밸런스, 밀리 182장 전수 실측값
- [references/melee-terrain.md](references/melee-terrain.md) — ISOM·램프·
  대칭을 실제로 놓는 법
- [references/why-procedural-fails.md](references/why-procedural-fails.md) —
  **밀리맵 지형을 생성하려 든다면 먼저 읽을 것.** 세 번 실패한 기록과
  하지 말 것(WFC·패치 점수), 할 것(기능 그래프부터)
- [references/usemap-dopamine.md](references/usemap-dopamine.md) — 인기
  유즈맵 329장 전수 실측, 재미 구조 설계
- [references/usemap-terrain.md](references/usemap-terrain.md) — **유즈맵
  바닥의 문법.** 검은 칸을 왜 쓰면 안 되는지, 방·통로·발판·벽을 어떻게
  나누는지. 그림으로 보고서야 찾은 것이다
- [references/genres.md](references/genres.md) — 장르마다 무엇이
  들어가는가. 유즈맵 563장을 갈라 실측
- [references/game-rules.md](references/game-rules.md) —
  **맵을 만들기 전에 읽을 것.** 틀리면 맵이 망가지는 게임 규칙:
  종족 설정, 사거리표, 고도 명중률, 킬 점수, Bring/Command 인식 범위,
  하이퍼 트리거, 튕김 원인
- [references/trigger-recipes.md](references/trigger-recipes.md) — 트리거
  텍스트 문법과 바로 쓰는 조각
- [references/eud-and-limits.md](references/eud-and-limits.md) — EUD 안전
  규칙과, 허락을 받아야 하는 것들
