// 맵에 놓이는 것들 — 유닛, 스프라이트, 두들, 로케이션.

#include "cli_common.h"

#include "io/game_graphics.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace splash::cli {

namespace {

constexpr int kTilePixels = 32;

/// `--tiles` 를 주면 좌표를 타일로 읽고 그 한가운데 픽셀로 바꾼다.
std::uint16_t toPixels(long long value, bool tiles)
{
    const long long pixels = tiles ? value * kTilePixels + kTilePixels / 2 : value;
    if (pixels < 0 || pixels > 0xFFFF)
        throw CliError("좌표가 범위를 벗어났습니다: " + std::to_string(pixels));
    return static_cast<std::uint16_t>(pixels);
}

/// 목록에서 지울 번호들. 큰 번호부터 지워야 앞 번호가 밀리지 않는다.
std::vector<std::size_t> descendingIndices(const std::vector<std::string> & texts,
                                           std::size_t limit, const char * what)
{
    std::vector<std::size_t> indices;
    for (const auto & text : texts)
    {
        const long long value = std::stoll(text);
        if (value < 0 || static_cast<std::size_t>(value) >= limit)
            throw CliError(std::string(what) + " 번호가 범위를 벗어났습니다: " + text +
                           " (0~" + std::to_string(limit == 0 ? 0 : limit - 1) + ")");
        indices.push_back(static_cast<std::size_t>(value));
    }
    if (indices.empty())
        throw CliError(std::string(what) + " 번호를 하나 이상 적어야 합니다.");

    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

/// 애드온·나이더스로 이어진 짝의 자리. 없으면 -1.
///
/// 게임은 classId 로 짝을 찾는다. 자기 자신을 가리키는 것은 이어지지 않은
/// 유닛이다 (addUnit 이 relationClassId 에 제 번호를 넣어 둔다).
int linkedPartner(const std::vector<io::RawUnit> & units, std::size_t index)
{
    const auto & unit = units[index];
    if (unit.relationFlags == 0 || unit.relationClassId == unit.classId)
        return -1;
    if ((unit.relationFlags & 0x0600) == 0) // 나이더스(0x200) 도 애드온(0x400) 도 아니다
        return -1;

    for (std::size_t i = 0; i < units.size(); ++i)
    {
        if (i != index && units[i].classId != 0 && units[i].classId == unit.relationClassId)
            return static_cast<int>(i);
    }
    return -1;
}

std::string stateFlagsText(std::uint16_t flags)
{
    std::string text;
    const struct { std::uint16_t bit; const char * name; } kStates[5] {
        { 0x01, "은폐" }, { 0x02, "버로우" }, { 0x04, "떠 있음" },
        { 0x08, "환영" }, { 0x10, "무적" },
    };
    for (const auto & state : kStates)
    {
        if ((flags & state.bit) == 0)
            continue;
        if (!text.empty()) text += ",";
        text += state.name;
    }
    return text;
}

// --- 유닛 ---

int unitList(Args & args)
{
    const auto limit = args.number("--limit");
    const auto ownerFilter = args.option("--owner");
    const auto typeFilter = args.option("--type");
    args.finish();

    const std::uint8_t wantOwner = ownerFilter ? parseOwner(*ownerFilter) : 0;
    const std::uint16_t wantType = typeFilter ? parseUnitType(*typeFilter) : 0;

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto units = archive.units();
        const std::size_t cap = limit ? static_cast<std::size_t>(*limit) : units.size();

        std::size_t shown = 0;
        std::size_t matched = 0;
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            const auto & unit = units[i];
            if (ownerFilter && unit.owner != wantOwner) continue;
            if (typeFilter && unit.type != wantType)   continue;
            ++matched;
            if (shown >= cap) continue;
            ++shown;

            std::cout << "  " << std::setw(4) << i << "  "
                      << std::setw(5) << unit.x << "," << std::setw(5) << unit.y
                      << "  P" << std::setw(2) << (unit.owner + 1)
                      << "  " << io::unitTypeName(unit.type) << " (" << unit.type << ")";
            if (unit.resourceAmount != 0)
                std::cout << "  자원 " << unit.resourceAmount;
            const std::string states = stateFlagsText(unit.stateFlags);
            if (!states.empty())
                std::cout << "  [" << states << "]";

            // 이어진 짝을 보여 준다 — object copy/paste 가 연결을 지켰는지
            // 확인하려면 눈에 보여야 한다.
            if (const int partner = linkedPartner(units, i); partner >= 0)
            {
                std::cout << "  ["
                          << ((unit.relationFlags & 0x0400) != 0 ? "애드온" : "나이더스")
                          << " " << partner << "]";
            }
            std::cout << "\n";
        }

        std::cout << "  유닛 " << matched << "개";
        if (matched != units.size())
            std::cout << " (전체 " << units.size() << "개 가운데)";
        if (shown < matched)
            std::cout << ", " << shown << "개만 보임 (--limit)";
        std::cout << "\n";
        return 0;
    });
}

