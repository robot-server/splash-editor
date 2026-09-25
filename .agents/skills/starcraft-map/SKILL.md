---
name: starcraft-map
description: StarCraft: Brood War 맵(.scm/.scx)의 밀리·유즈맵을 실제 맵 데이터와 게임 동작을 확인하며 만들고 수정한다. 맵 제작, 트리거, 지형, 밸런스, .scx/.scm 요청에 사용한다.
---

# StarCraft 맵 제작

`splash-cli`와 `.agents/skills/starcraft-map/scripts/`의 Python 생성기·조립 도구를 모두 맵 제작 도구로 쓴다. 요청한 장르와 가까운 레시피가 있으면 그 코드와 옵션을 살펴 출발점으로 삼고, 맵 목적·타일셋·인원에 맞춰 조정한다. 생성기 출력은 완성 증거가 아니므로 맵 구조·렌더·게임 동작을 따로 확인한다. 램프 등 개별 분석 도구는 해당 문서에서 측정 범위와 한계를 확인한 뒤 쓴다. 새 맵은 레포가 아닌 사용자의 StarCraft `Maps` 폴더 아래에 둔다. 개인 경로를 코드·문서·예시에 하드코딩하지 않는다. `STARCRAFT_MAPS`가 있으면 우선 쓰고, 없으면 기기에서 설치/사용자 Maps 폴더를 찾아 선택한다. 후보가 모호하거나 찾지 못하면 사용자에게 경로를 묻는다. 임의 fallback으로 레포에 저장하지 않는다.

`data/`의 코퍼스 집계 JSON과 이전 결과물은 설계 목표나 정답의 근거가 아니다. Python 생성기와 조립 도구는 재사용 가능한 구현 레시피로 검토하되, 그 안의 통계 기본값·추론·검사 범위가 요청한 맵에 맞는지 확인한다. 지형·기능·배치 선택은 `docs/README.md`에서 관련 메커니즘 문서로 이동해 근거와 적용 범위를 확인하고, 문서의 관찰과 권고를 구분해 적용한다.

설치 에셋이 필요한 작업은 사용자가 제공한 설치 경로 또는 `SC_INSTALL`을 쓴다. 경로를 임의로 가정하지 않는다.

## 제작 지식 확인

