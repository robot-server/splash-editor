// 맵에 놓인 것들을 네모째 오려 두었다가 다른 자리에 붙이기.
//
// 지형·가리개는 칸마다 값 하나라 숫자만 옮기면 되지만(terrain/fog copy),
// 유닛·스프라이트·두들·로케이션은 좌표 말고도 소유자·체력·상태·애드온
// 연결을 함께 옮겨야 한다. 그래서 꼴을 따로 두고 갈래도 따로 뒀다.

#include "cli_common.h"

#include "io/game_graphics.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace splash::cli {

namespace {

constexpr int kTilePixels = 32;

/// 오려 둔 것 하나. 좌표는 모두 베낀 네모의 왼위 모서리에서 잰 픽셀이다.
/// 타일이 아니라 픽셀로 두어야 유닛이 칸 안에서 어디 서 있었는지 지킨다.
struct ClipUnit
{
    int dx = 0, dy = 0;
    std::uint16_t type = 0;
    std::uint8_t owner = 0;
    std::uint8_t hitpointPercent = 100;
    std::uint8_t shieldPercent = 100;
    std::uint8_t energyPercent = 100;
    std::uint32_t resourceAmount = 0;
    std::uint16_t hangarAmount = 0;
    std::uint16_t stateFlags = 0;

    /// 이 파일 안에서 이어진 짝의 자리. 없으면 -1.
    int link = -1;
    bool addonLink = false; ///< 참이면 애드온, 거짓이면 나이더스
};

struct ClipSprite
{
    int dx = 0, dy = 0;
    std::uint16_t type = 0;
    std::uint8_t owner = 0;
    bool drawnAsSprite = false;
    bool disabled = false;
};

struct ClipDoodad
{
    int dx = 0, dy = 0;  ///< 가운데 픽셀
    std::uint16_t type = 0;
    std::uint8_t owner = 0;
    bool enabled = true;
};

struct ClipLocation
{
    int dleft = 0, dtop = 0, dright = 0, dbottom = 0;
    std::uint16_t elevationFlags = 0;
    std::string name;
};

struct Clipboard
{
    int tileWidth = 0;
    int tileHeight = 0;
    std::vector<ClipUnit> units;
    std::vector<ClipSprite> sprites;
    std::vector<ClipDoodad> doodads;
    std::vector<ClipLocation> locations;

    bool empty() const
    {
        return units.empty() && sprites.empty() && doodads.empty() && locations.empty();
    }
    std::size_t count() const
    {
        return units.size() + sprites.size() + doodads.size() + locations.size();
    }
};

constexpr std::uint16_t kNydusLink = 0x0200;
constexpr std::uint16_t kAddonLink = 0x0400;

/// Chk::LocationId::Anywhere. 트리거가 기본으로 쓰는 자리라 맵마다 있다.
constexpr std::size_t kAnywhereLocation = 64;

/// 어떤 갈래를 다룰지. 아무것도 안 고르면 전부.
struct Kinds
{
    bool units = false, sprites = false, doodads = false, locations = false;

