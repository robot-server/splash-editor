# 트리거 — 문법과 바로 쓰는 조각

트리거는 **텍스트로 쓰고 컴파일해 넣는다.** 낱개 명령으로 한 줄씩
고치는 것보다 이 편이 훨씬 빠르다.

```sh
# 지금 든 트리거를 텍스트로 빼기
splash-cli trigger show <맵> trig.txt --install "$SC_INSTALL"

# 고친 텍스트를 넣기 (--in-place 는 안 받는다. -o 만 받는다)
splash-cli trigger apply <맵> trig.txt --install "$SC_INSTALL" -o out.scx
```

파이썬에서는 `scmap.Cli.apply_triggers(text)` 가 임시 파일과 바꿔치기를
대신 해 준다.

`trigger apply` 는 **텍스트 전체로 갈아 끼운다.** 있는 것에 더하려면
먼저 `trigger show` 로 빼서 뒤에 붙인 다음 넣는다.

---

## 1. 생김새

```
Trigger("Player 1","Player 2"){
Conditions:
	Always();

Actions:
	Display Text Message(Always Display, "안녕");
	Preserve Trigger();
}

//-----------------------------------------------------------------//

Trigger("All players"){
...
}
```

- `Trigger(...)` 괄호 안은 **이 트리거를 실행할 플레이어**다.
  `"Player 1"` ~ `"Player 12"`, `"All players"`, `"Force 1"` 등.
- 조건이 **모두** 참일 때 동작이 **차례대로** 실행된다.
- 조건이 없으면 `Always()` 를 쓴다.
- 트리거 사이 구분선은 없어도 되지만 `trigger show` 가 넣어 준다.
- 한글과 색 코드가 그대로 왕복한다 (직접 확인함).

### 자주 틀리는 것

- **`Preserve Trigger()` 를 빼먹으면 한 번 돌고 죽는다.** 되풀이할
  트리거에는 반드시 붙인다.
- `Current Player` 는 "지금 이 트리거를 실행 중인 플레이어"다.
  `Trigger("All players")` 안에서 쓰면 사람마다 따로 돈다.
- 파일 경로의 역슬래시는 두 번 쓴다: `"sound\\Misc\\Button.wav"`.
- 로케이션은 **이름**으로 가리킨다. 먼저 만들어 두어야 한다:
  `splash-cli location add <맵> 10 10 20 20 --tiles --name "Zone1" --in-place`
- **유닛 이름은 `unit types` 가 내는 것을 쓴다.** 짐작해 쓰면 컴파일이
  통째로 실패한다 (`Terran Vulture Spider Mine` ✗ → `Spider Mine` ✓).
  실패하면 오류가 어느 줄인지 말해 주지 않으므로, 의심되는 줄만 남기고
  하나씩 넣어 보며 좁힌다.
- 되읽으면 이름이 달라 보일 수 있다 — 넣을 때 `"Spider Mine"`,
  `trigger show` 는 `"Vulture Spider Mine"`, 넣을 때 `"Switch 1"`,
  되읽으면 `"Switch1"`. 같은 값이다.

### 인자 차례 (헷갈리는 것만, 실제로 컴파일해 확인함)

```
Create Unit(플레이어, 유닛, 개수, 로케이션);
Create Unit with Properties(플레이어, 유닛, 개수, 로케이션, 프리셋번호);
Remove Unit At Location(플레이어, 유닛, 개수|All, 로케이션);
Move Unit(플레이어, 유닛, 개수|All, 출발로케이션, 도착로케이션);
Order(플레이어, 유닛, 출발로케이션, 도착로케이션, attack|move|patrol);
Modify Unit Hit Points(플레이어, 유닛, 개수, 퍼센트, 로케이션);
Give Units to Player(주는쪽, 받는쪽, 유닛, 개수, 로케이션);
Set Deaths(플레이어, 유닛, Set To|Add|Subtract, 값);
Set Resources(플레이어, Set To|Add|Subtract, 값, ore|gas|ore and gas);
Set Score(플레이어, Set To|Add|Subtract, 값, Kills|Total|Custom|...);
Bring(플레이어, 유닛, 로케이션, At least|At most|Exactly, 개수);
Command(플레이어, 유닛, At least|At most|Exactly, 개수);
Deaths(플레이어, 유닛, At least|At most|Exactly, 값);
```