- 레시피 시작점: 밀리맵은 [`make_melee.py`](scripts/make_melee.py), 퀴즈는 [`make_quiz.py`](scripts/make_quiz.py), 컨트롤 전투는 [`make_control.py`](scripts/make_control.py), RPG는 [`make_rpg.py`](scripts/make_rpg.py), 사각/웨이브 디펜스는 [`make_square_defense.py`](scripts/make_square_defense.py)·[`make_wave_defense.py`](scripts/make_wave_defense.py), 좀비 생존은 [`make_zombie.py`](scripts/make_zombie.py), 비대칭 숨바꼭질은 [`make_hide_seek.py`](scripts/make_hide_seek.py), 단계형 마이크로 시험은 [`make_micro_trial.py`](scripts/make_micro_trial.py), 방별 상태 퍼즐은 [`make_room_escape.py`](scripts/make_room_escape.py), 사전 유닛 드래프트 협동 방어는 [`make_loadout_gauntlet.py`](scripts/make_loadout_gauntlet.py)를 살핀다. 먼저 `--help`와 해당 생성 코드가 실제로 놓는 유닛·지형·로케이션·트리거를 읽고, 가장 가까운 레시피를 맵 목적에 맞게 바꾼다. 이름만 비슷한 레시피를 그대로 복사하지 않는다.
- `verify_map.py`와 개별 측정 스크립트는 검사 대상과 한계를 확인한 뒤 진단 보조로 쓴다. 통과 결과만으로 게임 플레이 동작을 확정하지 않는다.
- 문서의 수치 빈도, 평균, 중앙값을 설계 목표로 복사하지 않는다.
- 구조 지식을 적용할 때는 [docs/README.md](../../../docs/README.md)에서 관련 기능 문서로 이동한다. 트리거는 [조건](../../../docs/trigger/conditions.md), [액션](../../../docs/trigger/actions.md), [실행 순서](../../../docs/trigger/execution.md), [Deaths 상태값](../../../docs/trigger/death-counts.md), [Bring 판정](../../../docs/trigger/bring-command.md), [로케이션](../../../docs/trigger/locations.md), [기능 조립 예시](../../../docs/trigger/recipes.md)를 함께 참조한다.
- 유즈맵 상호작용은 [기능 설계](../../../docs/usemap/functional-design.md), [장르별 공간 구성](../../../docs/usemap/genres.md), [지형·이동 제약](../../../docs/usemap/terrain.md)을 참조한다. 유닛 생성 예외는 [유닛 특이 동작](../../../docs/unit/quirks.md), 스킬 반복 타이머는 [스킬 트리거 지침](../../../docs/trigger/skills.md)을 확인한다.
- 밀리 지형·자원은 [지형 설계](../../../docs/melee/terrain.md), [기지와 자원 자리](../../../docs/melee/resource-sites.md), [자리 밸런스](../../../docs/melee/balance.md)를 참조한다. 램프는 [램프 진단](../../../docs/tileset/ramps.md)의 측정 범위와 한계를 확인한다.
- `docs/`에 정리된 원본 관찰은 해당 문서의 출처와 적용 범위를 따른다. 문서에서 다루지 않은 동작이나 서로 충돌하는 사례가 있을 때만 원본을 추가 조사하고, 확인하지 않은 세부사항은 사실처럼 채우지 않는다.
- 관찰된 패턴과 추가로 권장하는 예외 처리/검증 규칙을 구분한다.
- 유즈맵 상호작용은 효과·비용·실패 경로를 플레이어가 이해할 수 있도록 배치와 지속 안내를 함께 설계한다. `Always Display`는 자막 옵션과 관계없이 메시지를 출력하는 설정이지 영구 HUD가 아니다.
- 밀리 멀티는 자원 존재만 확인하지 않는다. 각 기지 건물 footprint, 테란 애드온 공간, 일꾼 채집 접근과 실제 게임 동작을 본다.
- 램프 후보 표의 `walks`는 측정용 지형에서의 국소 미니타일 연결만 뜻한다. 배치 전 `doodad fits`로 타일셋 배치 표를 확인하고, 놓은 뒤 국소 연결·착지부·게임 내 통과를 별도로 검증한다. 표가 없거나 후보 지형이 맞지 않으면 그 후보는 쓰지 않는다.
- 유닛 설정은 이름·역할·체력·무기 피해·적용 플레이어를 함께 지정한다. 기본값 해제를 누락하지 않고 공유 무기 데이터 영향을 확인한다.
- 새 유즈맵 장르 레시피(`make_hide_seek.py`, `make_micro_trial.py`, `make_room_escape.py`, `make_loadout_gauntlet.py`)는 `scripts/recipe_profiles/`의 JSON을 복사해 맵마다 새로 작성하고 `--config`로 준다. 맵 설명·이름·브리핑·인게임 문구·초상화·유닛·방/로케이션 이름·웨이브·보상·타이머·시작 자원·업그레이드·기술·종족을 레시피 기본값으로 고정하지 않는다. AI가 장르와 코퍼스 근거에 맞춰 고르고, 코드는 입력 범위와 구조 연결을 제한한다.
- 맵 설명은 컨셉만 짧게, 구체 조작은 브리핑과 다시 볼 수 있는 맵 내 표식에 둔다. 초상화는 주제 유닛과 맞춘다. 상점·비콘은 효과 유닛/건물/표식을 주변에 짝지어 효과와 비용을 미리 읽게 한다. 현재 `unitdef` 명령은 유닛 타입 커스텀 이름 편집을 지원하지 않으므로 개체별 이름처럼 쓰지 않는다.
- 밀리맵은 기본적으로 본진을 둘러싼 고도 형태와 낮은 출입 통로를 만든다. 앞마당에는 자원 포켓과 주변 고도, 본진을 향한 단일 평지 입구를 둔다. 램프/다리 출입은 실제 두뎃 배치·미니타일 경로 검사를 통과한 후보만 쓴다. AI가 선택한 전투 구역 걷기 가능·건축 불가 지형은 `--max-unbuildable-pct` 전체 타일 상한 안에 둔다(기본 30%, 허용 범위 최대 45%). 중립 크리쳐는 `--critters` 수를 0~플레이어 수로 제한하고 종류는 맵마다 AI가 고른다.
- 밀리 지형은 반복 무늬나 무작위 굴곡을 먼저 만들지 않는다. 시작지·자원·진입로·중앙 교전 구역의 관계를 실제 원본 맵에서 추출한 뒤 ISOM 형태를 구성한다. 같은 도형을 회전 복사해 모든 구역을 채우지 않는다.
- 유즈맵 트리거가 유닛을 만들면 생성 위치의 점유·생성 수량·이동 경로와 실패 뒤 비용/상태를 점검한다. 원본의 성공 체인만으로 생성 실패 처리까지 구현됐다고 가정하지 않는다.
- 맵 설명은 플레이 유형을 고를 짧은 요약으로 쓴다. Mission Briefing은 목표·조작·상호작용·가격·실패 조건을 설명한다. 게임 중 다시 읽을 정보는 Mission Objectives 같은 지속 접근 수단과 맵 안의 표식에 둔다. 시작 시 한 번 나오는 Display Message를 지속 안내로 취급하지 않는다.

## 확인 및 저장

맵 변경 후 CLI 진단과 렌더를 확인한다. 트리거 생성·건물 배치·일꾼 채집·램프 이동 등 게임 엔진 동작은 정적 문서나 오버레이만으로 확정하지 않는다. 가능한 경우 게임에서 열어 확인하고, 실행 확인을 할 수 없으면 제한을 명시한다. 맵은 사용자의 `Maps` 하위 폴더에 명시된 출력 경로로 저장한다.

실제 맵 파일명은 레포 문서나 커밋 메시지에 쓰지 않는다. Git commit/push는 사용자가 현재 대화에서 명시적으로 요청한 경우에만 한다.
