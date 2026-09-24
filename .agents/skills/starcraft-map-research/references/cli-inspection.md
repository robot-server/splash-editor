# `splash-cli` 원본 맵 열람 목록

열람 범위는 빌드된 CLI가 정본이다. 아래 표는 현재 CLI 전체 도움말과 그룹별 도움말에서 확인한 원본 맵의 읽기 표면을 정리한 체크리스트다. 저장소 루트에서 실행 파일을 정하고 전체 도움말과 각 그룹 도움말을 확인한다. `SPLASH_CLI`가 지정됐으면 그 경로를 쓴다. 기본 빌드본이 없으면 임의로 오래된 CLI나 생성 스크립트를 쓰지 말고 올바른 빌드 위치를 확인한다. CLI에 읽기 명령이 추가되면 해당 표면을 이 체크리스트에도 추가한다.

```sh
CLI="${SPLASH_CLI:-./build-cli/src/cli/splash-cli}"
"$CLI" help
"$CLI" unit
```

아래 표의 명령은 전부 `"$CLI"`를 앞에 붙여 실행한다. 설치 에셋이 필요한 명령의 `<설치경로>`는 `SC_INSTALL` 또는 사용자가 직접 제공한 경로에서만 가져온다. 값이 없으면 묻고 경로를 추측하지 않는다. 이 표는 현재 저장소 CLI의 읽기 명령이며, 실제 조사 때 최신 그룹 도움말로 구문을 확인한다.

## 컨테이너·맵·설치 에셋

| 범위 | 읽기 명령 | 확인할 근거 |
| --- | --- | --- |
| MPQ/CHK·메타데이터 | `info <맵>`, `map info <맵>`, `chk <맵> <임시.chk>`, `roundtrip <맵>` | 버전/형식, 크기, 타일셋, 이름/설명, 섹션 보존, CHK 바이트. 추출 CHK는 임시 위치에 둔다. |
| 문자열 인코딩 | `map encoding <맵> [--encoding cp949|cp932|cp936|cp1252|utf8|ascii] [--limit N]` | 저장을 바꾸지 않고 표시되는 코드 페이지별 문자열 해석을 비교한다. |
| 설치 에셋 | `assets <설치경로>`, `unit-stats <설치경로> [--json]`, `unit-classes <설치경로>`, `images-tbl <설치경로> [검색어]`, `has-asset <설치경로> <아카이브경로>` | 사용 가능한 타일셋·에셋, 유닛/무기 속성, 이미지 및 파일 이름. |
| 타일·두들 에셋 | `tileset-info <설치경로> <타일셋>`, `tileset-groups <설치경로> <타일셋>`, `tileset-tiles <설치경로> <타일셋>`, `tileset-ramps <설치경로> <타일셋>`, `find-creep <설치경로> <타일셋>`, `creep-kin <설치경로> <타일셋> <메가타일> <개수>`, `tile-sheet`, `mega-sheet` — 정확한 인자는 현재 도움말 참조 | 지형 그룹/타일 속성, 램프 후보, 크립 관련 자료, 시각 타일 근거. 지역 램프 후보가 실제 배치된 경로 전체의 연결을 증명하지는 않는다. |
| 유닛 이미지 | `unit types [--find]`, `unit-image <설치경로> <유닛ID> <임시.ppm> [소유자] [타일셋]`, `icon`, `icon-histogram` | 정확한 유닛 이름/ID, 기능에 필요한 이미지·아이콘 구분. |
| 맵 렌더 | `render <맵> <설치경로> <임시.ppm> [--units] [--locations] [--creep]`, `scenario image`(그룹 명령) | 지형, 유닛, 로케이션, 크립과 알 수 없는 타일 경고. 로그뿐 아니라 렌더 이미지를 직접 확인한다. |

## 맵 오브젝트와 지형

