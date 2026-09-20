#pragma once

// euddraft 를 불러 EUD 맵을 빌드한다.
//
// EUD 페이로드를 만드는 일은 eudplib 가 이미 아주 잘 한다. 그것을 다시
// 만들지 않고 바깥 프로그램으로 부른다 — AGENTS.md 의 "파서를 처음부터
// 다시 만들지 않는다" 와 같은 뜻이다.
//
// 이 파일은 Qt 를 쓰지 않는다. 설정 파일을 만드는 부분(settingsText)은
// 순수 함수라 테스트로 감쌀 수 있다.

#include <string>
#include <vector>

namespace splash::io::euddraft {

/// euddraft 플러그인 하나. 이름만 있으면 동봉 플러그인, `.eps`·`.py` 로
/// 끝나면 파일이다.
struct Plugin
{
    std::string name;
    std::vector<std::pair<std::string, std::string>> settings;
};

/// 빌드 한 번에 필요한 것.
struct BuildRequest
{
    std::string executable;   ///< 비어 있으면 findExecutable() 로 찾는다
    std::string inputMap;     ///< 원본 맵 (.scx/.scm)
    std::string outputMap;    ///< 만들 맵. 원본과 같으면 euddraft 가 거부한다
    std::vector<std::string> scripts; ///< epScript(.eps)·파이썬(.py) 경로
    std::vector<Plugin> plugins;      ///< 동봉 플러그인 (eudTurbo 등)

    /// 맵 보호(freeze). euddraft 는 기본으로 켠다. 끄면 다시 편집할 수 있는
    /// 맵이 나온다 — 우리가 그 결과를 다시 열어 봐야 하므로 기본은 끔이다.
    bool freeze = false;

    /// 설정 파일과 로그를 둘 곳. 비어 있으면 출력 맵 옆에 만든다.
    std::string workingDirectory;

    /// 그 밖의 `[main]` 옵션 (debug, sectorSize 등).
    std::vector<std::pair<std::string, std::string>> mainOptions;
};

/// 빌드 결과.
struct BuildResult
{
    bool ok = false;
    int exitCode = 0;
    int signalNumber = 0;       ///< 신호로 죽었으면 그 번호 (macOS 빌드의 SIGBUS 등)
    bool outputWritten = false; ///< 출력 맵이 실제로 생겼는지
    std::string settingsPath;   ///< 우리가 만든 .eds 경로
    std::string command;        ///< 실제로 돌린 명령
    std::string log;            ///< euddraft 가 뱉은 것 (stdout+stderr)
    std::string message;        ///< 사람에게 보일 한 줄
};

/// euddraft 실행 파일을 찾는다. 없으면 빈 글자.
///
/// 보는 곳: 환경 변수 `SPLASH_EUDDRAFT`, PATH, 그리고 흔히 풀어 두는
/// 자리들(홈 폴더·응용 프로그램 폴더의 `euddraft*`).
std::string findExecutable();

/// 설정 파일(.eds) 내용을 만든다.
///
/// `.eds` 는 한 번 돌고 끝나는 모드다. `.edd` 로 주면 euddraft 가 파일을
/// 지켜보는 데몬으로 돌아 끝나지 않는다 — 우리가 부를 때는 언제나 .eds 다.
std::string settingsText(const BuildRequest & request);

/// 경로를 절대 경로로 펴고 어긋난 데가 없는지 본다.
///
/// euddraft 는 설정 파일이 있는 폴더로 옮겨 간 뒤 상대 경로를 풀기
/// 때문에, 우리가 미리 절대 경로로 적어야 한다. build() 가 먼저 이것을
/// 부르고, 미리보기도 같은 결과를 보이려고 이것을 쓴다.
///
/// 탈이 없으면 빈 글자, 있으면 사유를 돌려준다.
std::string normalize(BuildRequest & request);

/// 설정 파일을 만들고 euddraft 를 돌린다.
BuildResult build(const BuildRequest & request);

/// 로그에서 사람이 먼저 봐야 할 줄(오류·경고)만 추린다.
std::vector<std::string> importantLines(const std::string & log);

} // namespace splash::io::euddraft
