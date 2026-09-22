# 트리거 API 레퍼런스 — 인자 순서

**이 문서는 생성된 것이다.** `measure_trigger_api.py` 가
MappingCore 의 `chk.cpp` 에서 정본 표를 뽑아 쓴다. 손으로 고치지
않는다 — 고치려면 스크립트를 고친다.

## 먼저 알 것 — 인자 순서가 **두 벌**이다

| 표 | 어디서 쓰나 |
| --- | --- |
| **text** | **텍스트 트리거.** `splash-cli trigger apply` 가 먹는 것 |
| classic | 에디터의 Classic Trigger 창에 보이는 차례 |

카페 강좌의 문장(`Player brings comparison quantity units to
location`)은 **classic 쪽**이다. 그대로 텍스트 트리거에 옮겨 적으면
틀린다. 내가 여러 번 틀린 자리다.

두 차례가 다른 것이 **36가지**나 된다. 아래 표에서 ⚠ 표시.

## 조건 (Condition)

| # | 이름 | 텍스트 인자 순서 | 실전 |
| ---: | --- | --- | ---: |
| 1 | **Countdown Timer** | 견줌(at least/at most/exactly), 값 | 1,161 |
| 2 | **Command** ⚠ | 플레이어, 유닛, 견줌(at least/at most/exactly), 값 | 5,706 |
| 3 | **Bring** ⚠ | 플레이어, 유닛, 로케이션, 견줌(at least/at most/exactly), 값 | 24,826 |
| 4 | **Accumulate** | 플레이어, 견줌(at least/at most/exactly), 값, 자원 | 2,741 |
| 5 | **Kill** ⚠ | 플레이어, 유닛, 견줌(at least/at most/exactly), 값 | 4,569 |
| 6 | **Command The Most** | 유닛 | — |
| 7 | **Command The Most At** | 유닛, 로케이션 | — |
| 8 | **Most Kills** | 유닛 | — |
| 9 | **Highest Score** | 점수 갈래 | — |
| 10 | **Most Resources** | 자원 | — |
| 11 | **Switch** | 스위치, 스위치 상태(set/not set) | 18,826 |
| 12 | **Elapsed Time** | 견줌(at least/at most/exactly), 값 | 1,746 |
| 13 | **Is Briefing** | — | — |
| 14 | **Opponents** | 플레이어, 견줌(at least/at most/exactly), 값 | 19 |
| 15 | **Deaths** ⚠ | 플레이어, 유닛, 견줌(at least/at most/exactly), 값 | 16,371 |
| 16 | **Command The Least** | 유닛 | — |
| 17 | **Command The Least At** | 유닛, 로케이션 | — |
| 18 | **Least Kills** | 유닛 | 50 |
| 19 | **Lowest Score** | 점수 갈래 | 22 |
| 20 | **Least Resources** | 자원 | — |
| 21 | **Score** | 플레이어, 점수 갈래, 견줌(at least/at most/exactly), 값 | 2,446 |
| 22 | **Always** | — | 5,449 |
| 23 | **Never** | — | 246 |

### ⚠ 두 차례가 다른 4가지

| 이름 | text (쓸 것) | classic (쓰지 말 것) |
| --- | --- | --- |
| Command | `playerCnd, unitCnd, numericComparisonCnd, amountCnd` | `playerCnd, numericComparisonCnd, amountCnd, unitCnd` |
| Bring | `playerCnd, unitCnd, locationCnd, numericComparisonCnd, amountCnd` | `playerCnd, numericComparisonCnd, amountCnd, unitCnd, locationCnd` |
| Kill | `playerCnd, unitCnd, numericComparisonCnd, amountCnd` | `playerCnd, numericComparisonCnd, amountCnd, unitCnd` |
| Deaths | `playerCnd, unitCnd, numericComparisonCnd, amountCnd` | `playerCnd, numericComparisonCnd, amountCnd, unitCnd` |

## 액션 (Action)

