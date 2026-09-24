# [트리거/로직 패턴]

- 패턴 이름: 에디터 조건·액션을 텍스트 트리거 인자 순서로 옮기기
- 구현하고자 하는 기능: 기능 조건과 결과 액션을 잘못된 매개변수 순서 없이 CHK 트리거로 저장한다.
- 조건(Condition) 및 액션(Action) 구조 체인: MappingCore `chk.cpp`의 명령 정의에 따라 `splash-cli trigger apply`의 text 인자 순서를 사용한다. 아래 Condition/Action 표에서 필요한 명령을 고르고, 명령 ID와 각 필드 순서를 그대로 연결한다.
- 예외 처리/버그 방지 로직 (예: 스위치 리셋, 트리거 순서 등): SCMDraft classic 편집기 창의 표시 순서는 text 명령 순서와 다른 항목이 있으므로, classic 강좌의 필드 순서를 CLI 입력에 그대로 복사하지 않는다. `⚠` 표시가 있는 액션·조건은 순서가 틀리면 엉뚱한 플레이어·유닛·수량·위치가 적용될 수 있어 저장 뒤 맵의 트리거 텍스트를 다시 읽어 확인한다.

## Condition 텍스트 인자 순서

| ID | 이름 | 텍스트 인자 순서 |
| ---: | --- | --- |
| 1 | **Countdown Timer** | 견줌(at least/at most/exactly), 값 |
| 2 | **Command** ⚠ | 플레이어, 유닛, 견줌(at least/at most/exactly), 값 |
| 3 | **Bring** ⚠ | 플레이어, 유닛, 로케이션, 견줌(at least/at most/exactly), 값 |
| 4 | **Accumulate** | 플레이어, 견줌(at least/at most/exactly), 값, 자원 |
| 5 | **Kill** ⚠ | 플레이어, 유닛, 견줌(at least/at most/exactly), 값 |
| 6 | **Command The Most** | 유닛 |
| 7 | **Command The Most At** | 유닛, 로케이션 |
| 8 | **Most Kills** | 유닛 |
| 9 | **Highest Score** | 점수 갈래 |
| 10 | **Most Resources** | 자원 |
| 11 | **Switch** | 스위치, 스위치 상태(set/not set) |
| 12 | **Elapsed Time** | 견줌(at least/at most/exactly), 값 |
| 13 | **Is Briefing** | — |
| 14 | **Opponents** | 플레이어, 견줌(at least/at most/exactly), 값 |
| 15 | **Deaths** ⚠ | 플레이어, 유닛, 견줌(at least/at most/exactly), 값 |
| 16 | **Command The Least** | 유닛 |
| 17 | **Command The Least At** | 유닛, 로케이션 |
| 18 | **Least Kills** | 유닛 |
| 19 | **Lowest Score** | 점수 갈래 |
| 20 | **Least Resources** | 자원 |
| 21 | **Score** | 플레이어, 점수 갈래, 견줌(at least/at most/exactly), 값 |
| 22 | **Always** | — |
| 23 | **Never** | — |

### 인자 순서가 다른 명령

| 이름 | text (splash-cli) | classic (에디터 창) |
| --- | --- | --- |
| Command | `playerCnd, unitCnd, numericComparisonCnd, amountCnd` | `playerCnd, numericComparisonCnd, amountCnd, unitCnd` |
| Bring | `playerCnd, unitCnd, locationCnd, numericComparisonCnd, amountCnd` | `playerCnd, numericComparisonCnd, amountCnd, unitCnd, locationCnd` |
| Kill | `playerCnd, unitCnd, numericComparisonCnd, amountCnd` | `playerCnd, numericComparisonCnd, amountCnd, unitCnd` |
| Deaths | `playerCnd, unitCnd, numericComparisonCnd, amountCnd` | `playerCnd, numericComparisonCnd, amountCnd, unitCnd` |

## Action 텍스트 인자 순서

