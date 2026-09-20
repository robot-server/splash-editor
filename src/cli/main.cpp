// splash-cli — Splash Editor 코어의 커맨드라인 프런트엔드.
//
// GUI 없이 코어를 두드리기 위한 도구다. round-trip 검증(M1 의 합격 조건)은
// 여기서 실행하며, tests/ 의 자동 테스트도 같은 코드를 쓴다.

#include "cli_common.h"

#include "chk/map_document.h"
#include "io/game_assets.h"
#include "io/game_graphics.h"
#include "io/map_archive.h"

#include <filesystem>
#include <set>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

int usage(const char * argv0)
{
    std::cout <<
        "사용법: " << argv0 << " <갈래> <명령> [인자...]\n"
        "        " << argv0 << " <명령> [인자...]        (맵·설치본 진단)\n\n";

    splash::cli::printGroupList(argv0);

    std::cout <<
        "\n진단·검증 명령:\n"
        "  " << argv0 << " info <맵파일>\n"
        "      맵을 열고 메타데이터를 출력한다.\n\n"
        "  " << argv0 << " roundtrip <맵파일> [출력경로]\n"
        "      맵을 열고 다시 저장한 뒤, 저장본의 CHK 바이트가 원본과\n"
        "      같은지 검사한다. 출력경로를 생략하면 임시 파일을 쓰고 지운다.\n\n"
        "  " << argv0 << " chk <맵파일> <출력.chk>\n"
        "      맵 안의 시나리오 청크(CHK)를 그대로 꺼낸다.\n\n"
        "  " << argv0 << " new <출력파일> [가로] [세로] [타일셋ID] [--melee]\n"
        "          [--install 설치폴더] [--terrain 지형]\n"
        "      빈 맵을 만든다. 확장자로 포맷을 고른다(.scm=하이브리드, .scx=브루드워).\n"
        "      기본값: 64 64 4(Jungle)\n"
        "      --install 을 주면 고른 지형으로 바닥을 채운다. 주지 않으면 타일이\n"
        "      0 으로 남아 terrain isom 이 아무것도 놓지 못한다.\n"
        "      --terrain 은 terrain types 가 내는 번호나 이름이다(기본: 첫 지형).\n\n"
        "  " << argv0 << " assets <StarCraft 설치폴더>\n"
        "      설치본을 조사한다. 아카이브를 열고 타일셋 데이터가 읽히는지 확인한다.\n\n"
        "  " << argv0 << " render <맵파일> <설치폴더> <출력.ppm> [--units] [--locations] [--creep]\n"
        "      맵 지형을 이미지로 그린다 (scenario image 와 같다).\n\n"
        "  " << argv0 << " unit-image <설치폴더> <유닛번호> <출력.ppm> [소유자] [타일셋]\n"
        "      유닛 하나를 격자 배경 위에 그린다. 스프라이트 검증용이다.\n\n"
        "  " << argv0 << " icon <설치폴더> <아이콘번호> <출력.ppm>\n"
        "  " << argv0 << " tile-sheet <설치폴더> <타일셋> <첫타일> <개수> <출력.ppm>\n"
        "  " << argv0 << " mega-sheet <설치폴더> <타일셋> <첫메가타일> <개수> <출력.ppm>\n"
        "      타일·아이콘 그림을 한 장으로 뽑는다. 디코딩 검증용이다.\n\n"
        "  " << argv0 << " tileset-info <설치폴더> <타일셋>\n"
        "  " << argv0 << " unit-classes <설치폴더>\n"
        "  " << argv0 << " images-tbl <설치폴더> [찾을글자]\n"
        "  " << argv0 << " has-asset <설치폴더> <아카이브경로>\n"
        "  " << argv0 << " icon-histogram <설치폴더> <아이콘번호>\n"
        "  " << argv0 << " tileset-groups <설치폴더> <타일셋>\n"
        "  " << argv0 << " tileset-ramps <설치폴더> <타일셋>\n"
        "  " << argv0 << " find-creep <설치폴더> <타일셋>\n"
        "  " << argv0 << " creep-kin <설치폴더> <타일셋> <메가타일> <개수>\n"
        "      설치본 자료를 들여다본다.\n\n"
        "예전 이름도 그대로 받는다 (move-unit, place-isom, set-triggers,\n"
        "set-trigger-arg, set-briefing, add-sound, unprotect, units, sounds,\n"
        "switches, doodads, triggers, trigger-list, trigger-args, briefing,\n"
        "briefing-args). 새 이름은 위의 갈래 쪽입니다.\n";
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
        << "  보호      : " << (info.isProtected ? "예" : "아니오") << "\n"
        << "  글자      : " << splash::io::encodingName(doc.textEncoding()) << "\n";
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

/// 지형을 브러시 번호나 이름으로 고른다. 이름은 빈칸·대소문자를 가리지
/// 않고, 딱 하나만 걸리면 부분 일치도 받는다 (parseUnitType 과 같은 잣대).
std::optional<std::size_t> findTerrainType(
    const std::vector<splash::io::GameGraphics::TerrainType> & types,
    const std::string & text)
{
    if (!text.empty() && text.find_first_not_of("0123456789") == std::string::npos)
    {
        const auto want = static_cast<std::size_t>(std::stoull(text));
        for (const auto & type : types)
        {
            if (type.brushIndex == want)
                return type.brushIndex;
        }
        return std::nullopt;
    }

    const std::string want = splash::cli::squashName(text);
    for (const auto & type : types)
    {
        if (splash::cli::squashName(type.name) == want)
            return type.brushIndex;
    }

    std::optional<std::size_t> partial;
    std::size_t hits = 0;
    for (const auto & type : types)
    {
        if (splash::cli::squashName(type.name).find(want) != std::string::npos)
        {
            partial = type.brushIndex;
            ++hits;
        }
    }
    return (hits == 1) ? partial : std::nullopt;
}

int cmdNew(const std::vector<std::string> & args)
{
    const std::string & outPath = args[1];

    std::uint16_t width = 64;
    std::uint16_t height = 64;
    std::uint16_t tilesetId = 4; // Jungle
    bool melee = false;
    std::string installPath;
    std::string terrainArg;

    std::vector<std::string> positional;
    for (std::size_t i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--melee")
            melee = true;
        else if (args[i] == "--install" && i + 1 < args.size())
            installPath = args[++i];
        else if (args[i] == "--terrain" && i + 1 < args.size())
            terrainArg = args[++i];
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

    // 설치본을 주면 고른 지형으로 바닥을 제대로 채운다. 주지 않으면 타일이
    // 0 으로 남아 ISOM 브러시가 아무것도 놓지 못한다 — 빈 칸 위에는 절벽·
    // 경계를 이을 수 없기 때문이다.
    splash::io::GameGraphics graphics;
    const splash::io::GameGraphics * graphicsPtr = nullptr;
    std::size_t terrainBrush = 0;
    if (!installPath.empty())
    {
        std::string error;
        if (!graphics.load(installPath, &error))
        {
            std::cerr << "게임 데이터 로드 실패: " << error << "\n";
            return 1;
        }
        graphicsPtr = &graphics;

        const auto types = graphics.terrainTypes(tilesetId);
        if (!terrainArg.empty())
        {
            const auto found = findTerrainType(types, terrainArg);
            if (!found)
            {
                std::cerr << "지형을 찾지 못했습니다: " << terrainArg << "\n";
                std::cerr << "고를 수 있는 것:\n";
                for (const auto & type : types)
                    std::cerr << "  " << type.brushIndex << "  " << type.name << "\n";
                return 2;
            }
            terrainBrush = *found;
        }
    }
    else if (!terrainArg.empty())
    {
        std::cerr << "--terrain 은 --install 과 함께 써야 합니다.\n";
        return 2;
    }

    splash::io::MapArchive archive;
    if (auto r = archive.createNew(format, tilesetId, width, height,
            melee ? splash::io::MapArchive::DefaultTriggers::Melee
                  : splash::io::MapArchive::DefaultTriggers::None,
            graphicsPtr, terrainBrush); !r)
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

int cmdMoveUnit(const std::string & mapPath, std::size_t unitIndex,
                std::uint16_t x, std::uint16_t y,
                const std::string & outPath, bool thenUndo)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    const auto before = archive.units();
    if (unitIndex >= before.size())
    {
        std::cerr << "유닛 번호가 범위를 벗어났습니다 (유닛 " << before.size() << "개)\n";
        return 1;
    }

    std::cout << "  이전 위치 : (" << before[unitIndex].x << ", " << before[unitIndex].y << ")  "
              << splash::io::unitTypeName(before[unitIndex].type) << "\n";

    if (auto r = archive.moveUnit(unitIndex, x, y); !r)
    {
        std::cerr << "이동 실패: " << r.message << "\n";
        return 1;
    }

    {
        const auto after = archive.units();
        std::cout << "  옮긴 위치 : (" << after[unitIndex].x << ", " << after[unitIndex].y << ")\n";
    }

    if (thenUndo)
    {
        if (auto r = archive.undo(); !r)
        {
            std::cerr << "되돌리기 실패: " << r.message << "\n";
            return 1;
        }
        const auto after = archive.units();
        const bool restored = after.size() == before.size() &&
                              after[unitIndex].x == before[unitIndex].x &&
                              after[unitIndex].y == before[unitIndex].y;
        std::cout << "  되돌린 위치: (" << after[unitIndex].x << ", " << after[unitIndex].y << ")"
                  << (restored ? "  [원본 복원]" : "  [복원 실패]") << "\n";
        if (!restored)
            return 1;
    }

    if (auto r = archive.saveAs(outPath); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        return 1;
    }

    // 저장본을 다시 열어 반영됐는지 확인한다.
    splash::io::MapArchive reopened;
    if (auto r = reopened.open(outPath); !r)
    {
        std::cerr << "재열기 실패: " << r.message << "\n";
        return 1;
    }
    const auto saved = reopened.units();
    if (unitIndex < saved.size())
    {
        std::cout << "  저장본 위치: (" << saved[unitIndex].x << ", " << saved[unitIndex].y << ")\n";
    }
    std::cout << "  -> " << outPath << "\n";
    return 0;
}

int cmdTriggers(const std::string & mapPath, const std::string & installPath,
                const std::string & outPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    const auto text = archive.triggerText(graphics);
    if (!text)
    {
        std::cerr << "트리거를 텍스트로 옮기지 못했습니다.\n";
        return 1;
    }

    std::cout << "  트리거 " << archive.info().triggerCount << "개, "
              << text->size() << " 글자\n";

    if (outPath.empty())
    {
        // 앞부분만 보여 준다 — 트리거가 많으면 화면을 덮는다.
        constexpr std::size_t kPreview = 1200;
        std::cout << "\n" << text->substr(0, std::min(kPreview, text->size()));
        if (text->size() > kPreview)
            std::cout << "\n... (" << (text->size() - kPreview) << " 글자 더)\n";
        std::cout << "\n";
    }
    else
    {
        std::ofstream out(outPath, std::ios::trunc);
        if (!out) { std::cerr << "출력 파일 열기 실패\n"; return 1; }
        out << *text;
        std::cout << "  -> " << outPath << "\n";
    }
    return 0;
}

int cmdSetTriggers(const std::string & mapPath, const std::string & installPath,
                   const std::string & textPath, const std::string & outPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    std::ifstream in(textPath);
    if (!in)
    {
        std::cerr << "텍스트 파일을 열지 못했습니다: " << textPath << "\n";
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

    const std::size_t before = archive.info().triggerCount;
    std::cout << "  이전 트리거 : " << before << "개\n";

    if (auto r = archive.setTriggerText(text, graphics); !r)
    {
        std::cerr << "컴파일 실패: " << r.message << "\n";
        return 1;
    }

    std::cout << "  이후 트리거 : " << archive.info().triggerCount << "개\n";

    if (auto r = archive.saveAs(outPath); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::MapArchive reopened;
    if (auto r = reopened.open(outPath); !r)
    {
        std::cerr << "재열기 실패: " << r.message << "\n";
        return 1;
    }
    std::cout << "  저장본      : " << reopened.info().triggerCount << "개\n";
    std::cout << "  -> " << outPath << "\n";
    return 0;
}

int cmdPlaceIsom(const std::string & mapPath, const std::string & installPath,
                 std::size_t pixelX, std::size_t pixelY,
                 std::size_t terrainType, std::size_t brushExtent,
                 const std::string & outPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    const auto before = archive.terrainTiles();

    if (auto r = archive.placeIsomTerrain(graphics, pixelX, pixelY, terrainType, brushExtent); !r)
    {
        std::cerr << "배치 실패: " << r.message << "\n";
        return 1;
    }

    const auto after = archive.terrainTiles();
    std::size_t changed = 0;
    for (std::size_t i = 0; i < std::min(before.size(), after.size()); ++i)
    {
        if (before[i] != after[i])
            ++changed;
    }
    std::cout << "  바뀐 타일 : " << changed << "개\n";

    if (auto r = archive.saveAs(outPath); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        return 1;
    }
    std::cout << "  -> " << outPath << "\n";
    return 0;
}

int cmdBriefing(const std::string & mapPath, const std::string & installPath,
                const std::string & outPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    const auto summaries = archive.briefingSummaries(graphics);
    std::cout << "  브리핑 " << summaries.size() << "개\n";
    for (const auto & s : summaries)
    {
        std::cout << "    #" << s.index << "  " << s.players << "  동작 " << s.actions;
        if (!s.firstAction.empty())
            std::cout << "  — " << s.firstAction;
        std::cout << "\n";
    }

    const auto text = archive.briefingText(graphics);
    if (!text)
    {
        std::cerr << "브리핑 텍스트를 만들지 못했습니다.\n";
        return 1;
    }

    if (outPath.empty())
    {
        std::cout << "\n" << *text << "\n";
    }
    else
    {
        std::ofstream out(outPath, std::ios::binary);
        out << *text;
        std::cout << "  -> " << outPath << "\n";
    }
    return 0;
}

int cmdSetBriefing(const std::string & mapPath, const std::string & installPath,
                   const std::string & textPath, const std::string & outPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    std::ifstream in(textPath, std::ios::binary);
    if (!in)
    {
        std::cerr << "텍스트 파일을 열지 못했습니다: " << textPath << "\n";
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

    if (auto r = archive.setBriefingText(text, graphics); !r)
    {
        std::cerr << "컴파일 실패: " << r.message << "\n";
        return 1;
    }

    if (auto r = archive.saveAs(outPath); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        return 1;
    }

    std::cout << "  브리핑 " << archive.briefingSummaries(graphics).size() << "개 적용\n"
              << "  -> " << outPath << "\n";
    return 0;
}

int cmdBriefingArgs(const std::string & mapPath, const std::string & installPath,
                    std::size_t index)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    const auto elements = archive.briefingActions(index, graphics);
    std::cout << "  브리핑 " << index << " 의 동작:\n";
    for (std::size_t slot = 0; slot < elements.size(); ++slot)
    {
        const auto & element = elements[slot];
        if (element.type == 0)
            continue;

        std::cout << "    [" << slot << "] " << element.text << "\n";
        for (std::size_t i = 0; i < element.args.size(); ++i)
        {
            const auto & arg = element.args[i];
            std::cout << "         인자 " << i << " " << arg.label
                      << " = " << arg.text << " (값 " << arg.value;
            if (!arg.choices.empty())
                std::cout << ", 선택지 " << arg.choices.size() << "개";
            std::cout << ")\n";
        }
    }
    return 0;
}

int cmdTriggerArgs(const std::string & mapPath, const std::string & installPath,
                   std::size_t triggerIndex)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    const auto show = [](const char * title,
                         const std::vector<splash::io::TriggerElement> & elements) {
        std::cout << "  " << title << ":\n";
        for (std::size_t slot = 0; slot < elements.size(); ++slot)
        {
            const auto & element = elements[slot];
            if (element.type == 0)
                continue;

            std::cout << "    [" << slot << "] " << element.text << "\n";
            for (std::size_t i = 0; i < element.args.size(); ++i)
            {
                const auto & arg = element.args[i];
                const char * kind = "?";
                switch (arg.kind)
                {
                    case splash::io::TriggerArgKind::Choice: kind = "고르기"; break;
                    case splash::io::TriggerArgKind::Number: kind = "숫자"; break;
                    case splash::io::TriggerArgKind::Text:   kind = "글자"; break;
                    case splash::io::TriggerArgKind::Sound:  kind = "소리"; break;
                    case splash::io::TriggerArgKind::None:   kind = "없음"; break;
                }
                std::cout << "         인자 " << i << " " << arg.label
                          << " [" << kind << "] = " << arg.text
                          << " (값 " << arg.value;
                if (!arg.choices.empty())
                    std::cout << ", 선택지 " << arg.choices.size() << "개";
                std::cout << ")\n";
            }
        }
    };

    show("조건", archive.triggerConditions(triggerIndex, graphics));
    show("동작", archive.triggerActions(triggerIndex, graphics));
    return 0;
}

int cmdSetTriggerArg(const std::string & mapPath, const std::string & installPath,
                     const std::string & which, std::size_t triggerIndex, std::size_t slot,
                     std::size_t argIndex, const std::string & value, const std::string & outPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    const bool isCondition = (which == "condition" || which == "조건");

    // 숫자면 원시 값으로, 아니면 문자열 인자로 본다.
    //
    // 16진도 받는다 — EUD 비트마스크는 0x00FF0000 처럼 쓰는 편이 읽기 쉽고,
    // 갈래 명령들도 0x 를 받으므로 여기만 십진이면 걸린다.
    splash::io::Result result = splash::io::Result::failure("알 수 없는 인자");
    std::uint32_t raw = 0;
    bool numeric = false;
    if (!value.empty())
    {
        try
        {
            std::size_t used = 0;
            const unsigned long long parsed = std::stoull(value, &used, 0);
            numeric = used == value.size() && parsed <= 0xFFFFFFFFull;
            raw = static_cast<std::uint32_t>(parsed);
        }
        catch (const std::exception &)
        {
            numeric = false;
        }
    }

    // 수가 아니면 그 자리가 고르기인지 보고 이름으로 찾아본다.
    // 숫자 경로는 그대로 두므로 이름표 없는 값도 계속 넣을 수 있다.
    const auto elements = isCondition ? archive.triggerConditions(triggerIndex, graphics)
                                      : archive.triggerActions(triggerIndex, graphics);
    const splash::io::TriggerArg * argSlot = nullptr;
    if (slot < elements.size() && argIndex < elements[slot].args.size())
        argSlot = &elements[slot].args[argIndex];

    if (!numeric && argSlot != nullptr)
    {
        const splash::cli::ChoiceMatch chosen = splash::cli::matchChoice(*argSlot, value);
        if (chosen.count > 0)
        {
            raw = chosen.value;
            numeric = true;
            std::cout << "  고름      : " << value << " -> " << raw << "\n";
            // 로케이션·스위치·문자열은 같은 이름을 여럿 가질 수 있다.
            // 첫 번째를 쓰되 갈리지 않았다는 것은 알려야 한다.
            if (chosen.count > 1)
                std::cout << "  알림      : 같은 이름이 " << chosen.count
                          << "개입니다. 첫 번째를 썼습니다 — 다른 것을 쓰려면"
                             " 번호로 주세요.\n";
        }
    }

    if (numeric)
    {
        result = isCondition ? archive.setConditionArg(triggerIndex, slot, argIndex, raw)
                             : archive.setActionArg(triggerIndex, slot, argIndex, raw);
    }
    else if (!isCondition)
    {
        // 고를 수 있는 목록이 있는 자리인데 이름으로 못 찾았다면, 오타로
        // 새 문자열이 하나 생기는 것이다. 자유 글자 자리(Text Message 등)
        // 에서는 그게 정상이므로 그때는 잠자코 넘어간다.
        if (argSlot != nullptr && !argSlot->choices.empty())
        {
            std::cout << "  알림      : 목록에 없는 이름이라 새 값으로 넣었습니다.\n"
                      << "              고를 수 있는 것: "
                      << splash::cli::choiceListText(*argSlot) << "\n";
        }
        result = archive.setActionArgText(triggerIndex, slot, argIndex, value);
    }
    else
    {
        std::cerr << "조건 인자에는 수(십진 또는 0x 16진)나 고를 수 있는 이름만"
                     " 넣을 수 있습니다.\n";
        if (argSlot != nullptr && !argSlot->choices.empty())
            std::cerr << "  고를 수 있는 것: " << splash::cli::choiceListText(*argSlot) << "\n";
        return 1;
    }

    if (!result)
    {
        std::cerr << "인자를 바꾸지 못했습니다: " << result.message << "\n";
        return 1;
    }

    if (auto r = archive.saveAs(outPath); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        return 1;
    }

    std::cout << "  -> " << outPath << "\n";
    return 0;
}

int cmdTriggerList(const std::string & mapPath, const std::string & installPath,
                   int detailIndex)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return 1;
    }

    const auto summaries = archive.triggerSummaries(graphics);
    std::cout << "  트리거 " << summaries.size() << "개\n";
    for (std::size_t i = 0; i < summaries.size() && i < 12; ++i)
    {
        const auto & s = summaries[i];
        std::cout << "    #" << s.index << "  " << s.players
                  << "  조건 " << s.conditions << " · 동작 " << s.actions;
        if (!s.firstAction.empty())
            std::cout << "  — " << s.firstAction;
        if (s.disabled)
            std::cout << "  [꺼짐]";
        std::cout << "\n";
    }

    if (detailIndex >= 0)
    {
        const auto detail = archive.triggerDetail(static_cast<std::size_t>(detailIndex), graphics);
        if (!detail)
        {
            std::cerr << "트리거를 읽지 못했습니다.\n";
            return 1;
        }
        std::cout << "\n  [트리거 " << detailIndex << "]\n  조건:\n";
        for (const auto & c : detail->conditions)
            std::cout << "    " << c << "\n";
        std::cout << "  동작:\n";
        for (const auto & a : detail->actions)
            std::cout << "    " << a << "\n";
    }
    return 0;
}

int cmdUnits(const std::string & mapPath, std::size_t limit)
{
    splash::chk::MapDocument doc;
    if (!doc.open(mapPath))
    {
        std::cerr << "열기 실패: " << doc.lastError() << "\n";
        return 1;
    }

    const auto & units = doc.units();
    const auto & locations = doc.locations();

    std::cout << "  유닛 " << units.size() << "개, 로케이션 " << locations.size() << "개\n\n";

    std::cout << "  [유닛]\n";
    for (std::size_t i = 0; i < units.size() && i < limit; ++i)
    {
        const auto & u = units[i];
        std::cout << "    P" << static_cast<int>(u.owner) + 1
                  << "  (" << u.x << ", " << u.y << ")  "
                  << u.typeName << "\n";
    }
    if (units.size() > limit)
        std::cout << "    ... " << (units.size() - limit) << "개 더\n";

    std::cout << "\n  [로케이션]\n";
    for (std::size_t i = 0; i < locations.size() && i < limit; ++i)
    {
        const auto & l = locations[i];
        std::cout << "    #" << l.index << "  ("
                  << l.left << "," << l.top << ")-(" << l.right << "," << l.bottom << ")  "
                  << (l.name.empty() ? "(이름 없음)" : l.name) << "\n";
    }
    if (locations.size() > limit)
        std::cout << "    ... " << (locations.size() - limit) << "개 더\n";

    return 0;
}

int cmdCreepKin(const std::string & installPath, std::uint16_t tilesetId,
                std::uint32_t creepFirst, int creepCount)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    // 확실한 크립 메가타일이 쓰는 미니타일(vr4) 인덱스를 모은다.
    std::set<std::uint32_t> creepMinis;
    for (int i = 0; i < creepCount; ++i)
    {
        const auto info = graphics.describeMegaTile(tilesetId, creepFirst + i);
        for (int k = 0; k < 16; ++k)
            creepMinis.insert(info.vr4[k]);
    }
    std::cout << "  크립 미니타일 " << creepMinis.size() << "종\n";

    // 그 미니타일을 쓰는 다른 메가타일을 찾는다 — 크립과 지형이 섞인
    // 가장자리 타일이 있다면 여기서 드러난다.
    const std::size_t total = graphics.megaTileCount(tilesetId);
    int found = 0;
    for (std::uint32_t mt = 0; mt < total; ++mt)
    {
        if (mt >= creepFirst && mt < creepFirst + creepCount)
            continue;

        const auto info = graphics.describeMegaTile(tilesetId, mt);
        int hits = 0;
        for (int k = 0; k < 16; ++k)
        {
            if (creepMinis.count(info.vr4[k]) != 0)
                ++hits;
        }
        if (hits > 0)
        {
            std::cout << "    megatile " << mt << ": 크립 미니타일 " << hits << "/16\n";
            if (++found >= 40) { std::cout << "    ...\n"; break; }
        }
    }
    if (found == 0)
        std::cout << "    (크립 미니타일을 쓰는 다른 메가타일 없음)\n";
    return 0;
}