| # | 이름 | 텍스트 인자 순서 | 실전 |
| ---: | --- | --- | ---: |
| 1 | **Victory** | — | 200 |
| 2 | **Defeat** | — | 297 |
| 3 | **Preserve Trigger** | — | 21,688 |
| 4 | **Wait** | 밀리초 | 19,631 |
| 5 | **Pause Game** | — | 4 |
| 6 | **Unpause Game** | — | 3 |
| 7 | **Transmission** ⚠ | 글 깃발(Always Display), 글(스트링), 유닛, 로케이션, 고침(Set To/Add/Subtract), 값, 소리(WAV), 밀리초 | 717 |
| 8 | **Play Sound** ⚠ | 소리(WAV), 밀리초 | — |
| 9 | **Display Text Message** ⚠ | 글 깃발(Always Display), 글(스트링) | 17,789 |
| 10 | **Center View** | 로케이션 | 2,993 |
| 11 | **Create Unit with Properties** ⚠ | 플레이어, 유닛, 마리 수 (All 가능), 로케이션, 유닛 속성(64가지 제한) | 2,298 |
| 12 | **Set Mission Objectives** | 글(스트링) | 406 |
| 13 | **Set Switch** ⚠ | 스위치, 스위치 고침(set/clear/toggle/randomize) | 5,245 |
| 14 | **Set Countdown Timer** | 고침(Set To/Add/Subtract), 밀리초 | 405 |
| 15 | **Run AI Script** | AI 스크립트 | 722 |
| 16 | **Run AI Script At Location** | AI 스크립트, 로케이션 | 792 |
| 17 | **Leader Board (Control)** ⚠ | 글(스트링), 유닛 | — |
| 18 | **Leader Board (Control At Location)** ⚠ | 글(스트링), 유닛, 로케이션 | — |
| 19 | **Leader Board (Resources)** ⚠ | 글(스트링), 자원 | — |
| 20 | **Leader Board (Kills)** ⚠ | 글(스트링), 유닛 | — |
| 21 | **Leader Board (Points)** ⚠ | 글(스트링), 점수 갈래 | — |
| 22 | **Kill Unit** ⚠ | 플레이어, 유닛 | 4,063 |
| 23 | **Kill Unit At Location** ⚠ | 플레이어, 유닛, 마리 수, 로케이션 | 6,291 |
| 24 | **Remove Unit** ⚠ | 플레이어, 유닛 | 1,481 |
| 25 | **Remove Unit At Location** ⚠ | 플레이어, 유닛, 마리 수, 로케이션 | 6,414 |
| 26 | **Set Resources** | 플레이어, 고침(Set To/Add/Subtract), 값, 자원 | 7,755 |
| 27 | **Set Score** | 플레이어, 고침(Set To/Add/Subtract), 값, 점수 갈래 | 2,621 |
| 28 | **Minimap Ping** | 로케이션 | 2,747 |
| 29 | **Talking Portrait** | 유닛, 밀리초 | 101 |
| 30 | **Mute Unit Speech** | — | 2 |
| 31 | **Unmute Unit Speech** | — | 3 |
| 32 | **Leaderboard Computer Players** | 상태(enable/disable/toggle) | 133 |
| 33 | **Leaderboard Goal (Control)** ⚠ | 글(스트링), 유닛, 값 | — |
| 34 | **Leaderboard Goal (Control At Location)** ⚠ | 글(스트링), 유닛, 값, 로케이션 | — |
| 35 | **Leaderboard Goal (Resources)** ⚠ | 글(스트링), 값, 자원 | — |
| 36 | **Leaderboard Goal (Kills)** ⚠ | 글(스트링), 유닛, 값 | — |
| 37 | **Leaderboard Goal (Points)** ⚠ | 글(스트링), 점수 갈래, 값 | — |
| 38 | **Move Location** ⚠ | 플레이어, 유닛, 로케이션, 목적지 로케이션 | 3,265 |
| 39 | **Move Unit** ⚠ | 플레이어, 유닛, 마리 수, 로케이션, 목적지 로케이션 | 8,875 |
| 40 | **Leaderboard (Greed)** | 값 | — |
| 41 | **Set Next Scenario** | 글(스트링) | 168 |
| 42 | **Set Doodad State** ⚠ | 플레이어, 유닛, 로케이션, 상태(enable/disable/toggle) | 440 |
| 43 | **Set Invincibility** ⚠ | 플레이어, 유닛, 로케이션, 상태(enable/disable/toggle) | 2,731 |
| 44 | **Create Unit** ⚠ | 플레이어, 유닛, 마리 수 (All 가능), 로케이션 | 35,753 |
| 45 | **Set Deaths** ⚠ | 플레이어, 유닛, 고침(Set To/Add/Subtract), 값 | 16,908 |
| 46 | **Order** ⚠ | 플레이어, 유닛, 로케이션, 목적지 로케이션, 명령(Move/Patrol/Attack) | 6,531 |
| 47 | **Comment** | 글(스트링) | 81,785 |
| 48 | **Give Units to Player** ⚠ | 플레이어, 받는 플레이어, 유닛, 마리 수, 로케이션 | 2,271 |
| 49 | **Modify Unit Hit Points** ⚠ | 플레이어, 유닛, 퍼센트, 마리 수 (All 가능), 로케이션 | 1,600 |
| 50 | **Modify Unit Energy** ⚠ | 플레이어, 유닛, 퍼센트, 마리 수 (All 가능), 로케이션 | 501 |
| 51 | **Modify Unit Shield points** ⚠ | 플레이어, 유닛, 퍼센트, 마리 수 (All 가능), 로케이션 | — |
| 52 | **Modify Unit Resource Amount** ⚠ | 플레이어, 값, 마리 수 (All 가능), 로케이션 | 3 |
| 53 | **Modify Unit Hangar Count** ⚠ | 플레이어, 유닛, 값, 마리 수 (All 가능), 로케이션 | — |
| 54 | **Pause Timer** | — | 39 |
| 55 | **Unpause Timer** | — | 13 |
| 56 | **Draw** | — | 19 |
| 57 | **Set Alliance Status** | 플레이어, 동맹 상태(Enemy/Ally/Allied Victory) | 617 |
| 58 | **Disable Debug Mode** | — | 1 |
| 59 | **Enable Debug Mode** | — | — |

