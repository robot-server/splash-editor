# AI 스크립트 — 컴퓨터를 실제로 움직이는 것

출처: 스타 에디터 아카데미
[[팁] Run AI Script의 스크립트 목록](https://cafe.naver.com/edac/book5095361/76434)
(댓글 포함) ·
[[팁] AI 스크립트 명령어 목록](https://cafe.naver.com/edac/book5095361/76757).
실측 유즈맵에서 `Run AI Script` 722번 · `At Location` 792번.

## 왜 필요한가 — `Order` 로는 잘 안 움직인다

> 컴퓨터는 `Order` 의 **Attack 을 줘도 공격받기 전엔 안 움직이는 유닛이
> 많다.** `Patrol` 이 훨씬 낫다 (패트롤도 공격한다).

그 한 칸 위가 AI 스크립트다. 디펜스·살아남기 유즈맵에서 적이 실제로
달려들게 만드는 것은 대개 이쪽이다.

## 세 가지 규칙

1. **플레이어 체크에 넣은 플레이어에게만** 걸린다.
2. **로케이션이 필요한 것과 아닌 것**이 나뉜다. 설명에 로케이션 말이
   없으면 `Run AI Script`(로케이션 없는 쪽)를 쓴다. 반대로 쓰면 제대로
   안 돈다.
3. **컴퓨터의 종족과 스크립트의 종족이 같아야** 제대로 돈다.

## 공격시키기

| 스크립트 | 무엇을 하나 |
| --- | --- |
| **`Send All Units on Random Suicide Missions`** | 모든 컴퓨터 유닛이 **적이 전멸할 때까지** 적을 찾아다니며 공격. **살아남기류 유즈맵에서 가장 많이 쓴다.** 로케이션 없이 맵 전체 |
| **`Send All Units on Strategic Suicide Missions`** | 같되 **전략적으로** 움직인다. 바로 안 움직이고 얼마쯤 모았다가 간다 |
| `Set Unit Order To: Junk Yard Dog` | 일명 **정야독.** 로케이션 안의 컴퓨터 유닛이 **맵의 무작위 지점**으로 어택 간다 |
| `Move Dark Templars to Region` | 컴퓨터의 다크 템플러들이 그 로케이션으로 공격 간다. **`Order` 의 Attack 과 달리 만나면 선공한다** (제라툴은 안 움직인다) |
| `Make These Units Patrol` | 로케이션 안 유닛을 **맵 좌측 상단**으로 패트롤. 지점은 `Set Generic Command Target` 으로 바꾼다 |
| `AI Harass Here` | 그 지점을 **특히 더 자주** 공격한다 |
| `Value This Area Higher` | 컴퓨터가 그 지점을 중요하게 여겨 **방어 병력을 둔다** |

### Random 과 Strategic 의 실제 차이

`Random` 은:

- **일꾼까지 전부** 공격에 동원된다.
- 마법 유닛·럴커처럼 **못 때리는 대상도 공격 대상으로 삼는다**
  (럴커가 옵저버에게 가서 멀뚱히 서 있는다).
- 공격받아도 **처음 고른 상대를 끝까지** 쫓는 경향이 있다.
- 적 플레이어가 여럿이면 **플레이어마다 부대를 따로 보낸다.**
  플레이어3 의 건물이 하나뿐이어도 그것만 노리는 유닛이 꼭 생긴다.

> 카페 댓글: **Strategic 을 걸기 전에는 적이 시야·사거리에서 벗어나면
> 왔다 갔다 하는데, 걸고 나면 "(영웅)처럼 죽을 때까지 쫓아온다."**
> "영웅은 끝까지 쫓아간다" 는 이야기가 실은 **AI 스크립트 쪽**일 수
> 있다는 실마리다 → [../unit/heroes.md](../unit/heroes.md).
> **아직 직접 재지 않았다.**

## 마법·탑승

| 스크립트 | |
| --- | --- |
| `AI Nuke Here` | 컴퓨터 고스트가 그 로케이션에 **핵 한 발**. 고스트와 장전된 사일로가 있어야 한다 |
| `Cast Disruption Web` | 컴퓨터 커세어가 웹. **영웅(라자갈)은 안 쓴다.** 개발되어 있고 마나가 있어야 한다 |
| `Cast Recall (Arbiter required)` | 아비터가 리콜. **영웅(다니모스)은 안 쓴다.** 여러 지점에 쓰려면 아비터가 그 수만큼 필요하다 |
| `Enter Closest Bunker` | 로케이션 안 유닛을 가장 가까운 벙커에 |
| `Enter Transport` | 가장 가까운 벙커·수송선에 태운다. **컴퓨터는 태우자마자 다른 명령을 주거나 기브하지 않으면 바로 내린다** |
| `Exit Transport` | 내린다 |

> **영웅은 마법 AI 스크립트를 안 쓴다.** 라자갈로 웹을, 다니모스로
> 리콜을 시키려는 계획은 처음부터 안 된다.

## 시야 · 관계

| 스크립트 | |
| --- | --- |
| **`Turn ON/OFF Shared Vision of Player N with current player`** | **시야 공유.** 세력 설정의 Shared Vision 깃발이 안 먹으므로 이쪽을 쓴다 |
| `Switch Computer Player To Rescue Passive` | 그 컴퓨터를 **구조 가능(Rescuable)** 으로 |
| `Set Player To Ally/Enemy here` | **지금은 안 돈다.** `Set Alliance Status` 액션이 나오기 전에 쓰던 것 |

## 기지를 굴리는 AI

| 갈래 | |
| --- | --- |
| `(Expansion) (종족) Campaign (난이도)` | 일반적인 컴퓨터 AI. **컴퓨터 본진에 로케이션을 찍어야** 한다. Expansion 이 없으면 오리지널 유닛만 쓴다 |
| `Area Town` | **자원만 캔다** |
| `Easy` · `Medium` · `Difficult` | 난이도 |
| **`Insane`** | 컴퓨터에게 **주기적으로 자원이 들어오고 확장도 한다** |
| `Custom Level` | 보통 밀리맵을 할 때의 그 컴퓨터 |
| `(종족) (숫자) - ...` | 오리지널 캠페인의 AI |
| `Broodwar (종족) (숫자) - Town (알파벳)` | 브루드워 캠페인의 AI. **이름의 종족은 그 캠페인의 종족이지 AI 의 종족이 아니다** (`Broodwar Protoss 1` 의 적 AI 는 저그다) |
| `Clear Previous Combat Data` | 심하게 부서졌다고 판단해 **AI 가 버린 지역을 다시 쓰게** 만든다 |

컴퓨터가 본진을 복구하지 않는다면 이 마지막 것을 먼저 의심한다.

## 관련

- [actions.md](actions.md) — `Run AI Script` 액션
- [locations.md](locations.md) — 로케이션
- [../game/rules.md](../game/rules.md) — 세력 깃발이 안 먹는 것