int cmdMegaSheet(const std::string & installPath, std::uint16_t tilesetId,
                 std::uint32_t first, int count, const std::string & outPath)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    constexpr int kCols = 16;
    const int rows = (count + kCols - 1) / kCols;
    const int W = kCols * splash::io::kTilePixels;
    const int H = rows * splash::io::kTilePixels;
    std::vector<std::uint8_t> canvas(static_cast<std::size_t>(W) * H * 3, 20);
    std::vector<std::uint8_t> tile(splash::io::kTileRgbaBytes);

    for (int i = 0; i < count; ++i)
    {
        if (!graphics.renderMegaTile(tilesetId, first + i, tile.data()))
            continue;
        const int r = i / kCols, c = i % kCols;
        for (int y = 0; y < splash::io::kTilePixels; ++y)
        {
            for (int x = 0; x < splash::io::kTilePixels; ++x)
            {
                const std::size_t src =
                    (static_cast<std::size_t>(y) * splash::io::kTilePixels + x) * 4;
                const std::size_t dst =
                    ((static_cast<std::size_t>(r) * splash::io::kTilePixels + y) * W +
                     c * splash::io::kTilePixels + x) * 3;
                canvas[dst + 0] = tile[src + 0];
                canvas[dst + 1] = tile[src + 1];
                canvas[dst + 2] = tile[src + 2];
            }
        }
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) { std::cerr << "출력 파일 열기 실패\n"; return 1; }
    out << "P6\n" << W << " " << H << "\n255\n";
    out.write(reinterpret_cast<const char *>(canvas.data()),
              static_cast<std::streamsize>(canvas.size()));
    std::cout << "  메가타일 " << first << "~" << (first + count - 1)
              << " / 총 " << graphics.megaTileCount(tilesetId)
              << " -> " << outPath << "\n";
    return 0;
}

