#pragma once

// EUD 자리 셈과 오프셋 표. Qt 에 기대지 않는 순수 계산이라 테스트로 감쌀 수 있다.
//
// EUD 트리거는 "플레이어 N 의 유닛 M 을 죽인 수"를 세는 Deaths 조건이 실은
// 게임 메모리의 한 자리를 읽는다는 점을 이용한다. 자리와 주소를 서로 바꾸는
// 셈이 여기 있다.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace splash::io::eud {

/// Deaths 표가 시작하는 자리. 1.16.1 기준이며 리마스터도 같은 배치를 쓴다 —
/// EUD 맵이 이 값을 전제로 만들어져 있다.
inline constexpr std::uint32_t kDeathsBase = 0x0058A364;

/// 유닛 종류 수. Deaths 표의 바깥 차원이다.
inline constexpr std::uint32_t kUnitTypes = 228;

/// 한 유닛 종류가 차지하는 플레이어 칸 수.
///
/// 표는 **유닛 바깥, 플레이어 안쪽**으로 늘어서 있다 — P1 마린, P2 마린,
/// … P12 마린, P1 고스트, … 꼴이다. 한 항목이 48바이트(12 x 4)이고 그런
/// 항목이 228개다. 이 순서를 뒤집으면 주소가 통째로 어긋난다.
inline constexpr std::uint32_t kPlayers = 12;

/// 자리 -> EPD. Deaths 표 첫 칸으로부터 네 바이트 단위로 센 거리다.
///
/// 게임은 이 셈을 32비트로 하므로 그대로 감아 돌게 둔다.
inline constexpr std::uint32_t epdForSlot(std::uint32_t player, std::uint32_t unit)
{
    return unit * kPlayers + player;
}

/// 자리 -> 주소.
inline constexpr std::uint32_t addressFor(std::uint32_t player, std::uint32_t unit)
{
    return kDeathsBase + 4u * epdForSlot(player, unit);
}

/// EPD -> 주소.
inline constexpr std::uint32_t addressForEpd(std::uint32_t epd)
{
    return kDeathsBase + 4u * epd;
}

/// 주소 -> EPD.
///
/// 나눗셈을 **부호 있는 수로** 해야 한다. Deaths 표보다 앞인 자리는 거리가
/// 음수인데, 부호 없이 나누면 같은 주소를 가리키는 다른 답(2^30 만큼
/// 어긋난 값)이 나온다. 게임은 곱셈을 32비트로 감아 하므로 둘 다 같은
/// 자리를 가리키지만, eudplib·EUD Book·SCMDraft 가 모두 부호 있는 쪽을
/// 쓰므로 여기서도 맞춘다 (미네랄은 -11421 이다).
inline constexpr std::uint32_t epdFor(std::uint32_t address)
{
    const auto distance = static_cast<std::int32_t>(address - kDeathsBase);
    return static_cast<std::uint32_t>(distance / 4);
}

/// 주소 -> EPD 를 부호 있는 수로. Deaths 표보다 앞인 자리를 사람에게
/// 보일 때 쓴다 (미네랄은 -11421 처럼 음수다).
inline constexpr std::int32_t signedEpdFor(std::uint32_t address)
{
    return static_cast<std::int32_t>(epdFor(address));
}

/// 주소가 네 바이트 경계에 놓여 있는지. Deaths 칸은 4바이트 단위다.
inline constexpr bool isAligned(std::uint32_t address)
{
    return (address - kDeathsBase) % 4u == 0;
}

/// 그 주소가 든 네 바이트 칸의 첫 자리.
///
/// 게임의 바이트·워드 값은 네 바이트 경계에 놓여 있지 않은 것이 흔하다
/// (유닛 색 0x581D76 처럼). Deaths 는 네 바이트 단위로만 읽고 쓰므로,
/// 그런 자리는 **담긴 칸을 읽고 비트마스크로 걸러** 다룬다 — 그것이
/// Memory Masked 가 있는 까닭이다.
inline constexpr std::uint32_t containingDword(std::uint32_t address)
{
    return address - ((address - kDeathsBase) % 4u);
}

/// 그 주소의 값 size 바이트만 남기는 비트마스크.
///
/// 칸 안에서 몇 번째 바이트인지에 따라 자리가 달라진다.
///
/// size 가 네 바이트를 넘으면 그것은 값의 폭이 아니라 **항목 사이의 간격**
/// 이다(우리 표는 그 둘을 한 칸에 담는다 — Deaths 표의 48 처럼). 폭을 알 수
/// 없으니 한 바이트만 남긴다. 넓게 잡아 옆 값까지 덮는 것보다 안전하다.
inline constexpr std::uint32_t maskFor(std::uint32_t address, std::uint32_t size)
{
    const std::uint32_t byteInDword = (address - kDeathsBase) % 4u;
    const std::uint32_t width = (size == 0 || size > 4) ? 1u : size;
    const std::uint32_t bytes = (width > 4u - byteInDword) ? 4u - byteInDword : width;
    const std::uint32_t bits = bytes * 8u;
    const std::uint32_t value = (bits >= 32u) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    return value << (byteInDword * 8u);
}