int unitPlace(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto ownerText = args.option("--owner");
    const bool tiles = args.flag("--tiles");
    const auto copies = args.number("--copies");

    const std::string mapPath = args.at(0);
    const std::uint16_t type = parseUnitType(args.at(1));
    const std::uint16_t x = toPixels(args.integer(2), tiles);
    const std::uint16_t y = toPixels(args.integer(3), tiles);
    const std::uint8_t owner = ownerText ? parseOwner(*ownerText) : 0;
    const long long count = copies ? *copies : 1;
    if (count < 1)
        throw CliError("--copies 는 1 이상이어야 합니다.");

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        for (long long i = 0; i < count; ++i)
        {
            if (auto r = archive.addUnit(type, owner, x, y); !r)
            {
                std::cerr << "놓기 실패: " << r.message << "\n";
                return false;
            }
        }
        std::cout << "  놓음      : " << io::unitTypeName(type) << " x" << count
                  << " @ (" << x << ", " << y << ") P" << (owner + 1) << "\n";
        return true;
    });
}

int unitRemove(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto texts = args.from(1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto indices = descendingIndices(texts, archive.units().size(), "유닛");
        for (std::size_t index : indices)
        {
            if (auto r = archive.removeUnit(index); !r)
            {
                std::cerr << "지우기 실패(" << index << "): " << r.message << "\n";
                return false;
            }
        }
        std::cout << "  지움      : 유닛 " << indices.size() << "개\n";
        return true;
    });
}

int unitMove(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const bool tiles = args.flag("--tiles");
    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));
    const std::uint16_t x = toPixels(args.integer(2), tiles);
    const std::uint16_t y = toPixels(args.integer(3), tiles);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto before = archive.units();
        if (index >= before.size())
        {
            std::cerr << "유닛 번호가 범위를 벗어났습니다 (유닛 " << before.size() << "개)\n";
            return false;
        }
        std::cout << "  이전 위치 : (" << before[index].x << ", " << before[index].y << ")  "
                  << io::unitTypeName(before[index].type) << "\n";
        if (auto r = archive.moveUnit(index, x, y); !r)
        {
            std::cerr << "이동 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  옮긴 위치 : (" << x << ", " << y << ")\n";
        return true;
    });
}

int unitSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto ownerText = args.option("--owner");
    const auto hp = args.number("--hp");
    const auto shield = args.number("--shield");
    const auto energy = args.number("--energy");
    const auto resource = args.number("--resource");
    const auto hangar = args.number("--hangar");

    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));

    // 상태 깃발은 editMap 안에서 현재 값을 읽어야 하므로 여기서 미리 꺼내
    // 두고, 적용은 아래에서 한다.
    struct StateOption { const char * name; std::uint16_t bit; std::optional<bool> value; };
    StateOption states[5] {
        { "--cloaked",      0x01, args.toggle("--cloaked") },
        { "--burrowed",     0x02, args.toggle("--burrowed") },
        { "--lifted",       0x04, args.toggle("--lifted") },
        { "--hallucinated", 0x08, args.toggle("--hallucinated") },
        { "--invincible",   0x10, args.toggle("--invincible") },
    };

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        auto properties = archive.unitProperties(index);
        if (!properties)
        {
            std::cerr << "유닛 번호가 범위를 벗어났습니다 (유닛 "
                      << archive.units().size() << "개)\n";
            return false;
        }

        bool changed = false;
        const auto assign = [&](auto & field, auto value) {
            using Field = std::remove_reference_t<decltype(field)>;
            const auto next = static_cast<Field>(value);
            if (field != next) { field = next; changed = true; }
        };

        if (ownerText) assign(properties->owner, parseOwner(*ownerText));
        if (hp)        assign(properties->hitpointPercent, *hp);
        if (shield)    assign(properties->shieldPercent, *shield);
        if (energy)    assign(properties->energyPercent, *energy);
        if (resource)  assign(properties->resourceAmount, *resource);
        if (hangar)    assign(properties->hangarAmount, *hangar);

        for (const auto & state : states)
        {
            if (!state.value)
                continue;
            const std::uint16_t next = *state.value
                ? static_cast<std::uint16_t>(properties->stateFlags | state.bit)
                : static_cast<std::uint16_t>(properties->stateFlags & ~state.bit);
            if (next != properties->stateFlags)
            {
                properties->stateFlags = next;
                changed = true;
            }
        }

        if (!(ownerText || hp || shield || energy || resource || hangar ||
            std::any_of(std::begin(states), std::end(states),
                        [](const StateOption & s) { return s.value.has_value(); })))
            throw CliError("고칠 값을 옵션으로 주세요. 쓸 수 있는 옵션은 도움말에 있습니다.");
        if (!changed)
        {
            // 옵션은 줬는데 이미 그 값이다. 스크립트가 여러 번 돌아도
            // 같은 결과가 되도록, 오류로 끊지 않고 그대로 저장한다.
            std::cout << "  바뀐 것 없음 (이미 그 값입니다)\n";
            return true;
        }

        if (auto r = archive.setUnitProperties(index, *properties); !r)
        {
            std::cerr << "속성 바꾸기 실패: " << r.message << "\n";
            return false;
        }

        std::cout << "  유닛 " << index << "  P" << (properties->owner + 1)
                  << "  체력 " << int(properties->hitpointPercent) << "%"
                  << "  방패 " << int(properties->shieldPercent) << "%"
                  << "  마나 " << int(properties->energyPercent) << "%";
        if (properties->resourceAmount != 0)
            std::cout << "  자원 " << properties->resourceAmount;
        const std::string text = stateFlagsText(properties->stateFlags);
        if (!text.empty())
            std::cout << "  [" << text << "]";
        std::cout << "\n";
        return true;
    });
}

int unitStack(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));
    const long long copies = args.integerOr(2, 1);
    if (copies < 1)
        throw CliError("몇 겹을 쌓을지는 1 이상이어야 합니다.");

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto units = archive.units();
        if (index >= units.size())
        {
            std::cerr << "유닛 번호가 범위를 벗어났습니다 (유닛 " << units.size() << "개)\n";
            return false;
        }
        const auto source = units[index];
        for (long long i = 0; i < copies; ++i)
        {
            if (auto r = archive.addUnit(source.type, source.owner, source.x, source.y); !r)
            {
                std::cerr << "쌓기 실패: " << r.message << "\n";
                return false;
            }
        }
        std::cout << "  쌓음      : " << io::unitTypeName(source.type)
                  << " @ (" << source.x << ", " << source.y << ") 에 " << copies << "겹 더\n";
        return true;
    });
}

int unitLink(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const bool nydus = args.flag("--nydus");
    const bool addon = args.flag("--addon") || !nydus;

    const std::string mapPath = args.at(0);
    const auto a = static_cast<std::size_t>(args.integer(1));
    const auto b = static_cast<std::size_t>(args.integer(2));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.linkUnits(a, b, addon); !r)
        {
            std::cerr << "잇기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  이음      : 유닛 " << a << " <-> " << b
                  << (addon ? " (애드온)" : " (나이더스)") << "\n";
        return true;
    });
}

int unitUnlink(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.unlinkUnit(index); !r)
        {
            std::cerr << "끊기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  끊음      : 유닛 " << index << "\n";
        return true;
    });
}

