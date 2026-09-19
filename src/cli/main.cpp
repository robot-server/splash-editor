// splash-cli — Splash Editor 코어의 커맨드라인 프런트엔드.
//
// GUI 없이 코어를 두드리기 위한 도구다. round-trip 검증(M1 의 합격 조건)은
// 여기서 실행하며, tests/ 의 자동 테스트도 같은 코드를 쓴다.

#include "chk/map_document.h"
#include "io/game_assets.h"
#include "io/map_archive.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

int usage(const char * argv0)
{
    std::cout <<
        "사용법:\n"
        "  " << argv0 << " info <맵파일>\n"
        "      맵을 열고 메타데이터를 출력한다.\n\n"
        "  " << argv0 << " roundtrip <맵파일> [출력경로]\n"
        "      맵을 열고 다시 저장한 뒤, 저장본의 CHK 바이트가 원본과\n"
        "      같은지 검사한다. 출력경로를 생략하면 임시 파일을 쓰고 지운다.\n\n"
        "  " << argv0 << " chk <맵파일> <출력.chk>\n"
        "      맵 안의 시나리오 청크(CHK)를 그대로 꺼낸다.\n\n"
        "  " << argv0 << " new <출력파일> [가로] [세로] [타일셋ID] [--melee]\n"
        "      빈 맵을 만든다. 확장자로 포맷을 고른다(.scm=하이브리드, .scx=브루드워).\n"
        "      기본값: 64 64 4(Jungle)\n\n"
        "  " << argv0 << " assets <StarCraft 설치폴더>\n"
        "      설치본을 조사한다. 아카이브를 열고 타일셋 데이터가 읽히는지 확인한다.\n";
    return 2;
}

void printInfo(const splash::chk::MapDocument & doc)
{
    const auto & info = doc.info();
    std::cout
        << "  파일      : " << doc.filePath() << "\n"
        << "  이름      : " << (info.name.empty() ? "(없음)" : info.name) << "\n"
        << "  크기      : " << info.width << " x " << info.height << " 타일\n"
        << "  타일셋    : " << info.tilesetName << " (" << info.tilesetId << ")\n"
        << "  버전      : " << info.versionName << " (" << info.versionId << ")\n"
        << "  유닛      : " << info.unitCount << "\n"
        << "  로케이션  : " << info.locationCount << "\n"
        << "  트리거    : " << info.triggerCount << "\n"
        << "  문자열    : " << info.stringCount << "\n"
        << "  보호      : " << (info.isProtected ? "예" : "아니오") << "\n";
    if (!info.description.empty())
        std::cout << "  설명      : " << info.description << "\n";
}

int cmdInfo(const std::string & mapPath)
{
    splash::chk::MapDocument doc;
    if (!doc.open(mapPath))
    {
        std::cerr << "열기 실패: " << doc.lastError() << "\n";
        return 1;
    }
    printInfo(doc);
    return 0;
}

int cmdChk(const std::string & mapPath, const std::string & outPath)
{
    auto chk = splash::io::readScenarioChk(mapPath);
    if (!chk)
    {
        std::cerr << "CHK 를 추출하지 못했습니다: " << mapPath << "\n";
        return 1;
    }

    std::ofstream out(outPath, std::ios::binary);
    if (!out)
    {
        std::cerr << "출력 파일을 열지 못했습니다: " << outPath << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char *>(chk->data()),
              static_cast<std::streamsize>(chk->size()));
    std::cout << chk->size() << " 바이트를 " << outPath << " 에 썼습니다.\n";
    return 0;
}

/// 두 CHK 바이트열의 첫 차이 지점을 찾는다. 같으면 npos.
std::size_t firstDifference(const std::vector<std::uint8_t> & a,
                            const std::vector<std::uint8_t> & b)
{
    const std::size_t shared = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < shared; ++i)
    {
        if (a[i] != b[i])
            return i;
    }
    return (a.size() == b.size()) ? std::string::npos : shared;
}

