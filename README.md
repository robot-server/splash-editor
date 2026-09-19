# Splash Editor

StarCraft: Brood War / Remastered 맵 에디터. Windows · macOS · Linux.

장기 목표는 **SCMDraft 2 수준의 기능 패리티**다. StarEdit 수준에서 멈추지 않는다.

> **현재 상태: M0~M5 완료.**
> 맵을 열고, 지형·유닛·로케이션을 보고, 편집하고, 트리거를 고쳐 저장할 수 있다.
> 재저장본이 게임에서 정상으로 열리는 것까지 확인했다.
> 다음은 SCMDraft 2 패리티를 향한 다듬기다 — 남은 것은
> [백로그](BACKLOG.md)와 아래 [마일스톤](#마일스톤) 참고.

---

## 지금 되는 것

- `.scm` · `.scx` · `.chk` 열기
- 이름 · 크기 · 타일셋 · 버전 · 유닛/로케이션/트리거/문자열 개수 표시
- 다시 저장 (저장 · 다른 이름으로 저장)
- 빈 맵 새로 만들기 (CLI)
- **CHK 바이트를 보존하는 round-trip** — 편집하지 않은 섹션은 한 바이트도 바뀌지 않는다
- **지형 보기** — 게임 설치본의 타일셋으로 실제 지형을 그린다. 스크롤 · 확대/축소
- **유닛 · 로케이션 보기** — 실제 유닛 스프라이트(그림자·플레이어 색 적용), 로케이션 이름
- **새 맵 만들기 · 맵 속성** — 이름·설명·크기·타일셋
- **미니맵** — 클릭으로 이동, 보고 있는 영역 표시
- **맵 스프라이트 보기** — THG2 에 배치된 장식 스프라이트
- **크립 보기** — 저그 건물 주변 크립 (토글 가능)
- **유닛 편집** — 유닛 팔레트로 배치, 클릭 선택, 드래그 이동, 삭제, 실행 취소/다시 실행
- **로케이션 편집** — 클릭 선택, 드래그 이동
- **지형 편집** — 세 가지 방식
  - Isometric: 절벽·해안이 자동으로 이어진다 (ISOM 브러시)
  - Rectangular: 브러시 크기만큼 사각으로 칠한다
  - Subtile: 한 칸씩 정밀하게 칠한다
  타일 팔레트, Alt+클릭으로 타일 집기
- **트리거 보기 · 편집** — SCMDraft 형식의 텍스트 트리거를 고쳐 적용

## 아직 안 되는 것

유닛/로케이션/트리거 표시와 편집, 지형 편집, 실행 취소, 도킹 UI, 멀티 맵 탭.
모두 M3 이후다.

---

## 빌드

### macOS (Apple Silicon)

Apple Silicon(M1/M2/M3/M4) 에서 확인한 절차다. 검증 환경: macOS 15 (Sequoia),
Apple clang 21, CMake 4.4, Qt 6.11, arm64 네이티브.

```sh
# 1. 사전 준비 — Homebrew 가 이미 있다고 가정한다
brew install cmake ninja qt

# 2. 설정
cmake -S . -B build -G Ninja

# 3. 빌드
cmake --build build

# 4. 실행
./build/src/ui/splash-editor
```

Xcode Command Line Tools 가 필요하다 (`xcode-select --install`).

**ICU 는 따로 설치하지 않아도 된다.** Qt 가 의존성으로 끌어오며, Homebrew 의
ICU 는 keg-only 라 CMake 기본 경로에 없으므로 빌드 스크립트가 `brew --prefix`
로 자동 탐지한다. 자동 탐지가 실패하면 직접 지정할 수 있다:

```sh
cmake -S . -B build -G Ninja -DICU_ROOT=$(brew --prefix icu4c@78)
```

Qt 가 자동으로 안 잡히면:

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
```

### Linux

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev libicu-dev
cmake -S . -B build -G Ninja
cmake --build build
```

### Windows

Visual Studio 2022 (C++20) 와 Qt 6 가 필요하다.

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64"
cmake --build build --config RelWithDebInfo
```

### 빌드 옵션

| 옵션 | 기본값 | 설명 |
|---|---|---|
| `SPLASH_BUILD_GUI` | `ON` | Qt GUI 를 빌드한다. `OFF` 면 Qt 없이 코어+CLI 만 빌드 |
| `SPLASH_BUILD_TESTS` | `ON` | 테스트를 빌드한다 |

첫 설정 때 의존성을 네트워크로 받으므로 시간이 걸린다. 이후 빌드는 캐시된다.

---

## 사용법

### GUI

```sh
./build/src/ui/splash-editor              # 빈 창으로 시작
./build/src/ui/splash-editor path/to.scx  # 맵을 열고 시작

# 타일셋을 쓰려면 StarCraft 설치 폴더가 필요하다.
# 한 번 지정하면 기억하므로 다음부터는 생략해도 된다.
./build/src/ui/splash-editor path/to.scx --install "/경로/StarCraft"
```

설치 폴더는 메뉴 `파일 › StarCraft 설치 폴더 지정…` 으로도 고를 수 있다.
지정하지 않으면 지형 대신 안내 문구가 뜨고, 나머지 기능은 그대로 동작한다.

조작:

| | |
|---|---|
| 스크롤 | 스크롤바 · 휠 |
| 확대 / 축소 | `⌘+` / `⌘-` · `Ctrl`+휠 |
| 실제 크기 | `⌘0` |
| 유닛 표시 | `⌘1` |
| 로케이션 표시 | `⌘2` |
| 크립 표시 | `⌘3` |
| 유닛 · 로케이션 선택 | 클릭 (겹치면 유닛 우선) |
| 이동 | 드래그 |
| 선택 해제 | `Esc` |
| 선택 삭제 | `Delete` |
| 실행 취소 / 다시 실행 | `⌘Z` / `⇧⌘Z` |
| 선택 / 유닛 놓기 / 지형 도구 | `S` / `U` / `T` |
| 타일 집기 (지형 도구) | `Alt`+클릭 |
| 타일 · 유닛 팔레트 | 도구 메뉴 (해당 도구를 고르면 자동으로 열림) |
| 트리거 보기 | `⌃T` |
| 새 맵 | `⌘N` |
| 맵 속성 | `⌘I` |

### CLI

CLI 는 GUI 없이 코어를 두드리는 도구이자 테스트 하네스다.

```sh
# 메타데이터 보기
./build/src/cli/splash-cli info map.scx

# round-trip 검증 — 열고 저장한 뒤 CHK 바이트를 비교한다
./build/src/cli/splash-cli roundtrip map.scx

# 결과를 파일로 남기기 (게임에서 열어 보려면 이쪽)
./build/src/cli/splash-cli roundtrip map.scx /경로/Maps/재저장본.scx

# 시나리오 청크만 꺼내기
./build/src/cli/splash-cli chk map.scx scenario.chk

# 빈 맵 만들기 (확장자로 포맷 결정: .scm=하이브리드, .scx=브루드워)
./build/src/cli/splash-cli new new.scx 128 128 4 --melee

# 게임 설치본 조사 — 아카이브 종류와 타일셋 데이터 유무를 확인한다
./build/src/cli/splash-cli assets "/경로/StarCraft"

# 지형을 이미지로 뽑기 (타일셋 디코딩 검증용, PPM 출력)
./build/src/cli/splash-cli render map.scx "/경로/StarCraft" out.ppm

# 유닛·로케이션·크립을 겹쳐 그리기
./build/src/cli/splash-cli render map.scx "/경로/StarCraft" out.ppm --units --locations --creep

# 유닛 하나만 검증용으로 뽑기 (격자·경계·중심 표시)
./build/src/cli/splash-cli unit-image "/경로/StarCraft" 176 out.ppm 0 0 1500

# 타일셋 조사 / 타일 시트 뽑기
./build/src/cli/splash-cli tileset-info "/경로/StarCraft" 4
./build/src/cli/splash-cli tile-sheet "/경로/StarCraft" 4 0 16 sheet.ppm

# 유닛·로케이션 목록 보기
./build/src/cli/splash-cli units map.scx 30

# 트리거를 텍스트로 (SCMDraft 형식)
./build/src/cli/splash-cli triggers map.scx "/경로/StarCraft" triggers.txt

# 고친 텍스트를 다시 맵에 적용
./build/src/cli/splash-cli set-triggers map.scx "/경로/StarCraft" triggers.txt out.scx
```

---

## 테스트

```sh
ctest --test-dir build --output-on-failure
```

테스트는 두 층으로 나뉜다.

1. **합성 맵** — 코어가 직접 만든 맵으로 round-trip 을 돈다. 저작권 자료가
   없어도 CI 에서 항상 돌아간다.
2. **실제 맵** — `tests/maps/` 에 넣어 둔 맵이 있으면 전부 검증한다.
   비어 있으면 **건너뛰었다고 분명히 출력한다**. 합성 맵만으로 통과한 결과를
   실제 맵 검증으로 착각하지 않기 위해서다.

실제 맵을 넣는 방법은 [`tests/maps/README.md`](tests/maps/README.md) 참고.

### 실측 결과

실제 맵 컬렉션 966개(1MB 미만)로 검증한 결과다.

| 맵 종류 | CHK 바이트 보존 |
|---|---|
| 래더 · 공식 맵 | 100 / 100 (100%) |
| Enslavers 캠페인 | 10 / 10 (100%) |
| Precursor 캠페인 | 6 / 6 (100%) |
| Custom | 11 / 11 (100%) |
| 배포 유즈맵 | 397 / 744 (53%) |

**규격을 지키는 맵은 전부 바이트 단위로 보존된다.** 실패는 모두 배포
유즈맵에 몰려 있으며, 원인은 우리 쪽 버그가 아니라 그 맵들이 의도적으로
CHK 규격을 벗어나 있기 때문이다. 확인된 유형:

- **부풀린 STR/STRx** — 문자열 섹션이 9~15MB. 실제 쓰이는 문자열은 일부뿐
- **가짜 중복 섹션** — 한 맵에 섹션 항목이 414개(정상은 28개 안팎).
  CHK 는 같은 섹션이 여러 번 나오면 뒤엣것이 이기므로, 앞쪽에 쓰레기를
  깔아 에디터를 혼란시킨다
- **잘린 MTXM** — 지형 데이터를 맵 크기보다 작게 넣는다. 게임은 나머지를
  0 으로 채우지만, 다시 쓸 때는 정상 크기가 된다
- **규격 초과 선언** — STR 에 문자열 65536 개 선언(최대 32766),
  VER 섹션 누락 등. 이런 맵은 저장 자체를 거부한다

이런 맵을 다시 쓸 때 바이트가 달라지는 것은 정규화이지 손상이 아니다.
다만 **게임에서 열리는지는 별개 문제**이므로, 유즈맵을 편집할 계획이라면
저장본을 게임에서 확인해야 한다.

참고로 MappingCore 의 `isProtected()` 는 이 판별에 쓸 수 없다 — 위
유형의 맵들이 전부 "보호 아님"으로 보고된다. 그래서 정보 표시에만 쓴다.

### round-trip 이 검사하는 것

맵 파일 **전체**가 아니라 그 안의 **CHK(시나리오 청크) 바이트**를 비교한다.
MPQ 컨테이너는 해시 테이블 배치나 압축 결과가 달라질 수 있어 내용이 같아도
바이트가 달라진다. 보존해야 하는 것은 시나리오 데이터다.

---

## 구조

```
src/
  io/    MappingCore 를 감싸는 얇은 I/O 계층 (Qt 없음)
           map_archive     맵 열기/저장, CHK 바이트 추출
           game_assets     설치본 조사 (CASC / MPQ)
           tileset_source  타일셋 그래픽 -> 타일 픽셀
  chk/   MapDocument — 열린 맵 + 더티 플래그 + save() (Qt 없음)
  ui/    Qt Widgets GUI — 단일 QMainWindow + 맵 캔버스
  cli/   커맨드라인 프런트엔드
tests/   round-trip 테스트
cmake/   의존성 취득, MappingCore 빌드 정의
```

### 그래픽

맵 캔버스는 **보이는 영역의 타일만** 그린다. 맵 전체를 한 장 이미지로 만들면
256x256 맵이 8192x8192 픽셀, RGBA 로 268MB 가 되기 때문이다. 타일 그림은
타일 ID 별로 캐시한다 — 맵 하나가 쓰는 고유 타일은 보통 수백~수천 개다.

타일 하나(32x32)를 그리는 경로는 전부 MappingCore 가 파싱한 자료를 쓴다:

```
tileId -> CV5 타일 그룹 -> VX4 메가타일 -> VR4 미니타일(8x8) -> WPE 팔레트
```

유닛 스프라이트도 같은 방식으로 게임 데이터를 따라간다:

```
unitType -> units.dat(flingy) -> flingy.dat(sprite) -> sprites.dat(image)
         -> images.dat(GRP) -> GRP 프레임 디코딩
```

GRP 의 행 압축 규약(투명/단색/얼룩 라인)은 MappingCore 의 `Sc::Sprite::PixelLine`
이 캡슐화한 것을 그대로 쓴다. 플레이어 색은 팔레트 인덱스 8-15 구간을
`tunit.pcx` 의 플레이어별 8색 그라데이션으로 바꿔 넣어 표현한다.

스프라이트는 (유닛 타입, 소유자, 자원량 구간) 조합으로 캐시한다.

맵 스프라이트(THG2)도 같은 경로로 그린다 — 액터 초기화만 다르고
레이어 합성은 유닛과 공유한다.

유닛 하나는 단일 이미지가 아니라 여러 이미지 오버레이로 구성된다
(본체 + 그림자 + 부가물). 그 조립은 iscript 가 정하므로 MappingCore 의
`AnimContext` 를 돌려서 얻는다. 이 계층은 OpenGL 에 의존하지 않아
QPainter 경로에서도 그대로 쓸 수 있다.

그림자는 배경을 어둡게 하는 효과다(`dark.pcx` 는 "배경색 → 어두운 색"
매핑표다). 스프라이트를 따로 그리는 구조에서는 배경을 모르므로 반투명
검정으로 근사하고, 합성하는 쪽에서 알파를 섞는다.

**크립 가장자리에 대하여.** 타일셋에는 크립 가장자리 전용 타일이 없다.
네 가지 방법으로 확인했다.

1. `images.tbl` 929개 항목에 크립 관련 그래픽이 없다 — 별도 스프라이트가 아니다
2. 크립 메가타일은 13종뿐이고 전부 가득 찬 질감이다 (Jungle 기준 128~140,
   앞뒤는 풀·돌이다)
3. 그 13종에 비어 있는(투명) 미니타일이 하나도 없다 — 지형이 비쳐 보이는
   반투명 가장자리 타일이 아니다
4. 크립의 미니타일을 함께 쓰는 다른 메가타일을 전부 찾아봤지만, 풀·플랫폼·흙
   처럼 어두운 텍스처를 재사용한 것들이었다. Jungle 과 Badlands 둘 다 같다

즉 **크립 경계는 타일 단위**이며, 게임도 그렇게 퍼뜨린다. 여기서는 건물마다
타원으로 범위를 잡고 테두리만 부드럽게 처리한다 — 게임보다 매끄러운 쪽이다.
게임의 정확한 확산 패턴은 실행 파일 안에 있어 데이터로는 알 수 없다.

직접 확인하려면: `splash-cli images-tbl`, `mega-sheet`, `find-creep`, `creep-kin`

크립 바닥 타일은 타일셋에서 `Creep` 플래그가 선 타일 그룹을 찾아 쓴다.
그룹 안에서 실제 메가타일이 배정된 칸만 고르고(남는 칸은 0 으로 채워져
있다), 좌표를 섞어 변형을 골라 격자 줄무늬가 생기지 않게 한다.

**코어에는 Qt 타입이 없다.** `splash_io` 와 `splash_core` 는 Qt 를 링크하지
않으며, 헤더에 표준 라이브러리 타입만 노출한다. UI 는 `MapDocument` 만 알고,
`MapDocument` 는 UI 를 모른다.

MappingCore 헤더도 마찬가지로 `src/io/map_archive.cpp` 안에만 존재한다.
`chk.h` 하나가 90KB 를 넘고 리플렉션 매크로를 끌고 오므로, pimpl 뒤에 가둔다.

---

## 의존성

전부 CMake `FetchContent` 로 **커밋 해시를 고정해** 가져온다. 소스를 복사해
넣지 않으므로 업스트림 이력을 추적할 수 있고, 고정 해시를 쓰므로 업스트림
변경으로 CHK 파싱 동작이 조용히 바뀌는 일을 막는다.

| 라이브러리 | 용도 | 라이선스 |
|---|---|---|
| [MappingCore](https://github.com/TheNitesWhoSay/Chkdraft) (Chkdraft) | CHK 파싱 · 맵 구조 | MIT |
| [StormLib](https://github.com/ladislav-zezula/StormLib) | MPQ 아카이브 (맵 파일) | MIT |
| [CascLib](https://github.com/ladislav-zezula/CascLib) | CASC 아카이브 (리마스터 설치본 에셋) | MIT |
| [RareCpp](https://github.com/TheNitesWhoSay/RareCpp) | MappingCore 가 쓰는 리플렉션 | MIT |
| ICU | UTF-8 / UTF-16 변환 | Unicode-DFS-2016 |
| Qt 6 Widgets | GUI | **LGPLv3** (동적 링크) |

CHK 와 지형 파싱은 **MappingCore 만** 재사용한다. 파서를 직접 다시 만들지 않는다.

---

## 라이선스

**Splash Editor 자체는 MIT 다.** [`LICENSE`](LICENSE) 참고.

### Qt 와 LGPLv3

Splash Editor 는 Qt 6 을 **LGPLv3** 조건으로 사용하며, 그 의무를 다음과 같이 지킨다.

- **동적 링크만 쓴다.** Qt 를 정적 링크하지 않는다. 빌드 스크립트가 정적 Qt 를
  감지하면 설정 단계에서 빌드를 중단한다 (`src/ui/CMakeLists.txt`).
- **Qt 는 수정하지 않는다.** 배포판 Qt 를 그대로 쓴다.
- 사용자는 Qt 를 자신이 고른 다른 버전으로 **교체할 수 있다** — 동적 링크이므로
  호환되는 Qt 6 공유 라이브러리로 바꿔 넣으면 된다.

**Qt 소스 코드 받는 법.** Qt 는 LGPLv3 에 따라 소스를 제공받을 권리를 준다.

- 공식 배포: <https://download.qt.io/official_releases/qt/>
- Git: <https://code.qt.io/cgit/qt/qt5.git/>
- Homebrew 로 설치했다면: `brew fetch --build-from-source qt` 가 소스 tarball 을
  받아 온다. `brew edit qt` 로 어느 버전을 쓰는지 확인할 수 있다.

이 저장소가 배포하는 바이너리에 대응하는 정확한 Qt 버전은 빌드 시점의
`qmake --version` 출력으로 확인할 수 있다.

---

## 알려진 한계

- **문자열 인코딩** — CHK 문자열은 UTF-8 로 취급한다(MappingCore 의 전제).
  한국어 맵은 대개 CP949 라 이름·설명이 깨져 보인다. round-trip 바이트에는
  영향이 없다(원본 바이트를 그대로 보존한다). 표시 계층의 문제이며 M2 에서
  다룬다.
- **보호된 맵** — 위 [실측 결과](#실측-결과) 참고. 재저장 시 정규화되거나
  저장이 거부된다.
- **포맷 변환 없음** — 저장은 원본 버전을 유지한다. 하이브리드 맵을
  브루드워로 바꾸는 식의 변환은 아직 없다(의도적으로 뺐다 — 자동 변환이
  섹션을 조용히 바꾸기 때문이다).

## 기본안에서 바꾼 것

프로젝트 초기 기본안을 따르되, 근거가 있을 때만 바꿨다. 바꾼 항목과 이유:

1. **MappingCore 의 `lite_scenario` / `lite_map_file` 대신 `scenario` / `map_file`
   경로를 쓴다.** `lite_*` 는 주석상 "3rd party application 용"이라 딱 맞아
   보이지만, 업스트림 빌드 목록에서 빠져 있어 **현재 컴파일되지 않는다**
   (`LiteScenario` 에 `REFLECT` 선언이 없고, 삭제된 `ModifiedAsset` 타입을
   참조한다). Chkdraft 본체가 실제로 빌드하고 검증하는 경로를 쓰는 편이 안전하다.

2. **MappingCore 를 업스트림 CMakeLists 로 빌드하지 않고 직접 타겟을 만든다.**
   업스트림 루트 CMakeLists 는 MSVC 전용 플래그(`/permissive-`)를 무조건 추가하고,
   vcpkg 기반 `find_package` 와 Win32 UI 타겟을 끌고 온다. 소스 자체는 한 줄도
   고치지 않고 필요한 번역 단위만 골라 컴파일한다.

3. **C++17 이 아니라 C++20 을 쓴다.** MappingCore 의 `chk.h` 가 RareCpp 리플렉션을
   요구하고, 그것이 C++20 을 요구한다. 닫힌 제약("C++17 이상")을 만족한다.

4. **CascLib 을 M1 단계에서 이미 링크한다.** 기본안은 M1 에 CascLib 이 필수가
   아니라고 했고 실제로 기능적으로는 쓰지 않는다. 그러나 `scenario.cpp` 가
   참조하는 유닛 기본 이름 테이블이 CASC 접근 코드와 같은 번역 단위(`sc.cpp`)에
   있어 정적 테이블만 떼어 올 수 없다. 어차피 M2 그래픽에서 필요하므로 지금
   배선해 두었다.

5. **저장할 때 `lockAnywhere` 와 `autoDefragmentLocations` 를 끈다.** MappingCore
   `save()` 의 기본값은 둘 다 켜져 있는데, 우리가 편집하지 않은 섹션의 바이트를
   바꾼다. "아는 섹션만 수정, 나머지 바이트 보존" 제약을 지키려면 꺼야 한다.
   이 선택 덕분에 round-trip 이 바이트 단위로 일치한다.

6. **round-trip 비교 대상은 맵 파일 전체가 아니라 CHK 바이트다.** MPQ 컨테이너는
   해시 테이블 배치·압축 결과가 달라질 수 있어 의미가 같아도 바이트가 다르다.
   보존 여부를 판단할 대상은 그 안의 시나리오 데이터다.

7. **테스트 프레임워크를 쓰지 않고 작은 자체 러너를 뒀다.** 지금 필요한 것은
   "확인하고 세는" 것뿐이라 GoogleTest 를 끌어올 만한 이득이 없다. 어서션 종류가
   늘어나면 그때 교체한다.

8. **저장에 `MapFile::save()` 를 쓰지 않는다.** 그 함수는 saveType 에 맞춰
   `Scenario::changeVersionTo()` 를 무조건 호출하고, 그 안에서
   `deleteUnusedStrings(Both)` 가 실행되어 참조를 추적하지 못한 문자열이
   저장 때마다 사라진다(실제 맵에서 고유 문자열 302 → 233 개). 게다가 인자로
   받은 `lockAnywhere`/`autoDefragmentLocations` 는 그 호출에 전달조차 되지
   않아 무시된다. 대신 `Scenario::write()` 로 CHK 를 만들고 `MpqFile` 로 직접
   넣는다 — 둘 다 public API 이며 `MapFile::save()` 가 내부에서 하는 일과 같다.

9. **저장 후 `strTailData` 를 복원한다.** MappingCore 는 STR 뒤에 남은 바이트를
   읽을 때는 보존하지만(`syncBytesToStrings`) 쓸 때는 붙이지 않는다
   (`syncStringsToBytes`). 맵 보호나 서명에 쓰이는 경우가 있어 직렬화 결과를
   후처리해 되살린다.

10. **코어에 "빈 맵 만들기"(`MapArchive::createNew`)를 넣었다.** 기본안의 M1 범위를
   살짝 넘지만, 저작권 있는 맵 없이도 CI 에서 round-trip 을 돌리려면 픽스처를
   스스로 만들 수 있어야 한다. M2 이후 "새 맵" 기능의 토대이기도 하다.

---

## 마일스톤

| | 내용 | 상태 |
|---|---|---|
| **M0** | 문서 · 라이선스 | ✅ 완료 |
| **M1** | 열기 · 메타데이터 · 재저장, round-trip, 게임에서 열림 | ✅ 완료 |
| **M2** | 읽기 전용 뷰 (지형 렌더링) | ✅ 완료 |
| **M3** | 유닛 / 로케이션 편집 | ✅ 완료 |
| **M4** | 지형 편집 | ✅ 타일 브러시 완료 |
| **M5** | 트리거 → 이후 SCMDraft 2 패리티 | ✅ 보기·편집 완료 |

---

## 참고한 것

- **Chkdraft** — 코어(MappingCore)만 재사용한다. UI 는 복사하지 않는다.
- **ChkForge** — Qt 와 코어를 잇는 방식에서 아이디어만 참고했다.
- **SCMDraft 2** — 동작과 저장 결과의 오라클. 코드 공급원이 아니며 역공학하지 않는다.

충돌이 있으면 **게임이 실제로 여는 저장본이 언제나 정답**이다.
