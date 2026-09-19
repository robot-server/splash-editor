// M1 의 합격 조건을 지키는 테스트.
//
// 핵심 불변식: 맵을 열고 다시 저장했을 때 시나리오 청크(CHK)의 바이트가
// 그대로여야 한다. 우리는 아직 어떤 섹션도 편집하지 않으므로, 한 바이트라도
// 달라졌다면 그것은 MappingCore 사용법이 틀렸거나 우리가 모르는 섹션을
// 건드렸다는 뜻이다.
//
// 맵 파일 전체를 비교하지 않는 이유는 map_archive.h 의 readScenarioChk 주석 참고.

#include "chk/map_document.h"
#include "io/map_archive.h"
#include "test_support.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// 테스트 산출물을 담을 디렉터리. 빌드 트리 안에 둔다.
fs::path workDir()
{
    static const fs::path dir = [] {
        fs::path d = fs::temp_directory_path() / "splash-editor-tests";
        std::error_code ec;
        fs::create_directories(d, ec);
        return d;
    }();
    return dir;
}

/// 한 맵에 대해 열기 -> 저장 -> CHK 바이트 비교까지 수행한다.
/// 성공하면 true.
bool roundTripPreservesChk(const fs::path & mapPath, const std::string & label)
{
    std::cout << "  · " << label << " (" << mapPath.filename().string() << ")\n";

    splash::chk::MapDocument doc;
    if (!doc.open(mapPath.string()))
    {
        std::cerr << "    열기 실패: " << doc.lastError() << "\n";
        return false;
    }

    const auto original = splash::io::readScenarioChk(mapPath.string());
    if (!original)
    {
        std::cerr << "    원본 CHK 추출 실패\n";
        return false;
    }

    const fs::path outPath =
        workDir() / ("roundtrip-" + mapPath.filename().string());

    if (!doc.saveAs(outPath.string()))
    {
        std::cerr << "    저장 실패: " << doc.lastError() << "\n";
        return false;
    }

    const auto saved = splash::io::readScenarioChk(outPath.string());
    if (!saved)
    {
        std::cerr << "    저장본 CHK 추출 실패\n";
        return false;
    }

    if (original->size() != saved->size())
    {
        std::cerr << "    CHK 크기가 달라졌습니다: " << original->size()
                  << " -> " << saved->size() << "\n";
        return false;
    }

    for (std::size_t i = 0; i < original->size(); ++i)
    {
        if ((*original)[i] != (*saved)[i])
        {
            std::cerr << "    CHK 바이트가 오프셋 " << i << " 에서 달라졌습니다\n";
            return false;
        }
    }

    // 저장본을 다시 열었을 때 메타데이터가 같아야 한다.
    splash::chk::MapDocument reopened;
    if (!reopened.open(outPath.string()))
    {
        std::cerr << "    재열기 실패: " << reopened.lastError() << "\n";
        return false;
    }

    const auto & a = doc.info();
    const auto & b = reopened.info();
    const bool same =
        a.width == b.width && a.height == b.height &&
        a.tilesetId == b.tilesetId && a.versionId == b.versionId &&
        a.unitCount == b.unitCount && a.locationCount == b.locationCount &&
        a.triggerCount == b.triggerCount && a.name == b.name;

    if (!same)
    {
        std::cerr << "    재열기 후 메타데이터가 달라졌습니다\n";
        return false;
    }

    std::error_code ec;
    fs::remove(outPath, ec);
    return true;
}

/// 합성 맵을 만들어 경로를 돌려준다. 실패하면 빈 경로.
fs::path makeSyntheticMap(const std::string & fileName,
                          splash::io::MapFormat format,
                          std::uint16_t tilesetId,
                          std::uint16_t width,
                          std::uint16_t height,
                          bool melee)
{
    const fs::path path = workDir() / fileName;

    splash::io::MapArchive archive;
    if (!archive.createNew(format, tilesetId, width, height, melee))
        return {};
    if (!archive.saveAs(path.string()))
        return {};

    return path;
}

// -------------------------------------------------------------------------
// 테스트들
// -------------------------------------------------------------------------

void testDisplayNames()
{
    std::cout << "\n[표시 이름]\n";

    SPLASH_CHECK_EQ(splash::chk::tilesetDisplayName(0), std::string("Badlands"));
    SPLASH_CHECK_EQ(splash::chk::tilesetDisplayName(4), std::string("Jungle"));
    SPLASH_CHECK_EQ(splash::chk::tilesetDisplayName(7), std::string("Twilight"));

    // 8 이상은 8 로 나눈 나머지를 쓰되, 원시값을 표기에 남긴다.
    SPLASH_CHECK(splash::chk::tilesetDisplayName(12).find("Jungle") == 0);
    SPLASH_CHECK(splash::chk::tilesetDisplayName(12).find("12") != std::string::npos);

    SPLASH_CHECK_EQ(splash::chk::versionDisplayName(63), std::string("Hybrid (1.04+)"));
    SPLASH_CHECK_EQ(splash::chk::versionDisplayName(205), std::string("Brood War"));
    SPLASH_CHECK(!splash::chk::versionDisplayName(9999).empty());
}