int cmdNew(const std::vector<std::string> & args)
{
    const std::string & outPath = args[1];

    std::uint16_t width = 64;
    std::uint16_t height = 64;
    std::uint16_t tilesetId = 4; // Jungle
    bool melee = false;

    std::vector<std::string> positional;
    for (std::size_t i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--melee")
            melee = true;
        else
            positional.push_back(args[i]);
    }

    try
    {
        if (positional.size() > 0) width     = static_cast<std::uint16_t>(std::stoi(positional[0]));
        if (positional.size() > 1) height    = static_cast<std::uint16_t>(std::stoi(positional[1]));
        if (positional.size() > 2) tilesetId = static_cast<std::uint16_t>(std::stoi(positional[2]));
    }
    catch (const std::exception &)
    {
        std::cerr << "숫자 인자를 해석하지 못했습니다.\n";
        return 2;
    }

    // 확장자로 포맷을 정한다. .scx 는 브루드워, 그 외는 하이브리드.
    std::string ext = std::filesystem::path(outPath).extension().string();
    for (char & c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto format = (ext == ".scx") ? splash::io::MapFormat::ExpansionScx
                                        : splash::io::MapFormat::HybridScm;

    splash::io::MapArchive archive;
    if (auto r = archive.createNew(format, tilesetId, width, height, melee); !r)
    {
        std::cerr << "생성 실패: " << r.message << "\n";
        return 1;
    }
    if (auto r = archive.saveAs(outPath); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        return 1;
    }

    std::cout << "만들었습니다: " << outPath << " ("
              << width << "x" << height
              << ", 타일셋 " << tilesetId
              << (melee ? ", melee 트리거" : "") << ")\n";
    return 0;
}

int cmdAssets(const std::string & installPath)
{
    const auto info = splash::io::probeInstallation(installPath);

    std::cout << "  경로    : " << info.path << "\n";
    std::cout << "  종류    : ";
    switch (info.kind)
    {
        case splash::io::InstallationKind::Casc: std::cout << "CASC (리마스터)\n"; break;
        case splash::io::InstallationKind::Mpq:  std::cout << "MPQ (구버전)\n";   break;
        case splash::io::InstallationKind::None: std::cout << "알 수 없음\n";      break;
    }
    std::cout << "  설명    : " << info.detail << "\n";
    std::cout << "  타일셋  : " << info.tilesetsFound.size() << " / "
              << splash::io::tilesetAssetNames().size() << "\n";

    for (const auto & name : info.tilesetsFound)
        std::cout << "      - " << name << "\n";

    return info.ok() ? 0 : 1;
}

int cmdRoundtrip(const std::string & mapPath, const std::string & requestedOut)
{
    std::cout << "== round-trip: " << mapPath << " ==\n";

    splash::chk::MapDocument doc;
    if (!doc.open(mapPath))
    {
        std::cerr << "열기 실패: " << doc.lastError() << "\n";
        return 1;
    }
    printInfo(doc);

    const auto originalChk = splash::io::readScenarioChk(mapPath);
    if (!originalChk)
    {
        std::cerr << "원본에서 CHK 를 추출하지 못했습니다.\n";
        return 1;
    }

    // 출력 경로. 생략되면 임시 경로를 쓰고 끝나면 지운다.
    const bool useTemp = requestedOut.empty();
    std::filesystem::path outPath;
    if (useTemp)
    {
        outPath = std::filesystem::temp_directory_path() /
                  ("splash-roundtrip-" + std::filesystem::path(mapPath).filename().string());
    }
    else
    {
        outPath = requestedOut;
    }

    if (!doc.saveAs(outPath.string()))
    {
        std::cerr << "저장 실패: " << doc.lastError() << "\n";
        return 1;
    }
    std::cout << "  -> 저장: " << outPath.string() << "\n";

    int exitCode = 0;

    // 1) 저장본을 다시 열 수 있어야 한다.
    splash::chk::MapDocument reopened;
    if (!reopened.open(outPath.string()))
    {
        std::cerr << "재열기 실패: " << reopened.lastError() << "\n";
        exitCode = 1;
    }
    else
    {
        const auto & a = doc.info();
        const auto & b = reopened.info();
        const bool metaSame =
            a.width == b.width && a.height == b.height &&
            a.tilesetId == b.tilesetId && a.versionId == b.versionId &&
            a.unitCount == b.unitCount && a.locationCount == b.locationCount &&
            a.triggerCount == b.triggerCount && a.name == b.name;

        std::cout << "  메타데이터 일치 : " << (metaSame ? "예" : "아니오") << "\n";
        if (!metaSame)
            exitCode = 1;
    }

    // 2) CHK 바이트가 보존되어야 한다.
    const auto savedChk = splash::io::readScenarioChk(outPath.string());
    if (!savedChk)
    {
        std::cerr << "저장본에서 CHK 를 추출하지 못했습니다.\n";
        exitCode = 1;
    }
    else
    {
        const std::size_t diff = firstDifference(*originalChk, *savedChk);
        std::cout << "  CHK 크기        : " << originalChk->size()
                  << " -> " << savedChk->size() << "\n";
        if (diff == std::string::npos)
        {
            std::cout << "  CHK 바이트 일치 : 예\n";
        }
        else
        {
            std::cout << "  CHK 바이트 일치 : 아니오 (첫 차이 오프셋 0x"
                      << std::hex << diff << std::dec << ")\n";
            exitCode = 1;
        }
    }

    if (useTemp)
    {
        std::error_code ec;
        std::filesystem::remove(outPath, ec);
    }

    std::cout << (exitCode == 0 ? "== 통과 ==\n" : "== 실패 ==\n");
    return exitCode;
}

} // namespace

int main(int argc, char ** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
        return usage(argv[0]);

    const std::string & command = args[0];

    if (command == "info" && args.size() == 2)
        return cmdInfo(args[1]);

    if (command == "roundtrip" && (args.size() == 2 || args.size() == 3))
        return cmdRoundtrip(args[1], args.size() == 3 ? args[2] : std::string{});

    if (command == "chk" && args.size() == 3)
        return cmdChk(args[1], args[2]);

    if (command == "new" && args.size() >= 2)
        return cmdNew(args);

    if (command == "assets" && args.size() == 2)
        return cmdAssets(args[1]);

    return usage(argv[0]);
}
