// 갈래별 명령이 함께 쓰는 것들 — 인자 파싱, 열기·고치기·저장 틀, 이름 조회.
//
// 명령이 마흔 개를 넘으면 평평한 목록은 읽을 수 없다. 그래서 명령을
// `splash-cli <갈래> <명령>` 꼴로 묶고, 도움말도 갈래 단위로 낸다.

#pragma once

#include "io/map_archive.h"

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace splash::io { class GameGraphics; }

namespace splash::cli {

/// 인자를 잘못 준 경우. 잡아서 사유와 사용법을 함께 낸다.
class CliError : public std::runtime_error
{
public:
    explicit CliError(const std::string & why) : std::runtime_error(why) {}
};

/// 한 명령의 인자. 옵션(`--이름 값`, `--이름=값`, 값 없는 깃발)을 먼저
/// 떼어 내고 남은 것을 위치 인자로 쓴다.
///
/// 옵션은 꺼낼 때 지워진다. 명령이 아는 옵션을 다 꺼낸 뒤 finish() 를
/// 부르면, 남은 `--무엇` 은 오타이므로 오류가 된다.
class Args
{
public:
    explicit Args(std::vector<std::string> tokens);

    /// 값 없는 깃발. 있으면 지우고 참.
    bool flag(const std::string & name);

    /// 값 있는 옵션. 없으면 nullopt.
    std::optional<std::string> option(const std::string & name);

    /// 값 있는 옵션을 수로. 숫자가 아니면 CliError.
    std::optional<long long> number(const std::string & name);

    /// on/off·true/false·1/0 을 받는 옵션.
    std::optional<bool> toggle(const std::string & name);

    /// 위치 인자 개수. 옵션을 다 꺼낸 뒤에 세야 맞다.
    std::size_t count() const;

    /// 위치 인자 하나. 없으면 CliError.
    const std::string & at(std::size_t index) const;

    /// 위치 인자 하나를 수로. 없거나 숫자가 아니면 CliError.
    long long integer(std::size_t index) const;

    /// 위치 인자가 있으면 수로, 없으면 기본값.
    long long integerOr(std::size_t index, long long fallback) const;

    /// 남은 위치 인자 전부 (index 부터).
    std::vector<std::string> from(std::size_t index) const;

    /// 모르는 옵션이 남았으면 CliError.
    void finish() const;

private:
    // 토큰을 미리 나누지 않는다. `--owner 2` 의 2 는 옵션을 꺼낼 때에야
    // 값인지 위치 인자인지 갈리기 때문이다.
    std::vector<std::string> tokens_;
    std::size_t fixedFrom_ = std::size_t(-1); ///< `--` 뒤는 모두 위치 인자

    /// 그 자리 토큰이 아직 안 꺼낸 옵션처럼 보이는지.
    bool looksLikeOption(std::size_t index) const;