int cmdImagesTbl(const std::string & installPath, const std::string & needle)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto names = graphics.imageFileNames();
    std::cout << "  images.tbl 항목 " << names.size() << "개\n";

    std::size_t shown = 0;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        std::string lower = names[i];
        for (char & ch : lower)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        std::string needleLower = needle;
        for (char & ch : needleLower)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        if (needle.empty() || lower.find(needleLower) != std::string::npos)
        {
            std::cout << "    [" << i << "] " << names[i] << "\n";
            if (++shown >= 60) { std::cout << "    ...\n"; break; }
        }
    }
    if (shown == 0)
        std::cout << "    (일치하는 항목 없음)\n";
    return 0;
}

/// 램프 타일을 그룹별로 모아 보여 준다.
///
/// ISOM 브러시에는 램프가 없다 — 지형 종류 표(`terrain types`)에 램프
/// 항목이 없고, 고지대를 칠하면 절벽만 생긴다. 램프는 VF4 의 램프 비트가
/// 선 타일로만 놓을 수 있어서, 그 타일 번호를 알아야 한다.
int cmdTilesetRamps(const std::string & installPath, std::uint16_t tilesetId)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto info = graphics.describeTileset(tilesetId);

    // 같은 그룹의 타일은 같은 램프의 조각들이다. 그룹째 묶어 내야 어느
    // 타일을 나란히 놓아야 비탈이 되는지 보인다.
    std::size_t groupCount = 0;
    std::size_t tileCount = 0;
    for (std::size_t group = 0; group < info.tileGroupCount; ++group)
    {
        std::vector<std::uint16_t> rampTiles;
        int elevation = 0;
        for (std::uint16_t sub = 0; sub < 16; ++sub)
        {
            const auto tileId = static_cast<std::uint16_t>(group * 16 + sub);
            const auto terrain = graphics.tileTerrain(tilesetId, tileId);
            if (terrain.ramp)
            {
                rampTiles.push_back(tileId);
                elevation = terrain.elevation;
            }
        }
        if (rampTiles.empty())
            continue;

        ++groupCount;
        tileCount += rampTiles.size();
        std::cout << "  그룹 " << std::setw(4) << group
                  << "  높이 " << elevation
                  << "  타일 " << rampTiles.size() << "개 :";
        for (const auto tileId : rampTiles)
            std::cout << " " << tileId;
        std::cout << "\n";
    }

    std::cout << "  타일셋 " << tilesetId << " 의 램프 그룹 " << groupCount
              << "개, 타일 " << tileCount << "개\n";
    if (groupCount == 0)
        std::cout << "  (이 타일셋에는 램프가 없습니다 — 높이 차이가 없는 타일셋입니다)\n";
    return 0;
}

