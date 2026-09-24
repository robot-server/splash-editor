# splash-cli — 하고 싶은 일에서 명령 찾기

전체 목록은 `splash-cli help`, 갈래별은 `splash-cli <갈래>` 로 본다.
여기에는 맵을 만들 때 실제로 자주 쓰는 것만 추렸다.

**고치는 명령은 저장할 곳을 반드시 받는다** — `-o <출력맵>` 이거나
`--in-place`. 아래 예시는 작업 중을 가정해 `--in-place` 를 쓴다.
(`trigger apply` 만 예외로 `-o` 만 받는다.)

```sh
CLI=./build-cli/src/cli/splash-cli
export SC_INSTALL=/경로/StarCraft
```

---

## 맵 만들고 열기

| 하고 싶은 일 | 명령 |
| --- | --- |
| 빈 맵 만들기 | `$CLI new out.scx 128 128 4 --melee --install "$SC_INSTALL" --terrain Dirt` |
| 메타데이터 보기 | `$CLI info map.scx` |
| 이름·설명 바꾸기 | `$CLI map name map.scx "이름" --in-place` |
| 크기·타일셋 바꾸기 | `$CLI map size map.scx 128 128 --in-place` |
| 저장이 멀쩡한지 | `$CLI roundtrip map.scx` |
| 글자가 깨질 때 | `$CLI map encoding map.scx --encoding cp949` |

`new` 에 **`--install` 을 꼭 준다.** 안 주면 타일이 0 으로 남아 ISOM
브러시가 듣지 않는다.

## 지형

| | |
| --- | --- |
| 지형 종류 보기 | `$CLI terrain types map.scx --install "$SC_INSTALL"` |
| ISOM 으로 칠하기 | `$CLI terrain isom map.scx <픽셀x> <픽셀y> <지형> --install "$SC_INSTALL" --in-place` |
| 타일 보기 | `$CLI terrain show map.scx 0 0 16 16` |
| 네모 칠하기 | `$CLI terrain fill map.scx 10 10 8 8 0x0042 --in-place` |
| 베끼기 / 붙이기 | `$CLI terrain copy map.scx 10 10 6 6 p.tiles` · `$CLI terrain paste map.scx 40 40 p.tiles --in-place` |
| 램프 타일 찾기 | `$CLI tileset-ramps "$SC_INSTALL" 4` |
| 두뎃 목록 / 놓기 | `$CLI doodad list map.scx --catalogue --install "$SC_INSTALL"` · `$CLI doodad place map.scx 12 30 20 --install "$SC_INSTALL" --in-place` |
| 어긋난 두뎃 고치기 | `$CLI doodad repair map.scx --install "$SC_INSTALL" --in-place` |

`terrain mirror` 는 **절벽이 있는 지형에 쓰지 않는다** —
[밀리 지형 원칙](../melee/terrain.md) 참고.

## 유닛

| | |
| --- | --- |
| 유닛 번호·이름 찾기 | `$CLI unit types --find marine` |
| 놓인 유닛 보기 | `$CLI unit list map.scx --limit 50 --owner 1` |
| 놓기 | `$CLI unit place map.scx "Terran Marine" 50 30 --tiles --owner 1 --in-place` |
| 속성 바꾸기 | `$CLI unit set map.scx 34 --owner 3 --hp 50 --cloaked on --in-place` |
| 자원량 | `$CLI unit set map.scx 12 --resource 1500 --in-place` |
| 옮기기 / 지우기 | `$CLI unit move map.scx 34 60 40 --tiles --in-place` · `$CLI unit remove map.scx 34 --in-place` |
| 겹쳐 쌓기 | `$CLI unit stack map.scx 34 5 --in-place` |
| 애드온·나이더스 잇기 | `$CLI unit link map.scx 3 4 --addon --in-place` |

좌표는 **픽셀**이 기본이고 `--tiles` 를 주면 타일이다.

## 로케이션

| | |
| --- | --- |
| 목록 | `$CLI location list map.scx` |
| 만들기 | `$CLI location add map.scx 10 10 20 20 --tiles --name "Zone1" --in-place` |
| 이름·범위 | `$CLI location name map.scx 1 "새 이름" --in-place` |
| 높이 조건 | `$CLI location elevation map.scx 1 저지대,고공 --in-place` |
| 안팎 뒤집기 | `$CLI location invert map.scx 1 on --in-place` |

유즈맵은 255개를 다 써도 된다. 트리거가 이름으로 가리킨다.

## 트리거

