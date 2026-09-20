// 게임 자료 없이 도는 단위 테스트.
//
// round-trip 테스트는 실제 맵이 있어야 돌지만, 여기 있는 것들은 순수 계산이라
// 어디서나 돈다. 실제 맵에서 겪고 고친 일들을 다시 겪지 않기 위한 그물이다.

#include "io/chk_bytes.h"
#include "io/eud.h"
#include "io/euddraft.h"
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
    // 잘 알려진 자리들. EPD 는 공개된 EUD 자료(EUD Book 의 pid 칸)와
    // 맞춰 본 값이다.
    struct Known { std::uint32_t address; int epd; };
    const Known cases[] {
        { 0x0057F0F0, -11421 }, // 미네랄
        { 0x0057F120, -11409 }, // 가스
        { 0x0058A364,      0 }, // Deaths 표 첫 자리
        { 0x0059CCA8,  19025 }, // 유닛 표 첫 자리
        { 0x006509A0, 203151 }, // 트리거 실행 타이머
        { 0x0058DC60,   3647 }, // 로케이션 표
    };

    for (const auto & known : cases)
    {
        SPLASH_CHECK_EQ(io::eud::signedEpdFor(known.address), known.epd);
        SPLASH_CHECK_EQ(io::eud::addressForEpd(io::eud::epdFor(known.address)),
                        known.address);

        std::uint32_t player = 0;
        std::uint32_t unit = 0;
        SPLASH_CHECK(io::eud::slotFor(known.address, &player, &unit));
        SPLASH_CHECK_EQ(io::eud::addressFor(player, unit), known.address);
    }

    // Deaths 표는 **유닛 바깥, 플레이어 안쪽**이다 — P1 마린, P2 마린, …
    // P12 마린, P1 고스트 꼴. 한 유닛이 48바이트(12 x 4)를 차지한다.
    // 이 순서를 뒤집으면 주소가 통째로 어긋난다.
    SPLASH_CHECK_EQ(io::eud::addressFor(0, 0), io::eud::kDeathsBase);
    SPLASH_CHECK_EQ(io::eud::addressFor(1, 0), io::eud::kDeathsBase + 4);
    SPLASH_CHECK_EQ(io::eud::addressFor(0, 1), io::eud::kDeathsBase + 48);
    SPLASH_CHECK_EQ(io::eud::epdForSlot(3, 1), 15u);

    // 표 안의 자리는 본래의 (플레이어, 유닛) 으로 돌아와야 한다.
    for (std::uint32_t unit = 0; unit < io::eud::kUnitTypes; unit += 37)
    {
        for (std::uint32_t player = 0; player < io::eud::kPlayers; ++player)
        {
            std::uint32_t gotPlayer = 0;
            std::uint32_t gotUnit = 0;
            SPLASH_CHECK(io::eud::slotFor(io::eud::addressFor(player, unit),
                                          &gotPlayer, &gotUnit));
            SPLASH_CHECK_EQ(gotPlayer, player);
            SPLASH_CHECK_EQ(gotUnit, unit);
        }
    }

    // 표 밖은 유닛 0 에 EPD 를 통째로 담는다 — euddraft·SCMDraft 의 표기다.
    {
        std::uint32_t player = 0;
        std::uint32_t unit = 0;
        SPLASH_CHECK(io::eud::slotFor(0x0057F0F0, &player, &unit));
        SPLASH_CHECK_EQ(unit, 0u);
        SPLASH_CHECK_EQ(player, io::eud::epdFor(0x0057F0F0));
    }

    // 네 바이트에 맞지 않는 주소는 Deaths 칸이 아니다.
    SPLASH_CHECK(!io::eud::slotFor(0x0058A365, nullptr, nullptr));
    SPLASH_CHECK(!io::eud::isAligned(0x0058A366));

    // 붙박이 표에는 네 바이트 경계가 아닌 자리도 있다 — 유닛 색(0x581D76)
    // 같은 바이트·워드 값이다. Deaths 는 네 바이트 단위로만 읽으므로 그런
    // 자리는 담긴 칸과 마스크로 다룬다. 어느 쪽이든 길이 있어야 한다.
    for (const auto & entry : io::eud::builtinOffsets())
    {
        if (io::eud::isAligned(entry.address))
        {
            SPLASH_CHECK(io::eud::slotFor(entry.address, nullptr, nullptr));
            continue;
        }

        const std::uint32_t dword = io::eud::containingDword(entry.address);
        SPLASH_CHECK(io::eud::isAligned(dword));
        SPLASH_CHECK(dword <= entry.address && entry.address - dword < 4);

        // 마스크는 비어 있으면 안 된다 — 걸러 낼 것이 없다는 뜻이 된다.
        SPLASH_CHECK(io::eud::maskFor(entry.address, entry.size) != 0);
    }

    // 마스크 셈. 칸 안 두 번째 바이트의 한 바이트 값이면 0x0000FF00 이다.
    SPLASH_CHECK_EQ(io::eud::maskFor(io::eud::kDeathsBase, 4), 0xFFFFFFFFu);
    SPLASH_CHECK_EQ(io::eud::maskFor(io::eud::kDeathsBase + 1, 1), 0x0000FF00u);
    SPLASH_CHECK_EQ(io::eud::maskFor(io::eud::kDeathsBase + 2, 2), 0xFFFF0000u);

    // size 가 네 바이트를 넘으면 값의 폭이 아니라 항목 사이의 간격이다.
    // 폭을 모르니 한 바이트만 — 넓게 잡아 옆 값을 덮으면 안 된다.
    SPLASH_CHECK_EQ(io::eud::maskFor(io::eud::kDeathsBase + 2, 8), 0x00FF0000u);

    // 칸 끝을 넘겨 달라고 해도 칸 안에서 끊는다.
    SPLASH_CHECK_EQ(io::eud::maskFor(io::eud::kDeathsBase + 3, 4), 0xFF000000u);
    SPLASH_CHECK_EQ(io::eud::containingDword(io::eud::kDeathsBase + 3),
                    io::eud::kDeathsBase);
}