/// 타일 그룹마다 높이·걷기·짓기·램프 여부를 한 줄씩 낸다.
///
/// 지형을 수로 재려면 "이 타일이 고지대인가, 걸을 수 있는가" 를 알아야
/// 하는데 타일 값만으로는 알 수 없다. 맵의 지형을 훑을 때 이 표를 옆에
/// 놓고 타일 값 / 16 으로 찾아본다.
int cmdTilesetGroups(const std::string & installPath, std::uint16_t tilesetId)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto info = graphics.describeTileset(tilesetId);
    std::cout << "# 그룹 높이 걷기 모두걷기 짓기 램프 걷기비트16진\n";
    for (std::size_t group = 0; group < info.tileGroupCount; ++group)
    {
        // 그룹의 대표로 첫 칸을 본다. 같은 그룹은 성질이 같다.
        const auto tileId = static_cast<std::uint16_t>(group * 16);
        const auto t = graphics.tileTerrain(tilesetId, tileId);
        std::cout << group << " " << t.elevation << " " << (t.walkable ? 1 : 0)
                  << " " << (t.fullyWalkable ? 1 : 0) << " " << (t.buildable ? 1 : 0)
                  << " " << (t.ramp ? 1 : 0) << " " << std::hex << t.walkMask
                  << std::dec << "\n";
    }
    std::cout << "# 타일셋 " << tilesetId << " 그룹 " << info.tileGroupCount << "개\n";
    return 0;
}

int cmdFindCreep(const std::string & installPath, std::uint16_t tilesetId)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto creepIds = graphics.creepTileIds(tilesetId);
    if (creepIds.empty())
    {
        std::cerr << "기준 크립 타일을 찾지 못했습니다.\n";
        return 1;
    }

    // 기준 크립 타일의 평균 색을 구한다.
    const auto averageOf = [&](std::uint16_t tileId, double & r, double & g, double & b) {
        std::vector<std::uint8_t> buf(splash::io::kTileRgbaBytes);
        if (!graphics.renderTile(tilesetId, tileId, buf.data()))
            return false;
        double sr = 0, sg = 0, sb = 0;
        const int n = splash::io::kTilePixels * splash::io::kTilePixels;
        for (int i = 0; i < n; ++i)
        {
            sr += buf[i * 4 + 0];
            sg += buf[i * 4 + 1];
            sb += buf[i * 4 + 2];
        }
        r = sr / n; g = sg / n; b = sb / n;
        return true;
    };

    double br = 0, bg = 0, bb = 0;
    if (!averageOf(creepIds[0], br, bg, bb))
        return 1;
    std::cout << "  기준 크립 타일 평균색: (" << int(br) << ", " << int(bg) << ", " << int(bb) << ")\n";

    const auto info = graphics.describeTileset(tilesetId);
    std::cout << "  타일 그룹 " << info.tileGroupCount << "개를 훑습니다...\n";

    // 색이 비슷한 타일이 섞인 그룹을 찾는다. 가장자리 타일은 크립과 지형이
    // 반반 섞이므로 "일부 칸만 비슷한" 그룹이 후보다.
    int reported = 0;
    for (std::size_t group = 0; group < info.tileGroupCount && reported < 40; ++group)
    {
        int similar = 0;
        for (std::size_t sub = 0; sub < 16; ++sub)
        {
            double r = 0, g = 0, b = 0;
            if (!averageOf(static_cast<std::uint16_t>(group * 16 + sub), r, g, b))
                continue;
            // 밝기만 보면 어두운 바위·절벽이 함께 걸린다. 크립은 보라 계열이라
            // 파랑이 초록보다 높다(B>G). 그 관계를 함께 본다.
            const double d = std::abs(r - br) + std::abs(g - bg) + std::abs(b - bb);
            const bool purplish = (b - g) > 3.0 && r > g;
            if (d < 30.0 && purplish)
                ++similar;
        }
        if (similar > 0)
        {
            std::cout << "    그룹 " << group << ": 유사 타일 " << similar << "/16"
                      << "  flags=0x" << std::hex << 0 << std::dec << "\n";
            ++reported;
        }
    }
    return 0;
}

int cmdTileSheet(const std::string & installPath, std::uint16_t tilesetId,
                 std::uint16_t firstGroup, int groupCount, const std::string & outPath)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    // 그룹마다 16개 타일이 있다. 한 줄에 16개씩 놓아 그룹을 행으로 본다.
    constexpr int kCols = 16;
    const int rows = groupCount;
    const int W = kCols * splash::io::kTilePixels;
    const int H = rows * splash::io::kTilePixels;

    std::vector<std::uint8_t> canvas(static_cast<std::size_t>(W) * H * 3, 20);
    std::vector<std::uint8_t> tile(splash::io::kTileRgbaBytes);

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < kCols; ++c)
        {
            const std::uint16_t tileId =
                static_cast<std::uint16_t>((firstGroup + r) * 16 + c);
            if (!graphics.renderTile(tilesetId, tileId, tile.data()))
                continue;

            for (int y = 0; y < splash::io::kTilePixels; ++y)
            {
                for (int x = 0; x < splash::io::kTilePixels; ++x)
                {
                    const std::size_t src =
                        (static_cast<std::size_t>(y) * splash::io::kTilePixels + x) * 4;
                    const std::size_t dst =
                        ((static_cast<std::size_t>(r) * splash::io::kTilePixels + y) * W +
                         c * splash::io::kTilePixels + x) * 3;
                    canvas[dst + 0] = tile[src + 0];
                    canvas[dst + 1] = tile[src + 1];
                    canvas[dst + 2] = tile[src + 2];
                }
            }
        }
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) { std::cerr << "출력 파일 열기 실패\n"; return 1; }
    out << "P6\n" << W << " " << H << "\n255\n";
    out.write(reinterpret_cast<const char *>(canvas.data()),
              static_cast<std::streamsize>(canvas.size()));
    std::cout << "  그룹 " << firstGroup << "~" << (firstGroup + rows - 1)
              << " -> " << outPath << " (" << W << "x" << H << ")\n";
    return 0;
}

