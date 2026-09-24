# [지형/배치 메커니즘]

## 의도한 고도층을 타일셋 데이터로 선택

- 메커니즘 이름: 타일셋별 지형 종류·고도 속성 확인
- 적용 이유/목적: 저지, 고지, 평탄 발판, 차단 지형을 구분해 base와 ramp를 계획한다. “High” 같은 지형 이름이 실제 고도값을 정확히 뜻한다고 가정하지 않는다.
- 구체적 규칙 (타일 단위, 좌표 단위, 배치 각도, 언덕 연결 규칙 등): `splash-cli terrain types <map> --install "$SC_INSTALL"`로 현재 타일셋 지형 종류를 읽고, `splash-cli tileset-tiles "$SC_INSTALL" <tileset>`의 타일 walk/build/height 속성을 본다. ISOM 종류가 저장 타일에 어떤 높이 값을 내는지는 `.agents/skills/starcraft-map/scripts/measure_terrain_types.py`가 빈 맵에 지형 종류를 하나씩 칠해 얻은 `data/terrain-types.json`을 참고한다. 기준 높이는 타일셋의 평지와 높은 지형을 비교해 정하고, 이름 접두사만으로 분류하지 않는다.
- 잘된 예시 및 실패하는 나쁜 예시: Jungle의 `Dirt`와 `High Dirt`, Space의 `Platform`과 `High Platform`처럼 실제로 생성되는 높이/이동 속성을 확인한 짝을 써서 단차를 계획하면 성공이다. Space의 일반 `Platform`이나 Installation의 `Floor`처럼 평탄한 바닥이 높이 1일 수 있는데 전체 타일셋에 height ≥ 1을 적용해 고지로 세는 것은 실패다. Twilight의 `High Sunken Ground`처럼 이름과 수치가 직관과 다를 수 있다.

# [에디터 타일/배치 제약사항]

- 지형 간 연결 규칙: 고지 판정 문턱을 모든 타일셋에 같은 값으로 고정하지 않는다. 절벽과 ramp 연결은 높이값뿐 아니라 walkability 및 Height Transition/Pathfinder Regions 오버레이로 확인한다.
- 유닛 길막/동선 보장 규칙: 높이 분류는 path 연결과 별개다. 저지·고지 양쪽에서 실제 보행이 되는지 이동 속성 및 미니타일 경로 검사로 확인한다.