void testEudOffsetLookup()
{
    // 이름으로 찾기. 똑 맞는 이름이 있으면 그것만.
    const auto minerals = io::eud::findOffsets("플레이어 미네랄");
    SPLASH_CHECK_EQ(minerals.size(), std::size_t(1));
    if (!minerals.empty())
        SPLASH_CHECK_EQ(minerals.front()->address, 0x0057F0F0u);

    // 주소로 찾으면 그 자리를 덮는 항목이 나온다. 미네랄은 12칸짜리라
    // 두 번째 플레이어 자리도 같은 항목에 든다.
    const auto * second = io::eud::offsetAt(0x0057F0F4);
    SPLASH_CHECK(second != nullptr);
    if (second != nullptr)
        SPLASH_CHECK_EQ(second->address, 0x0057F0F0u);

    // 어디에도 없는 자리.
    SPLASH_CHECK(io::eud::offsetAt(0x00100000) == nullptr);

    // 붙박이 표에는 우리가 적은 것과 eudplib(MIT)에서 뽑아 온 것이 함께
    // 들어 있다. 생성물이 빠지면 DAT 표를 이름으로 못 찾는다.
    const auto & builtin = io::eud::builtinOffsets();
    SPLASH_CHECK(builtin.size() > 150);

    // 같은 주소를 두 번 담지 않는다. 담으면 offsetAt 이 어느 쪽을 고를지
    // 그때그때 달라진다.
    for (std::size_t i = 1; i < builtin.size(); ++i)
        SPLASH_CHECK(builtin[i - 1].address != builtin[i].address);

    // 주소가 겹치면 우리말 이름이 이겨야 한다 — eudplib 에도 0x0057F0F0 이
    // `플레이어 · mineral` 로 들어 있지만, 설명과 리마스터 지원 여부가 붙은
    // 우리 것이 더 쓸모 있다.
    const auto * mineralEntry = io::eud::offsetAt(0x0057F0F0);
    SPLASH_CHECK(mineralEntry != nullptr);
    if (mineralEntry != nullptr)
    {
        SPLASH_CHECK_EQ(mineralEntry->name, std::string("플레이어 미네랄"));
        SPLASH_CHECK(mineralEntry->scr == io::eud::ScrSupport::SimpleData);
    }

    // eudplib 에서만 오는 자리도 찾아야 한다 (units.dat 의 최대 체력).
    const auto * maxHp = io::eud::offsetAt(0x00662350);
    SPLASH_CHECK(maxHp != nullptr);
    if (maxHp != nullptr)
    {
        SPLASH_CHECK_EQ(maxHp->address, 0x00662350u);
        SPLASH_CHECK_EQ(maxHp->size, 4u);
        SPLASH_CHECK(maxHp->name.find("units.dat") != std::string::npos);
    }

    // 주소 읽기: 0x 가 붙든 안 붙든, 열여섯 자리 글자가 섞이면 16진수.
    SPLASH_CHECK_EQ(io::eud::parseAddress("0x58A364").value_or(0), 0x0058A364u);
    SPLASH_CHECK_EQ(io::eud::parseAddress("58A364").value_or(0), 0x0058A364u);
    SPLASH_CHECK_EQ(io::eud::parseAddress("12").value_or(0), 12u);
    SPLASH_CHECK(!io::eud::parseAddress("미네랄").has_value());
}