int unitTypes(Args & args)
{
    const auto needle = args.option("--find");
    args.finish();

    constexpr std::uint16_t kRealUnitTypes = 228;
    std::string lowered;
    if (needle)
    {
        lowered = *needle;
        for (char & c : lowered)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    std::size_t shown = 0;
    for (std::uint16_t type = 0; type < kRealUnitTypes; ++type)
    {
        std::string name = io::unitTypeName(type);
        if (needle)
        {
            std::string haystack = name;
            for (char & c : haystack)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (haystack.find(lowered) == std::string::npos)
                continue;
        }
        std::cout << "  " << std::setw(4) << type << "  " << name << "\n";
        ++shown;
    }
    std::cout << "  " << shown << "종\n";
    return 0;
}

// --- 스프라이트 ---

int spriteList(Args & args)
{
    const auto limit = args.number("--limit");
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto sprites = archive.sprites();
        const std::size_t cap = limit ? static_cast<std::size_t>(*limit) : sprites.size();
        for (std::size_t i = 0; i < sprites.size() && i < cap; ++i)
        {
            const auto & sprite = sprites[i];
            std::cout << "  " << std::setw(4) << i << "  "
                      << std::setw(5) << sprite.x << "," << std::setw(5) << sprite.y
                      << "  P" << std::setw(2) << (sprite.owner + 1)
                      << "  스프라이트 " << sprite.type
                      << (sprite.drawnAsSprite ? "  [스프라이트 그림]" : "  [유닛 그림]")
                      << "\n";
        }
        std::cout << "  스프라이트 " << sprites.size() << "개\n";
        return 0;
    });
}

int spritePlace(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto ownerText = args.option("--owner");
    const bool tiles = args.flag("--tiles");
    const bool asUnit = args.flag("--as-unit");

    const std::string mapPath = args.at(0);
    const auto type = static_cast<std::uint16_t>(args.integer(1));
    const std::uint16_t x = toPixels(args.integer(2), tiles);
    const std::uint16_t y = toPixels(args.integer(3), tiles);
    const std::uint8_t owner = ownerText ? parseOwner(*ownerText) : 0;

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.addSprite(type, owner, x, y, !asUnit); !r)
        {
            std::cerr << "놓기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  놓음      : 스프라이트 " << type << " @ (" << x << ", " << y
                  << ") P" << (owner + 1) << (asUnit ? " [유닛 그림]" : "") << "\n";
        return true;
    });
}

int spriteRemove(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto texts = args.from(1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto indices = descendingIndices(texts, archive.sprites().size(), "스프라이트");
        for (std::size_t index : indices)
        {
            if (auto r = archive.removeSprite(index); !r)
            {
                std::cerr << "지우기 실패(" << index << "): " << r.message << "\n";
                return false;
            }
        }
        std::cout << "  지움      : 스프라이트 " << indices.size() << "개\n";
        return true;
    });
}

int spriteSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto ownerText = args.option("--owner");
    const auto drawn = args.toggle("--as-sprite");
    const auto disabled = args.toggle("--disabled");

    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        auto properties = archive.spriteProperties(index);
        if (!properties)
        {
            std::cerr << "스프라이트 번호가 범위를 벗어났습니다 ("
                      << archive.sprites().size() << "개)\n";
            return false;
        }

        bool changed = false;
        if (ownerText)
        {
            const std::uint8_t owner = parseOwner(*ownerText);
            if (properties->owner != owner) { properties->owner = owner; changed = true; }
        }
        if (drawn && properties->drawnAsSprite != *drawn)
        {
            properties->drawnAsSprite = *drawn;
            changed = true;
        }
        if (disabled && properties->disabled != *disabled)
        {
            properties->disabled = *disabled;
            changed = true;
        }

        if (!(ownerText || drawn || disabled))
            throw CliError("고칠 값을 옵션으로 주세요. 쓸 수 있는 옵션은 도움말에 있습니다.");
        if (!changed)
        {
            // 옵션은 줬는데 이미 그 값이다. 스크립트가 여러 번 돌아도
            // 같은 결과가 되도록, 오류로 끊지 않고 그대로 저장한다.
            std::cout << "  바뀐 것 없음 (이미 그 값입니다)\n";
            return true;
        }
        if (auto r = archive.setSpriteProperties(index, *properties); !r)
        {
            std::cerr << "속성 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  스프라이트 " << index << "  P" << (properties->owner + 1)
                  << (properties->drawnAsSprite ? "  [스프라이트 그림]" : "  [유닛 그림]")
                  << (properties->disabled ? "  [꺼짐]" : "") << "\n";
        return true;
    });
}

// --- 두들 ---

