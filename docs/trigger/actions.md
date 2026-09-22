# 액션 (Action) — 전부

출처: 스타 에디터 아카데미
[[초급3] 트리거 : 액션(Action)](https://cafe.naver.com/edac/book5095361/76412)
+ 유즈맵 329장 실측.

## 얼마나 쓰이나 (실측 329장)

| 액션 | 쓰는 맵 |
| --- | ---: |
| `Create Unit` | **98%** |
| `Preserve Trigger` | **98%** |
| `Display Text Message` | 98% |
| `Set Resources` | 높음 |
| 체력·에너지 고치기 | 89% |
| `Leader Board *` | 82% |
| `Center View` | 74% |

## 유닛을 다루는 액션

| 액션 | 하는 일 |
| --- | --- |
| **`Create Units`** | (a)마리 (b)유닛을 (c)로케이션에 (d)플레이어 소유로 만든다 |
| `Create Units with Properties` | 위와 같되 미리 만든 **속성**을 입혀 만든다 |
| `Kill Unit` | (b)플레이어의 (a)유닛을 **맵 전체에서** 죽인다 |
| `Kill Unit At Location` | 로케이션 안에서 (a)마리만 죽인다 |
| **`Remove Unit`** | 죽이지 않고 **깨끗이 없앤다.** 시체도 안 남는다 |
| `Remove Unit At Location` | 로케이션 안에서 (a)마리만 없앤다 |
| **`Give Units to Player`** | 소유자를 바꾼다 |
| `Move Unit` | **순간이동**시킨다 |
| `Order` | Move · Patrol · Attack 명령을 내린다 |
| `Modify Unit Hit Points` · `Shield Points` · `Energy` | **퍼센트로** 설정한다 |
| `Modify Unit Resources Amount` | 미네랄·가스 덩이의 남은 양 |
| `Modify Unit Hangar Count` | 스캐럽·인터셉터를 **더한다** |
| `Set Invincibility` | 무적 켜기/끄기/전환 |
| `Set Doodad State` | 문·트랩을 여닫는다 |

### 함정

| | |
| --- | --- |
| **`Create Units` 로 못 만드는 것** | 스캐럽 · 인터셉터 · 더미 유닛, **초상화 전용 더미(알다리스 · 듀갈 · 멩스크)**, 그리고 **플레이어 9 이상의 유닛** |
| **`Create Units with Properties` 의 속성** | **한 맵에 64가지**뿐이다. "체력 1%" 와 "체력 1% + 무적" 은 서로 다른 가지로 센다 |
| **`Give Units`** | 받는 쪽에 **업그레이드가 자동으로 연구된다** (공격력·방어력은 빼고) |
| **`Order` 의 Attack** | 컴퓨터는 **공격받기 전엔 안 움직이는 유닛이 많다.** `Patrol` 이 훨씬 낫다 (패트롤도 공격한다) |
| `Modify Unit Hangar Count` | **더하기만 있다.** 빼거나 정해 넣는 액션이 없다 |
| **`Set Doodad State`** | 이름에 s 가 붙어 있지만 **한 기에만 걸린다.** 여러 개면 로케이션을 하나씩 씌워야 한다 |
| `Remove Unit` | 죽는 게 아니므로 **데스값이 안 오른다** → [death-counts.md](death-counts.md) |

## 화면과 소리

| 액션 | 하는 일 |
| --- | --- |
| **`Display Text Message`** | 화면에 글을 띄운다 |
| **`Center View`** | 현재 플레이어 화면을 로케이션으로 옮긴다 |
| `Minimap Ping` | 미니맵에 핑 |
| `Set Mission Objectives` | 임무 목표(F10 → J) |
| `Talking Portrait` | 초상화가 말하는 모습을 N밀리초 |
| `Transmission` | 초상화 + 소리 + 글 + 대기를 **한꺼번에** |
| `Play WAV` | 소리 |
| `Mute Unit Speech` / `Unmute` | 트리거 소리만 남기고 줄인다 |

### 함정

| | |
| --- | --- |
| **`Display Text Message` 의 `Always display`** | **반드시 켠다.** 끄면 게임 자막 옵션을 끈 사람에게는 **글이 안 보인다** |
| 글 길이 | 길수록 화면에 오래 남는다 |
| `Center View` | 싱글은 **서서히**, 멀티는 **즉시** 옮겨진다 |
| `Talking Portrait` | 싱글에서 시간이 끝나는 순간 **대기 중인 `Wait` 가 씹힌다** |
| **`Transmission`** | (e)밀리초 동안 **`Wait` 와 같은 효과**가 있다. 모르고 쓰면 뒤 액션이 늦는다 |

## 점수판 (Leader Board)

실측 유즈맵의 **82%** 가 쓴다. 없으면 "지금 누가 이기는지" 를 알 수 없다.

| 액션 | 무엇으로 줄 세우나 |
| --- | --- |
| `Leaderboard (Control)` | 그 유닛을 가진 수 |
| `Leaderboard (Control At Location)` | 로케이션 안에서 가진 수 |
| `Leaderboard (Kills)` | 그 유닛을 죽인 수 |
| `Leaderboard (Points)` | 점수 |
| `Leaderboard (Resources)` | 자원 보유량 |
| `Leaderboard (Greed)` | 미네랄+가스가 목표치에 가까운 순 (**라벨이 없다**) |
| `Leaderboard Goal *` | 위와 같되 **많은 순이 아니라 목표치에 가까운 순** |
| `Leaderboard Computer Players` | 점수판에 컴퓨터를 넣을지 |

라벨은 점수판 옆에 붙는 설명 글이다.

## 판정과 흐름

| 액션 | 하는 일 |
| --- | --- |
| **`Preserve Trigger`** | **이 트리거를 남긴다.** 없으면 한 번 돌고 사라진다 |
| **`Wait`** | 다음 액션까지 N밀리초 기다린다 |
| `Comment` | 트리거 이름 (스트링을 먹는다) |
| **`Set Deaths`** | 데스값을 **직접 쓴다.** 변수로 쓰는 근거 |
| **`Set Switch`** | 스위치를 켜고 끄고 뒤집는다 |
| `Set Resources` · `Set Score` | 자원 · 점수 |
| `Set Countdown Timer` · `Pause Timer` · `Unpause Timer` | 화면 위 타이머 |
| **`Set Alliance Status`** | 적 · 동맹 · **동맹 승리** 로 만든다 |
| `Victory` · `Defeat` · `Draw` | 승 · 패 · 무승부 |
| `Run AI Script` · `Run AI Script At Location` | AI 명령. **시야 공유도 여기 있다** |
| `Pause Game` · `Unpause Game` | **싱글 전용** |
| `Set Next Scenario` | **싱글 전용.** 같은 폴더의 맵만 |

### 함정

| | |
| --- | --- |
| **`Preserve Trigger` 를 빼면** | 그 트리거는 **한 번만** 돈다. 위치는 아무 데나 좋다 |
| **`Wait` 꼬임** | 플레이어당 하나씩만 돈다 → [execution.md](execution.md) |
| **`Pause Game` · `Unpause Game`** | 멀티에서 안 먹고, **EUD 맵에서는 게임이 먹통이 된다** |
| `Set Next Scenario` | **EUD 맵에서 안 먹는다** |
| 타이머 값 | `Set Countdown Timer` 의 1 은 실제 **0.672초** → [../game/time.md](../game/time.md) |
| **세력 깃발을 믿지 않는다** | 사람끼리 한 편으로 만들려면 **`Set Alliance Status` 로 `Allied Victory`** 를 트리거로 건다. 세력 설정의 깃발이 먹는지는 **확인하지 못했다** → [../usemap/essentials.md](../usemap/essentials.md) |

## 관련

- [conditions.md](conditions.md) — 조건 전부
- [execution.md](execution.md) — 도는 차례와 Wait 꼬임
- [death-counts.md](death-counts.md) — 데스값을 변수로
- [recipes.md](recipes.md) — 바로 쓰는 조각
