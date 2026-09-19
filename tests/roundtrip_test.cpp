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

/// round-trip 한 번의 결과.
enum class Outcome
{
    BytesPreserved, ///< CHK 바이트가 그대로다. 우리가 원하는 결과.
    RefusedToSave,  ///< 저장을 거부했다. 손상시키느니 거부하는 편이 옳다.
    Mismatch,       ///< 저장은 됐는데 바이트가 달라졌다. 이것이 진짜 실패다.
    OpenFailed      ///< 열지 못했다.
};

const char * describe(Outcome o)
{
    switch (o)
    {
        case Outcome::BytesPreserved: return "바이트 보존";
        case Outcome::RefusedToSave:  return "저장 거부 (손상 방지)";
        case Outcome::Mismatch:       return "바이트 불일치";
        case Outcome::OpenFailed:     return "열기 실패";
    }
    return "?";
}

/// 한 맵에 대해 열기 -> 저장 -> CHK 바이트 비교까지 수행한다.
Outcome roundTrip(const fs::path & mapPath, bool verbose)
{
    splash::chk::MapDocument doc;
    if (!doc.open(mapPath.string()))
    {
        if (verbose)
            std::cerr << "    열기 실패: " << doc.lastError() << "\n";
        return Outcome::OpenFailed;
    }

    const auto original = splash::io::readScenarioChk(mapPath.string());
    if (!original)
    {
        if (verbose)
            std::cerr << "    원본 CHK 추출 실패\n";
        return Outcome::OpenFailed;
    }

    const fs::path outPath =
        workDir() / ("roundtrip-" + mapPath.filename().string());

    struct Cleanup
    {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove(path, ec); }
    } cleanup{outPath};

    if (!doc.saveAs(outPath.string()))
    {
        // MappingCore 가 유효하지 않은 CHK 쓰기를 거부한 경우가 여기 해당한다
        // (예: 보호된 맵이 STR 상한을 넘겨 선언하거나 VER 섹션이 없는 경우).
        // 데이터를 조용히 망가뜨리는 것보다 나은 동작이므로 실패로 치지 않는다.
        if (verbose)
            std::cerr << "    저장 거부: " << doc.lastError() << "\n";
        return Outcome::RefusedToSave;
    }

    const auto saved = splash::io::readScenarioChk(outPath.string());
    if (!saved)
        return Outcome::Mismatch;

    if (original->size() != saved->size())
    {
        if (verbose)
            std::cerr << "    CHK 크기: " << original->size()
                      << " -> " << saved->size() << "\n";
        return Outcome::Mismatch;
    }

    for (std::size_t i = 0; i < original->size(); ++i)
    {
        if ((*original)[i] != (*saved)[i])
        {
            if (verbose)
                std::cerr << "    CHK 바이트가 오프셋 0x" << std::hex << i
                          << std::dec << " 에서 달라졌습니다\n";
            return Outcome::Mismatch;
        }
    }

    // 저장본을 다시 열었을 때 메타데이터가 같아야 한다.
    splash::chk::MapDocument reopened;
    if (!reopened.open(outPath.string()))
        return Outcome::Mismatch;

    const auto & a = doc.info();
    const auto & b = reopened.info();
    const bool same =
        a.width == b.width && a.height == b.height &&
        a.tilesetId == b.tilesetId && a.versionId == b.versionId &&
        a.unitCount == b.unitCount && a.locationCount == b.locationCount &&
        a.triggerCount == b.triggerCount && a.name == b.name;

    return same ? Outcome::BytesPreserved : Outcome::Mismatch;
}

/// 합성 맵 전용: 바이트 보존만이 통과다.
bool roundTripPreservesChk(const fs::path & mapPath, const std::string & label)
{
    std::cout << "  · " << label << " (" << mapPath.filename().string() << ")\n";
    const Outcome outcome = roundTrip(mapPath, /*verbose*/ true);
    if (outcome != Outcome::BytesPreserved)
        std::cerr << "    결과: " << describe(outcome) << "\n";
    return outcome == Outcome::BytesPreserved;
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

    int preserved = 0, refused = 0;
    for (const fs::path & map : maps)
    {
        const Outcome outcome = roundTrip(map, /*verbose*/ true);
        std::cout << "  · " << map.filename().string()
                  << "  ->  " << describe(outcome) << "\n";

        switch (outcome)
        {
            case Outcome::BytesPreserved: ++preserved; break;
            case Outcome::RefusedToSave:  ++refused;   break;
            default: break;
        }

        // 바이트 불일치와 열기 실패만 실패로 친다.
        // 저장 거부는 손상을 막은 것이므로 통과로 본다.
        SPLASH_CHECK(outcome == Outcome::BytesPreserved ||
                     outcome == Outcome::RefusedToSave);
    }

    std::cout << "\n  맵 " << maps.size() << "개: 바이트 보존 " << preserved
              << ", 저장 거부 " << refused << "\n";
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
