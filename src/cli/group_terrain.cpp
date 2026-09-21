// 지형(TILE·MTXM)과 시야 가리개(MASK).

#include "cli_common.h"

#include "io/game_graphics.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace splash::cli {

namespace {

/// 잘라 낸 네모 한 조각. 지형과 가리개가 같은 꼴을 쓴다.
struct Patch
{
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint32_t> values;
};

/// 조각을 글로 적는다. 눈으로 보고 손으로 고칠 수 있어야 해서 글로 둔다.
bool writePatch(const std::string & path, const char * magic, const Patch & patch)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        std::cerr << "출력 파일을 열지 못했습니다: " << path << "\n";
        return false;
    }

    out << magic << " 1\n" << patch.width << " " << patch.height << "\n";
    out << std::hex;
    for (std::size_t y = 0; y < patch.height; ++y)
    {
        for (std::size_t x = 0; x < patch.width; ++x)
            out << (x ? " " : "") << patch.values[y * patch.width + x];
        out << "\n";
    }
    return true;
}

std::optional<Patch> readPatch(const std::string & path, const char * magic)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "입력 파일을 열지 못했습니다: " << path << "\n";
        return std::nullopt;
    }

    std::string word;
    int version = 0;
    in >> word >> version;
    if (word != magic)
    {
        std::cerr << "이 파일은 " << magic << " 가 아닙니다: " << word << "\n";
        return std::nullopt;
    }
    if (version != 1)
    {
        std::cerr << "모르는 판입니다: " << version << "\n";
        return std::nullopt;
    }

    Patch patch;
    in >> patch.width >> patch.height;
    if (!in || patch.width == 0 || patch.height == 0)
    {
        std::cerr << "크기를 읽지 못했습니다.\n";
        return std::nullopt;
    }

    patch.values.resize(patch.width * patch.height);
    for (auto & value : patch.values)
    {
        in >> std::hex >> value;
        if (!in)
        {
            std::cerr << "값이 모자랍니다 (" << patch.width << "x" << patch.height
                      << " 개가 있어야 합니다).\n";
            return std::nullopt;
        }
    }
    return patch;
}

/// 지형을 쓰면 밑 지형(TILE)도 함께 맞춰진다 — 브러시가 하는 일과 같다.
/// 두들이 깔아 둔 자리라면 밑 지형이 달라지므로 미리 알린다.
///
/// 게임이 보는 MTXM 은 쓴 값 그대로다. 달라지는 것은 에디터가 보는
/// 밑 지형뿐이다.
void warnIfUnderlyingDiffers(const io::MapArchive & archive,
                             const std::vector<io::MapArchive::TileWrite> & writes)
{
    const auto info = archive.info();
    const auto game = archive.terrainTiles();
    const auto editor = archive.underlyingTiles();
    if (game.size() != editor.size() || game.empty())
        return;

    std::size_t differing = 0;
    for (const auto & write : writes)
    {
        const std::size_t at = write.y * info.tileWidth + write.x;
        if (at < game.size() && game[at] != editor[at])
            ++differing;
    }
    if (differing == 0)
        return;

    std::cout << "  알림      : 두들이 깔아 둔 자리 " << differing
              << "칸의 밑 지형(TILE)도 함께 맞춰집니다. 게임이 보는 MTXM 은"
                 " 쓴 값 그대로입니다.\n";
}

/// 맵 안으로 잘라 낸 네모. w·h 를 생략하면 맵 끝까지.
struct Rect { std::size_t x = 0, y = 0, w = 0, h = 0; };

Rect readRect(const Args & args, std::size_t first,
              std::size_t mapWidth, std::size_t mapHeight)
{
    Rect rect;
    rect.x = static_cast<std::size_t>(args.integer(first));
    rect.y = static_cast<std::size_t>(args.integer(first + 1));
    rect.w = static_cast<std::size_t>(args.integerOr(first + 2, 1));
    rect.h = static_cast<std::size_t>(args.integerOr(first + 3, 1));

    if (rect.x >= mapWidth || rect.y >= mapHeight)
        throw CliError("네모가 맵 밖입니다 (맵은 " + std::to_string(mapWidth) + "x" +
                       std::to_string(mapHeight) + " 타일).");

    rect.w = std::min(rect.w, mapWidth - rect.x);
    rect.h = std::min(rect.h, mapHeight - rect.y);
    return rect;
}

