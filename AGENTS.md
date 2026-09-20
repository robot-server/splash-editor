# 이 저장소에서 일할 때

Splash Editor 는 StarCraft: Brood War / Remastered 맵 에디터다. 목표는
**SCMDraft 2 수준의 기능 패리티**이며, StarEdit 수준에서 멈추지 않는다.

## 어기면 안 되는 것

- **C++17 이상, CMake, Qt 6 Widgets.** 실제로는 C++20 을 쓴다 — MappingCore 의
  `chk.h` 가 RareCpp 리플렉션을 요구하고 그것이 C++20 을 요구한다.
- **LGPLv3 이하 라이선스의 모듈만** 쓴다. GPL 전용 모듈은 넣지 않는다.
- **Qt 는 동적 링크만.** 정적 링크는 금지이며, 빌드 스크립트가 정적 Qt 를
  감지하면 설정 단계에서 멈춘다(`src/ui/CMakeLists.txt`).
- **MPQ 는 StormLib, CASC 는 CascLib.** 맵 파일은 MPQ, 설치본 그래픽은 CASC 다.
- **CHK·지형 파싱은 MappingCore(Chkdraft)만 재사용한다.** 파서를 처음부터 다시
  만들지 않는다.
- **앱은 MIT.** README 에 Qt LGPLv3, 동적 링크, Qt 소스 받는 법을 적어 둔다.
- **금지**: UI 통째 포크, Win32 포팅, Electron·Tauri, Qt 정적 링크,
  **SCMDraft 역공학**.
- **아는 구역만 고친다.** 나머지 바이트는 그대로 보존한다.
- 다툼이 있으면 **게임이 실제로 여는 저장본이 언제나 정답**이다.

## 일하는 방식

- 고친 것은 실제 맵으로 확인한다. 열기 → 고치기 → 저장 → 다시 열기 →
  견주기. 숫자로 말할 수 있을 때까지 확인한다.
- 커밋 메시지와 저장소에 남는 문서에는 **사용자의 맵 파일 이름을 적지
  않는다.** 증상·수치·원인만 적는다. 블리자드 공식 캠페인 이름은 예외다.
- 막힌 일은 [BACKLOG.md](BACKLOG.md) 에 "무엇이 막고 있는지"와 측정값을 함께
  적는다. 같은 조사를 두 번 하지 않기 위해서다.
- 의존성은 CMake `FetchContent` 로 **커밋 해시를 고정해** 가져온다. 소스를
  복사해 넣지 않는다 — 업스트림 변경으로 CHK 파싱이 조용히 달라지는 일을
  막는다.

## 저장 경로에서 지켜야 하는 것

맵을 쓰는 코드를 건드릴 때는 아래를 기억한다. 모두 실제 맵에서 겪어 보고
정한 것이다.

- **`MapFile::save()` 를 쓰지 않는다.** 그 함수는 saveType 에 맞춰
  `Scenario::changeVersionTo()` 를 무조건 부르고, 그 안의
  `deleteUnusedStrings(Both)` 가 참조를 추적하지 못한 문자열을 지운다(실제
  맵에서 고유 문자열 302 → 233 개). 인자로 받은 `lockAnywhere` /
  `autoDefragmentLocations` 는 전달조차 되지 않는다. 대신 `Scenario::write()`
  로 CHK 를 만들고 `MpqFile` 로 직접 넣는다.
- **`strTailData` 를 되살린다.** MappingCore 는 STR 뒤에 남은 바이트를 읽을
  때는 보존하지만 쓸 때는 붙이지 않는다. 맵 보호나 서명에 쓰이는 경우가 있다.
- **화면에 그리는 지형은 MTXM 이다.** TILE 은 두들을 걷어낸 밑 지형이라
  그쪽을 그리면 캠페인 맵의 절벽이며 바위가 사라진다.
- **round-trip 은 맵 파일이 아니라 CHK 바이트로 견준다.** MPQ 컨테이너는
  해시 테이블 배치·압축 결과가 달라질 수 있다.

## 참고하는 것

- **Chkdraft** — 코어(MappingCore)만 재사용한다. UI 는 복사하지 않는다.
- **ChkForge** — Qt 와 코어를 잇는 방식에서 아이디어만 참고한다.
- **SCMDraft 2** — 동작과 저장 결과의 오라클. 코드 공급원이 아니며
  역공학하지 않는다.