| ID | 이름 | 텍스트 인자 순서 |
| ---: | --- | --- |
| 1 | **Victory** | — |
| 2 | **Defeat** | — |
| 3 | **Preserve Trigger** | — |
| 4 | **Wait** | 밀리초 |
| 5 | **Pause Game** | — |
| 6 | **Unpause Game** | — |
| 7 | **Transmission** ⚠ | 글 깃발(Always Display), 글(스트링), 유닛, 로케이션, 고침(Set To/Add/Subtract), 값, 소리(WAV), 밀리초 |
| 8 | **Play Sound** ⚠ | 소리(WAV), 밀리초 |
| 9 | **Display Text Message** ⚠ | 글 깃발(Always Display), 글(스트링) |
| 10 | **Center View** | 로케이션 |
| 11 | **Create Unit with Properties** ⚠ | 플레이어, 유닛, 마리 수 (All 가능), 로케이션, 유닛 속성(64가지 제한) |
| 12 | **Set Mission Objectives** | 글(스트링) |
| 13 | **Set Switch** ⚠ | 스위치, 스위치 고침(set/clear/toggle/randomize) |
| 14 | **Set Countdown Timer** | 고침(Set To/Add/Subtract), 밀리초 |
| 15 | **Run AI Script** | AI 스크립트 |
| 16 | **Run AI Script At Location** | AI 스크립트, 로케이션 |
| 17 | **Leader Board (Control)** ⚠ | 글(스트링), 유닛 |
| 18 | **Leader Board (Control At Location)** ⚠ | 글(스트링), 유닛, 로케이션 |
| 19 | **Leader Board (Resources)** ⚠ | 글(스트링), 자원 |
| 20 | **Leader Board (Kills)** ⚠ | 글(스트링), 유닛 |
| 21 | **Leader Board (Points)** ⚠ | 글(스트링), 점수 갈래 |
| 22 | **Kill Unit** ⚠ | 플레이어, 유닛 |
| 23 | **Kill Unit At Location** ⚠ | 플레이어, 유닛, 마리 수, 로케이션 |
| 24 | **Remove Unit** ⚠ | 플레이어, 유닛 |
| 25 | **Remove Unit At Location** ⚠ | 플레이어, 유닛, 마리 수, 로케이션 |
| 26 | **Set Resources** | 플레이어, 고침(Set To/Add/Subtract), 값, 자원 |
| 27 | **Set Score** | 플레이어, 고침(Set To/Add/Subtract), 값, 점수 갈래 |
| 28 | **Minimap Ping** | 로케이션 |
| 29 | **Talking Portrait** | 유닛, 밀리초 |
| 30 | **Mute Unit Speech** | — |
| 31 | **Unmute Unit Speech** | — |
| 32 | **Leaderboard Computer Players** | 상태(enable/disable/toggle) |
| 33 | **Leaderboard Goal (Control)** ⚠ | 글(스트링), 유닛, 값 |
| 34 | **Leaderboard Goal (Control At Location)** ⚠ | 글(스트링), 유닛, 값, 로케이션 |
| 35 | **Leaderboard Goal (Resources)** ⚠ | 글(스트링), 값, 자원 |
| 36 | **Leaderboard Goal (Kills)** ⚠ | 글(스트링), 유닛, 값 |
| 37 | **Leaderboard Goal (Points)** ⚠ | 글(스트링), 점수 갈래, 값 |
| 38 | **Move Location** ⚠ | 플레이어, 유닛, 로케이션, 목적지 로케이션 |
| 39 | **Move Unit** ⚠ | 플레이어, 유닛, 마리 수, 로케이션, 목적지 로케이션 |
| 40 | **Leaderboard (Greed)** | 값 |
| 41 | **Set Next Scenario** | 글(스트링) |
| 42 | **Set Doodad State** ⚠ | 플레이어, 유닛, 로케이션, 상태(enable/disable/toggle) |
| 43 | **Set Invincibility** ⚠ | 플레이어, 유닛, 로케이션, 상태(enable/disable/toggle) |
| 44 | **Create Unit** ⚠ | 플레이어, 유닛, 마리 수 (All 가능), 로케이션 |
| 45 | **Set Deaths** ⚠ | 플레이어, 유닛, 고침(Set To/Add/Subtract), 값 |
| 46 | **Order** ⚠ | 플레이어, 유닛, 로케이션, 목적지 로케이션, 명령(Move/Patrol/Attack) |
| 47 | **Comment** | 글(스트링) |
| 48 | **Give Units to Player** ⚠ | 플레이어, 받는 플레이어, 유닛, 마리 수, 로케이션 |
| 49 | **Modify Unit Hit Points** ⚠ | 플레이어, 유닛, 퍼센트, 마리 수 (All 가능), 로케이션 |
| 50 | **Modify Unit Energy** ⚠ | 플레이어, 유닛, 퍼센트, 마리 수 (All 가능), 로케이션 |
| 51 | **Modify Unit Shield points** ⚠ | 플레이어, 유닛, 퍼센트, 마리 수 (All 가능), 로케이션 |
| 52 | **Modify Unit Resource Amount** ⚠ | 플레이어, 값, 마리 수 (All 가능), 로케이션 |
| 53 | **Modify Unit Hangar Count** ⚠ | 플레이어, 유닛, 값, 마리 수 (All 가능), 로케이션 |
| 54 | **Pause Timer** | — |
| 55 | **Unpause Timer** | — |
| 56 | **Draw** | — |
| 57 | **Set Alliance Status** | 플레이어, 동맹 상태(Enemy/Ally/Allied Victory) |
| 58 | **Disable Debug Mode** | — |
| 59 | **Enable Debug Mode** | — |

### 인자 순서가 다른 명령

| 이름 | text (splash-cli) | classic (에디터 창) |
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

## 근거

각 인자 배열은 MappingCore `chk.cpp`의 `textArguments` 및 `classicArguments` 정의를 따른다. 예시 호출은 [recipes.md](recipes.md)를, 의미와 동작은 [conditions.md](conditions.md) 및 [actions.md](actions.md)를 참조한다.