// --- 지형 ---

int terrainShow(Args & args)
{
    const bool underlying = args.flag("--underlying");
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto info = archive.info();
        const auto tiles = underlying ? archive.underlyingTiles() : archive.terrainTiles();
        if (tiles.size() != std::size_t(info.tileWidth) * info.tileHeight)
        {
            std::cerr << "지형을 읽지 못했습니다.\n";
            return 1;
        }

        const Rect rect = readRect(args, 1, info.tileWidth, info.tileHeight);
        std::cout << "  " << (underlying ? "밑 지형(TILE)" : "지형")
                  << "  (" << rect.x << ", " << rect.y << ") "
                  << rect.w << "x" << rect.h << "\n";
        for (std::size_t y = 0; y < rect.h; ++y)
        {
            std::cout << "  " << std::setw(4) << std::dec << (rect.y + y) << " |";
            for (std::size_t x = 0; x < rect.w; ++x)
            {
                std::cout << " " << std::setw(4) << std::setfill('0') << std::hex
                          << tiles[(rect.y + y) * info.tileWidth + rect.x + x]
                          << std::setfill(' ');
            }
            std::cout << std::dec << "\n";
        }
        return 0;
    });
}

int terrainFill(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto info = archive.info();
        const Rect rect = readRect(args, 1, info.tileWidth, info.tileHeight);
        const auto value = static_cast<std::uint16_t>(args.integer(5));

        std::vector<io::MapArchive::TileWrite> writes;
        writes.reserve(rect.w * rect.h);
        for (std::size_t y = 0; y < rect.h; ++y)
        {
            for (std::size_t x = 0; x < rect.w; ++x)
                writes.push_back({rect.x + x, rect.y + y, value});
        }

        warnIfUnderlyingDiffers(archive, writes);
        if (auto r = archive.writeTiles(writes); !r)
        {
            std::cerr << "칠하기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  칠함      : (" << rect.x << ", " << rect.y << ") "
                  << rect.w << "x" << rect.h << " 를 타일 0x" << std::hex << value
                  << std::dec << " 로 (" << writes.size() << "칸)\n";
        return true;
    });
}

int terrainSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto tileX = static_cast<std::size_t>(args.integer(1));
    const auto tileY = static_cast<std::size_t>(args.integer(2));
    const auto value = static_cast<std::uint16_t>(args.integer(3));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setTile(tileX, tileY, value); !r)
        {
            std::cerr << "칠하기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  칠함      : (" << tileX << ", " << tileY << ") -> 0x"
                  << std::hex << value << std::dec << "\n";
        return true;
    });
}

int terrainCopy(Args & args)
{
    const bool underlying = args.flag("--no-doodads");
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto info = archive.info();
        // 두들을 빼고 베끼려면 화면에 보이는 MTXM 이 아니라 밑 지형을 쓴다.
        const auto tiles = underlying ? archive.underlyingTiles() : archive.terrainTiles();
        if (tiles.size() != std::size_t(info.tileWidth) * info.tileHeight)
        {
            std::cerr << "지형을 읽지 못했습니다.\n";
            return 1;
        }

        const Rect rect = readRect(args, 1, info.tileWidth, info.tileHeight);
        const std::string outPath = args.at(5);

        Patch patch;
        patch.width = rect.w;
        patch.height = rect.h;
        patch.values.reserve(rect.w * rect.h);
        for (std::size_t y = 0; y < rect.h; ++y)
        {
            for (std::size_t x = 0; x < rect.w; ++x)
                patch.values.push_back(tiles[(rect.y + y) * info.tileWidth + rect.x + x]);
        }

        if (!writePatch(outPath, "splash-tiles", patch))
            return 1;
        std::cout << "  베낌      : " << rect.w << "x" << rect.h << " -> " << outPath
                  << (underlying ? "  (두들 뺌)" : "") << "\n";
        return 0;
    });
}