int cmdTilesetInfo(const std::string & installPath, std::uint16_t tilesetId)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto info = graphics.describeTileset(tilesetId);
    {
        const auto types = graphics.terrainTypes(tilesetId);
        std::cout << "  지형 종류     : " << types.size() << "\n";
        for (std::size_t i = 0; i < types.size() && i < 20; ++i)
            std::cout << "      brush=" << types[i].brushIndex
                      << " preview=" << (types[i].hasPreview
                                         ? std::to_string(types[i].previewTileId) : std::string("없음"))
                      << "  " << types[i].name << "\n";
    }
    std::cout << "  팔레트 타일   : " << graphics.paletteTileIds(tilesetId).size() << "\n";
    std::cout << "  타일셋 " << tilesetId << "\n"
              << "  타일 그룹     : " << info.tileGroupCount << "\n"
              << "  메가타일      : " << info.megaTileCount << "\n"
              << "  Creep 그룹    : " << info.creepGroups.size() << "\n"
              << "  TempCreep 그룹: " << info.tempCreepGroups.size() << "\n"
              << "  Receding 그룹 : " << info.recedingGroups.size() << "\n";

    const auto show = [](const char * label, const std::vector<std::uint16_t> & v) {
        if (v.empty()) return;
        std::cout << "    " << label << ": ";
        for (std::size_t i = 0; i < v.size() && i < 30; ++i)
            std::cout << v[i] << " ";
        if (v.size() > 30) std::cout << "... (" << v.size() << "개)";
        std::cout << "\n";
    };
    show("Creep", info.creepGroups);
    show("TempCreep", info.tempCreepGroups);
    show("Receding", info.recedingGroups);
    return 0;
}

int cmdUnitClasses(const std::string & installPath)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const char * raceName[] = {"Zerg", "Terran", "Protoss", "Neutral"};
    for (std::uint16_t t : {std::uint16_t(0), std::uint16_t(7), std::uint16_t(37)})
    {
        const auto wav = graphics.unitSound(t);
        std::cout << "  소리 " << t << " (" << splash::io::unitTypeName(t) << "): "
                  << wav.size() << " 바이트  " << graphics.unitSoundName(t) << "\n";
    }
    // 알려진 유닛으로 분류가 맞는지 확인한다.
    for (std::uint16_t type : {std::uint16_t(0), std::uint16_t(37), std::uint16_t(65),
                               std::uint16_t(106), std::uint16_t(131), std::uint16_t(154),
                               std::uint16_t(176), std::uint16_t(188)})
    {
        const auto info = graphics.unitClass(type);
        std::cout << "  " << type << "  "
                  << raceName[static_cast<int>(info.race)]
                  << (info.building ? " 건물" : " 유닛")
                  << "  flags=0x" << std::hex << int(info.groupFlags) << std::dec
                  << "  " << splash::io::unitTypeName(type) << "\n";
    }
    return 0;
}

int cmdSounds(const std::string & mapPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    const auto sounds = archive.sounds();
    std::cout << "  소리 " << sounds.size() << "개\n";
    for (const auto & sound : sounds)
    {
        std::cout << "    ";
        if (sound.registered)
            std::cout << "#" << sound.index;
        else
            std::cout << "(미등록)";
        std::cout << "  " << sound.path;
        std::cout << (sound.inArchive ? "  [맵 안 " + std::to_string(sound.bytes) + "바이트]"
                                      : "  [맵에 없음]");
        if (sound.usedByTrigger)
            std::cout << "  [트리거가 씀]";
        std::cout << "\n";
    }
    return 0;
}

int cmdAddSound(const std::string & mapPath, const std::string & wavPath,
                const std::string & outPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    if (auto r = archive.addSound(wavPath); !r)
    {
        std::cerr << "소리를 넣지 못했습니다: " << r.message << "\n";
        return 1;
    }

    if (auto r = archive.saveAs(outPath); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        return 1;
    }

    std::cout << "  소리 " << archive.sounds().size() << "개\n  -> " << outPath << "\n";
    return 0;
}

int cmdUnprotect(const std::string & mapPath, const std::string & outPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    std::cout << "  보호됨: " << (archive.isProtected() ? "예" : "아니오")
              << "   비밀번호: " << (archive.hasPassword() ? "예" : "아니오") << "\n";

    const auto result = archive.unprotect();
    std::cout << "  고친 것:\n" << result.message << "\n";

    if (auto r = archive.saveAs(outPath); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        return 1;
    }

    std::cout << "  -> " << outPath << "\n";
    return 0;
}

int cmdSwitches(const std::string & mapPath)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    const auto names = archive.switchNames();
    std::size_t named = 0;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (names[i].empty())
            continue;
        std::cout << "    #" << i << "  " << names[i] << "\n";
        ++named;
    }
    std::cout << "  이름 붙은 스위치 " << named << "개\n";
    return 0;
}

int cmdDoodads(const std::string & installPath, std::uint16_t tilesetId)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto list = graphics.doodads(tilesetId);
    std::cout << "  두들 " << list.size() << "개\n";
    std::size_t shown = 0;
    for (const auto & doodad : list)
    {
        std::cout << "    #" << doodad.id << "  " << doodad.name
                  << "  " << doodad.tileWidth << "x" << doodad.tileHeight
                  << "  시작그룹 " << doodad.startTileGroup << "\n";
        if (++shown >= 15)
            break;
    }
    return 0;
}

int cmdIconHistogram(const std::string & installPath, std::uint16_t iconIndex)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto histogram = graphics.iconPaletteHistogram(iconIndex);
    std::cout << "  아이콘 " << iconIndex << " 이 쓰는 팔레트 인덱스 ("
              << histogram.size() << "가지)\n";
    std::size_t shown = 0;
    for (const auto & [index, count] : histogram)
    {
        std::cout << "    " << int(index) << " : " << count << "\n";
        if (++shown >= 20)
            break;
    }
    return 0;
}

int cmdHasAsset(const std::string & installPath, const std::string & assetPath)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto size = graphics.assetSize(assetPath);
    if (size == 0)
    {
        std::cout << "  없음: " << assetPath << "\n";
        return 1;
    }
    std::cout << "  있음: " << assetPath << "  " << size << " 바이트\n";
    return 0;
}

int cmdIcon(const std::string & installPath, std::uint16_t iconIndex, const std::string & outPath)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }

    const auto image = graphics.renderIcon(iconIndex, 4);
    if (image.width <= 0 || image.height <= 0)
    {
        std::cerr << "아이콘을 그리지 못했습니다.\n";
        return 1;
    }

    std::ofstream out(outPath, std::ios::binary);
    out << "P6\n" << image.width << " " << image.height << "\n255\n";
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4)
    {
        out.put(static_cast<char>(image.rgba[i + 0]));
        out.put(static_cast<char>(image.rgba[i + 1]));
        out.put(static_cast<char>(image.rgba[i + 2]));
    }

    std::cout << "  아이콘 " << iconIndex << "  " << image.width << "x" << image.height
              << " -> " << outPath << "\n";
    return 0;
}