    bool none() const { return !units && !sprites && !doodads && !locations; }
    void all() { units = sprites = doodads = locations = true; }
};

Kinds readKinds(Args & args)
{
    Kinds kinds;
    if (args.flag("--units"))     kinds.units = true;
    if (args.flag("--sprites"))   kinds.sprites = true;
    if (args.flag("--doodads"))   kinds.doodads = true;
    if (args.flag("--locations")) kinds.locations = true;

    if (const auto only = args.option("--only"))
    {
        std::string current;
        const auto take = [&] {
            if (current.empty())
                return;
            if      (current == "unit"     || current == "units")     kinds.units = true;
            else if (current == "sprite"   || current == "sprites")   kinds.sprites = true;
            else if (current == "doodad"   || current == "doodads")   kinds.doodads = true;
            else if (current == "location" || current == "locations") kinds.locations = true;
            else throw CliError("모르는 갈래입니다: " + current +
                                " (unit/sprite/doodad/location)");
            current.clear();
        };
        for (char c : *only)
        {
            if (c == ',' || c == ' ') take();
            else current.push_back(c);
        }
        take();
    }

    if (kinds.none())
        kinds.all();
    return kinds;
}

// --- 파일로 적고 읽기 ---

/// 줄이 끊기지 않게 눕힌다. 로케이션 이름은 줄 끝까지 읽으므로
/// 줄바꿈이 들어 있으면 파일이 어긋난다.
std::string flatten(std::string text)
{
    for (char & c : text)
        if (c == '\n' || c == '\r') c = ' ';
    return text;
}

bool writeClipboard(const std::string & path, const Clipboard & clip)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        std::cerr << "출력 파일을 열지 못했습니다: " << path << "\n";
        return false;
    }

    out << "splash-objects 1\n";
    out << clip.tileWidth << " " << clip.tileHeight << "\n";

    for (const auto & unit : clip.units)
    {
        out << "unit " << unit.dx << " " << unit.dy << " " << unit.type
            << " " << int(unit.owner)
            << " " << int(unit.hitpointPercent)
            << " " << int(unit.shieldPercent)
            << " " << int(unit.energyPercent)
            << " " << unit.resourceAmount
            << " " << unit.hangarAmount
            << " " << unit.stateFlags
            << " " << unit.link
            << " " << (unit.addonLink ? "addon" : "nydus") << "\n";
    }
    for (const auto & sprite : clip.sprites)
    {
        out << "sprite " << sprite.dx << " " << sprite.dy << " " << sprite.type
            << " " << int(sprite.owner)
            << " " << (sprite.drawnAsSprite ? 1 : 0)
            << " " << (sprite.disabled ? 1 : 0) << "\n";
    }
    for (const auto & doodad : clip.doodads)
    {
        out << "doodad " << doodad.dx << " " << doodad.dy << " " << doodad.type
            << " " << int(doodad.owner)
            << " " << (doodad.enabled ? 1 : 0) << "\n";
    }
    for (const auto & location : clip.locations)
    {
        out << "location " << location.dleft << " " << location.dtop
            << " " << location.dright << " " << location.dbottom
            << " " << location.elevationFlags
            << " " << flatten(location.name) << "\n";
    }
    return true;
}

std::optional<Clipboard> readClipboard(const std::string & path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "입력 파일을 열지 못했습니다: " << path << "\n";
        return std::nullopt;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "splash-objects")
    {
        std::cerr << "이 파일은 splash-objects 가 아닙니다: " << magic << "\n";
        return std::nullopt;
    }
    if (version != 1)
    {
        std::cerr << "모르는 판입니다: " << version << "\n";
        return std::nullopt;
    }

    Clipboard clip;
    in >> clip.tileWidth >> clip.tileHeight;
    if (!in)
    {
        std::cerr << "크기를 읽지 못했습니다.\n";
        return std::nullopt;
    }
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::string line;
    std::size_t lineNumber = 2;
    while (std::getline(in, line))
    {
        ++lineNumber;
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream parts(line);
        std::string kind;
        parts >> kind;

        const auto fail = [&](const char * why) {
            std::cerr << lineNumber << "번째 줄: " << why << "\n  " << line << "\n";
        };

        if (kind == "unit")
        {
            ClipUnit unit;
            int owner = 0, hp = 0, shield = 0, energy = 0, state = 0;
            std::string linkKind;
            parts >> unit.dx >> unit.dy >> unit.type >> owner >> hp >> shield >> energy
                  >> unit.resourceAmount >> unit.hangarAmount >> state >> unit.link
                  >> linkKind;
            if (!parts) { fail("유닛 줄을 읽지 못했습니다."); return std::nullopt; }
            unit.owner = static_cast<std::uint8_t>(owner);
            unit.hitpointPercent = static_cast<std::uint8_t>(hp);
            unit.shieldPercent = static_cast<std::uint8_t>(shield);
            unit.energyPercent = static_cast<std::uint8_t>(energy);
            unit.stateFlags = static_cast<std::uint16_t>(state);
            unit.addonLink = linkKind == "addon";
            clip.units.push_back(unit);
        }
        else if (kind == "sprite")
        {
            ClipSprite sprite;
            int owner = 0, drawn = 0, disabled = 0;
            parts >> sprite.dx >> sprite.dy >> sprite.type >> owner >> drawn >> disabled;
            if (!parts) { fail("스프라이트 줄을 읽지 못했습니다."); return std::nullopt; }
            sprite.owner = static_cast<std::uint8_t>(owner);
            sprite.drawnAsSprite = drawn != 0;
            sprite.disabled = disabled != 0;
            clip.sprites.push_back(sprite);
        }
        else if (kind == "doodad")
        {
            ClipDoodad doodad;
            int owner = 0, enabled = 1;
            parts >> doodad.dx >> doodad.dy >> doodad.type >> owner >> enabled;
            if (!parts) { fail("두들 줄을 읽지 못했습니다."); return std::nullopt; }
            doodad.owner = static_cast<std::uint8_t>(owner);
            doodad.enabled = enabled != 0;
            clip.doodads.push_back(doodad);
        }
        else if (kind == "location")
        {
            ClipLocation location;
            int elevation = 0;
            parts >> location.dleft >> location.dtop >> location.dright >> location.dbottom
                  >> elevation;
            if (!parts) { fail("로케이션 줄을 읽지 못했습니다."); return std::nullopt; }
            location.elevationFlags = static_cast<std::uint16_t>(elevation);
            std::string name;
            std::getline(parts, name);
            if (!name.empty() && name.front() == ' ')
                name.erase(name.begin());
            location.name = name;
            clip.locations.push_back(location);
        }
        else
        {
            fail("모르는 갈래입니다.");
            return std::nullopt;
        }
    }

    // 짝 번호가 파일 밖을 가리키면 이을 수 없다. 조용히 끊는다.
    for (auto & unit : clip.units)
    {
        if (unit.link >= static_cast<int>(clip.units.size()))
            unit.link = -1;
    }
    return clip;
}

