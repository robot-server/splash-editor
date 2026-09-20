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
6. **저장본을 다시 연다** — 고치는 명령은 저장 뒤 스스로 다시 열어
   확인하고 한 줄로 찍는다. 그 줄을 읽는다.

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
것이다. 인기 유즈맵 93개를 뜯어 센 결과가
[references/usemap-dopamine.md](references/usemap-dopamine.md) 에 있다.
요지만 옮기면:

- 트리거 중앙값 **153개**, 그중 절반이 `Preserve Trigger` 로 되풀이된다.
- 가장 흔한 보상 통로는 **유닛 주기**(93개 중 82개 맵), 그다음이
  **글 띄우기**(78개), **소리**(50개), **점수·순위표**(64개).
- 로케이션은 거의 모든 맵이 **255개를 다 쓴다**. 아껴 쓸 이유가 없다.

```sh
# 유즈맵 뼈대 (장르 고르면 그에 맞는 트리거 뼈대까지)
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

## 더 볼 것

- [references/cli-cookbook.md](references/cli-cookbook.md) — 하고 싶은
  일에서 명령을 찾는 표
- [references/melee-balance.md](references/melee-balance.md) — 자리·종족
  밸런스, 공식 리그 맵 56개 실측값
- [references/melee-terrain.md](references/melee-terrain.md) — ISOM·램프·
  대칭을 실제로 놓는 법
- [references/usemap-dopamine.md](references/usemap-dopamine.md) — 인기
  유즈맵 93개 실측, 재미 구조 설계
- [references/trigger-recipes.md](references/trigger-recipes.md) — 트리거
  텍스트 문법과 바로 쓰는 조각
- [references/eud-and-limits.md](references/eud-and-limits.md) — EUD 안전
  규칙과, 허락을 받아야 하는 것들