int doodadList(Args & args)
{
    const bool catalogue = args.flag("--catalogue");
    io::GameGraphics graphics;
    const bool needGraphics = catalogue;
    if (needGraphics && !loadGraphics(args, graphics))
        return 1;
    // 목록만 볼 때도 이름을 붙이려면 자료가 필요하지만, 없어도 번호는 낸다.
    io::GameGraphics named;
    const bool haveNames = !needGraphics && [&] {
        try { return loadGraphics(args, named); }
        catch (const CliError &) { return false; }
    }();
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const std::uint16_t tileset = archive.info().tilesetId;

        if (catalogue)
        {
            const auto & known = graphics.doodads(tileset);
            for (const auto & doodad : known)
            {
                std::cout << "  " << std::setw(5) << doodad.id << "  "
                          << std::setw(2) << doodad.tileWidth << "x"
                          << std::setw(2) << doodad.tileHeight << "  "
                          << doodad.name << "\n";
            }
            std::cout << "  타일셋 " << tileset << " 의 두들 " << known.size() << "종\n";
            return 0;
        }

        const auto placed = archive.doodads();
        for (std::size_t i = 0; i < placed.size(); ++i)
        {
            const auto & doodad = placed[i];
            std::cout << "  " << std::setw(4) << i << "  ("
                      << doodad.x << ", " << doodad.y << ")  타일 ("
                      << (doodad.x / kTilePixels) << ", " << (doodad.y / kTilePixels) << ")"
                      << "  두들 " << doodad.type;
            if (haveNames)
            {
                const auto & known = named.doodads(tileset);
                const auto it = std::find_if(known.begin(), known.end(),
                    [&](const auto & entry) { return entry.id == doodad.type; });
                if (it != known.end())
                    std::cout << "  " << it->name;
            }
            std::cout << "  P" << (doodad.owner + 1)
                      << (doodad.enabled ? "" : "  [꺼짐]") << "\n";
        }
        std::cout << "  두들 " << placed.size() << "개\n";
        return 0;
    });
}

int doodadPlace(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    io::GameGraphics graphics;
    if (!loadGraphics(args, graphics))
        return 1;
    const auto ownerText = args.option("--owner");

    const std::string mapPath = args.at(0);
    const auto doodadId = static_cast<std::uint16_t>(args.integer(1));
    const int tileX = static_cast<int>(args.integer(2));
    const int tileY = static_cast<int>(args.integer(3));
    const std::uint8_t owner = ownerText ? parseOwner(*ownerText) : 0;

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.placeDoodad(graphics, doodadId, tileX, tileY, owner); !r)
        {
            std::cerr << "놓기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  놓음      : 두들 " << doodadId
                  << " @ 타일 (" << tileX << ", " << tileY << ")\n";
        return true;
    });
}

int doodadRemove(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    io::GameGraphics graphics;
    if (!loadGraphics(args, graphics))
        return 1;

    const std::string mapPath = args.at(0);
    const auto texts = args.from(1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto indices = descendingIndices(texts, archive.doodads().size(), "두들");
        for (std::size_t index : indices)
        {
            if (auto r = archive.removeDoodad(graphics, index); !r)
            {
                std::cerr << "지우기 실패(" << index << "): " << r.message << "\n";
                return false;
            }
        }
        std::cout << "  지움      : 두들 " << indices.size() << "개\n";
        return true;
    });
}

int doodadToTerrain(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    io::GameGraphics graphics;
    if (!loadGraphics(args, graphics))
        return 1;

    const std::string mapPath = args.at(0);
    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const std::size_t count = archive.convertDoodadsToTerrain(graphics);
        std::cout << "  풀었음    : 두들 " << count << "개를 지형으로\n";
        return true;
    });
}

int doodadCheck(Args & args)
{
    io::GameGraphics graphics;
    if (!loadGraphics(args, graphics))
        return 1;
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto broken = archive.findBrokenDoodads(graphics);
        const auto placed = archive.doodads();
        for (std::size_t index : broken)
        {
            std::cout << "  " << std::setw(4) << index;
            if (index < placed.size())
                std::cout << "  타일 (" << (placed[index].x / kTilePixels) << ", "
                          << (placed[index].y / kTilePixels) << ")  두들 "
                          << placed[index].type;
            std::cout << "\n";
        }
        std::cout << "  어긋난 두들 " << broken.size() << "개 / 전체 " << placed.size() << "개\n";
        return broken.empty() ? 0 : 1;
    });
}