// --- 베끼기 ---

int objectCopy(Args & args)
{
    Kinds kinds = readKinds(args);
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto info = archive.info();

        const int tileX = static_cast<int>(args.integer(1));
        const int tileY = static_cast<int>(args.integer(2));
        const int tileW = static_cast<int>(args.integer(3));
        const int tileH = static_cast<int>(args.integer(4));
        const std::string outPath = args.at(5);

        if (tileX < 0 || tileY < 0 || tileW <= 0 || tileH <= 0)
            throw CliError("네모는 맵 안의 양수 크기여야 합니다.");
        if (tileX >= info.tileWidth || tileY >= info.tileHeight)
            throw CliError("네모가 맵 밖입니다 (맵은 " + std::to_string(info.tileWidth) +
                           "x" + std::to_string(info.tileHeight) + " 타일).");

        const int originX = tileX * kTilePixels;
        const int originY = tileY * kTilePixels;
        const int rightX = originX + tileW * kTilePixels;
        const int bottomY = originY + tileH * kTilePixels;

        // 가운데가 네모 안에 들어야 베낀다.
        const auto inside = [&](int x, int y) {
            return x >= originX && x < rightX && y >= originY && y < bottomY;
        };

        Clipboard clip;
        clip.tileWidth = tileW;
        clip.tileHeight = tileH;

        const auto units = archive.units();

        // 유닛은 먼저 어느 것을 베낄지 정해 둬야 짝 번호를 매길 수 있다.
        std::vector<std::size_t> taken;
        if (kinds.units)
        {
            for (std::size_t i = 0; i < units.size(); ++i)
            {
                if (inside(units[i].x, units[i].y))
                    taken.push_back(i);
            }
        }

        // classId -> 베낀 목록에서의 자리.
        std::vector<std::pair<std::uint32_t, int>> byClassId;
        for (std::size_t slot = 0; slot < taken.size(); ++slot)
        {
            const auto & unit = units[taken[slot]];
            if (unit.classId != 0)
                byClassId.emplace_back(unit.classId, static_cast<int>(slot));
        }

        for (std::size_t slot = 0; slot < taken.size(); ++slot)
        {
            const auto & source = units[taken[slot]];
            ClipUnit unit;
            unit.dx = source.x - originX;
            unit.dy = source.y - originY;
            unit.type = source.type;
            unit.owner = source.owner;
            unit.hitpointPercent = source.hitpointPercent;
            unit.shieldPercent = source.shieldPercent;
            unit.energyPercent = source.energyPercent;
            unit.resourceAmount = source.resourceAmount;
            unit.hangarAmount = source.hangarAmount;
            unit.stateFlags = source.stateFlags;

            // 짝이 함께 베껴진 경우에만 연결을 지킨다. 한쪽만 베끼면
            // 이을 상대가 없으므로 끊는다.
            const bool linked = source.relationFlags != 0 &&
                                source.relationClassId != source.classId &&
                                (source.relationFlags & (kAddonLink | kNydusLink)) != 0;
            if (linked)
            {
                const auto found = std::find_if(byClassId.begin(), byClassId.end(),
                    [&](const auto & entry) { return entry.first == source.relationClassId; });
                if (found != byClassId.end())
                {
                    unit.link = found->second;
                    unit.addonLink = (source.relationFlags & kAddonLink) != 0;
                }
            }
            clip.units.push_back(unit);
        }

        if (kinds.sprites)
        {
            for (const auto & sprite : archive.sprites())
            {
                if (!inside(sprite.x, sprite.y))
                    continue;
                ClipSprite copy;
                copy.dx = sprite.x - originX;
                copy.dy = sprite.y - originY;
                copy.type = sprite.type;
                copy.owner = sprite.owner;
                copy.drawnAsSprite = sprite.drawnAsSprite;
                clip.sprites.push_back(copy);
            }
        }

        if (kinds.doodads)
        {
            for (const auto & doodad : archive.doodads())
            {
                if (!inside(doodad.x, doodad.y))
                    continue;
                ClipDoodad copy;
                copy.dx = doodad.x - originX;
                copy.dy = doodad.y - originY;
                copy.type = doodad.type;
                copy.owner = doodad.owner;
                copy.enabled = doodad.enabled;
                clip.doodads.push_back(copy);
            }
        }

        if (kinds.locations)
        {
            for (const auto & location : archive.locations())
            {
                // "Anywhere"(64번)는 맵마다 이미 있는 예약 자리다. 베껴
                // 두면 붙일 때마다 똑같은 것이 하나씩 늘어난다.
                if (location.index == kAnywhereLocation)
                    continue;

                // 로케이션은 네모 안에 통째로 들어야 베낀다 — 절반만
                // 옮기면 크기가 달라져 트리거가 가리키는 곳이 바뀐다.
                const auto left   = std::min(location.left, location.right);
                const auto right  = std::max(location.left, location.right);
                const auto top    = std::min(location.top, location.bottom);
                const auto bottom = std::max(location.top, location.bottom);
                if (static_cast<int>(left) < originX || static_cast<int>(right) > rightX ||
                    static_cast<int>(top) < originY || static_cast<int>(bottom) > bottomY)
                    continue;

                ClipLocation copy;
                copy.dleft   = static_cast<int>(location.left) - originX;
                copy.dtop    = static_cast<int>(location.top) - originY;
                copy.dright  = static_cast<int>(location.right) - originX;
                copy.dbottom = static_cast<int>(location.bottom) - originY;
                copy.elevationFlags = location.elevationFlags;
                copy.name = location.name;
                clip.locations.push_back(copy);
            }
        }

        if (!writeClipboard(outPath, clip))
            return 1;

        std::cout << "  베낌      : " << tileW << "x" << tileH << " 타일에서"
                  << "  유닛 " << clip.units.size()
                  << ", 스프라이트 " << clip.sprites.size()
                  << ", 두들 " << clip.doodads.size()
                  << ", 로케이션 " << clip.locations.size() << "\n";

        const std::size_t links = static_cast<std::size_t>(
            std::count_if(clip.units.begin(), clip.units.end(),
                          [](const ClipUnit & unit) { return unit.link >= 0; }));
        if (links != 0)
            std::cout << "  연결      : 짝이 함께 든 유닛 " << links << "개\n";
        std::cout << "  -> " << outPath << "\n";
        return 0;
    });
}