`Order` 의 마지막 인자는 **소문자**다 (`attack`, `move`, `patrol`).

### 글자 색

`\x01`~`\x1F` 를 문자열 안에 넣는다. 자주 쓰는 것:

| 코드 | 색 | | 코드 | 색 |
| --- | --- | --- | --- | --- |
| `\x04` | 기본 연노랑 | | `\x08` | 빨강 |
| `\x07` | 연두 | | `\x0F` | 하양 |
| `\x11` | 파랑 | | `\x16` | 청록 |
| `\x1F` | 회색 | | `\r\n` | 줄바꿈 |

---

## 2. 바로 쓰는 조각

### 게임 시작 안내

```
Trigger("All players"){
Conditions:
	Always();

Actions:
	Set Mission Objectives("\x041. 유닛을 모아 웨이브를 막습니다\r\n\x042. 10웨이브를 넘기면 승리");
	Display Text Message(Always Display, "\x0F디펜스 v1.0\r\n\x16게임 방법은 임무목표(F10→J)를 보세요");
	Set Resources("Current Player", Set To, 50, ore);
}
```

### 되풀이하는 고리 (기본형)

```
Trigger("All players"){
Conditions:
	Bring("Current Player", "Men", "Reward Zone", At least, 1);

Actions:
	Set Resources("Current Player", Add, 100, ore);
	Display Text Message(Always Display, "\x07+100 미네랄!");
	Play WAV("sound\\Misc\\Button.wav", 300);
	Remove Unit At Location("Current Player", "Men", All, "Reward Zone");
	Preserve Trigger();
}
```

### 디펜스 웨이브 (카운터로 세기)

`Deaths` 를 변수로 쓴다. `Player 8` 의 `Spider Mine` 칸을 웨이브 번호로
삼았다.

```
# 30초마다 웨이브 번호를 하나 올린다
Trigger("Player 1"){
Conditions:
	Elapsed Time(At least, 30);
	Deaths("Player 8", "Spider Mine", At most, 9);

Actions:
	Set Deaths("Player 8", "Spider Mine", Add, 1);
	Set Countdown Timer(Set To, 30);
	Preserve Trigger();
}

# 3웨이브: 저글링 12마리
Trigger("Player 1"){
Conditions:
	Deaths("Player 8", "Spider Mine", Exactly, 3);

Actions:
	Display Text Message(Always Display, "\x08웨이브 3 \x0F— 저글링 12");
	Play WAV("sound\\Zerg\\Zergling\\ZLiRdy00.wav", 500);
	Create Unit("Player 7", "Zerg Zergling", 12, "Spawn");
	Order("Player 7", "Men", "Spawn", "Goal", attack);
	Minimap Ping("Spawn");
	Preserve Trigger();
}

# 다 막으면 보상
Trigger("All players"){
Conditions:
	Deaths("Player 8", "Spider Mine", Exactly, 3);
	Command("Player 7", "Men", Exactly, 0);

Actions:
	Set Resources("Current Player", Add, 200, ore);
	Display Text Message(Always Display, "\x07웨이브 3 막음! \x04+200");
	Set Deaths("Player 8", "Spider Mine", Add, 1);
	Preserve Trigger();
}
```

### 키우기 — 경험치와 승급

```
# 잡으면 경험치
Trigger("All players"){
Conditions:
	Kill("Current Player", "Zerg Zergling", At least, 1);

Actions:
	Set Deaths("Current Player", "Terran Civilian", Add, 1);
	Set Score("Current Player", Add, 10, Kills);
	Preserve Trigger();
}

# 10 모이면 승급
Trigger("All players"){
Conditions:
	Deaths("Current Player", "Terran Civilian", At least, 10);

Actions:
	Set Deaths("Current Player", "Terran Civilian", Subtract, 10);
	Display Text Message(Always Display, "\x07레벨 업! \x0F히드라리스크");
	Play WAV("sound\\Misc\\Button.wav", 300);
	Remove Unit At Location("Current Player", "Zerg Zergling", 1, "Hero");
	Create Unit("Current Player", "Zerg Hydralisk", 1, "Hero");
	Preserve Trigger();
}
```