int doodadRepair(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    io::GameGraphics graphics;
    if (!loadGraphics(args, graphics))
        return 1;

    const std::string mapPath = args.at(0);
    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const std::size_t count = archive.repairDoodads(graphics);
        std::cout << "  고침      : 두들 " << count << "개\n";
        return true;
    });
}

// --- 로케이션 ---

int locationList(Args & args)
{
    args.finish();
    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto locations = archive.locations();
        for (const auto & location : locations)
        {
            const bool inverted = location.left > location.right ||
                                  location.top > location.bottom;
            std::cout << "  " << std::setw(4) << location.index << "  ("
                      << location.left << ", " << location.top << ") - ("
                      << location.right << ", " << location.bottom << ")"
                      << (inverted ? "  [안팎 뒤집힘]" : "")
                      << "  " << elevationFlagsText(location.elevationFlags)
                      << "  " << (location.name.empty() ? "(이름 없음)" : location.name) << "\n";
        }
        std::cout << "  로케이션 " << locations.size() << "개\n";
        return 0;
    });
}

int locationAdd(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto name = args.option("--name");
    const bool tiles = args.flag("--tiles");

    const std::string mapPath = args.at(0);
    const auto left   = static_cast<std::uint32_t>(args.integer(1) * (tiles ? kTilePixels : 1));
    const auto top    = static_cast<std::uint32_t>(args.integer(2) * (tiles ? kTilePixels : 1));
    const auto right  = static_cast<std::uint32_t>(args.integer(3) * (tiles ? kTilePixels : 1));
    const auto bottom = static_cast<std::uint32_t>(args.integer(4) * (tiles ? kTilePixels : 1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const std::size_t index = archive.addLocation(left, top, right, bottom,
                                                      name ? *name : std::string());
        if (index == 0)
        {
            std::cerr << "만들기 실패: 빈 자리가 없습니다 (로케이션은 255 개까지).\n";
            return false;
        }
        std::cout << "  만듦      : 로케이션 " << index << "  ("
                  << left << ", " << top << ") - (" << right << ", " << bottom << ")"
                  << (name ? "  " + *name : "") << "\n";
        return true;
    });
}

int locationRemove(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const bool force = args.flag("--force");
    const std::string mapPath = args.at(0);
    const auto texts = args.from(1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        std::vector<std::size_t> indices;
        for (const auto & text : texts)
            indices.push_back(static_cast<std::size_t>(std::stoll(text)));
        if (indices.empty())
            throw CliError("로케이션 번호를 하나 이상 적어야 합니다.");

        for (std::size_t index : indices)
        {
            if (auto r = archive.removeLocation(index, force); !r)
            {
                std::cerr << "지우기 실패(" << index << "): " << r.message << "\n";
                if (!force)
                    std::cerr << "  트리거가 쓰고 있다면 --force 로 밀어붙일 수 있습니다.\n";
                return false;
            }
        }
        std::cout << "  지움      : 로케이션 " << indices.size() << "개\n";
        return true;
    });
}

int locationName(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));
    const std::string name = args.at(2);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setLocationName(index, name); !r)
        {
            std::cerr << "이름 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  로케이션 " << index << " -> " << name << "\n";
        return true;
    });
}

int locationBounds(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const bool tiles = args.flag("--tiles");
    const std::string mapPath = args.at(0);
    const auto index  = static_cast<std::size_t>(args.integer(1));
    const auto left   = static_cast<std::uint32_t>(args.integer(2) * (tiles ? kTilePixels : 1));
    const auto top    = static_cast<std::uint32_t>(args.integer(3) * (tiles ? kTilePixels : 1));
    const auto right  = static_cast<std::uint32_t>(args.integer(4) * (tiles ? kTilePixels : 1));
    const auto bottom = static_cast<std::uint32_t>(args.integer(5) * (tiles ? kTilePixels : 1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setLocationBounds(index, left, top, right, bottom); !r)
        {
            std::cerr << "크기 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  로케이션 " << index << " -> (" << left << ", " << top
                  << ") - (" << right << ", " << bottom << ")\n";
        return true;
    });
}