int terrainPaste(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto tileX = static_cast<std::size_t>(args.integer(1));
    const auto tileY = static_cast<std::size_t>(args.integer(2));
    const std::string inPath = args.at(3);

    const auto patch = readPatch(inPath, "splash-tiles");
    if (!patch)
        return 1;

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto info = archive.info();

        std::vector<io::MapArchive::TileWrite> writes;
        std::size_t clipped = 0;
        for (std::size_t y = 0; y < patch->height; ++y)
        {
            for (std::size_t x = 0; x < patch->width; ++x)
            {
                const std::size_t targetX = tileX + x;
                const std::size_t targetY = tileY + y;
                if (targetX >= info.tileWidth || targetY >= info.tileHeight)
                {
                    ++clipped;
                    continue;
                }
                writes.push_back({targetX, targetY,
                                  static_cast<std::uint16_t>(patch->values[y * patch->width + x])});
            }
        }

        if (writes.empty())
        {
            std::cerr << "붙일 곳이 맵 밖입니다.\n";
            return false;
        }
        warnIfUnderlyingDiffers(archive, writes);
        if (auto r = archive.writeTiles(writes); !r)
        {
            std::cerr << "붙이기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  붙임      : " << patch->width << "x" << patch->height
                  << " @ (" << tileX << ", " << tileY << ")  " << writes.size() << "칸";
        if (clipped != 0)
            std::cout << ", 맵 밖 " << clipped << "칸은 버림";
        std::cout << "\n";
        return true;
    });
}

int terrainMirror(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto fromText = args.option("--from");
    const std::string mapPath = args.at(0);
    const std::string axis = args.at(1);

    const bool horizontal = axis == "horizontal" || axis == "좌우" || axis == "both" ||
                            axis == "네곳" || axis == "rot90";
    const bool vertical   = axis == "vertical" || axis == "위아래" || axis == "both" ||
                            axis == "네곳" || axis == "rot90";
    const bool rotate180  = axis == "rot180" || axis == "rot90" || axis == "180";
    const bool rotate90   = axis == "rot90";
    if (!horizontal && !vertical && !rotate180)
        throw CliError("모르는 대칭입니다: " + axis +
                       " (horizontal/vertical/both/rot180/rot90)");

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto info = archive.info();
        const auto tiles = archive.terrainTiles();
        const std::size_t width = info.tileWidth;
        const std::size_t height = info.tileHeight;
        if (tiles.size() != width * height)
        {
            std::cerr << "지형을 읽지 못했습니다.\n";
            return false;
        }
        if (rotate90 && width != height)
        {
            std::cerr << "90도 돌리기는 가로세로가 같은 맵에서만 됩니다 ("
                      << width << "x" << height << ").\n";
            return false;
        }

        // 베낄 쪽. 가로 대칭이면 절반, 네 곳이면 사분면만 읽으면 된다.
        const bool fromRight  = fromText && (*fromText == "right" || *fromText == "오른쪽");
        const bool fromBottom = fromText && (*fromText == "bottom" || *fromText == "아래");
        if (fromText && !fromRight && !fromBottom &&
            *fromText != "left" && *fromText != "왼쪽" &&
            *fromText != "top" && *fromText != "위")
        {
            std::cerr << "--from 은 left/right/top/bottom 입니다: " << *fromText << "\n";
            return false;
        }

        const std::size_t lastX = width - 1;
        const std::size_t lastY = height - 1;

        const std::size_t halfW = width / 2;
        const std::size_t halfH = height / 2;
        const std::size_t sourceX0 = (horizontal && fromRight) ? width - halfW : 0;
        const std::size_t sourceX1 = horizontal ? sourceX0 + halfW : width;
        const std::size_t sourceY0 = (vertical && fromBottom) ? height - halfH : 0;
        const std::size_t sourceY1 = vertical ? sourceY0 + halfH : height;

        // 같은 칸을 두 번 쓰지 않도록 마지막 값만 남긴다.
        std::vector<io::MapArchive::TileWrite> writes;
        std::vector<bool> written(width * height, false);
        const auto stamp = [&](std::size_t x, std::size_t y, std::uint16_t value) {
            if (x >= width || y >= height)
                return;
            const std::size_t at = y * width + x;
            if (written[at] || tiles[at] == value)
                return;
            written[at] = true;
            writes.push_back({x, y, value});
        };

        for (std::size_t y = sourceY0; y < sourceY1; ++y)
        {
            for (std::size_t x = sourceX0; x < sourceX1; ++x)
            {
                const auto value = tiles[y * width + x];
                if (horizontal)          stamp(lastX - x, y, value);
                if (vertical)            stamp(x, lastY - y, value);
                if (horizontal && vertical) stamp(lastX - x, lastY - y, value);
                if (rotate180)           stamp(lastX - x, lastY - y, value);
                if (rotate90)
                {
                    stamp(lastY - y, x, value);
                    stamp(y, lastX - x, value);
                }
            }
        }

        if (writes.empty())
        {
            std::cout << "  이미 대칭입니다. 바꾼 칸이 없습니다.\n";
            return false;
        }
        warnIfUnderlyingDiffers(archive, writes);
        if (auto r = archive.writeTiles(writes); !r)
        {
            std::cerr << "대칭 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  대칭      : " << axis << "  " << writes.size() << "칸을 고침\n";
        std::cout << "  알림      : 지형만 옮깁니다. 유닛·두들·로케이션은 그대로입니다.\n";
        return true;
    });
}