int cmdUnitImage(const std::string & installPath,
                 std::uint16_t unitType,
                 const std::string & outPath,
                 std::uint8_t owner,
                 std::uint16_t tilesetId,
                 std::uint32_t resourceAmount,
                 std::uint16_t stateFlags = 0,
                 std::uint16_t relationFlags = 0)
{
    splash::io::GameGraphics graphics;
    std::string error;
    if (!graphics.load(installPath, &error))
    {
        std::cerr << "그래픽 로드 실패: " << error << "\n";
        return 1;
    }
    if (!graphics.hasUnitGraphics())
    {
        std::cerr << "유닛 그래픽을 읽지 못했습니다.\n";
        return 1;
    }

    // 진단용으로 상태를 환경 변수로 넘길 수 있게 둔다.
    if (const char * chosen = std::getenv("SPLASH_UNIT_STATE"))
        stateFlags = static_cast<std::uint16_t>(std::stoul(chosen, nullptr, 0));
    if (const char * chosen = std::getenv("SPLASH_UNIT_RELATION"))
        relationFlags = static_cast<std::uint16_t>(std::stoul(chosen, nullptr, 0));

    const auto image = graphics.renderUnit(unitType, owner, tilesetId, resourceAmount,
                                           stateFlags, relationFlags);
    if (image.width <= 0 || image.height <= 0)
    {
        std::cerr << "유닛 " << unitType << " 을 그리지 못했습니다.\n";
        return 1;
    }

    // 스프라이트가 잘렸는지 보이도록 여백과 격자를 깔고, 유닛 중심에 십자를 긋는다.
    constexpr int kPad = 24;
    const int W = image.width + kPad * 2;
    const int H = image.height + kPad * 2;
    std::vector<std::uint8_t> canvas(static_cast<std::size_t>(W) * H * 3, 0);

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const bool grid = (x % 16 == 0) || (y % 16 == 0);
            const std::uint8_t v = grid ? 70 : 40;
            const std::size_t at = (static_cast<std::size_t>(y) * W + x) * 3;
            canvas[at + 0] = v; canvas[at + 1] = v; canvas[at + 2] = v;
        }
    }

    // 스프라이트 경계를 빨간 테두리로 표시
    for (int x = 0; x < image.width; ++x)
    {
        for (int yy : {0, image.height - 1})
        {
            const std::size_t at = ((static_cast<std::size_t>(yy + kPad)) * W + x + kPad) * 3;
            canvas[at + 0] = 200; canvas[at + 1] = 40; canvas[at + 2] = 40;
        }
    }
    for (int y = 0; y < image.height; ++y)
    {
        for (int xx : {0, image.width - 1})
        {
            const std::size_t at = ((static_cast<std::size_t>(y + kPad)) * W + xx + kPad) * 3;
            canvas[at + 0] = 200; canvas[at + 1] = 40; canvas[at + 2] = 40;
        }
    }

    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            const std::size_t src = (static_cast<std::size_t>(y) * image.width + x) * 4;
            const std::uint8_t alpha = image.rgba[src + 3];
            if (alpha == 0)
                continue;
            const std::size_t at = ((static_cast<std::size_t>(y + kPad)) * W + x + kPad) * 3;
            const auto mix = [alpha](std::uint8_t dst, std::uint8_t src2) {
                return static_cast<std::uint8_t>((src2 * alpha + dst * (255 - alpha)) / 255);
            };
            canvas[at + 0] = mix(canvas[at + 0], image.rgba[src + 0]);
            canvas[at + 1] = mix(canvas[at + 1], image.rgba[src + 1]);
            canvas[at + 2] = mix(canvas[at + 2], image.rgba[src + 2]);
        }
    }

    // 유닛 중심(anchor) 십자
    const int ax = image.anchorX + kPad;
    const int ay = image.anchorY + kPad;
    for (int d = -6; d <= 6; ++d)
    {
        if (ax + d >= 0 && ax + d < W && ay >= 0 && ay < H)
        {
            const std::size_t at = (static_cast<std::size_t>(ay) * W + ax + d) * 3;
            canvas[at + 0] = 90; canvas[at + 1] = 255; canvas[at + 2] = 90;
        }
        if (ay + d >= 0 && ay + d < H && ax >= 0 && ax < W)
        {
            const std::size_t at = (static_cast<std::size_t>(ay + d) * W + ax) * 3;
            canvas[at + 0] = 90; canvas[at + 1] = 255; canvas[at + 2] = 90;
        }
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        std::cerr << "출력 파일을 열지 못했습니다: " << outPath << "\n";
        return 1;
    }
    out << "P6\n" << W << " " << H << "\n255\n";
    out.write(reinterpret_cast<const char *>(canvas.data()),
              static_cast<std::streamsize>(canvas.size()));

    std::cout << "  유닛 " << unitType << " (" << splash::io::unitTypeName(unitType) << ")\n"
              << "  스프라이트 크기 : " << image.width << " x " << image.height << "\n"
              << "  중심(anchor)    : (" << image.anchorX << ", " << image.anchorY << ")\n"
              << "  -> " << outPath << "\n";
    return 0;
}