int locationMove(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const bool tiles = args.flag("--tiles");
    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));
    const std::int64_t dx = args.integer(2) * (tiles ? kTilePixels : 1);
    const std::int64_t dy = args.integer(3) * (tiles ? kTilePixels : 1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto locations = archive.locations();
        const auto it = std::find_if(locations.begin(), locations.end(),
            [&](const auto & entry) { return entry.index == index; });
        if (it == locations.end())
        {
            std::cerr << "그런 로케이션이 없습니다: " << index << "\n";
            return false;
        }

        // 좌표는 부호 없는 값이라 음수로 내려가면 거대한 값이 된다.
        // 맵 안에 머물도록 잘라 낸다.
        const auto info = archive.info();
        const std::int64_t left   = std::min(it->left, it->right);
        const std::int64_t top    = std::min(it->top, it->bottom);
        const std::int64_t width  = std::max(it->left, it->right) - left;
        const std::int64_t height = std::max(it->top, it->bottom) - top;
        const std::int64_t maxX = static_cast<std::int64_t>(info.tileWidth) * kTilePixels - width;
        const std::int64_t maxY = static_cast<std::int64_t>(info.tileHeight) * kTilePixels - height;

        const std::int64_t newLeft = std::clamp<std::int64_t>(
            left + dx, 0, std::max<std::int64_t>(0, maxX));
        const std::int64_t newTop = std::clamp<std::int64_t>(
            top + dy, 0, std::max<std::int64_t>(0, maxY));

        // 뒤집힌 로케이션은 뒤집힌 채로 옮긴다.
        const bool invertedX = it->left > it->right;
        const bool invertedY = it->top > it->bottom;
        const auto outLeft   = static_cast<std::uint32_t>(invertedX ? newLeft + width : newLeft);
        const auto outRight  = static_cast<std::uint32_t>(invertedX ? newLeft : newLeft + width);
        const auto outTop    = static_cast<std::uint32_t>(invertedY ? newTop + height : newTop);
        const auto outBottom = static_cast<std::uint32_t>(invertedY ? newTop : newTop + height);

        if (auto r = archive.setLocationBounds(index, outLeft, outTop, outRight, outBottom); !r)
        {
            std::cerr << "옮기기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  로케이션 " << index << " -> (" << outLeft << ", " << outTop
                  << ") - (" << outRight << ", " << outBottom << ")\n";
        return true;
    });
}

int locationElevation(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));
    const std::uint16_t flags = parseElevationFlags(args.at(2));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setLocationElevationFlags(index, flags); !r)
        {
            std::cerr << "높이 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  로케이션 " << index << " -> " << elevationFlagsText(flags) << "\n";
        return true;
    });
}

int locationInvert(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));

    // 값을 안 주면 뒤집는다.
    std::optional<bool> want;
    if (args.count() > 2)
    {
        const std::string text = args.at(2);
        want = (text == "on" || text == "true" || text == "1");
        if (!*want && !(text == "off" || text == "false" || text == "0"))
            throw CliError("on 이나 off 를 적으세요: " + text);
    }

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto locations = archive.locations();
        const auto it = std::find_if(locations.begin(), locations.end(),
            [&](const auto & entry) { return entry.index == index; });
        if (it == locations.end())
        {
            std::cerr << "그런 로케이션이 없습니다: " << index << "\n";
            return false;
        }

        const bool inverted = it->left > it->right;
        const bool target = want ? *want : !inverted;
        if (target == inverted)
        {
            std::cout << "  로케이션 " << index << " 은 이미 "
                      << (inverted ? "뒤집혀" : "바로") << " 있습니다.\n";
            return false;
        }

        // 좌우를 맞바꾸면 게임이 "이 네모 바깥" 으로 읽는다. 위아래는
        // 그대로 둔다 — GUI 와 같은 방식이다.
        const std::uint32_t left  = target ? std::max(it->left, it->right)
                                           : std::min(it->left, it->right);
        const std::uint32_t right = target ? std::min(it->left, it->right)
                                           : std::max(it->left, it->right);
        if (auto r = archive.setLocationBounds(index, left, it->top, right, it->bottom); !r)
        {
            std::cerr << "뒤집기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  로케이션 " << index << " -> "
                  << (target ? "안팎 뒤집힘" : "보통") << "\n";
        return true;
    });
}

} // namespace

