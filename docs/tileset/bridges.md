# [지형/배치 메커니즘]

## 물길을 가로지르는 Bridge 두들

- 메커니즘 이름: 물길 위의 `Bridges` DD2 두들
- 적용 이유/목적: 양쪽 지상 지형 사이에 수송 없이 건널 수 있는 통로를 둔다. 물 위에 두들만 겹치는 것이 아니라, 물 가장자리와 다리 양끝의 지상 접근을 함께 설계해야 한다.
- 구체적 규칙 (타일 단위, 좌표 단위, 배치 각도, 언덕 연결 규칙 등): [scmscx 원본(맵 ID: DRhHR3cR)](https://scmscx.com/map/DRhHR3cR)은 Jungle 타일셋의 256x64 오리지널 StarCraft 맵이다. `unit list`에서 P1 스타팅 픽셀은 `(8064,1008)`, P2는 `(96,1136)`이다. MTXM 렌더에서 폭이 긴 Water 수역에 Bridge DD2가 놓인 모습이 보인다. CLI 카탈로그에서 확인한 Jungle Bridge 종류는 14x10(ID 268·269)과 10x8(ID 270·271)이다. `doodad list`가 출력하는 `타일 (91,6)`은 ID 268 DD2의 중심 타일 좌표다. 짝수 크기 두들의 왼쪽 위는 `doodadOriginTile` 공식에 따라 `(84,1)`이며, 이 TILE 밑 지형 14x10을 `terrain copy ... --no-doodads`로 복사해 새 Jungle 맵의 `(25,25)`에 붙인 테스트에서는 ID 268의 `doodad fits (32,30)`가 `예`였다. 별도로 복사한 MTXM 표면 패치와 DD2를 조합해 검사기가 보는 그림이 일치하는지도 확인했다. 저장소의 `.agents/skills/starcraft-map/data/bridge-patterns.json`은 ID 268·269의 14x10, ID 270·271의 10x8 밑 지형과 표면 패치를 기록한다. 생성기는 현재 Jungle 2인 rot180, 4인 rot90 조합만 지원하며, 설치본의 `doodad fits`를 밑 지형에 적용한 뒤 표면 패치와 DD2를 놓는다. 이후 지형 편집이 브리지 표면을 덮지 않도록 마지막에 붙이고 `doodad check`에서 정렬 오류 0건을 요구한다. 실제 생성·검증은 `make_melee.py --players 2 --size 128x128 --tileset jungle --symmetry rot180 --main-entry bridge --natural-minerals 0 --natural-gas 0 --expansions 0 --doodads 0 --install <SC_INSTALL>` 및 같은 옵션의 `--players 4 --symmetry rot90` 구성으로 했다. `doodad check`는 각각 `어긋난 두들 0개 / 전체 2개`, `0개 / 전체 4개`; 두 `roundtrip`은 `CHK 바이트 일치 : 예`를 출력했다. 최종 시작 연결 검사는 `scmap.walk_grid`에서 설치본 `tileset-tiles`의 각 4x4 walk mask를 미니타일 격자로 펼쳐 수행하고, 각 스타팅의 가장 가까운 walkable 셀 사이 `walk_reachable`를 확인했다. 두 생성 모두 `스타팅 N곳이 모두 이어집니다`를 출력했다. 원본 맵의 시작 픽셀 `(8064,1008)`과 `(96,1136)`을 타일 `(pixel_x//32,pixel_y//32)`로 내리고, 각 타일 중심 탐색점 `(tile_x*4+2,tile_y*4+2)`을 P1 `(1010,126)`, P2 `(14,142)`로 계산했을 때도 원본 그래프 연결은 참이었다. 이 검사는 정적 walk mask 연결만 본다. 브리지의 개별 기여를 분리하지 않으며 게임 런타임에서 유닛이 실제로 건너는지는 증명하지 않는다. 원본은 Brood War 버전도 아니므로, 생성기에서 검증한 2인·4인 템플릿 범위를 다른 크기·대칭·타일셋으로 넓히지 않는다.
- 잘된 예시 및 실패하는 나쁜 예시: 원본에서는 물길 위에 여러 Bridge 두들이 시각적으로 이어져 있고, 별도로 측정한 정적 보행 그래프에서 두 스타팅이 연결됐다. 생성 예시에서는 맞는 TILE 패턴에서 `doodad fits`를 먼저 확인하고, MTXM 표면과 Bridge DD2를 정렬해 검사 오류 0건을 얻었다. 이후 지형 붓질로 DD2 아래 MTXM을 덮어 정렬을 깨거나, 반대 방향 템플릿을 같은 ID로 복제하면 실패다. 원본 분석과 생성 예시 모두 실제 게임에서 유닛이 건너는 동작을 입증하지 않는다.

## 트리거/로직 패턴

- 패턴 이름: 해당 없음
- 구현하고자 하는 기능: 원본에서 Bridge 두들은 지형 오브젝트다. `trigger show`에서 다리를 생성·관리하는 트리거 체인은 확인되지 않았다.
- 조건(Condition) 및 액션(Action) 구조 체인: 해당 없음. 이 원본에서 확인한 승리·패배 트리거는 본진 소멸 판정이며 Bridge 통행과 연결되지 않는다.
- 예외 처리/버그 방지 로직 (예: 스위치 리셋, 트리거 순서 등): 트리거 로직이 Bridge 이동을 보증한다고 가정하지 않는다. 생성기는 패턴이 검증된 Jungle 2인 rot180 또는 4인 rot90에서만 Bridge 입구를 허용한다. 그 밖의 타일셋·대칭에는 `--main-entry bridge` 요청을 거절하고, AI가 고른 경우 검증된 ramp 또는 평지 통로를 사용한다.

## 에디터 타일/배치 제약사항

- 지형 간 연결 규칙: Jungle Bridge 두들의 물길·육지 호환 조건은 한 원본 배치로 새 맵에 일반화할 수 없다. `doodad fits`는 입력한 현재 게임 타일 배열과 지정 중심 좌표를 비교한다. 원본 기존 DD2 중심에서 부정 응답을 받아도 배치 불가나 기존 데이터 오류로 결론내리지 않는다. 같은 원본에서 `terrain copy <source> 84 1 14 10 <patch.tiles> --no-doodads`로 밑 지형을 옮긴 뒤, 빈 Jungle 맵에 `terrain paste <target> 25 25 <patch.tiles> -o <pasted>`하고 `doodad fits <pasted> 268 32 30 --install <SC_INSTALL>`을 실행해 `예`를 확인했다. 이 재현은 패치 지형의 DD2 배치 적합성만 입증한다.
- 유닛 길막/동선 보장 규칙: 생성 맵은 설치본 walk mask로 계산한 정적 시작지 연결과 `doodad check` 정렬 검사를 모두 통과해야 한다. 두 검사는 실제 게임 유닛의 이동 성공이나 Bridge 위의 충돌·폭을 증명하지 않는다. 실제 게임 내 통행은 아직 확인되지 않았다.
