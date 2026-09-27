---
name: starcraft-map-usemap
description: StarCraft: Brood War 유즈맵(.scm/.scx)을 실제 맵 데이터와 게임 동작을 확인하며 만들고 수정한다. 유즈맵 장르 생성기, 트리거, UNIS 유닛 설정, 상점·비콘 상호작용 요청에 사용한다.
---

# StarCraft 유즈맵 제작

공통 절차·도구·판정 규칙은 [docs/agent-workflow.md](../../../docs/agent-workflow.md)를 먼저 따른다. 이 문서는 유즈맵에서 다른 부분만 적는다.

## Plan (설계)

유즈맵은 목적·동선·공간 배치·적 구성·경제·진행 규칙을 이 맵의 컨셉에 맞춰 새로 정한다. 여기서 정한 값을 Implement 단계에서 `--config` 프로필과 생성기 인자로 명시한다.

- 레시피 시작점: 퀴즈는 [make_quiz.py](scripts/make_quiz.py), 컨트롤 전투는 [make_control.py](scripts/make_control.py), RPG는 [make_rpg.py](scripts/make_rpg.py), 사각/웨이브 디펜스는 [make_square_defense.py](scripts/make_square_defense.py)·[make_wave_defense.py](scripts/make_wave_defense.py), 좀비 생존은 [make_zombie.py](scripts/make_zombie.py), 비대칭 숨바꼭질은 [make_hide_seek.py](scripts/make_hide_seek.py), 단계형 마이크로 시험은 [make_micro_trial.py](scripts/make_micro_trial.py), 방별 상태 퍼즐은 [make_room_escape.py](scripts/make_room_escape.py), 사전 유닛 드래프트 협동 방어는 [make_loadout_gauntlet.py](scripts/make_loadout_gauntlet.py), 추격 체크포인트는 [make_usemap.py](scripts/make_usemap.py)를 살핀다. 어느 루프인지 고를 때는 [장르 레시피](../../../docs/usemap/genre-recipes.md) 표를 따른다. 먼저 `--help`와 해당 생성 코드가 실제로 놓는 유닛·지형·로케이션·트리거를 읽고, 가장 가까운 레시피를 맵 목적에 맞게 바꾼다. 이름만 비슷한 레시피를 그대로 복사하지 않는다.
- 구조 지식을 적용할 때는 [docs/README.md](../../../docs/README.md)에서 관련 기능 문서로 이동한다. 트리거는 [조건](../../../docs/trigger/conditions.md), [액션](../../../docs/trigger/actions.md), [실행 순서](../../../docs/trigger/execution.md), [Deaths 상태값](../../../docs/trigger/death-counts.md), [Bring 판정](../../../docs/trigger/bring-command.md), [로케이션](../../../docs/trigger/locations.md), [기능 조립 예시](../../../docs/trigger/recipes.md)를 함께 참조한다.
- 유즈맵 상호작용은 [기능 설계](../../../docs/usemap/functional-design.md), [장르별 공간 구성](../../../docs/usemap/genres.md), [지형·이동 제약](../../../docs/usemap/terrain.md)을 참조한다. 유닛 생성 예외는 [유닛 특이 동작](../../../docs/unit/quirks.md), 스킬 반복 타이머는 [스킬 트리거 지침](../../../docs/trigger/skills.md)을 확인한다.
- 유즈맵 상호작용은 효과·비용·실패 경로를 플레이어가 이해할 수 있도록 배치와 지속 안내를 함께 설계한다. `Always Display`는 자막 옵션과 관계없이 메시지를 출력하는 설정이지 영구 HUD가 아니다.
- 장르 레시피는 최소 품질을 보장하는 골격이다. 각 맵마다 AI가 목적·동선·공간 배치·적 구성·경제·진행 규칙을 새로 설계해 프로필과 실제 출력에 반영한다. 레시피 생성 결과를 손대지 않은 채 최종 맵으로 내지 않는다. 파일명이나 표식 문구만 바꾸는 것도 충분한 차별화가 아니다.
- 맵 설명은 컨셉만 짧게, 구체 조작은 브리핑과 다시 볼 수 있는 맵 내 표식에 둔다. 초상화는 주제 유닛과 맞춘다. 상점·비콘은 효과 유닛/건물/표식을 주변에 짝지어 효과와 비용을 미리 읽게 한다.
- 맵 설명은 플레이 유형을 고를 짧은 요약으로 쓴다. Mission Briefing은 목표·조작·상호작용·가격·실패 조건을 설명한다. 게임 중 다시 읽을 정보는 Mission Objectives 같은 지속 접근 수단과 맵 안의 표식에 둔다. 시작 시 한 번 나오는 Display Message를 지속 안내로 취급하지 않는다.
- 제작 전에 아래 문서의 제약만 추가로 확인하고, 그 본문이나 유닛 목록을 맵 설명에 복사하지 않는다. [브리핑](../../../docs/trigger/briefing-strings.md)은 조건 분기가 없으므로 플레이 중 상태에 따라 갈라지는 안내를 브리핑 안에 넣지 않는다. [공유 로케이션](../../../docs/trigger/composition.md)은 다음 플레이어 트리거가 덮어쓰므로 여러 영웅 위치를 한 칸에 동시에 담지 않는다. [랜덤](../../../docs/trigger/random.md)은 다음 시행 전에 death count를 초기화하고 그 스위치·death를 다른 시스템과 공유하지 않는다. [시작 슬롯](../../../docs/usemap/essentials.md)은 아무도 맡지 않는 슬롯에 진행 트리거를 의존시키지 않는다. [소리](../../../docs/usemap/sound.md)는 Preserve마다 WAV를 다시 재생하지 않고, 브리핑 대기와 WAV 길이를 같은 값으로 두지 않는다.