| | |
| --- | --- |
| 텍스트로 빼기 | `$CLI trigger show map.scx t.txt --install "$SC_INSTALL"` |
| 텍스트 넣기 | `$CLI trigger apply map.scx t.txt --install "$SC_INSTALL" -o out.scx` |
| 목록 / 하나 뜯어 보기 | `$CLI trigger list map.scx --install "$SC_INSTALL"` · `$CLI trigger args map.scx 3 --install "$SC_INSTALL"` |
| 종류 이름 찾기 | `$CLI trigger types map.scx action --find score --install "$SC_INSTALL"` |
| 실행 플레이어 | `$CLI trigger owners map.scx 3 1,3,5 --in-place` |
| 인자 하나만 | `$CLI trigger set-arg map.scx action 0 0 0 "Player 3" --install "$SC_INSTALL" --in-place` |

`trigger apply` 는 **텍스트 전체로 갈아 끼운다.** 문법은
[trigger-recipes.md](../trigger/recipes.md).

## 플레이어·세력·문자열

| | |
| --- | --- |
| 플레이어 슬롯·종족 | `$CLI player list map.scx` · `$CLI player set map.scx 1 --race terran --slot human --in-place` |
| 세력 | `$CLI force list map.scx` · `$CLI force set map.scx 1 --name "이름" --shared-vision on --in-place` |
| 문자열 | `$CLI string list map.scx` · `$CLI string set map.scx 1 "새 글자" --encoding cp949 --in-place` |
| 스위치 이름 | `$CLI switch name map.scx 0 "술래" --in-place` |
| 유닛 능력치 | `$CLI unitdef set map.scx "Terran Marine" --hp 100 --in-place` |

## 맵 전체 손보기

| | |
| --- | --- |
| 시야 열기 (유즈맵 필수급) | `$CLI scenario revealers map.scx --owner 1 --spacing 16 --in-place` |
| 리빌러 지우기 | `$CLI scenario remove-revealers map.scx --in-place` |
| 자원량 섞기 | `$CLI scenario randomize-resources map.scx 1200 1800 --in-place` |
| 맵 밖으로 나간 것 치우기 | `$CLI scenario clean-bounds map.scx --in-place` |
| 그림으로 저장 | `$CLI scenario image map.scx out.ppm --install "$SC_INSTALL" --units` |

## 소리

| | |
| --- | --- |
| 넣기 / 목록 / 꺼내기 | `$CLI sound add map.scx my.wav --in-place` · `$CLI sound list map.scx` |

`Play WAV` 로 쓰려면 먼저 맵에 넣어야 한다.

## 설치본 들여다보기

| | |
| --- | --- |
| 설치본 확인 | `$CLI assets "$SC_INSTALL"` |
| 타일셋 정보 | `$CLI tileset-info "$SC_INSTALL" 4` |
| 램프 타일 | `$CLI tileset-ramps "$SC_INSTALL" 4` |
| 유닛 분류 | `$CLI unit-classes "$SC_INSTALL"` |

## 맵을 렌더하고 원본 구조 확인

| | |
| --- | --- |
| 지형·유닛·로케이션 렌더 | `$CLI render map.scx "$SC_INSTALL" out.ppm --units --locations` |
| 트리거 실행 체인 확인 | `$CLI trigger show map.scx triggers.txt --install "$SC_INSTALL"` |
| 맵의 메타데이터 확인 | `$CLI info map.scx` |
| 저장 CHK 바이트 round-trip | `$CLI roundtrip map.scx` |

맵 구조는 직접 원본 사례에서 옮긴 규칙으로 만들고, 정적 검사 결과와
렌더를 확인한다. 과거 생성기·검증기 스크립트는 코퍼스 통계와 검증되지
않은 설계 가정을 포함하므로 이 절차의 입력으로 쓰지 않는다.

## 걸리기 쉬운 것

- `new` 에 `--install` 을 안 주면 지형이 빈 칸으로 남는다.
- `trigger apply` 는 `--in-place` 를 안 받는다. `-o` 만 받는다.
- 트리거의 유닛 이름은 짐작하지 말고 `unit types` 로 확인한다. 하나만
  틀려도 **파일 전체 컴파일이 실패**하고 어느 줄인지 알려 주지 않는다.
- 로케이션은 트리거보다 **먼저** 만든다.
- `unit place` 의 좌표는 픽셀이다. 타일로 주려면 `--tiles`.
- 코드 페이지는 맵에 저장되지 않는다. 읽고 쓸 때마다 `--encoding` 으로
  알려 준다.