void testFailureHandling()
{
    std::cout << "\n[실패 처리]\n";

    splash::chk::MapDocument doc;

    SPLASH_CHECK(!doc.open("/이런/경로는/없다.scm"));
    SPLASH_CHECK(!doc.isOpen());
    SPLASH_CHECK(!doc.lastError().empty());

    // 열린 맵이 없을 때 저장은 조용히 성공해서는 안 된다.
    SPLASH_CHECK(!doc.save());

    SPLASH_CHECK(!doc.open(""));

    // CHK 추출도 없는 파일에 대해 nullopt 여야 한다.
    SPLASH_CHECK(!splash::io::readScenarioChk("/이런/경로는/없다.scm").has_value());
}

void testSyntheticRoundTrips()
{
    std::cout << "\n[합성 맵 round-trip]\n";

    struct Case
    {
        const char * fileName;
        splash::io::MapFormat format;
        std::uint16_t tileset;
        std::uint16_t width;
        std::uint16_t height;
        bool melee;
        const char * label;
    };

    // 목표의 테스트 맵 구성을 합성으로 흉내 낸다:
    //  - 작은 melee 맵
    //  - 트리거/문자열이 들어간 맵 (melee 기본 트리거 세트)
    //  - 크기·타일셋·포맷이 다른 변형들
    const std::vector<Case> cases = {
        {"synthetic-small-melee.scm", splash::io::MapFormat::HybridScm,    4, 64,  64,  true,  "작은 melee (하이브리드)"},
        {"synthetic-plain.scm",       splash::io::MapFormat::HybridScm,    0, 64,  64,  false, "트리거 없는 기본 맵"},
        {"synthetic-bw.scx",          splash::io::MapFormat::ExpansionScx, 1, 128, 128, true,  "브루드워 128x128"},
        {"synthetic-wide.scx",        splash::io::MapFormat::ExpansionScx, 7, 192, 96,  false, "비정방형 192x96"},
        {"synthetic-tiny.scm",        splash::io::MapFormat::HybridScm,    2, 32,  32,  false, "최소 크기 32x32"},
    };

    for (const Case & c : cases)
    {
        const fs::path path =
            makeSyntheticMap(c.fileName, c.format, c.tileset, c.width, c.height, c.melee);

        if (path.empty())
        {
            std::cerr << "  [실패] 합성 맵 생성 실패: " << c.label << "\n";
            splash::test::registry().fail(std::string("생성: ") + c.label, __FILE__, __LINE__);
            continue;
        }

        SPLASH_CHECK(roundTripPreservesChk(path, c.label));

        // 생성한 맵의 메타데이터가 요청한 값과 맞는지도 본다.
        splash::chk::MapDocument doc;
        if (doc.open(path.string()))
        {
            SPLASH_CHECK_EQ(doc.info().width, c.width);
            SPLASH_CHECK_EQ(doc.info().height, c.height);
            SPLASH_CHECK_EQ(doc.info().tilesetId, c.tileset);
        }
        else
        {
            splash::test::registry().fail(std::string("재열기: ") + c.label, __FILE__, __LINE__);
        }

        std::error_code ec;
        fs::remove(path, ec);
    }
}

/// tests/maps/ 에 실제 맵이 있으면 전부 round-trip 한다.
///
/// 이 디렉터리는 비어 있을 수 있다(저작권 자료라 저장소에 커밋하지 않는다).
/// 비어 있으면 건너뛰되, 건너뛰었다는 사실을 분명히 출력한다 —
/// 합성 맵만으로 통과한 것을 "실제 맵 검증 완료"로 착각하면 안 되기 때문이다.
void testRealMaps(const fs::path & mapsDir)
{
    std::cout << "\n[실제 맵 round-trip]\n";

    std::error_code ec;
    if (!fs::exists(mapsDir, ec) || !fs::is_directory(mapsDir, ec))
    {
        std::cout << "  건너뜀: " << mapsDir.string() << " 가 없습니다.\n";
        return;
    }

    std::vector<fs::path> maps;
    for (const auto & entry : fs::directory_iterator(mapsDir, ec))
    {
        if (!entry.is_regular_file())
            continue;
        std::string ext = entry.path().extension().string();
        for (char & ch : ext)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ext == ".scm" || ext == ".scx" || ext == ".chk")
            maps.push_back(entry.path());
    }

    if (maps.empty())
    {
        std::cout << "  건너뜀: " << mapsDir.string() << " 에 맵이 없습니다.\n"
                  << "  ** 실제 게임 맵 검증은 아직 수행되지 않았습니다. **\n"
                  << "  맵을 넣는 방법은 tests/maps/README.md 참고.\n";
        return;
    }

    std::sort(maps.begin(), maps.end());
    for (const fs::path & map : maps)
        SPLASH_CHECK(roundTripPreservesChk(map, "실제 맵"));
}

} // namespace

int main(int argc, char ** argv)
{
    // 실제 맵 디렉터리는 CMake 가 인자로 넘겨 준다.
    const fs::path mapsDir = (argc > 1) ? fs::path(argv[1]) : fs::path("tests/maps");

    std::cout << "Splash Editor round-trip 테스트\n";
    std::cout << "작업 디렉터리: " << workDir().string() << "\n";

    testDisplayNames();
    testFailureHandling();
    testSyntheticRoundTrips();
    testRealMaps(mapsDir);

    return splash::test::registry().report("전체");
}