// --- 파일 들여다보기 ---

int objectShow(Args & args)
{
    args.finish();
    const auto clip = readClipboard(args.at(0));
    if (!clip)
        return 1;

    std::cout << "  네모      : " << clip->tileWidth << "x" << clip->tileHeight << " 타일\n";
    for (std::size_t i = 0; i < clip->units.size(); ++i)
    {
        const auto & unit = clip->units[i];
        std::cout << "  unit     " << std::setw(4) << i
                  << "  +" << std::setw(5) << unit.dx << ",+" << std::setw(5) << unit.dy
                  << "  P" << std::setw(2) << (unit.owner + 1)
                  << "  " << io::unitTypeName(unit.type) << " (" << unit.type << ")";
        if (unit.resourceAmount != 0) std::cout << "  자원 " << unit.resourceAmount;
        if (unit.link >= 0)
            std::cout << "  [" << (unit.addonLink ? "애드온" : "나이더스")
                      << " " << unit.link << "]";
        std::cout << "\n";
    }
    for (std::size_t i = 0; i < clip->sprites.size(); ++i)
    {
        const auto & sprite = clip->sprites[i];
        std::cout << "  sprite   " << std::setw(4) << i
                  << "  +" << std::setw(5) << sprite.dx << ",+" << std::setw(5) << sprite.dy
                  << "  P" << std::setw(2) << (sprite.owner + 1)
                  << "  스프라이트 " << sprite.type << "\n";
    }
    for (std::size_t i = 0; i < clip->doodads.size(); ++i)
    {
        const auto & doodad = clip->doodads[i];
        std::cout << "  doodad   " << std::setw(4) << i
                  << "  +" << std::setw(5) << doodad.dx << ",+" << std::setw(5) << doodad.dy
                  << "  두들 " << doodad.type
                  << (doodad.enabled ? "" : "  [꺼짐]") << "\n";
    }
    for (std::size_t i = 0; i < clip->locations.size(); ++i)
    {
        const auto & location = clip->locations[i];
        std::cout << "  location " << std::setw(4) << i
                  << "  +" << location.dleft << ",+" << location.dtop
                  << " - +" << location.dright << ",+" << location.dbottom
                  << "  " << elevationFlagsText(location.elevationFlags)
                  << "  " << (location.name.empty() ? "(이름 없음)" : location.name) << "\n";
    }
    std::cout << "  모두 " << clip->count() << "개\n";
    return 0;
}

