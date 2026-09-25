# StarCraft 맵 제작 지식

이 문서는 맵 생성 에이전트가 설계 결정을 내릴 때 참고하는 출처 기반 규칙이다. 빈도·평균·중앙값은 제작 기준으로 쓰지 않는다. 같은 기능도 맵마다 목적과 조건 체인이 다르므로, 실제 맵의 관련 트리거와 공간 배치를 함께 읽는다.

일반 맵 작업의 승인 필요 항목은 [사용자 허락이 필요한 맵 작업](limits.md), EUD 작업은 [EUD 안전 규칙](eud/limits.md)을 참조한다.

## 제작 패턴

- 밀리맵의 기지·자원 자리와 고저·램프: [melee/resource-sites.md](melee/resource-sites.md)
- 밀리맵 시작 자원·승패 트리거와 자리 밸런스: [melee/balance.md](melee/balance.md)
- 유즈맵의 상호작용·비콘·브리핑·스폰: [usemap/functional-design.md](usemap/functional-design.md)
- 유닛 이름·전투 역할·예외 동작: [unit/settings.md](unit/settings.md), [unit/damage.md](unit/damage.md), [unit/quirks.md](unit/quirks.md)
- 지형·이동·배치 제약 진단: [tileset/isom.md](tileset/isom.md), [tileset/ramps.md](tileset/ramps.md), [usemap/terrain.md](usemap/terrain.md)
- 트리거 조건·액션 실행 순서와 실제 기능 조립: [trigger/execution.md](trigger/execution.md), [trigger/recipes.md](trigger/recipes.md), [trigger/api.md](trigger/api.md)

## 출처 원칙

각 규칙은 에디터 강좌 원문, StarCraft 설치 데이터, 또는 scmscx 원본 맵에서 확인한 동작을 연결한다. 원본 맵 제목은 복사하지 않되, 재확인할 수 있도록 scmscx 맵 페이지를 ID 링크로 붙인다. 강좌에 없는 안전장치나 설계 제안은 관찰 사실과 구분해 권고로 표시한다. 출처를 찾지 못한 동작은 사실처럼 쓰지 않는다.

각 규칙에 근거 링크를 붙인다. 원본 맵은 익명화한 기능 체인으로 기술하고, 확인되지 않은 세부사항은 사실로 쓰지 않는다.