int terrainIsom(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    io::GameGraphics graphics;
    if (!loadGraphics(args, graphics))
        return 1;

    const std::string mapPath = args.at(0);
    const auto pixelX = static_cast<std::size_t>(args.integer(1));
    const auto pixelY = static_cast<std::size_t>(args.integer(2));
    const auto terrainType = static_cast<std::size_t>(args.integer(3));
    const auto brushExtent = static_cast<std::size_t>(args.integerOr(4, 1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.placeIsomTerrain(graphics, pixelX, pixelY,
                                              terrainType, brushExtent); !r)
        {
            std::cerr << "ISOM 놓기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  놓음      : ISOM 지형 " << terrainType
                  << " @ 픽셀 (" << pixelX << ", " << pixelY
                  << "), 브러시 " << brushExtent << "\n";
        return true;
    });
}

/// ISOM 붓질을 파일에서 한꺼번에 읽어 놓는다.
///
/// 붓질마다 명령을 부르면 맵을 열고 저장하는 값이 붓질 값보다 훨씬 크다
/// — 천 번 칠하는 데 몇 분이 걸린다. 한 번 열어 다 칠하고 한 번 저장한다.
int terrainIsomBatch(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    io::GameGraphics graphics;
    if (!loadGraphics(args, graphics))
        return 1;

    const std::string mapPath = args.at(0);
    const std::string listPath = args.at(1);

    std::ifstream in(listPath);
    if (!in)
    {
        std::cerr << "붓질 목록을 열지 못했습니다: " << listPath << "\n";
        return 1;
    }

    // 한 줄에 "<픽셀x> <픽셀y> <지형번호> [브러시]". # 로 시작하면 주석.
    struct Stroke { std::size_t x, y, terrain, brush; };
    std::vector<Stroke> strokes;
    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ls(line);
        Stroke st {};
        st.brush = 1;
        if (!(ls >> st.x >> st.y >> st.terrain))
        {
            std::cerr << listPath << ":" << lineNo << " 줄을 읽지 못했습니다.\n";
            return 2;
        }
        ls >> st.brush;
        strokes.push_back(st);
    }
    if (strokes.empty())
    {
        std::cerr << "붓질이 하나도 없습니다.\n";
        return 2;
    }

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        std::size_t done = 0, failed = 0;
        for (const auto & st : strokes)
        {
            if (auto r = archive.placeIsomTerrain(graphics, st.x, st.y,
                                                  st.terrain, st.brush))
                ++done;
            else
                ++failed;
        }
        std::cout << "  놓음      : 붓질 " << done << "번";
        if (failed != 0)
            std::cout << ", 놓지 못한 것 " << failed << "번";
        std::cout << "\n";
        return done > 0;
    });
}

int terrainTypes(Args & args)
{
    io::GameGraphics graphics;
    if (!loadGraphics(args, graphics))
        return 1;
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const std::uint16_t tileset = archive.info().tilesetId;
        const auto types = graphics.terrainTypes(tileset);
        for (const auto & type : types)
        {
            std::cout << "  " << std::setw(4) << type.brushIndex << "  " << type.name
                      << "  (index " << type.index << ")\n";
        }
        std::cout << "  타일셋 " << tileset << " 의 지형 " << types.size() << "종\n";
        std::cout << "  번호는 terrain isom 에 넘기는 값입니다.\n";
        return 0;
    });
}