std::vector<Group> objectGroups()
{
    return {
        Group{"unit", "맵에 놓인 유닛", {
            {"list",   "<맵> [--limit N] [--owner P] [--type 이름]", "유닛을 나열한다.", unitList},
            {"place",  "<맵> <유닛> <x> <y> [--owner P] [--tiles] [--copies N] -o <출력맵>",
                       "유닛을 놓는다. 유닛은 번호나 이름으로.", unitPlace},
            {"remove", "<맵> <번호...> -o <출력맵>", "유닛을 지운다.", unitRemove},
            {"move",   "<맵> <번호> <x> <y> [--tiles] -o <출력맵>", "유닛을 옮긴다.", unitMove},
            {"set",    "<맵> <번호> [--owner P] [--hp %] [--shield %] [--energy %] "
                       "[--resource N] [--hangar N] [--cloaked on|off] [--burrowed on|off] "
                       "[--lifted on|off] [--hallucinated on|off] [--invincible on|off] -o <출력맵>",
                       "유닛 속성을 바꾼다.", unitSet},
            {"stack",  "<맵> <번호> [겹수] -o <출력맵>", "같은 자리에 유닛을 더 쌓는다.", unitStack},
            {"link",   "<맵> <번호A> <번호B> [--addon|--nydus] -o <출력맵>",
                       "애드온·나이더스를 잇는다.", unitLink},
            {"unlink", "<맵> <번호> -o <출력맵>", "이어진 것을 끊는다.", unitUnlink},
            {"types",  "[--find 글자]", "유닛 번호와 이름을 나열한다.", unitTypes},
        }},
        Group{"sprite", "맵에 배치된 스프라이트 (THG2)", {
            {"list",   "<맵> [--limit N]", "스프라이트를 나열한다.", spriteList},
            {"place",  "<맵> <번호> <x> <y> [--owner P] [--tiles] [--as-unit] -o <출력맵>",
                       "스프라이트를 놓는다.", spritePlace},
            {"remove", "<맵> <번호...> -o <출력맵>", "스프라이트를 지운다.", spriteRemove},
            {"set",    "<맵> <번호> [--owner P] [--as-sprite on|off] [--disabled on|off] -o <출력맵>",
                       "스프라이트 속성을 바꾼다.", spriteSet},
        }},
        Group{"doodad", "지형에 얹는 두들 (DD2)", {
            {"list",       "<맵> [--install 설치폴더] [--catalogue]",
                           "맵에 놓인 두들을, --catalogue 면 타일셋의 두들 종류를 나열한다.", doodadList},
            {"place",      "<맵> <두들번호> <타일x> <타일y> [--owner P] --install 설치폴더 -o <출력맵>",
                           "두들을 놓는다.", doodadPlace},
            {"remove",     "<맵> <번호...> --install 설치폴더 -o <출력맵>", "두들을 지운다.", doodadRemove},
            {"to-terrain", "<맵> --install 설치폴더 -o <출력맵>",
                           "두들 항목을 지우고 지형만 남긴다.", doodadToTerrain},
            {"check",      "<맵> --install 설치폴더", "자리와 어긋난 두들을 찾는다.", doodadCheck},
            {"repair",     "<맵> --install 설치폴더 -o <출력맵>", "어긋난 두들을 고친다.", doodadRepair},
        }},
        Group{"location", "로케이션 (MRGN)", {
            {"list",      "<맵>", "로케이션을 나열한다.", locationList},
            {"add",       "<맵> <left> <top> <right> <bottom> [--name 이름] [--tiles] -o <출력맵>",
                          "로케이션을 만든다.", locationAdd},
            {"remove",    "<맵> <번호...> [--force] -o <출력맵>", "로케이션을 지운다.", locationRemove},
            {"name",      "<맵> <번호> <이름> -o <출력맵>", "이름을 바꾼다.", locationName},
            {"bounds",    "<맵> <번호> <left> <top> <right> <bottom> [--tiles] -o <출력맵>",
                          "네 모서리를 정한다.", locationBounds},
            {"move",      "<맵> <번호> <dx> <dy> [--tiles] -o <출력맵>", "크기를 두고 옮긴다.", locationMove},
            {"elevation", "<맵> <번호> <저지대,중지대,고지대,저공,중공,고공|all|none> -o <출력맵>",
                          "높이 조건을 정한다.", locationElevation},
            {"invert",    "<맵> <번호> [on|off] -o <출력맵>",
                          "안팎을 뒤집는다 (\"이 네모 바깥\").", locationInvert},
        }},
    };
}

} // namespace splash::cli
