# Bring과 Command 판정

출처: [스타 에디터 아카데미 트리거 조건 강좌](https://cafe.naver.com/edac/book5095361/76411), 런타임 동작은 선택한 게임 버전에서 검증한다.

## 기능에 맞는 범위 선택

- 특정 영역 안의 유닛 수: `Bring(Player, Unit, Location, Comparison, Amount)`
- 맵 전체에서 플레이어가 소유한 수: `Command(Player, Unit, Comparison, Amount)`
- 자원: `Accumulate`
- 죽음 수 기반 상태: `Deaths`

`Bring`과 `Command`가 수송 유닛 내부, 건설 중 건물, egg/cocoon 등 특수 상태를 모두 같은 방식으로 처리한다고 가정하지 않는다. `AtMost`, `Exactly`, `AtLeast` 각각에서 경계 사례를 실제로 검사한다. 제거/승리 판정처럼 잘못된 결과가 큰 경우 상태별 최소 재현 맵을 만든다.

## [트리거/로직 패턴]

- 패턴 이름: 공간 조건과 전체 보유 조건 구별
- 구현하고자 하는 기능: 플레이어가 상호작용 구역에 진입했는지 또는 특정 유닛을 보유 중인지 판정한다.
- 조건(Condition) 및 액션(Action) 구조 체인: 상호작용 영역 대상이면 `Bring`을, 전체 소유량이면 `Command`를 쓴다. 필요한 자원·상태 조건을 함께 결합하고 성공 액션에서 상태 갱신/토큰 소비를 수행한다.
- 예외 처리/버그 방지 로직 (예: 스위치 리셋, 트리거 순서 등): 서로의 부정이라고 가정해 `AtMost 0`과 `AtLeast 1`을 뒤집어 쓰지 않는다. 생성 중, 탑승, 변태 등 상태에서 각 비교 연산의 실제 결과를 확인한다. `Bring`은 위치 해석과 대상 유닛의 경계 판정도 검토한다([locations.md](locations.md)).