### 순위표

```
Trigger("All players"){
Conditions:
	Always();

Actions:
	Leader Board Points("\x1F점수", Kills);
	Leaderboard Computer Players(disabled);
}
```

### 술래 — 스위치로 상태 잡기

```
Trigger("Player 1"){
Conditions:
	Switch("Switch 1", not set);
	Bring("Player 1", "Men", "Tag Zone", At least, 1);

Actions:
	Set Switch("Switch 1", set);
	Display Text Message(Always Display, "\x08Player 1 이 술래!");
	Center View("Tag Zone");
	Preserve Trigger();
}
```

### 비콘으로 고르기 (퀴즈·상점)

```
Trigger("All players"){
Conditions:
	Bring("Current Player", "Men", "Answer O", At least, 1);

Actions:
	Display Text Message(Always Display, "\x07정답!");
	Set Score("Current Player", Add, 1, Kills);
	Move Unit("Current Player", "Men", All, "Answer O", "Lobby");
	Preserve Trigger();
}
```

### 시간 압박

```
Actions:
	Set Countdown Timer(Set To, 60);
	...
Conditions:
	Countdown Timer(Exactly, 0);
```

### 끝맺음

```
Trigger("All players"){
Conditions:
	Deaths("Player 8", "Spider Mine", At least, 10);

Actions:
	Display Text Message(Always Display, "\x07모든 웨이브를 막았습니다!");
	Victory();
}

Trigger("All players"){
Conditions:
	Command("Current Player", "Men", Exactly, 0);

Actions:
	Defeat();
}
```

---

## 3. 종류 이름을 모를 때

이름은 맵과 설치본에서 가져온다. 외우지 말고 물어본다.

```sh
# 쓸 수 있는 조건·동작 종류
splash-cli trigger types <맵> condition --install "$SC_INSTALL"
splash-cli trigger types <맵> action --find score --install "$SC_INSTALL"

# 트리거 하나를 인자 단위로 뜯어 보기 (어느 자리에 무엇이 들어가는지)
splash-cli trigger args <맵> 0 --install "$SC_INSTALL"

# 유닛 번호와 이름
splash-cli unit types --find hydra
```

이름으로 못 찾으면 CLI 가 **고를 수 있는 것을 함께 보여 준다.** 그
목록이 곧 "이 자리에 이름표가 붙은 값" 의 전부다 — 이름표가 없는 값은
수로 넣는다.

---

## 4. 낱개로 고치기

텍스트 전체를 갈아 끼우지 않고 한 군데만 고칠 때.

```sh
splash-cli trigger list <맵> --install "$SC_INSTALL"          # 목록
splash-cli trigger list <맵> 3 --install "$SC_INSTALL"        # 3번의 조건·동작
splash-cli trigger add <맵> --count 5 --in-place
splash-cli trigger remove <맵> 7 8 9 --in-place
splash-cli trigger duplicate <맵> 3 --in-place
splash-cli trigger owners <맵> 3 1,3,5 --in-place
splash-cli trigger enabled <맵> 3 off --in-place
splash-cli trigger set-type <맵> condition 0 1 23 --in-place
splash-cli trigger set-arg <맵> action 0 0 0 "Player 3" --install "$SC_INSTALL" --in-place
splash-cli trigger line-enabled <맵> action 0 0 off --in-place
```

## 5. 딸린 것들

```sh
# 스위치 이름
splash-cli switch list <맵>
splash-cli switch name <맵> 0 "술래 정해짐" --in-place

# 소리 (Play WAV 로 쓰려면 맵에 넣어야 한다)
splash-cli sound add <맵> my.wav --in-place
splash-cli sound list <맵>

# 미션 브리핑
splash-cli briefing show <맵> b.txt --install "$SC_INSTALL"

# 유닛 프리셋 (Create Unit with Properties 가 쓴다)
splash-cli preset list <맵>
```