| 범위 | 읽기 명령 | 확인할 근거 |
| --- | --- | --- |
| 유닛·자원 | `unit list <맵> [--owner] [--type] [--limit]`, `unit types [--find]` | 유닛 종류/ID, 소유자, 픽셀 좌표, 자원, 스타팅, 배치 관계. `--limit`으로 잘렸는지 확인하고 필터/상향 제한으로 전체를 읽는다. |
| 스프라이트 | `sprite list <맵> [--limit]` | 스프라이트 ID·좌표·소유자·상태. |
| 두들 | `doodad list <맵> [--install] [--catalogue]`, `doodad fits <맵> <ID> <타일x> <타일y> --install`, `doodad check <맵> --install` | 배치된/카탈로그 두들, 배치 적합성, 어긋남 진단. 표가 없으면 `fits`는 알 수 없음일 수 있다. |
| 로케이션 | `location list <맵>`, `location ai-towns <맵>` | 이름·범위·고도/레이어 조건, AI 타운 사용 여부. |
| 지형 | `terrain show <맵> <x> <y> [w h] [--underlying]`, `terrain types <맵> --install` | 화면 지형 MTXM, 선택적 밑 지형 TILE, 타일 ID, ISOM 지형 이름. 기능 관련 구역을 각각 확인하고 작은 샘플 하나만으로 전체를 추정하지 않는다. |
| 시야 가림 | `fog show <맵> <x> <y> [w h]` | 구역별 플레이어 MASK 비트. |
| 플레이어·세력 | `player list <맵>`, `force list <맵>` | 슬롯 종류/종족/색, Force, 동맹 승리, 시야 공유, 시작 위치 섞기 설정. |
| 문자열·스위치 | `string list <맵> [--used] [--find] [--limit]`, `switch list <맵> [--named]` | 사용/미사용 문자열, 트리거 이름, 명명된 스위치. 제한에 걸리면 필터를 반복해 전체를 확인한다. |
| 유닛 프리셋·설정 | `preset list <맵> [--used]`, `unitdef get <맵> <유닛>` | CUWP 속성, UNIS/UNIx 덮어쓰기. 설치 기본값과 비교하고 플레이어 적용 범위를 확인한다. |
| 업그레이드·기술 | `upgrade list`, `upgrade get <맵> <ID>`, `tech list`, `tech get <맵> <ID>` | ID, 비용/시간, 시작·최대 레벨, 플레이어별 사용 가능/연구/기본 플래그. |
| 소리 | `sound list <맵> [--fast]`, `sound extract <맵> <ID> <임시.wav>` | 내장 WAV의 이름/ID 및 필요한 경우 실제 음성. `--fast`는 맵 내 확인을 건너뛰므로 참조 검증에는 전체 목록을 쓴다. |
| 오브젝트 클립보드 | `object show <임시.objects>`; 원본 구역을 확인하려면 `object copy <맵> ... <임시.objects>` 후 표시 | 의미 있는 합성 배치 자료가 원본에 있을 때만 확인한다. 복사는 원본 맵을 바꾸지 않지만 임시 조사 파일을 만든다. 기능 의미 자체를 증명하지는 않는다. |

## 트리거·브리핑 논리

| 범위 | 읽기 명령 | 확인할 근거 |
| --- | --- | --- |
| 트리거 텍스트·구조 | `trigger show <맵> [임시.txt] --install`, `trigger list <맵> [번호] --install`, `trigger args <맵> <번호> --install`, `trigger types <맵> [condition|action] [--find] --install` | `trigger list`의 요약은 기본적으로 처음 12개만 보인다. 전체 체인은 `trigger show`로 내보내고, 개별 조건/액션 및 인자는 `trigger list <번호>`/`trigger args`로 확인한다. 실행 소유자, 활성 여부, 순서, 문자열·로케이션·유닛·Force·스위치·Deaths·자원 참조, 상태 전이를 이어 읽는다. |
| 미션 브리핑 | `briefing show <맵> [임시.txt] --install`, `briefing args <맵> <번호> --install`, `briefing types <맵> [--find] --install`, `briefing detail <맵> <번호> --install` | 대상/소유자, 순서가 있는 브리핑 액션, 텍스트, 게임 시작 안내. |
| EUD 참조·도구 상태 | `eud list <맵> [--db]`, `eud check <맵> [--db]`; 주소 조사에는 `eud addr`, `epd`, `offsets`; 설치된 빌더 위치에는 `eud which` | 맵이 읽거나 쓰는 주소와 오프셋 DB의 정적 진단, euddraft 검색 결과. 호환성 검사는 게임 실행을 대신하지 않는다. EUD를 실행 가능한 방법으로 제안할 때는 권한과 게임 버전 검증을 확인한다. |

## 조사 원칙

- `splash-cli help`와 관련 그룹 도움말을 먼저 읽어 새 읽기 기능을 놓치지 않는다. 원본 열람에서는 수정 명령을 검사 목적으로 실행하지 않는다.
- `--limit` 출력이 잘리지 않았는지 확인한다. 더 있다는 문구가 있으면 필터나 큰 제한으로 나눠 읽는다. 대용량 결과는 임시 조사 폴더나 코퍼스 메타데이터에 두고 문서 본문으로 복사하지 않는다.
- 구조 출력과 `render`, `terrain show`를 짝지어 본다. 렌더는 CHK의 모든 필드를 보여 주지 않고, 목록 출력은 트리거와 공간의 관계를 보여 주지 않는다. 여러 뷰를 결합한다.
- 유즈맵은 `소유자 → 활성 트리거 → 조건 → 순서가 있는 액션 → 상태 변경 → 다음 트리거/종료`를 따라가고, 참조 로케이션·유닛·문자열을 렌더 위치와 연결한다.
- 밀리맵은 `스타팅 → 본진 자원 → 앞마당/확장 → 경로/램프 → 중앙/상대` 관계를 따라간다. 건물 footprint, 애드온 여유, 일꾼 접근, 지형·두들을 확인한다. 거리나 자원 수만으로 공정함 또는 실제 길찾기를 증명하지 않는다.
- 명령 출력, 렌더 관찰, 원본에서 관찰한 사실, 제작 권고를 구분한다. 뷰가 없거나 설치 에셋이 없으면 그 한계를 쓰고 빈 곳을 추측으로 채우지 않는다.
