# 에디터와 유틸리티 — 무엇을 쓰고 무엇을 피하나

출처: 스타 에디터 아카데미
[SCM Draft 2](https://cafe.naver.com/edac/book5095361/76273) ·
[TEP](https://cafe.naver.com/edac/book5095361/76274) ·
[텍스트 프리뷰](https://cafe.naver.com/edac/book5095361/76275) ·
[텍스트 프리뷰어 2](https://cafe.naver.com/edac/book5095361/76324) ·
[EUD 관련 유틸리티](https://cafe.naver.com/edac/book5095361/76276) ·
[FileManager](https://cafe.naver.com/edac/book5095361/76714).

이 저장소는 `splash-cli` 로 맵을 다루지만, **사람이 쓰는 도구를 알아야
남이 만든 맵의 흔적을 읽는다.** 맵에 남은 버릇이 어느 도구에서 왔는지
알면 짐작이 줄어든다.

## 맵 에디터

| 도구 | |
| --- | --- |
| **SCM Draft 2** | **지금 쓰이는 유일한 에디터.** 계속 손보고 있다 (스타 1.16 과는 호환 안 됨) |
| X-tra 에디터 · 스타포지 | 오래 멈춰 있다. 최신 SCMD2 와 호환도 안 된다 |
| **세디터** | **쓰지 말 것.** 기능이 더 적고 최신 버전과 호환이 안 돼 **맵을 날린 사람이 많다** |

### SCMDraft2 는 버전을 가려 쓴다

| | |
| --- | --- |
| **추천 버전** | **2019.04.21 (W)** |
| 최신 버전의 문제 | **트리거의 한글이 깨진다.** 한글 이름의 유닛을 트리거에서 못 쓴다 |
| 2019.04.21 의 한계 | 커스텀 플레이어 색상 없음, **스트링 확장 없음** |
| 그 밖 | **최신 버전으로 만든 맵은 구버전에서 안 열린다** |

> 한글 맵을 다루면서 트리거 글자가 깨져 있다면, **맵이 잘못된 게 아니라
> 그 사람이 쓴 에디터 버전 문제**일 수 있다. 우리 CLI 는 `--encoding
> cp949` 로 읽는다 — CHK 에는 코드 페이지 칸이 없다
> ([../chk/sections.md](../chk/sections.md)).

## 트리거 도구

| 도구 | |
| --- | --- |
| **TEP (TrigEditPlus)** | 트리거를 **lua 로 짜는** SCMD2 플러그인. **v2.0 보다 v1.0 을 권한다** |

설치하면 `Triggers` 메뉴에 `TrigEdit++` 가 생긴다. SCMD2 폴더의
`lua/Memory.lua` 와 `plugins/TrigEditPlus.sdp`.

> **TEP 로 컴파일하면 플레이어 8 에 EUD 트리거가 자동으로 들어간다**
> (코드 유지 기능 때문). 남의 맵에서 **P8 의 정체 모를 EUD 트리거**를
> 봤다면 이것일 수 있다 — 일부러 넣은 것이 아닐 수 있다.

## 글자와 색

트리거의 색 코드는 **인게임과 브리핑이 다르다**
([../trigger/briefing-strings.md](../trigger/briefing-strings.md)).

| 도구 | |
| --- | --- |
| **Text Preview** | SCMD2 색 코드로 미리 본다. `Export → SCM Draft 2` 로 **`Display Text Message` 액션 코드를 클립보드에 복사**. 애니메이션 미리보기도 있다 |
| **TextPreviewer 2** | 완전히 다른 프로그램. 오래됐지만 **`Out-Game` 탭이 있어 브리핑 색을 본다** |

| 탭 | 어디에 쓰는 색인가 |
| --- | --- |
| **In-Game** | `Display Text Message` · 유닛 이름 · 임무 목표 |
| **Out-Game** | **미션 브리핑** |

## EUD 도구

| 도구 | |
| --- | --- |
| EUD Editor 1 | 고전. **편집한 EUD 를 TEP 트리거로 보기에 가장 편하다.** 기능이 적어 이것만으로는 부족 |
| **EUD Editor 2** | 기본으로 쓸 것 |
| EUD Editor 3 | **epScript · eudplib 을 주로 쓰거나 SCA 를 써야 하면** 이쪽 |
| **EUD Draft** | 편집한 것을 **맵에 적용**하는 프로그램. **반드시 최신 버전**으로 |

우리 CLI 는 `splash-cli eud build ... --euddraft <경로>` 로 EUD Draft 를
부른다 ([../eud/limits.md](../eud/limits.md)).

## DatEdit · FireGraft · FileManager

게임 데이터 자체를 고치는 도구다. **맵이 아니라 게임을 바꾸는 것**이라
[../unit/settings.md](../unit/settings.md) 로 못 하는 것(사거리·속도·
공격 형태)을 여기서 한다.

| 도구 | |
| --- | --- |
| **DatEdit** | `units.dat` · `weapons.dat` 등을 고친다 |
| **FireGraft** | 버튼셋 · 요구사항 · 명령 |
| **FileManager** | 게임 안의 **글귀**(무기 이름 · 계급 · 툴팁)와 **와이어프레임** |

> **FileManager 로 글귀를 하나라도 고치면**, 그 맵에서는 플레이어의
> **언어 설정과 사용자 지정 단축키가 통째로 무시되고** 여기서 정한 것이
> 전부 적용된다. `tbl` 파일을 새로 만들어 갈아 끼우는 방식이라 그렇다.
>
> EUD Editor 1 은 리마스터에서 **와이어프레임 변경이 안 먹고**, 글귀도
> 기존 문구로 바꾸는 것만 되고 직접 쓴 문구는 못 넣는다.

## 우리 쪽에서 읽는 법

| 맵에서 본 것 | 짐작할 수 있는 것 |
| --- | --- |
| P8 에 뜬금없는 EUD 트리거 | TEP 컴파일 흔적 |
| 트리거 한글이 깨짐 | 최신 SCMD2 로 저장 |
| `STRx` 를 씀 | 스트링 확장이 되는 버전 |
| 비표준 섹션·부풀린 STR | 일부러 만든 보호 맵 — **다시 저장하면 깨진다** |

## 관련

- [cli-cookbook.md](cli-cookbook.md) — 우리 CLI 로 하는 법
- [../eud/limits.md](../eud/limits.md) — EUD 안전 규칙
- [../unit/settings.md](../unit/settings.md) — 맵이 고칠 수 있는 것
