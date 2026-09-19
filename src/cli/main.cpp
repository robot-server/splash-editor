// splash-cli — Splash Editor 코어의 커맨드라인 프런트엔드.
//
// GUI 없이 코어를 두드리기 위한 도구다. round-trip 검증(M1 의 합격 조건)은
// 여기서 실행하며, tests/ 의 자동 테스트도 같은 코드를 쓴다.

#include "chk/map_document.h"
#include "io/game_assets.h"
#include "io/game_graphics.h"
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
        "      설치본을 조사한다. 아카이브를 열고 타일셋 데이터가 읽히는지 확인한다.\n\n"
        "  " << argv0 << " render <맵파일> <설치폴더> <출력.ppm> [--units] [--locations]\n"
        "      맵 지형을 이미지로 그린다. 타일셋 디코딩 검증용이다.\n"
        "      --units / --locations 를 주면 유닛·로케이션도 겹쳐 그린다.\n\n"
        "  " << argv0 << " units <맵파일> [개수]\n"
        "      맵에 놓인 유닛을 나열한다 (기본 20개).\n\n"
        "  " << argv0 << " unit-image <설치폴더> <유닛번호> <출력.ppm> [소유자] [타일셋]\n"
        "      유닛 하나를 격자 배경 위에 그린다. 스프라이트 검증용이다.\n";
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

int cmdUnitImage(const std::string & installPath,
                 std::uint16_t unitType,
                 const std::string & outPath,
                 std::uint8_t owner,
                 std::uint16_t tilesetId,
                 std::uint32_t resourceAmount)
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

    const auto image = graphics.renderUnit(unitType, owner, tilesetId, resourceAmount);
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
              bool drawLocations)
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

    if (command == "render" && args.size() >= 4)
    {
        bool drawUnits = false;
        bool drawLocations = false;
        for (std::size_t i = 4; i < args.size(); ++i)
        {
            if (args[i] == "--units") drawUnits = true;
            else if (args[i] == "--locations") drawLocations = true;
        }
        return cmdRender(args[1], args[2], args[3], drawUnits, drawLocations);
    }

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
