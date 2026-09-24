# [트리거/로직 패턴]

## 게임 상태 판정 조건

- 패턴 이름: 공간·수량·자원·상태를 의도에 맞춰 조합
- 구현하고자 하는 기능: 상호작용 조건에 맞는 플레이어에게만 액션을 실행한다.
- 조건(Condition) 및 액션(Action) 구조 체인: 공간 안의 유닛을 판정할 때 `Bring(Player, Unit, Location, Comparison, Amount)`를 쓴다. 맵 전체 보유량이면 `Command`, 보유 자원이면 `Accumulate`, 상태 플래그면 `Switch` 또는 `Deaths`로 판정한다. 필요한 모든 조건을 하나의 체인에 명시하고 실행 액션과 연결한다. 자세한 의미와 제한은 [api.md](api.md) 및 [locations.md](locations.md) 참고.
- 예외 처리/버그 방지 로직 (예: 스위치 리셋, 트리거 순서 등): `At least`, `At most`, `Exactly`의 경계를 손으로 대입해 확인한다. `Current Player`, Force, All Players 조건은 트리거 소유자별로 실행될 때 중복 효과가 없는지 검증한다. 일회성 트리거와 `Preserve Trigger`를 구분하고 상태 조건이 계속 참인 경우 반복되는 액션을 제어한다. 출처: [강좌: 트리거 조건](https://cafe.naver.com/edac/book5095361/76411).

## 관련 레퍼런스

`Bring`의 유닛 경계와 로케이션 상호작용은 [bring-command.md](bring-command.md), 상태를 변수처럼 쓸 때는 [death-counts.md](death-counts.md), 반복 및 Wait 실행은 [execution.md](execution.md)을 본다.