// --- 시야 가리개 ---

int fogShow(Args & args)
{
    args.finish();
    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto info = archive.info();
        const auto fog = archive.fogTiles();
        if (fog.empty())
        {
            std::cout << "  가리개 구역(MASK)이 없습니다.\n";
            return 0;
        }

        const Rect rect = readRect(args, 1, info.tileWidth, info.tileHeight);
        for (std::size_t y = 0; y < rect.h; ++y)
        {
            std::cout << "  " << std::setw(4) << (rect.y + y) << " |";
            for (std::size_t x = 0; x < rect.w; ++x)
            {
                const std::size_t at = (rect.y + y) * info.tileWidth + rect.x + x;
                std::cout << " " << std::setw(3)
                          << (at < fog.size() ? int(fog[at]) : 0);
            }
            std::cout << "\n";
        }

        std::size_t covered = 0;
        for (std::uint8_t value : fog)
            if (value != 0) ++covered;
        std::cout << "  가려진 칸 " << covered << " / " << fog.size() << "\n";
        return 0;
    });
}

int fogFill(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto info = archive.info();
        const Rect rect = readRect(args, 1, info.tileWidth, info.tileHeight);
        const std::uint8_t players = parsePlayerBits(args.at(5));

        std::vector<std::pair<int, int>> tiles;
        tiles.reserve(rect.w * rect.h);
        for (std::size_t y = 0; y < rect.h; ++y)
        {
            for (std::size_t x = 0; x < rect.w; ++x)
                tiles.emplace_back(static_cast<int>(rect.x + x), static_cast<int>(rect.y + y));
        }

        if (auto r = archive.setFogTiles(tiles, players); !r)
        {
            std::cerr << "칠하기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  칠함      : (" << rect.x << ", " << rect.y << ") "
                  << rect.w << "x" << rect.h << " -> 플레이어 "
                  << playerBitsText(players) << " (" << tiles.size() << "칸)\n";
        return true;
    });
}

int fogAll(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const std::uint8_t players = parsePlayerBits(args.at(1));
    const std::string state = args.count() > 2 ? args.at(2) : std::string("on");
    if (state != "on" && state != "off")
        throw CliError("on 이나 off 를 적으세요: " + state);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setFogEverywhere(players, state == "on"); !r)
        {
            std::cerr << "일괄 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  맵 전체    : 플레이어 " << playerBitsText(players)
                  << " 가리개 " << (state == "on" ? "씌움" : "걷음") << "\n";
        return true;
    });
}

int fogCopy(Args & args)
{
    args.finish();
    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto info = archive.info();
        const auto fog = archive.fogTiles();
        if (fog.empty())
        {
            std::cerr << "가리개 구역(MASK)이 없습니다.\n";
            return 1;
        }

        const Rect rect = readRect(args, 1, info.tileWidth, info.tileHeight);
        const std::string outPath = args.at(5);

        Patch patch;
        patch.width = rect.w;
        patch.height = rect.h;
        patch.values.reserve(rect.w * rect.h);
        for (std::size_t y = 0; y < rect.h; ++y)
        {
            for (std::size_t x = 0; x < rect.w; ++x)
            {
                const std::size_t at = (rect.y + y) * info.tileWidth + rect.x + x;
                patch.values.push_back(at < fog.size() ? fog[at] : 0);
            }
        }

        if (!writePatch(outPath, "splash-fog", patch))
            return 1;
        std::cout << "  베낌      : " << rect.w << "x" << rect.h << " -> " << outPath << "\n";
        return 0;
    });
}

