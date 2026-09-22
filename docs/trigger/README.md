# 트리거

유즈맵의 알맹이다. 문법은 글이고, 글이라 짜고 검증할 수 있다.

| 글 | 무엇이 있나 |
| --- | --- |
| [conditions.md](conditions.md) | 조건 전부. 인자의 함정 |
| [actions.md](actions.md) | 액션 전부. 못 만드는 유닛, 64가지 제한 |
| [execution.md](execution.md) | **먼저 읽는다.** 트리거가 도는 순서, 웨잇 꼬임, 하이퍼 |
| [death-counts.md](death-counts.md) | 죽음 수 = 유즈맵의 변수. 어느 유닛을 쓸 수 있나 |
| [bring-command.md](bring-command.md) | `Bring`·`Command` 가 무엇을 세나. 견줌마다 다르다 |
| [recipes.md](recipes.md) | 문법과 바로 쓰는 조각 |

## 실측 — 사람들이 실제로 쓰는 것

유즈맵 329장 기준 (`data/trigger-usage.json`, 772장에서 인자까지 뜸).

| 쓰는 것 | 맵 비율 |
| --- | --- |
| `Create Unit` + `Preserve Trigger` | 98% |
| `Bring` 조건 | **97%** |
| 강화(체력·에너지 고치기) | 89% |
| 순위표 `Leader Board *` | 82% |
| 화면 이동 `Center View` | 74% |
| 미션 브리핑 | 85% (중앙 2트리거) |

## 틀리기 쉬운 것 — 전부 실제로 틀려 봤다

| 함정 | 어디에 적었나 |
| --- | --- |
| 하이퍼를 사람 플레이어에 검 | [execution.md](execution.md) |
| 한 플레이어에 웨잇 트리거 둘 | [execution.md](execution.md) |
| 누적 조건 + `Preserve` = 무한 보상 | [recipes.md](recipes.md) |
| 맵에 있는 유닛을 변수 칸으로 씀 | [death-counts.md](death-counts.md) |
| `At least 1` 의 반대를 `At most 0` 으로 씀 | [bring-command.md](bring-command.md) |
| `Modify Unit Hit Points` 인자 차례 (퍼센트가 먼저) | [recipes.md](recipes.md) |
| 스위치로 사람별 상태를 담으려 함 | [death-counts.md](death-counts.md) |
