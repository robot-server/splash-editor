// 게임 자료 없이 도는 단위 테스트.
//
// round-trip 테스트는 실제 맵이 있어야 돌지만, 여기 있는 것들은 순수 계산이라
// 어디서나 돈다. 실제 맵에서 겪고 고친 일들을 다시 겪지 않기 위한 그물이다.

#include "io/chk_bytes.h"
#include "io/eud.h"
#include "io/game_graphics.h"
#include "test_support.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace splash;

/// 구역 하나를 CHK 바이트로 만든다.
void appendSection(std::vector<std::uint8_t> & chk, const char * name,
                   std::int32_t size, std::size_t payload)
{
    chk.insert(chk.end(), name, name + 4);
    for (int i = 0; i < 4; ++i)
        chk.push_back(static_cast<std::uint8_t>((size >> (8 * i)) & 0xFF));
    chk.insert(chk.end(), payload, 0);
}

void testOverlongSections()
{
    // 멀쩡한 CHK 는 건드리지 않는다.
    {
        std::vector<std::uint8_t> chk;
        appendSection(chk, "VER ", 2, 2);
        appendSection(chk, "DIM ", 4, 4);
        const std::size_t before = chk.size();
        io::truncateOverlongSections(chk);
        SPLASH_CHECK_EQ(chk.size(), before);
    }

    // 끝을 넘어가는 구역은 그 머리말부터 버린다.
    {
        std::vector<std::uint8_t> chk;
        appendSection(chk, "VER ", 2, 2);
        const std::size_t good = chk.size();
        appendSection(chk, "CMP ", 1969382724, 9); // 1.8GB 라고 적어 둔 폭탄
        io::truncateOverlongSections(chk);
        SPLASH_CHECK_EQ(chk.size(), good);
    }

    // 길이가 음수인 구역은 되감기라 그대로 둔다.
    {
        std::vector<std::uint8_t> chk;
        appendSection(chk, "VER ", 2, 2);
        appendSection(chk, "BACK", -8, 0);
        appendSection(chk, "DIM ", 4, 4);
        const std::size_t before = chk.size();
        io::truncateOverlongSections(chk);
        SPLASH_CHECK_EQ(chk.size(), before);
    }
}

/// 만들어진 STR 구역에서 자리 번호로 문자열을 읽는다 — 게임이 읽는 방식과 같다.
std::string readPacked(const std::vector<std::uint8_t> & str, std::size_t index)
{
    if (str.size() < 2)
        return {};

    const std::uint16_t count = std::uint16_t(str[0] | (str[1] << 8));
    if (index == 0 || index > count)
        return {};

    const std::size_t at = 2 + 2 * (index - 1);
    const std::uint16_t offset = std::uint16_t(str[at] | (str[at + 1] << 8));
    if (offset >= str.size())
        return {};

    std::string out;
    for (std::size_t i = offset; i < str.size() && str[i] != 0; ++i)
        out.push_back(static_cast<char>(str[i]));
    return out;
}

void testPackStrings()
{
    // 꼬리가 겹치는 문자열들은 한 벌만 담긴다.
    {
        const std::vector<std::string> strings { "abc", "bc", "c", "abc" };
        const auto packed = io::packStringsSharingTails(strings);

        SPLASH_CHECK(!packed.empty());
        for (std::size_t i = 0; i < strings.size(); ++i)
            SPLASH_CHECK_EQ(readPacked(packed, i + 1), strings[i]);

        // 머리말 뒤 글자 자리는 첫 NUL + "abc\0" 뿐이어야 한다.
        const std::size_t headerSize = 2 + 2 * strings.size();
        SPLASH_CHECK_EQ(packed.size() - headerSize, std::size_t(1 + 4));
    }

    // 빈 자리는 모두 첫 NUL 을 가리킨다.
    {
        const std::vector<std::string> strings { "", "hi", "" };
        const auto packed = io::packStringsSharingTails(strings);

        SPLASH_CHECK(!packed.empty());
        SPLASH_CHECK_EQ(readPacked(packed, 1), std::string());
        SPLASH_CHECK_EQ(readPacked(packed, 2), std::string("hi"));
        SPLASH_CHECK_EQ(readPacked(packed, 3), std::string());
    }

    // 한 구역에 담지 못하면 빈 벡터.
    {
        std::vector<std::string> strings;
        for (int i = 0; i < 40000; ++i)
            strings.push_back(std::to_string(i));
        SPLASH_CHECK(io::packStringsSharingTails(strings).empty());
    }
}