int cmdRender(const std::string & mapPath,
              const std::string & installPath,
              const std::string & outPath,
              bool drawUnits,
              bool drawLocations,
              bool drawCreep)
{
    splash::io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    const auto info = archive.info();
    const int width  = info.tileWidth;
    const int height = info.tileHeight;
    if (width <= 0 || height <= 0)
    {
        std::cerr << "맵 크기를 읽지 못했습니다.\n";
        return 1;
    }

    const long long pixelsWide = static_cast<long long>(width) * splash::io::kTilePixels;
    const long long pixelsTall = static_cast<long long>(height) * splash::io::kTilePixels;

    // 전체 이미지를 메모리에 올리므로 상한을 둔다.
    // 256x256 맵이면 8192x8192 = 201MB(RGB) 다. 검증용 도구에는 과하다.
    constexpr long long kMaxPixels = 4096LL * 4096LL;
    if (pixelsWide * pixelsTall > kMaxPixels)
    {
        std::cerr << "맵이 너무 큽니다: " << pixelsWide << "x" << pixelsTall
                  << " 픽셀. 이 명령은 " << 4096 << "x" << 4096
                  << " 이하만 그립니다(검증용).\n";
        return 1;
    }

    const auto tiles = archive.terrainTiles();
    if (tiles.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    {
        std::cerr << "지형 타일 수가 맵 크기와 맞지 않습니다.\n";
        return 1;
    }

    const auto archiveUnits = archive.units();
    const auto archiveLocations = archive.locations();

    splash::io::GameGraphics tileset;
    std::string error;
    if (!tileset.load(installPath, &error))
    {
        std::cerr << "타일셋 로드 실패: " << error << "\n";
        return 1;
    }

    // RGB 로 모아 PPM(P6)으로 쓴다. 외부 이미지 라이브러리를 끌어오지 않기 위해서다.
    std::vector<std::uint8_t> image(
        static_cast<std::size_t>(pixelsWide) * static_cast<std::size_t>(pixelsTall) * 3, 0);

    std::vector<std::uint8_t> tileRgba(splash::io::kTileRgbaBytes);
    std::size_t unknownTiles = 0;

    for (int ty = 0; ty < height; ++ty)
    {
        for (int tx = 0; tx < width; ++tx)
        {
            const std::uint16_t tileId = tiles[static_cast<std::size_t>(ty) * width + tx];
            if (!tileset.renderTile(info.tilesetId, tileId, tileRgba.data()))
                ++unknownTiles;

            for (int py = 0; py < splash::io::kTilePixels; ++py)
            {
                for (int px = 0; px < splash::io::kTilePixels; ++px)
                {
                    const std::size_t src = (static_cast<std::size_t>(py) * splash::io::kTilePixels + px) * 4;
                    const std::size_t dstX = static_cast<std::size_t>(tx) * splash::io::kTilePixels + px;
                    const std::size_t dstY = static_cast<std::size_t>(ty) * splash::io::kTilePixels + py;
                    const std::size_t dst = (dstY * static_cast<std::size_t>(pixelsWide) + dstX) * 3;

                    image[dst + 0] = tileRgba[src + 0];
                    image[dst + 1] = tileRgba[src + 1];
                    image[dst + 2] = tileRgba[src + 2];
                }
            }
        }
    }

    // --- 오버레이 ---
    // 그림자는 반투명이라 배경과 섞어야 한다.
    const auto blendPixel = [&](long long x, long long y, int r, int g, int b, int a) {
        if (x < 0 || y < 0 || x >= pixelsWide || y >= pixelsTall)
            return;
        const std::size_t at = (static_cast<std::size_t>(y) * pixelsWide + x) * 3;
        const auto mix = [a](std::uint8_t dst, int src) {
            return static_cast<std::uint8_t>((src * a + dst * (255 - a)) / 255);
        };
        image[at + 0] = mix(image[at + 0], r);
        image[at + 1] = mix(image[at + 1], g);
        image[at + 2] = mix(image[at + 2], b);
    };

    const auto putPixel = [&](long long x, long long y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= pixelsWide || y >= pixelsTall)
            return;
        const std::size_t at = (static_cast<std::size_t>(y) * pixelsWide + x) * 3;
        image[at + 0] = static_cast<std::uint8_t>(r);
        image[at + 1] = static_cast<std::uint8_t>(g);
        image[at + 2] = static_cast<std::uint8_t>(b);
    };

    // 크립은 지형 위, 유닛 아래.
    if (drawCreep && tileset.hasUnitGraphics())
    {
        const auto creepTiles = tileset.creepTileIds(info.tilesetId);
        const auto mask = tileset.computeCreepMask(archiveUnits, tiles, width, height,
                                                   info.tilesetId);

        if (!creepTiles.empty() && !mask.empty())
        {
            std::vector<std::vector<std::uint8_t>> creepPixels;
            for (const auto id : creepTiles)
            {
                std::vector<std::uint8_t> buf(splash::io::kTileRgbaBytes);
                if (tileset.renderTile(info.tilesetId, id, buf.data()))
                    creepPixels.push_back(std::move(buf));
            }

            std::size_t creepBuildings = 0;
            if (!creepPixels.empty())
            {
                // 경계는 건물마다의 타원을 그대로 쓴다. 타일 격자에 맞추면
                // 네모나게 보이고, 흐리면 뿌옇게 보인다. 대신 크립이 넘어가면
                // 안 되는 지형(절벽·물·다른 고도)은 타일 마스크로 막는다.
                for (const auto & u : archiveUnits)
                {
                    const auto range = tileset.creepRange(u.type);
                    if (range.radiusX <= 0.0 || range.radiusY <= 0.0)
                        continue;
                    ++creepBuildings;

                    const long long x0 = std::max(0LL, static_cast<long long>(u.x - range.radiusX));
                    const long long x1 = std::min(pixelsWide - 1,
                                                  static_cast<long long>(u.x + range.radiusX));
                    const long long y0 = std::max(0LL, static_cast<long long>(u.y - range.radiusY));
                    const long long y1 = std::min(pixelsTall - 1,
                                                  static_cast<long long>(u.y + range.radiusY));

                    for (long long y = y0; y <= y1; ++y)
                    {
                        const int ty = static_cast<int>(y / splash::io::kTilePixels);
                        for (long long x = x0; x <= x1; ++x)
                        {
                            const int tx = static_cast<int>(x / splash::io::kTilePixels);
                            if (tx < 0 || ty < 0 || tx >= width || ty >= height)
                                continue;
                            if (mask[static_cast<std::size_t>(ty) * width + tx] == 0)
                                continue; // 크립이 못 가는 지형

                            const double dx = (x - static_cast<double>(u.x)) / range.radiusX;
                            const double dy = (y - static_cast<double>(u.y)) / range.radiusY;
                            const double d2 = dx * dx + dy * dy;
                            if (d2 > 1.0)
                                continue;

                            // 가장자리 한 겹만 부드럽게 — 타원 테두리에서 서서히 사라진다.
                            const double edge = (d2 > 0.82) ? (1.0 - d2) / 0.18 : 1.0;
                            const int alpha = static_cast<int>(255 * edge);
                            if (alpha <= 0)
                                continue;

                            const std::size_t hash =
                                (static_cast<std::size_t>(tx) * 73856093u) ^
                                (static_cast<std::size_t>(ty) * 19349663u);
                            const std::size_t plainCount =
                                std::max<std::size_t>(1, creepPixels.size() / 3);
                            const bool useDecor =
                                (hash % 11 == 0) && creepPixels.size() > plainCount;
                            const std::size_t variant = useDecor
                                ? plainCount + (hash / 11) % (creepPixels.size() - plainCount)
                                : hash % plainCount;

                            const auto & buf = creepPixels[variant];
                            const std::size_t at =
                                ((y % splash::io::kTilePixels) * splash::io::kTilePixels +
                                 (x % splash::io::kTilePixels)) * 4;
                            blendPixel(x, y, buf[at + 0], buf[at + 1], buf[at + 2], alpha);
                        }
                    }
                }
            }
            std::cout << "  크립 건물  : " << creepBuildings << "\n";
        }
    }

    if (drawLocations)
    {
        for (const auto & l : archiveLocations)
        {
            for (long long x = l.left; x <= static_cast<long long>(l.right); ++x)
            {
                putPixel(x, l.top, 255, 220, 90);
                putPixel(x, l.bottom, 255, 220, 90);
            }
            for (long long y = l.top; y <= static_cast<long long>(l.bottom); ++y)
            {
                putPixel(l.left, y, 255, 220, 90);
                putPixel(l.right, y, 255, 220, 90);
            }
        }
    }

    if (drawUnits)
    {
        const bool haveSprites = tileset.hasUnitGraphics();
        std::size_t drawn = 0;

        // 맵 스프라이트(THG2)를 먼저 — 대개 배경 장식이다.
        const auto archiveSprites = archive.sprites();
        std::size_t spritesDrawn = 0;
        if (haveSprites)
        {
            for (const auto & sp : archiveSprites)
            {
                const auto image = tileset.renderSprite(sp.type, sp.owner, info.tilesetId,
                                                        sp.drawnAsSprite);
                if (image.width <= 0 || image.height <= 0)
                    continue;

                const long long baseX = static_cast<long long>(sp.x) - image.anchorX;
                const long long baseY = static_cast<long long>(sp.y) - image.anchorY;
                for (int yy = 0; yy < image.height; ++yy)
                {
                    for (int xx = 0; xx < image.width; ++xx)
                    {
                        const std::size_t at =
                            (static_cast<std::size_t>(yy) * image.width + xx) * 4;
                        const std::uint8_t alpha = image.rgba[at + 3];
                        if (alpha == 0)
                            continue;
                        blendPixel(baseX + xx, baseY + yy,
                                   image.rgba[at + 0], image.rgba[at + 1],
                                   image.rgba[at + 2], alpha);
                    }
                }
                ++spritesDrawn;
            }
        }
        if (!archiveSprites.empty())
        {
            std::cout << "  맵 스프라이트: " << spritesDrawn << " / "
                      << archiveSprites.size() << "\n";
        }

        for (const auto & u : archiveUnits)
        {
            if (haveSprites)
            {
                const auto image =
                    tileset.renderUnit(u.type, u.owner, info.tilesetId, u.resourceAmount);
                if (image.width > 0 && image.height > 0)
                {
                    const long long baseX = static_cast<long long>(u.x) - image.anchorX;
                    const long long baseY = static_cast<long long>(u.y) - image.anchorY;
                    for (int yy = 0; yy < image.height; ++yy)
                    {
                        for (int xx = 0; xx < image.width; ++xx)
                        {
                            const std::size_t at =
                                (static_cast<std::size_t>(yy) * image.width + xx) * 4;
                            const std::uint8_t alpha = image.rgba[at + 3];
                            if (alpha == 0)
                                continue; // 투명
                            blendPixel(baseX + xx, baseY + yy,
                                       image.rgba[at + 0], image.rgba[at + 1],
                                       image.rgba[at + 2], alpha);
                        }
                    }
                    ++drawn;
                    continue;
                }
            }

            // 스프라이트를 못 구한 유닛은 소유자 색 원으로 대신한다.
            const auto color = splash::chk::playerColor(u.owner);
            constexpr int kRadius = 7;
            for (int dy = -kRadius; dy <= kRadius; ++dy)
            {
                for (int dx = -kRadius; dx <= kRadius; ++dx)
                {
                    if (dx * dx + dy * dy > kRadius * kRadius)
                        continue;
                    putPixel(static_cast<long long>(u.x) + dx,
                             static_cast<long long>(u.y) + dy,
                             color.r, color.g, color.b);
                }
            }
        }

        std::cout << "  스프라이트: " << drawn << " / " << archiveUnits.size()
                  << (haveSprites ? "" : " (유닛 그래픽 없음)") << "\n";
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        std::cerr << "출력 파일을 열지 못했습니다: " << outPath << "\n";
        return 1;
    }
    out << "P6\n" << pixelsWide << " " << pixelsTall << "\n255\n";
    out.write(reinterpret_cast<const char *>(image.data()),
              static_cast<std::streamsize>(image.size()));

    std::cout << "  맵        : " << width << " x " << height << " 타일\n"
              << "  타일셋    : " << info.tilesetId << "\n"
              << "  이미지    : " << pixelsWide << " x " << pixelsTall << " 픽셀\n"
              << "  알수없는타일: " << unknownTiles << " / " << tiles.size() << "\n"
              << "  유닛      : " << archiveUnits.size()
              << (drawUnits ? " (그림)" : " (생략)") << "\n"
              << "  로케이션  : " << archiveLocations.size()
              << (drawLocations ? " (그림)" : " (생략)") << "\n"
              << "  -> " << outPath << "\n";
    return 0;
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

// 갈래 명령(`splash-cli trigger show ...`)이 불러 쓰는 다리.
// 알맹이는 위에 그대로 두고 이름만 내놓는다.
namespace splash::cli {

int renderMapImage(const std::string & mapPath, const std::string & installPath,
                   const std::string & outPath,
                   bool drawUnits, bool drawLocations, bool drawCreep)
{
    return cmdRender(mapPath, installPath, outPath, drawUnits, drawLocations, drawCreep);
}

int triggerTextCommand(const std::string & mapPath, const std::string & installPath,
                       const std::string & outPath)
{
    return cmdTriggers(mapPath, installPath, outPath);
}

int setTriggersCommand(const std::string & mapPath, const std::string & installPath,
                       const std::string & textPath, const std::string & outMapPath)
{
    return cmdSetTriggers(mapPath, installPath, textPath, outMapPath);
}

int triggerListCommand(const std::string & mapPath, const std::string & installPath,
                       int triggerIndex)
{
    return cmdTriggerList(mapPath, installPath, triggerIndex);
}

int triggerArgsCommand(const std::string & mapPath, const std::string & installPath,
                       std::size_t triggerIndex)
{
    return cmdTriggerArgs(mapPath, installPath, triggerIndex);
}

int setTriggerArgCommand(const std::string & mapPath, const std::string & installPath,
                         const std::string & which, std::size_t triggerIndex,
                         std::size_t slot, std::size_t argIndex,
                         const std::string & value, const std::string & outMapPath)
{
    return cmdSetTriggerArg(mapPath, installPath, which, triggerIndex, slot, argIndex,
                            value, outMapPath);
}

int briefingTextCommand(const std::string & mapPath, const std::string & installPath,
                        const std::string & outPath)
{
    return cmdBriefing(mapPath, installPath, outPath);
}

int setBriefingCommand(const std::string & mapPath, const std::string & installPath,
                       const std::string & textPath, const std::string & outMapPath)
{
    return cmdSetBriefing(mapPath, installPath, textPath, outMapPath);
}

int briefingArgsCommand(const std::string & mapPath, const std::string & installPath,
                        std::size_t index)
{
    return cmdBriefingArgs(mapPath, installPath, index);
}

} // namespace splash::cli

int main(int argc, char ** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
        return usage(argv[0]);

    const std::string & command = args[0];

    if (command == "help" || command == "--help" || command == "-h")
    {
        usage(argv[0]);
        return 0;
    }

    // 갈래 명령을 먼저 본다. 갈래가 아니면 아래의 평평한 명령으로 넘어간다.
    if (const int code = splash::cli::runGroupCommand(argv[0], args);
        code != splash::cli::kUnknownCommand)
    {
        return code;
    }

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

    if (command == "render" && args.size() >= 4)
    {
        bool drawUnits = false;
        bool drawLocations = false;
        bool drawCreep = false;
        for (std::size_t i = 4; i < args.size(); ++i)
        {
            if (args[i] == "--units") drawUnits = true;
            else if (args[i] == "--locations") drawLocations = true;
            else if (args[i] == "--creep") drawCreep = true;
        }
        return cmdRender(args[1], args[2], args[3], drawUnits, drawLocations, drawCreep);
    }

    if (command == "creep-kin" && args.size() == 5)
    {
        try {
            return cmdCreepKin(args[1],
                static_cast<std::uint16_t>(std::stoul(args[2])),
                static_cast<std::uint32_t>(std::stoul(args[3])),
                std::stoi(args[4]));
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "mega-sheet" && args.size() == 6)
    {
        try {
            return cmdMegaSheet(args[1],
                static_cast<std::uint16_t>(std::stoul(args[2])),
                static_cast<std::uint32_t>(std::stoul(args[3])),
                std::stoi(args[4]), args[5]);
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "images-tbl" && (args.size() == 2 || args.size() == 3))
        return cmdImagesTbl(args[1], args.size() == 3 ? args[2] : std::string{});

    if (command == "find-creep" && args.size() == 3)
    {
        try { return cmdFindCreep(args[1], static_cast<std::uint16_t>(std::stoul(args[2]))); }
        catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "tileset-groups" && args.size() == 3)
    {
        try { return cmdTilesetGroups(args[1], static_cast<std::uint16_t>(std::stoul(args[2]))); }
        catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "tileset-ramps" && args.size() == 3)
    {
        try { return cmdTilesetRamps(args[1], static_cast<std::uint16_t>(std::stoul(args[2]))); }
        catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "tile-sheet" && args.size() == 6)
    {
        try {
            return cmdTileSheet(args[1],
                static_cast<std::uint16_t>(std::stoul(args[2])),
                static_cast<std::uint16_t>(std::stoul(args[3])),
                std::stoi(args[4]), args[5]);
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "tileset-info" && args.size() == 3)
    {
        try { return cmdTilesetInfo(args[1], static_cast<std::uint16_t>(std::stoul(args[2]))); }
        catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "unit-classes" && args.size() == 2)
        return cmdUnitClasses(args[1]);

    if (command == "unit-image" && args.size() >= 4)
    {
        try
        {
            const auto type = static_cast<std::uint16_t>(std::stoul(args[2]));
            const std::uint8_t owner = args.size() > 4
                ? static_cast<std::uint8_t>(std::stoul(args[4])) : 0;
            const std::uint16_t tileset = args.size() > 5
                ? static_cast<std::uint16_t>(std::stoul(args[5])) : 0;
            const std::uint32_t resource = args.size() > 6
                ? static_cast<std::uint32_t>(std::stoul(args[6])) : 1500u;
            return cmdUnitImage(args[1], type, args[3], owner, tileset, resource);
        }
        catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "move-unit" && args.size() >= 6)
    {
        try {
            bool thenUndo = false;
            for (std::size_t i = 6; i < args.size(); ++i)
                if (args[i] == "--undo") thenUndo = true;
            return cmdMoveUnit(args[1], static_cast<std::size_t>(std::stoul(args[2])),
                               static_cast<std::uint16_t>(std::stoul(args[3])),
                               static_cast<std::uint16_t>(std::stoul(args[4])),
                               args[5], thenUndo);
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "triggers" && (args.size() == 3 || args.size() == 4))
        return cmdTriggers(args[1], args[2], args.size() == 4 ? args[3] : std::string{});

    if (command == "set-triggers" && args.size() == 5)
        return cmdSetTriggers(args[1], args[2], args[3], args[4]);

    if (command == "place-isom" && args.size() == 8)
    {
        try {
            return cmdPlaceIsom(args[1], args[2],
                static_cast<std::size_t>(std::stoul(args[3])),
                static_cast<std::size_t>(std::stoul(args[4])),
                static_cast<std::size_t>(std::stoul(args[5])),
                static_cast<std::size_t>(std::stoul(args[6])),
                args[7]);
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "set-trigger-arg" && args.size() == 9)
    {
        try {
            return cmdSetTriggerArg(args[1], args[2], args[3],
                static_cast<std::size_t>(std::stoul(args[4])),
                static_cast<std::size_t>(std::stoul(args[5])),
                static_cast<std::size_t>(std::stoul(args[6])),
                args[7], args[8]);
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "sounds" && args.size() == 2)
        return cmdSounds(args[1]);

    if (command == "add-sound" && args.size() == 4)
        return cmdAddSound(args[1], args[2], args[3]);

    if (command == "unprotect" && args.size() == 3)
        return cmdUnprotect(args[1], args[2]);

    if (command == "switches" && args.size() == 2)
        return cmdSwitches(args[1]);

    if (command == "doodads" && args.size() == 3)
    {
        try { return cmdDoodads(args[1], static_cast<std::uint16_t>(std::stoul(args[2]))); }
        catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "icon-histogram" && args.size() == 3)
    {
        try { return cmdIconHistogram(args[1], static_cast<std::uint16_t>(std::stoul(args[2]))); }
        catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "has-asset" && args.size() == 3)
        return cmdHasAsset(args[1], args[2]);

    if (command == "icon" && args.size() == 4)
    {
        try { return cmdIcon(args[1], static_cast<std::uint16_t>(std::stoul(args[2])), args[3]); }
        catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "briefing-args" && args.size() == 4)
    {
        try {
            return cmdBriefingArgs(args[1], args[2],
                static_cast<std::size_t>(std::stoul(args[3])));
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "trigger-args" && args.size() == 4)
    {
        try {
            return cmdTriggerArgs(args[1], args[2],
                static_cast<std::size_t>(std::stoul(args[3])));
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "set-briefing" && args.size() == 5)
        return cmdSetBriefing(args[1], args[2], args[3], args[4]);

    if (command == "briefing" && (args.size() == 3 || args.size() == 4))
        return cmdBriefing(args[1], args[2], args.size() == 4 ? args[3] : std::string());

    if (command == "trigger-list" && (args.size() == 3 || args.size() == 4))
    {
        try {
            return cmdTriggerList(args[1], args[2],
                args.size() == 4 ? std::stoi(args[3]) : -1);
        } catch (const std::exception &) { return usage(argv[0]); }
    }

    if (command == "units" && (args.size() == 2 || args.size() == 3))
    {
        std::size_t limit = 20;
        if (args.size() == 3)
        {
            try { limit = static_cast<std::size_t>(std::stoul(args[2])); }
            catch (const std::exception &) { return usage(argv[0]); }
        }
        return cmdUnits(args[1], limit);
    }

    return usage(argv[0]);
}