// --- 붙이기 ---

int objectPaste(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const Kinds kinds = readKinds(args);
    const auto ownerText = args.option("--owner");
    const auto installPath = args.option("--install");

    const std::string mapPath = args.at(0);
    const int tileX = static_cast<int>(args.integer(1));
    const int tileY = static_cast<int>(args.integer(2));
    const std::string inPath = args.at(3);

    const auto clip = readClipboard(inPath);
    if (!clip)
        return 1;

    // 두들은 타일을 함께 써야 해서 게임 자료가 필요하다.
    const bool needDoodads = kinds.doodads && !clip->doodads.empty();
    io::GameGraphics graphics;
    if (needDoodads)
    {
        if (!installPath)
            throw CliError("두들이 든 파일을 붙이려면 게임 자료가 필요합니다: "
                           "--install <StarCraft 설치폴더>  (빼고 붙이려면 --only unit,sprite,location)");
        std::string error;
        if (!graphics.load(*installPath, &error))
        {
            std::cerr << "게임 데이터 로드 실패: " << error << "\n";
            return 1;
        }
    }

    const std::optional<std::uint8_t> forcedOwner =
        ownerText ? std::optional<std::uint8_t>(parseOwner(*ownerText)) : std::nullopt;

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto info = archive.info();
        const int originX = tileX * kTilePixels;
        const int originY = tileY * kTilePixels;
        const int mapRight = static_cast<int>(info.tileWidth) * kTilePixels;
        const int mapBottom = static_cast<int>(info.tileHeight) * kTilePixels;

        const auto insideMap = [&](int x, int y) {
            return x >= 0 && x < mapRight && y >= 0 && y < mapBottom;
        };

        std::size_t clipped = 0;
        std::size_t placedUnits = 0, placedSprites = 0, placedDoodads = 0, placedLocations = 0;

        // 두들을 먼저 놓는다. 두들은 지형 타일도 바꾸므로, 나중에 놓으면
        // 위에 얹힌 유닛·스프라이트와 그리는 차례가 어긋난다.
        std::vector<io::RawSprite> overlaysBefore;
        if (needDoodads)
        {
            overlaysBefore = archive.sprites();
            const auto & catalogue = graphics.doodads(info.tilesetId);
            std::size_t disabledSkipped = 0;

            for (const auto & doodad : clip->doodads)
            {
                const int x = originX + doodad.dx;
                const int y = originY + doodad.dy;
                if (!insideMap(x, y)) { ++clipped; continue; }

                const auto found = std::find_if(catalogue.begin(), catalogue.end(),
                    [&](const auto & entry) { return entry.id == doodad.type; });
                if (found == catalogue.end())
                {
                    std::cerr << "이 타일셋에 없는 두들이라 건너뜁니다: " << doodad.type << "\n";
                    ++clipped;
                    continue;
                }

                // DD2 는 가운데 픽셀을 적고, placeDoodad 는 가운데 타일을
                // 받는다. 두 꼴을 섞으면 한 칸씩 밀린다.
                const int left = io::doodadOriginTile(x, found->tileWidth);
                const int top  = io::doodadOriginTile(y, found->tileHeight);
                const std::uint8_t owner = forcedOwner ? *forcedOwner : doodad.owner;

                if (auto r = archive.placeDoodad(graphics, doodad.type,
                                                 left + found->tileWidth / 2,
                                                 top + found->tileHeight / 2, owner); !r)
                {
                    std::cerr << "두들을 놓지 못했습니다(" << doodad.type << "): "
                              << r.message << "\n";
                    ++clipped;
                    continue;
                }
                if (!doodad.enabled)
                    ++disabledSkipped;
                ++placedDoodads;
            }

            if (disabledSkipped != 0)
                std::cout << "  알림      : 꺼져 있던 두들 " << disabledSkipped
                          << "개는 켜진 채로 붙습니다 (끄는 API 가 없습니다).\n";
        }

        // placeDoodad 는 움직이는 두들에 그림 조각을 하나 더 얹는다.
        // 파일에도 그 조각이 들어 있으면 두 번 놓이므로 걸러낸다.
        std::vector<io::RawSprite> autoOverlays;
        if (needDoodads)
        {
            const auto after = archive.sprites();
            if (after.size() > overlaysBefore.size())
            {
                autoOverlays.assign(after.begin() +
                                    static_cast<std::ptrdiff_t>(overlaysBefore.size()),
                                    after.end());
            }
        }

        if (kinds.units)
        {
            // 짝을 이으려면 새로 놓인 자리를 기억해 둬야 한다.
            std::vector<std::size_t> placedAt(clip->units.size(), std::size_t(-1));

            for (std::size_t i = 0; i < clip->units.size(); ++i)
            {
                const auto & unit = clip->units[i];
                const int x = originX + unit.dx;
                const int y = originY + unit.dy;
                if (!insideMap(x, y)) { ++clipped; continue; }

                const std::uint8_t owner = forcedOwner ? *forcedOwner : unit.owner;
                if (auto r = archive.addUnit(unit.type, owner,
                                             static_cast<std::uint16_t>(x),
                                             static_cast<std::uint16_t>(y)); !r)
                {
                    std::cerr << "유닛을 놓지 못했습니다: " << r.message << "\n";
                    return false;
                }

                const std::size_t index = archive.units().size() - 1;
                placedAt[i] = index;

                io::UnitProperties properties;
                properties.owner = owner;
                properties.hitpointPercent = unit.hitpointPercent;
                properties.shieldPercent = unit.shieldPercent;
                properties.energyPercent = unit.energyPercent;
                properties.resourceAmount = unit.resourceAmount;
                properties.hangarAmount = unit.hangarAmount;
                properties.stateFlags = unit.stateFlags;
                if (auto r = archive.setUnitProperties(index, properties); !r)
                {
                    std::cerr << "유닛 속성을 되살리지 못했습니다: " << r.message << "\n";
                    return false;
                }
                ++placedUnits;
            }

            // 애드온·나이더스를 다시 잇는다. 짝마다 한 번씩만.
            std::size_t relinked = 0;
            for (std::size_t i = 0; i < clip->units.size(); ++i)
            {
                const auto & unit = clip->units[i];
                if (unit.link < 0 || static_cast<std::size_t>(unit.link) <= i)
                    continue;
                const std::size_t a = placedAt[i];
                const std::size_t b = placedAt[static_cast<std::size_t>(unit.link)];
                if (a == std::size_t(-1) || b == std::size_t(-1))
                    continue; // 한쪽이 맵 밖이라 못 놓였다
                if (auto r = archive.linkUnits(a, b, unit.addonLink); !r)
                {
                    std::cerr << "연결을 되살리지 못했습니다: " << r.message << "\n";
                    return false;
                }
                ++relinked;
            }
            if (relinked != 0)
                std::cout << "  연결      : " << relinked << "쌍을 다시 이었습니다.\n";
        }

        if (kinds.sprites)
        {
            std::size_t skippedOverlays = 0;
            for (const auto & sprite : clip->sprites)
            {
                const int x = originX + sprite.dx;
                const int y = originY + sprite.dy;
                if (!insideMap(x, y)) { ++clipped; continue; }

                // 두들이 방금 얹어 준 조각과 같은 것이면 건너뛴다.
                const auto same = std::find_if(autoOverlays.begin(), autoOverlays.end(),
                    [&](const auto & made) {
                        return made.type == sprite.type && made.x == x && made.y == y;
                    });
                if (same != autoOverlays.end())
                {
                    autoOverlays.erase(same);
                    ++skippedOverlays;
                    continue;
                }

                const std::uint8_t owner = forcedOwner ? *forcedOwner : sprite.owner;
                if (auto r = archive.addSprite(sprite.type, owner,
                                               static_cast<std::uint16_t>(x),
                                               static_cast<std::uint16_t>(y),
                                               sprite.drawnAsSprite); !r)
                {
                    std::cerr << "스프라이트를 놓지 못했습니다: " << r.message << "\n";
                    return false;
                }
                ++placedSprites;
            }
            if (skippedOverlays != 0)
                std::cout << "  알림      : 두들이 스스로 얹는 그림 조각 "
                          << skippedOverlays << "개는 두 번 놓지 않았습니다.\n";
        }

        if (kinds.locations)
        {
            std::size_t full = 0;
            for (const auto & location : clip->locations)
            {
                const long long left   = originX + location.dleft;
                const long long top    = originY + location.dtop;
                const long long right  = originX + location.dright;
                const long long bottom = originY + location.dbottom;
                if (std::min(left, right) < 0 || std::max(left, right) > mapRight ||
                    std::min(top, bottom) < 0 || std::max(top, bottom) > mapBottom)
                {
                    ++clipped;
                    continue;
                }

                const std::size_t index = archive.addLocation(
                    static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top),
                    static_cast<std::uint32_t>(right), static_cast<std::uint32_t>(bottom),
                    location.name);
                if (index == 0)
                {
                    ++full;
                    continue;
                }
                if (location.elevationFlags != 0)
                {
                    if (auto r = archive.setLocationElevationFlags(
                            index, location.elevationFlags); !r)
                    {
                        std::cerr << "로케이션 높이를 되살리지 못했습니다: "
                                  << r.message << "\n";
                        return false;
                    }
                }
                ++placedLocations;
            }
            if (full != 0)
                std::cout << "  알림      : 로케이션 자리가 차서 " << full
                          << "개를 넣지 못했습니다 (255개까지).\n";
        }

        const std::size_t placed = placedUnits + placedSprites + placedDoodads + placedLocations;
        if (placed == 0)
        {
            std::cerr << "붙인 것이 없습니다.\n";
            return false;
        }

        std::cout << "  붙임      : @ 타일 (" << tileX << ", " << tileY << ")"
                  << "  유닛 " << placedUnits
                  << ", 스프라이트 " << placedSprites
                  << ", 두들 " << placedDoodads
                  << ", 로케이션 " << placedLocations << "\n";
        if (clipped != 0)
            std::cout << "  알림      : 맵 밖으로 나간 " << clipped << "개는 버렸습니다.\n";
        return true;
    });
}

} // namespace

std::vector<Group> clipboardGroups()
{
    return {
        Group{"object", "유닛·스프라이트·두들·로케이션을 네모째 오려 붙이기", {
            {"copy",  "<맵> <타일x> <타일y> <w> <h> <출력.objects> "
                      "[--units] [--sprites] [--doodads] [--locations]",
                      "네모 안의 것을 파일로 베낀다. 갈래를 안 고르면 전부. "
                      "가운데가 네모 안에 들어야 하고, 로케이션은 통째로 들어야 한다.",
                      objectCopy},
            {"paste", "<맵> <타일x> <타일y> <입력.objects> [--owner P] "
                      "[--only unit,sprite,doodad,location] [--install 설치폴더] -o <출력맵>",
                      "베낀 것을 붙인다. 애드온·나이더스 연결도 함께 되살린다. "
                      "두들이 들어 있으면 --install 이 필요하다.",
                      objectPaste},
            {"show",  "<입력.objects>", "오려 둔 파일 안을 보여 준다.", objectShow},
        }},
    };
}

} // namespace splash::cli
