# 조건 (Condition) — 전부

출처: 스타 에디터 아카데미
[[초급2] 트리거 : 조건(Condition)](https://cafe.naver.com/edac/book5095361/76411)
+ 유즈맵 329장 실측.

**조건은 반드시 하나 이상 있어야 한다.** 여러 개면 **모두 참**이어야
액션이 돈다 (AND). OR 은 없다 — 트리거를 나눠 쓴다.

## 얼마나 쓰이나 (실측 329장)

| 조건 | 쓰는 맵 |
| --- | ---: |
| `Bring` | **97%** |
| `Always` | 97% |
| `Command` | 90% |
| `Score` | 88% |
| `Switch` | 75% |
| `Accumulate` | 74% |
| `Deaths` | 높음 (장르별로 갈림) |

거의 모든 판정이 **"로케이션에 무엇이 몇 기 있는가"** 다.

## 목록

| 조건 | 문장 | 뜻 |
| --- | --- | --- |
| **`Always`** | Always | 항상 참 |
| `Never` | Never | 절대 안 참. **버그 찾을 때** 트리거를 죽여 두는 용도 |
| **`Bring`** | (a)Player brings (b)cmp (c)N (d)units to (e)'loc' | 로케이션 안의 유닛 수 |
| **`Command`** | (a)Player commands (b)cmp (c)N (d)units | `Bring` 에서 로케이션을 뺀 것. **Anywhere 를 쓸 바엔 이쪽** |
| **`Accumulate`** | (a)Player accumulates (b)cmp (c)N (d)ore | 자원량 |
| **`Deaths`** | (a)Player has suffered (b)cmp (c)N deaths of (d)unit | 죽은 횟수. **데스값의 핵심** |
| `Kill` | (a)Player kills (b)cmp (c)N (d)units | **죽인 누적 수.** "죽일 때마다" 가 아니다 |
| **`Switch`** | (a)'Switch' is (b)set | 스위치 상태 |
| **`Score`** | (a)Player (b)type score is (c)cmp (d)N | 점수 |
| `Countdown Timer` | Countdown timer is (a)cmp (b)N game seconds | 화면 위 타이머 |
| `Elapsed Time` | Elapsed scenario time is (a)cmp (b)N game seconds | 게임 경과 시간 |
| `Opponents` | (a)Player has (b)cmp (c)N opponents remaining | 남은 적 수 |
| `Command The Most` / `The Least` | Current Player commands the most/least (a)units | 최다·최소 보유 |
| `Command The Most At` / `The Least At` | …at (b)'location' | 로케이션 안에서 최다·최소 |
| `Most Kills` / `Least Kills` | Current player has most/least kills of (a)unit | 최다·최소 처치 |
| `Most Resources` / `Least Resources` | Current player has most/least (a)resources | 최다·최소 자원 |
| `Highest Score` / `Lowest Score` | Current player has highest/lowest score (a)points | 최고·최저 점수 |
| `EUD: Memory Value` | Memory at Death Table + (a) (== (b)) is (c)cmp (d) | → [../eud/limits.md](../eud/limits.md) |
| `EUD: Location Position` | EUD: (a)location's (b)edge is (c)cmp (d)pixels | 같다 |

> **시간 두 조건은 게임 초다.** `Countdown Timer` · `Elapsed Time` 의
> 1 은 실제 **0.672초**다 → [../game/time.md](../game/time.md).

> **`Deaths` 는 전투로 죽은 것만 센다.** 트리거로 죽인 것(`Kill Unit`)은
> 안 들어간다. 다만 `Set Deaths` 액션으로 **직접 값을 쓸 수는 있다** —
> 그래서 데스값을 변수처럼 쓴다 → [death-counts.md](death-counts.md).

## 인자 — 여기가 함정이다

### Player

| 값 | 뜻 |
| --- | --- |
| `Player 1~12` | 그 플레이어. **트리거 소유자로는 9 이상이 안 먹지만, 조건과 일부 액션의 인자로는 잘 먹는다** |
| **`Current Player`** | **이 트리거를 실행한 플레이어.** 가장 중요한 개념 |
| `All Players` | 모두 |
| `Force 1~4` | 그 세력의 플레이어들 |
| **`Foes`** | **동맹(Ally)이 아닌** 플레이어들 |
| **`Allies`** | **동맹 + 동맹 승리 둘 다** 되어 있는 플레이어들 |
| **`Neutral Players`** | **동맹만 되어 있고 동맹 승리는 아닌** 플레이어들. 플레이어 12 와 **다르다** |
| `Non Allied Victory Players` | 동맹 승리가 아닌 플레이어들 |

> **`Opponents` 는 동맹 승리(Allied Victory)로만 적을 가른다.** 세력
> 설정도, 동맹(Ally) 설정도 안 본다. 컴퓨터도 적으로 센다.
> 세력 깃발이 안 먹는 것과 같은 뿌리다 → [../game/rules.md](../game/rules.md).

### Comparison

`at least` (이상) · `at most` (이하) · `exactly` (정확히). **초과·미만이
없다.** "3 초과" 는 `at least 4` 로 적는다.

### Units

`[any unit]` (아무 유닛) · `[buildings]` (건물) · `[factories]` (생산 건물)
같은 묶음 값이 있다. 유닛 하나를 고르는 것보다 이쪽이 편할 때가 많다.

### Location

`Anywhere` 는 맵 전체를 덮는 로케이션이다. **`Bring` 에 `Anywhere` 를
넣을 바엔 `Command` 를 쓴다** (인식 범위 계산이 없어 가볍다).

인식 범위는 유닛 크기와 로케이션 경계가 얽힌다 →
[bring-command.md](bring-command.md).

## 트리거는 한 번 돌면 사라진다

기본값이다. 계속 돌리려면 **`Preserve Trigger` 액션**을 넣는다.
실측 유즈맵의 98%가 `Create Unit` 과 `Preserve Trigger` 를 나란히 쓴다.

## 소유 플레이어는 "적용 대상" 이 아니다

트리거 창의 플레이어 체크는 **이 트리거를 누가 가지고 있나**이지
**누구에게 적용되나**가 아니다.

- 플레이어 1·2 에 체크하면 → **같은 내용의 트리거가 두 개** 있는 것과
  같다. 한 덩어리가 아니다.
- **플레이어 9 이상이 소유한 트리거는 작동하지 않는다.**
- 그 플레이어가 **게임에 없으면 그 트리거는 안 돈다.**

→ [execution.md](execution.md)

## 관련

- [../trigger/README.md](README.md) — 트리거 갈래
- [death-counts.md](death-counts.md) — 데스값을 변수로 쓰기
- [bring-command.md](bring-command.md) — Bring·Command 인식 범위
- [recipes.md](recipes.md) — 바로 쓰는 조각