### ⚠ 두 차례가 다른 32가지

| 이름 | text (쓸 것) | classic (쓰지 말 것) |
| --- | --- | --- |
| Transmission | `textFlags, string, unit, location, numericMod, amount, sound, duration` | `unit, location, sound, numericMod, amount, string` |
| Play Sound | `sound, duration` | `sound` |
| Display Text Message | `textFlags, string` | `string` |
| Create Unit with Properties | `player, unit, unitQuantity, location, cuwp` | `unitQuantity, unit, location, player, cuwp` |
| Set Switch | `switch, switchMod` | `switchMod, switch` |
| Leader Board (Control) | `string, unit` | `unit, string` |
| Leader Board (Control At Location) | `string, unit, location` | `unit, location, string` |
| Leader Board (Resources) | `string, resource` | `resource, string` |
| Leader Board (Kills) | `string, unit` | `unit, string` |
| Leader Board (Points) | `string, score` | `score, string` |
| Kill Unit | `player, unit` | `unit, player` |
| Kill Unit At Location | `player, unit, numUnits, location` | `numUnits, unit, player, location` |
| Remove Unit | `player, unit` | `unit, player` |
| Remove Unit At Location | `player, unit, numUnits, location` | `numUnits, unit, player, location` |
| Leaderboard Goal (Control) | `string, unit, amount` | `amount, unit, string` |
| Leaderboard Goal (Control At Location) | `string, unit, amount, location` | `amount, unit, location, string` |
| Leaderboard Goal (Resources) | `string, amount, resource` | `amount, resource, string` |
| Leaderboard Goal (Kills) | `string, unit, amount` | `amount, unit, string` |
| Leaderboard Goal (Points) | `string, score, amount` | `amount, score, string` |
| Move Location | `player, unit, location, secondaryLocation` | `secondaryLocation, unit, player, location` |
| Move Unit | `player, unit, numUnits, location, secondaryLocation` | `numUnits, unit, player, location, secondaryLocation` |
| Set Doodad State | `player, unit, location, stateMod` | `stateMod, unit, player, location` |
| Set Invincibility | `player, unit, location, stateMod` | `stateMod, unit, player, location` |
| Create Unit | `player, unit, unitQuantity, location` | `unitQuantity, unit, location, player` |
| Set Deaths | `player, unit, numericMod, amount` | `player, numericMod, amount, unit` |
| Order | `player, unit, location, secondaryLocation, order` | `unit, player, location, order, secondaryLocation` |
| Give Units to Player | `player, destPlayer, unit, numUnits, location` | `numUnits, unit, player, location, destPlayer` |
| Modify Unit Hit Points | `player, unit, percent, unitQuantity, location` | `numUnits, unit, player, location, percent` |
| Modify Unit Energy | `player, unit, percent, unitQuantity, location` | `numUnits, unit, player, location, percent` |
| Modify Unit Shield points | `player, unit, percent, unitQuantity, location` | `numUnits, unit, player, location, percent` |
| Modify Unit Resource Amount | `player, amount, unitQuantity, location` | `numUnits, player, location, amount` |
| Modify Unit Hangar Count | `player, unit, amount, unitQuantity, location` | `amount, numUnits, unit, location, player` |

## 대조

실제 유즈맵에서 뽑은 트리거 텍스트로 **58가지를 350,499번** 보았고,
인자 개수가 위 표와 모두 맞았다.

## 관련

- [conditions.md](conditions.md) — 조건이 무엇을 뜻하나
- [actions.md](actions.md) — 액션이 무엇을 하나
- [recipes.md](recipes.md) — 바로 쓰는 조각