/// 주소 -> Deaths 자리. 네 바이트에 맞지 않으면 거짓.
///
/// 표 안에 드는 주소는 본래의 (플레이어, 유닛) 으로 돌려준다. 표 밖이면
/// 유닛을 0 으로 두고 플레이어 자리에 EPD 를 통째로 넣는다 — 게임이
/// `유닛 x 12 + 플레이어` 를 32비트로 세므로 이 꼴로도 같은 자리를
/// 가리키고, euddraft·SCMDraft 의 Memory 조건이 쓰는 표기와 같다.
inline bool slotFor(std::uint32_t address, std::uint32_t * player, std::uint32_t * unit)
{
    if (!isAligned(address))
        return false;

    const std::uint32_t epd = epdFor(address);
    std::uint32_t outUnit = 0;
    std::uint32_t outPlayer = epd;

    if (epd < kUnitTypes * kPlayers)
    {
        outUnit = epd / kPlayers;
        outPlayer = epd % kPlayers;
    }

    if (player != nullptr)
        *player = outPlayer;
    if (unit != nullptr)
        *unit = outUnit;
    return true;
}

/// 그 자리가 정말 Deaths 표 안인지 (진짜 죽은 수를 세는 자리인지).
inline constexpr bool isInsideDeathTable(std::uint32_t player, std::uint32_t unit)
{
    return player < kPlayers && unit < kUnitTypes;
}

/// 리마스터의 EUD 에뮬레이터가 그 자리를 어떻게 다루는지.
///
/// 1.21 부터 블리자드는 EUD 를 전부 되살린 것이 아니라 자리마다 골라
/// 흉내 낸다. 쓰기가 막힌 자리에 쓰는 맵은 리마스터에서 아예 열리지
/// 않으므로, 저장하기 전에 말해 줄 수 있어야 한다.
enum class ScrSupport
{
    Unknown,      ///< 표에 없는 자리
    SimpleData,   ///< 그냥 값. 읽기·쓰기 모두 된다
    Supported,    ///< 에뮬레이터가 다룬다
    BackedByCode, ///< 코드가 뒤를 받친다 — 쓰면 게임이 따라 움직인다
    ReadOnly,     ///< 읽기만 된다
    Unsupported,  ///< 리마스터에서 안 된다
    SeeDescription
};

/// 사람이 읽는 이름.
std::string scrSupportName(ScrSupport support);

/// EUD Book 의 `scr` 칸 글자를 우리 열거값으로.
ScrSupport parseScrSupport(const std::string & text);

/// 이름 붙은 메모리 자리 하나.
struct OffsetEntry
{
    std::string name;            ///< 사람이 읽는 이름
    std::uint32_t address = 0;   ///< 1.16.1 기준 주소
    std::uint32_t size = 4;      ///< 항목 하나의 바이트 수
    std::uint32_t length = 1;    ///< 항목 개수 (플레이어별이면 12)
    ScrSupport scr = ScrSupport::Unknown;
    std::string description;     ///< 있으면 한 줄 설명

    /// 이 항목이 덮는 바이트 수.
    std::uint32_t span() const { return size * (length == 0 ? 1 : length); }

    /// 주소가 이 항목 안에 드는지.
    bool covers(std::uint32_t probe) const
    {
        return probe >= address && probe < address + span();
    }
};

/// 우리가 직접 적어 둔 자주 쓰는 자리. 설치본이나 바깥 파일이 없어도
/// 늘 쓸 수 있다. 출처는 공개된 EUD 자료들이며 이름은 우리말로 적었다.
const std::vector<OffsetEntry> & builtinOffsets();

/// 바깥에서 가져온 오프셋 표를 읽어 들인다.
///
/// EUD Book(`armoha/eud-book`) 의 `api.json` 을 그대로 받는다. 그 파일은
/// 라이선스가 밝혀져 있지 않아 저장소에 넣지 않는다 — 쓰려는 사람이
/// 직접 받아 가리키게 한다.
///
/// 성공하면 참. 실패하면 거짓이고 error 에 사유가 담긴다. 실패해도
/// 이미 읽어 둔 표는 그대로 둔다.
bool loadOffsetDatabase(const std::string & path, std::string * error = nullptr);

/// 환경 변수 `SPLASH_EUD_OFFSETS` 가 가리키는 표를 읽어 둔다.
///
/// CLI 는 설정 파일이 없으므로 명령마다 `--db` 를 적는 대신 이 길을 쓴다.
/// 가리키는 것이 없으면 아무 일도 하지 않고 거짓을 돌린다 — 시작할 때마다
/// 없다고 나무랄 일은 아니다.
bool loadOffsetDatabaseFromEnvironment(std::string * error = nullptr);

/// 읽어 들인 바깥 표를 비운다.
void clearOffsetDatabase();

/// 바깥 표를 읽어 두었는지, 그 경로와 항목 수.
bool hasOffsetDatabase();
const std::string & offsetDatabasePath();

/// 지금 쓸 수 있는 자리 전부 (붙박이 + 바깥 표). 주소 순이다.
const std::vector<OffsetEntry> & offsets();

/// 이름이나 주소로 찾는다. 빈 글자를 주면 전부.
///
/// 글자는 대소문자를 가리지 않는다. `0x` 로 시작하거나 열여섯 자리 수로
/// 읽히면 그 주소를 덮는 항목을 찾는다.
std::vector<const OffsetEntry *> findOffsets(const std::string & query);

/// 그 주소를 덮는 항목. 없으면 nullptr. 여럿이면 가장 좁은 것.
const OffsetEntry * offsetAt(std::uint32_t address);

/// 주소 하나를 사람이 읽는 한 줄로 푼다.
/// 예: `0x0057F0F0  EPD -11421  플레이어 1 미네랄 [+0]  (SCR: 그냥 값)`
std::string describeAddress(std::uint32_t address);

/// `0x58A364`·`58A364`·`5809508` 을 주소로 읽는다.
/// `0x` 가 붙었거나 열여섯 자리 글자가 섞였으면 16진수, 아니면 10진수다.
std::optional<std::uint32_t> parseAddress(const std::string & text);

} // namespace splash::io::eud
