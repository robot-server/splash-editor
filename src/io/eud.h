#pragma once

// EUD 자리 셈. Qt 에 기대지 않는 순수 계산이라 테스트로 감쌀 수 있다.
//
// EUD 트리거는 "플레이어 N 의 유닛 M 을 죽인 수"를 세는 Deaths 조건이 실은
// 게임 메모리의 한 자리를 읽는다는 점을 이용한다. 자리와 주소를 서로 바꾸는
// 셈이 여기 있다.

#include <cstdint>
#include <vector>

namespace splash::io::eud {

/// Deaths 표가 시작하는 자리. 1.16.1 기준이며 리마스터도 같은 배치를 쓴다 —
/// EUD 맵이 이 값을 전제로 만들어져 있다.
inline constexpr std::uint32_t kDeathsBase = 0x0058A364;

/// 유닛 종류 수. 한 플레이어분의 칸 수이기도 하다.
inline constexpr std::uint32_t kUnitTypes = 228;

/// 자리 -> 주소. 자리는 32비트로 감아 돈다.
inline std::uint32_t addressFor(std::uint32_t player, std::uint32_t unit)
{
    return kDeathsBase + 4u * (player * kUnitTypes + unit);
}

/// 주소 -> EPD. 네 바이트 단위로 센 Deaths 표로부터의 거리다.
inline int epdFor(std::uint32_t address)
{
    return static_cast<int>(address - kDeathsBase) / 4;
}

/// 주소 -> 자리. 네 바이트에 맞지 않으면 거짓.
///
/// EUD 가 노리는 곳은 대개 Deaths 표보다 앞이다. 그런 주소는 자리가 음수가
/// 되는데, 게임은 32비트로 감아 세므로 아주 큰 플레이어 번호로 나타난다.
inline bool slotFor(std::uint32_t address, std::uint32_t * player, std::uint32_t * unit)
{
    if ((address - kDeathsBase) % 4 != 0)
        return false;

    const std::uint32_t slot = (address - kDeathsBase) / 4;
    if (player != nullptr)
        *player = slot / kUnitTypes;
    if (unit != nullptr)
        *unit = slot % kUnitTypes;
    return true;
}

/// 널리 쓰이는 자리.
struct KnownAddress
{
    const char * name;
    std::uint32_t address;
};

/// 1.16.1 기준이며 리마스터도 같은 자리를 흉내 낸다.
/// 출처: StarEdit Network 의 EUD 주소 모음.
const std::vector<KnownAddress> & knownAddresses();

} // namespace splash::io::eud