void testEudSlots()
{
    // 잘 알려진 자리들. EPD 는 StarEdit Network 의 모음과 맞춰 본 값이다.
    struct Known { std::uint32_t address; int epd; };
    const Known cases[] {
        { 0x0057F0F0, -11421 }, // 미네랄
        { 0x0057F120, -11409 }, // 가스
        { 0x0058A364,      0 }, // Deaths 표 첫 자리
        { 0x0059CCA8,  19025 }, // 유닛 목록
    };

    for (const auto & known : cases)
    {
        SPLASH_CHECK_EQ(io::eud::epdFor(known.address), known.epd);

        std::uint32_t player = 0;
        std::uint32_t unit = 0;
        SPLASH_CHECK(io::eud::slotFor(known.address, &player, &unit));
        SPLASH_CHECK_EQ(io::eud::addressFor(player, unit), known.address);
        SPLASH_CHECK(unit < io::eud::kUnitTypes);
    }

    // 네 바이트에 맞지 않는 주소는 Deaths 칸이 아니다.
    SPLASH_CHECK(!io::eud::slotFor(0x0058A365, nullptr, nullptr));

    // 목록에 적어 둔 자리는 모두 네 바이트 경계여야 한다.
    for (const auto & entry : io::eud::knownAddresses())
        SPLASH_CHECK(io::eud::slotFor(entry.address, nullptr, nullptr));
}

void testTilesetPlayerColor()
{
    constexpr std::uint16_t kDesert = 5;
    constexpr std::uint16_t kArctic = 6;
    constexpr std::uint8_t kBrown = 5;
    constexpr std::uint8_t kWhite = 6;
    constexpr std::uint8_t kGreen = 8;

    // 얼음에서는 흰색이, 사막에서는 갈색이 초록으로 보인다.
    SPLASH_CHECK_EQ(io::tilesetPlayerColor(kArctic, kWhite), kGreen);
    SPLASH_CHECK_EQ(io::tilesetPlayerColor(kDesert, kBrown), kGreen);

    // 그 두 경우 말고는 그대로다.
    SPLASH_CHECK_EQ(io::tilesetPlayerColor(kArctic, kBrown), kBrown);
    SPLASH_CHECK_EQ(io::tilesetPlayerColor(kDesert, kWhite), kWhite);
    for (std::uint16_t tileset = 0; tileset < 8; ++tileset)
    {
        if (tileset == kArctic || tileset == kDesert)
            continue;
        SPLASH_CHECK_EQ(io::tilesetPlayerColor(tileset, kWhite), kWhite);
        SPLASH_CHECK_EQ(io::tilesetPlayerColor(tileset, kBrown), kBrown);
    }
}

void testDoodadOrigin()
{
    // 칸 수가 짝수면 중심이 타일 경계에, 홀수면 타일 한가운데에 온다.
    SPLASH_CHECK_EQ(io::doodadOriginTile(io::doodadCenterPixel(20, 2), 2), 20);
    SPLASH_CHECK_EQ(io::doodadOriginTile(io::doodadCenterPixel(7, 3), 3), 7);
    SPLASH_CHECK_EQ(io::doodadOriginTile(io::doodadCenterPixel(110, 6), 6), 110);

    // 실제 맵에서 본 값.
    SPLASH_CHECK_EQ(io::doodadOriginTile(3616, 6), 110);
    SPLASH_CHECK_EQ(io::doodadOriginTile(672, 2), 20);
}

} // namespace

int main()
{
    testOverlongSections();
    testPackStrings();
    testEudSlots();
    testTilesetPlayerColor();
    testDoodadOrigin();
    return splash::test::registry().report("단위 테스트");
}