## Implement (구현)

Plan에서 정한 값을 실제 프로필과 생성기 인자로 옮겨 맵을 만든다. 여기서부터는 Plan을 뒤집지 않는다.

- 영어를 금지하지 않는다. 프로필 루트의 `language`는 기본 `ko`로 두고, 사용자가 언어를 따로 요청하면 그 언어 태그로 바꾸어 모든 플레이어 표시 문구에 적용한다. 기본 한국어에는 맵 이름·설명·브리핑·목표·메시지·질문·지역명·표식·구매 영수증·커스텀 유닛 표시 이름을 포함한다. 프로필 예시의 다른 언어 문장을 요청 없이 복사해 남기지 않는다. API 구문, 유닛 타입 식별자, portrait 선택용 고유 유닛명은 원문을 유지한다.
- 유즈맵 프로필마다 `unit_settings`로 플레이어·적·보스의 체력·방패·방어·생산 시간·비용 등 바꿀 UNIS 값을 고른다. 지정하지 않은 필드는 대상 설치본 `units.dat`의 값으로 채운 뒤 기록해, 기본값 해제로 나머지가 0이 되지 않게 한다. UNIS 이름·능력치는 유닛 종류 전체에 적용되므로 같은 타입의 역할을 겹쳐 쓰지 않는다. 무기 피해·사거리·이동 속도는 맵 UNIS에서 바꿀 수 없으며 별도 데이터 모드 없이 바뀐 것처럼 설명하지 않는다.
- 유즈맵 장르 생성기(`make_control.py`, `make_hide_seek.py`, `make_loadout_gauntlet.py`, `make_micro_trial.py`, `make_quiz.py`, `make_room_escape.py`, `make_rpg.py`, `make_square_defense.py`, `make_wave_defense.py`, `make_zombie.py`, 추격 체크포인트 `make_usemap.py`)는 호출할 때마다 AI가 새로 쓴 JSON 프로필을 `--config`로 받는다. `--config` 없이 돌리거나 [scripts/recipe_profiles/](scripts/recipe_profiles/)를 기본 데이터로 읽지 않는다. 그 디렉터리의 JSON은 필드 모양을 보여주는 예시일 뿐이며, 그 문장·숫자·유닛·초상화를 맵에 옮겨 담지 않는다. 맵 설명·이름·브리핑·인게임 문구·초상화·유닛·방/로케이션 이름·웨이브·보상·타이머·시작 자원·업그레이드·기술·종족은 그 실행의 프로필에만 둔다. 코드는 입력 범위와 공간·트리거 연결만 제한한다.
- `unitdef set --name`은 개별 배치가 아닌 맵 전체 유닛 타입 이름을 바꾸므로, 서로 다른 효과에 같은 타입을 재사용하지 않는다.

## Test (확인 및 저장)

공통 Test 절차([docs/agent-workflow.md](../../../docs/agent-workflow.md#test-확인-및-저장))에 더해 아래를 확인한다.

- 유즈맵 트리거가 유닛을 만들면 생성 위치의 점유·생성 수량·이동 경로와 실패 뒤 비용/상태를 점검한다. 원본의 성공 체인만으로 생성 실패 처리까지 구현됐다고 가정하지 않는다.