void testEuddraftSettings()
{
    io::euddraft::BuildRequest request;
    request.inputMap = "/maps/in.scx";
    request.outputMap = "/maps/out.scx";
    request.scripts = { "/scripts/hello.eps" };
    request.plugins = { io::euddraft::Plugin{"eudTurbo", {}} };

    const std::string text = io::euddraft::settingsText(request);

    SPLASH_CHECK(text.find("[main]") != std::string::npos);
    SPLASH_CHECK(text.find("input: /maps/in.scx") != std::string::npos);
    SPLASH_CHECK(text.find("output: /maps/out.scx") != std::string::npos);
    SPLASH_CHECK(text.find("[/scripts/hello.eps]") != std::string::npos);
    SPLASH_CHECK(text.find("[eudTurbo]") != std::string::npos);

    // euddraft 는 freeze 를 기본으로 켠다. 우리 기본은 끔이므로 [freeze]
    // 구역이 있어야 하고, 켜 달라고 하면 없어야 한다.
    SPLASH_CHECK(text.find("[freeze]") != std::string::npos);

    request.freeze = true;
    SPLASH_CHECK(io::euddraft::settingsText(request).find("[freeze]") ==
                 std::string::npos);

    // 플러그인 한 줄 읽기 — 화면과 CLI 가 같은 표기를 쓴다.
    {
        const auto plain = io::euddraft::parsePlugin("  eudTurbo  ");
        SPLASH_CHECK_EQ(plain.name, std::string("eudTurbo"));
        SPLASH_CHECK(plain.settings.empty());

        const auto tuned = io::euddraft::parsePlugin("SCBank: bank=mybank, size = 100");
        SPLASH_CHECK_EQ(tuned.name, std::string("SCBank"));
        SPLASH_CHECK_EQ(tuned.settings.size(), std::size_t(2));
        if (tuned.settings.size() == 2)
        {
            SPLASH_CHECK_EQ(tuned.settings[0].first, std::string("bank"));
            SPLASH_CHECK_EQ(tuned.settings[0].second, std::string("mybank"));
            SPLASH_CHECK_EQ(tuned.settings[1].first, std::string("size"));
            SPLASH_CHECK_EQ(tuned.settings[1].second, std::string("100"));
        }

        // 값 없는 키. euddraft 의 readconfig 는 키만 있는 줄도 받으므로
        // 콜론을 붙이면 안 된다 — 붙이면 빈 글자가 값이 된다.
        const auto flagOnly = io::euddraft::parsePlugin("freeze: freeze");
        SPLASH_CHECK_EQ(flagOnly.settings.size(), std::size_t(1));

        io::euddraft::BuildRequest withSettings;
        withSettings.inputMap = "/a.scx";
        withSettings.outputMap = "/b.scx";
        withSettings.plugins = { io::euddraft::parsePlugin("SCBank: bank=mybank"),
                                 io::euddraft::parsePlugin("MSQC: quiet") };
        const std::string rendered = io::euddraft::settingsText(withSettings);
        SPLASH_CHECK(rendered.find("[SCBank]\nbank: mybank") != std::string::npos);
        SPLASH_CHECK(rendered.find("[MSQC]\nquiet\n") != std::string::npos);
    }

    // 로그에서 눈에 띄어야 할 줄만 추린다.
    const auto lines = io::euddraft::importantLines(
        " - Allocating objects..\nRuntimeError: bad thing\n - done\n");
    SPLASH_CHECK_EQ(lines.size(), std::size_t(1));
    if (!lines.empty())
        SPLASH_CHECK(lines.front().find("bad thing") != std::string::npos);
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
    testEudOffsetLookup();
    testEuddraftSettings();
    testTilesetPlayerColor();
    testDoodadOrigin();
    return splash::test::registry().report("단위 테스트");
}