    /// 남은 위치 인자만 모은다.
    std::vector<std::string> positionals() const;
};

/// 저장할 곳. `-o <경로>` 또는 `--in-place`.
struct SaveTarget
{
    std::string path;      ///< 실제로 쓸 경로. 비어 있으면 원본 자리.
    bool inPlace = false;  ///< 원본을 덮어쓰는지 (임시 파일을 거쳐 바꿔치기한다)
};

/// 저장 위치를 옵션에서 꺼낸다. 둘 다 없으면 CliError — 원본을 말없이
/// 덮어쓰지 않는다.
///
/// **위치 인자를 읽기 전에** 불러야 한다. `-o <경로>` 의 경로는 꺼내기
/// 전까지 위치 인자처럼 보이기 때문이다.
SaveTarget takeSaveTarget(Args & args);

/// 맵을 열고 body 로 고친 뒤 저장하고, 저장본을 다시 열어 확인한다.
///
/// body 가 거짓을 돌려주면 저장하지 않는다 (사유는 body 가 출력한다).
/// AGENTS.md 의 "열기 → 고치기 → 저장 → 다시 열기" 를 명령마다 되풀이하지
/// 않으려고 여기 모았다.
int editMap(const std::string & mapPath, const SaveTarget & target, Args & args,
            const std::function<bool(io::MapArchive &)> & body);

/// 맵을 열어 읽기만 한다.
int readMap(const std::string & mapPath,
            const std::function<int(io::MapArchive &)> & body);

/// 게임 자료를 읽는다. `--install <경로>` 가 없으면 CliError.
/// 비용이 크므로 필요한 명령에서만 부른다.
bool loadGraphics(Args & args, io::GameGraphics & graphics);

// --- 이름 조회 ---

/// 유닛을 번호나 이름으로 고른다. 이름은 대소문자를 가리지 않고,
/// 딱 하나만 걸리면 부분 일치도 받는다.
std::uint16_t parseUnitType(const std::string & text);

/// 플레이어를 1~12 로 받아 0~11 로 돌려준다. `p3` 꼴도 받는다.
std::uint8_t parseOwner(const std::string & text);

/// 고도 플래그를 이름으로 받는다: `저지대,고공` 또는 `low,high-air`,
/// 또는 그냥 수. 이름은 쉼표로 잇는다.
std::uint16_t parseElevationFlags(const std::string & text);

/// 고도 플래그를 사람이 읽는 이름으로.
std::string elevationFlagsText(std::uint16_t flags);

/// 플레이어 비트(1~8 번을 비트 0~7 로)를 `1,3,5` 또는 `all`·`none` 으로 받는다.
std::uint8_t parsePlayerBits(const std::string & text);

/// 플레이어 비트를 `1,3,5` 꼴로.
std::string playerBitsText(std::uint8_t bits);

/// 종족 이름 (Chk::Race).
std::string raceName(std::uint8_t race);
std::uint8_t parseRace(const std::string & text);

/// 슬롯 종류 이름 (Sc::Player::SlotType).
std::string slotTypeName(std::uint8_t slotType);
std::uint8_t parseSlotType(const std::string & text);

/// 색 이름 (Chk::PlayerColor).
std::string playerColorName(std::uint8_t color);
std::uint8_t parsePlayerColor(const std::string & text);

// --- main.cpp 에 있는 알맹이 ---
//
// 트리거·브리핑·그림 그리기는 갈래를 나누기 전부터 있던 명령이다. 코드를
// 옮기면 그만큼 어긋날 자리가 생기므로, 갈래 쪽에서 불러 쓰기만 한다.

int renderMapImage(const std::string & mapPath, const std::string & installPath,
                   const std::string & outPath,
                   bool drawUnits, bool drawLocations, bool drawCreep);

int triggerTextCommand(const std::string & mapPath, const std::string & installPath,
                       const std::string & outPath);
int setTriggersCommand(const std::string & mapPath, const std::string & installPath,
                       const std::string & textPath, const std::string & outMapPath);
int triggerListCommand(const std::string & mapPath, const std::string & installPath,
                       int triggerIndex);
int triggerArgsCommand(const std::string & mapPath, const std::string & installPath,
                       std::size_t triggerIndex);
int setTriggerArgCommand(const std::string & mapPath, const std::string & installPath,
                         const std::string & which, std::size_t triggerIndex,
                         std::size_t slot, std::size_t argIndex,
                         const std::string & value, const std::string & outMapPath);
int briefingTextCommand(const std::string & mapPath, const std::string & installPath,
                        const std::string & outPath);
int setBriefingCommand(const std::string & mapPath, const std::string & installPath,
                       const std::string & textPath, const std::string & outMapPath);
int briefingArgsCommand(const std::string & mapPath, const std::string & installPath,
                        std::size_t index);

// --- 갈래 표 ---

using CommandFn = int (*)(Args & args);

struct Command
{
    const char * name;
    const char * usage;   ///< 위치 인자·옵션 꼴
    const char * summary; ///< 한 줄 설명
    CommandFn run;
};

struct Group
{
    const char * name;
    const char * summary;
    std::vector<Command> commands;
};

/// 모든 갈래. 갈래 파일마다 하나씩 보태 만든다.
const std::vector<Group> & allGroups();

/// 갈래 이름인지.
bool isGroupName(const std::string & name);

/// 갈래 목록만 한 줄씩.
void printGroupList(const std::string & argv0);

/// 한 갈래의 명령을 모두.
void printGroupHelp(const std::string & argv0, const Group & group);

/// 갈래 명령을 실행한다. 갈래가 아니면 kUnknownCommand.
inline constexpr int kUnknownCommand = -1000;
int runGroupCommand(const std::string & argv0, const std::vector<std::string> & args);

// 갈래 파일들이 채워 넣는 것.
std::vector<Group> objectGroups();
std::vector<Group> terrainGroups();
std::vector<Group> settingsGroups();
std::vector<Group> scenarioGroups();
std::vector<Group> eudGroups();

} // namespace splash::cli