int fogPaste(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto tileX = static_cast<std::size_t>(args.integer(1));
    const auto tileY = static_cast<std::size_t>(args.integer(2));
    const std::string inPath = args.at(3);

    const auto patch = readPatch(inPath, "splash-fog");
    if (!patch)
        return 1;

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto info = archive.info();

        // setFogTiles 는 값 하나를 여러 칸에 쓴다. 값별로 묶어 부른다.
        std::vector<std::vector<std::pair<int, int>>> byValue(256);
        std::size_t clipped = 0;
        for (std::size_t y = 0; y < patch->height; ++y)
        {
            for (std::size_t x = 0; x < patch->width; ++x)
            {
                const std::size_t targetX = tileX + x;
                const std::size_t targetY = tileY + y;
                if (targetX >= info.tileWidth || targetY >= info.tileHeight)
                {
                    ++clipped;
                    continue;
                }
                const auto value = static_cast<std::uint8_t>(patch->values[y * patch->width + x]);
                byValue[value].emplace_back(static_cast<int>(targetX), static_cast<int>(targetY));
            }
        }

        std::size_t written = 0;
        for (std::size_t value = 0; value < byValue.size(); ++value)
        {
            if (byValue[value].empty())
                continue;
            if (auto r = archive.setFogTiles(byValue[value],
                                             static_cast<std::uint8_t>(value)); !r)
            {
                std::cerr << "붙이기 실패: " << r.message << "\n";
                return false;
            }
            written += byValue[value].size();
        }

        if (written == 0)
        {
            std::cerr << "붙일 곳이 맵 밖입니다.\n";
            return false;
        }
        std::cout << "  붙임      : " << patch->width << "x" << patch->height
                  << " @ (" << tileX << ", " << tileY << ")  " << written << "칸";
        if (clipped != 0)
            std::cout << ", 맵 밖 " << clipped << "칸은 버림";
        std::cout << "\n";
        return true;
    });
}

} // namespace

std::vector<Group> terrainGroups()
{
    return {
        Group{"terrain", "지형 타일 (TILE·MTXM)", {
            {"show",   "<맵> <x> <y> [w] [h] [--underlying]",
                       "타일 번호를 16진으로 보여 준다.", terrainShow},
            {"set",    "<맵> <x> <y> <타일값> -o <출력맵>", "타일 하나를 칠한다.", terrainSet},
            {"fill",   "<맵> <x> <y> <w> <h> <타일값> -o <출력맵>", "네모를 칠한다.", terrainFill},
            {"copy",   "<맵> <x> <y> <w> <h> <출력.tiles> [--no-doodads]",
                       "지형을 파일로 베낀다. --no-doodads 면 두들을 뺀 밑 지형을.", terrainCopy},
            {"paste",  "<맵> <x> <y> <입력.tiles> -o <출력맵>",
                       "베낀 지형을 붙인다. 밑 지형(TILE)도 함께 맞춰진다.", terrainPaste},
            {"mirror", "<맵> <horizontal|vertical|both|rot180|rot90> [--from left|right|top|bottom] -o <출력맵>",
                       "한쪽 지형을 맞은편에 베낀다.", terrainMirror},
            {"isom",   "<맵> <픽셀x> <픽셀y> <지형번호> [브러시] --install 설치폴더 -o <출력맵>",
                       "ISOM 브러시로 놓는다 (절벽·경계가 이어진다).", terrainIsom},
            {"isom-batch", "<맵> <붓질목록.txt> --install 설치폴더 -o <출력맵>",
                       "ISOM 붓질을 파일에서 한꺼번에 놓는다 (한 줄에 x y 지형 [브러시]).",
                       terrainIsomBatch},
            {"types",  "<맵> --install 설치폴더", "이 타일셋의 지형 종류를 나열한다.", terrainTypes},
        }},
        Group{"fog", "시야 가리개 (MASK)", {
            {"show",  "<맵> <x> <y> [w] [h]", "가려진 플레이어 비트를 보여 준다.", fogShow},
            {"fill",  "<맵> <x> <y> <w> <h> <플레이어…|all|none> -o <출력맵>",
                      "네모를 칠한다. 플레이어는 1,3,5 꼴.", fogFill},
            {"all",   "<맵> <플레이어…|all|none> [on|off] -o <출력맵>",
                      "맵 전체에 씌우거나 걷는다.", fogAll},
            {"copy",  "<맵> <x> <y> <w> <h> <출력.fog>", "가리개를 파일로 베낀다.", fogCopy},
            {"paste", "<맵> <x> <y> <입력.fog> -o <출력맵>", "베낀 가리개를 붙인다.", fogPaste},
        }},
    };
}

} // namespace splash::cli
