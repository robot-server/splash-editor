// 맵 설정 — 맵 속성, 플레이어, 세력, 문자열, 스위치, CUWP,
// 유닛·업그레이드·기술 설정.

#include "cli_common.h"

#include "chk/map_document.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>

namespace splash::cli {

namespace {

/// 플레이어 12 칸짜리 표를 `1,3,5` 또는 `all`·`none` 으로 받는다.
std::array<bool, 12> parsePlayerTable(const std::string & text)
{
    std::array<bool, 12> table {};
    std::string lowered = text;
    for (char & c : lowered)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowered == "none")
        return table;
    if (lowered == "all")
    {
        table.fill(true);
        return table;
    }

    std::string current;
    const auto take = [&] {
        if (current.empty())
            return;
        const std::uint8_t player = parseOwner(current);
        table[player] = true;
        current.clear();
    };
    for (char c : text)
    {
        if (c == ',' || c == ' ' || c == '+')
            take();
        else
            current.push_back(c);
    }
    take();
    return table;
}

std::string playerTableText(const std::array<bool, 12> & table)
{
    std::string text;
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        if (!table[i])
            continue;
        if (!text.empty()) text += ",";
        text += std::to_string(i + 1);
    }
    return text.empty() ? "(없음)" : text;
}

/// 값이 바뀌었을 때만 표시를 올린다.
template <typename T, typename U>
void assign(T & field, U value, bool & changed)
{
    const auto next = static_cast<T>(value);
    if (field != next)
    {
        field = next;
        changed = true;
    }
}

// --- 맵 속성 ---

int mapInfo(Args & args)
{
    args.finish();
    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto info = archive.info();
        std::cout
            << "  이름      : " << (info.scenarioName.empty() ? "(없음)" : info.scenarioName) << "\n"
            << "  크기      : " << info.tileWidth << " x " << info.tileHeight << " 타일\n"
            << "  타일셋    : " << chk::tilesetDisplayName(info.tilesetId)
            << " (" << info.tilesetId << ")\n"
            << "  버전      : " << chk::versionDisplayName(info.versionId)
            << " (" << info.versionId << ")\n"
            << "  유닛      : " << info.unitCount << "\n"
            << "  로케이션  : " << info.locationCount << "\n"
            << "  트리거    : " << info.triggerCount << "\n"
            << "  문자열    : " << info.stringCount << "\n"
            << "  보호      : " << (info.isProtected ? "예" : "아니오") << "\n";
        if (!info.scenarioDescription.empty())
            std::cout << "  설명      : " << info.scenarioDescription << "\n";
        return 0;
    });
}

int mapName(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const std::string name = args.at(1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setScenarioName(name); !r)
        {
            std::cerr << "이름 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  이름      : " << name << "\n";
        return true;
    });
}

int mapDescription(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const std::string text = args.at(1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setScenarioDescription(text); !r)
        {
            std::cerr << "설명 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  설명      : " << text << "\n";
        return true;
    });
}

int mapSize(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto width = static_cast<std::uint16_t>(args.integer(1));
    const auto height = static_cast<std::uint16_t>(args.integer(2));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto before = archive.info();
        if (auto r = archive.setDimensions(width, height); !r)
        {
            std::cerr << "크기 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  크기      : " << before.tileWidth << "x" << before.tileHeight
                  << " -> " << width << "x" << height << "\n";
        if (width < before.tileWidth || height < before.tileHeight)
            std::cout << "  알림      : 줄였습니다. 맵 밖으로 나간 것은 "
                         "scenario clean-bounds 로 치웁니다.\n";
        return true;
    });
}

int mapTileset(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto tilesetId = static_cast<std::uint16_t>(args.integer(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setTileset(tilesetId); !r)
        {
            std::cerr << "타일셋 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  타일셋    : " << chk::tilesetDisplayName(tilesetId)
                  << " (" << tilesetId << ")\n";
        std::cout << "  알림      : 지형 타일 값은 그대로라 그림이 달라집니다.\n";
        return true;
    });
}

// --- 플레이어 ---

int playerList(Args & args)
{
    args.finish();
    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto settings = archive.playerSettings();
        const auto forces = archive.forceNames();
        for (std::size_t i = 0; i < settings.size(); ++i)
        {
            const auto & player = settings[i];
            std::cout << "  P" << std::setw(2) << (i + 1)
                      << "  " << std::setw(10) << raceName(player.race)
                      << "  " << std::setw(12) << slotTypeName(player.slotType);
            if (i < 8)
            {
                std::cout << "  세력 " << (player.force + 1);
                if (player.force < forces.size() && !forces[player.force].empty())
                    std::cout << "(" << forces[player.force] << ")";
                std::cout << "  " << playerColorName(player.color);
                if (player.remasteredColors && player.colorSetting == 2)
                    std::cout << " [직접 " << int(player.customRed) << ","
                              << int(player.customGreen) << "," << int(player.customBlue) << "]";
            }
            std::cout << "\n";
        }
        return 0;
    });
}

int playerSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto raceText = args.option("--race");
    const auto slotText = args.option("--slot");
    const auto forceNumber = args.number("--force");
    const auto colorText = args.option("--color");

    const std::string mapPath = args.at(0);
    const std::uint8_t player = parseOwner(args.at(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        auto settings = archive.playerSettings();
        if (player >= settings.size())
        {
            std::cerr << "플레이어 번호가 범위를 벗어났습니다.\n";
            return false;
        }

        auto setting = settings[player];
        bool changed = false;
        if (raceText)  assign(setting.race, parseRace(*raceText), changed);
        if (slotText)  assign(setting.slotType, parseSlotType(*slotText), changed);
        if (forceNumber)
        {
            if (*forceNumber < 1 || *forceNumber > 4)
                throw CliError("세력은 1~4 입니다.");
            assign(setting.force, *forceNumber - 1, changed);
        }
        if (colorText) assign(setting.color, parsePlayerColor(*colorText), changed);

        if (!(raceText || slotText || forceNumber || colorText))
            throw CliError("고칠 값을 옵션으로 주세요 (--race/--slot/--force/--color).");
        if (!changed)
        {
            // 옵션은 줬는데 이미 그 값이다. 스크립트가 여러 번 돌아도
            // 같은 결과가 되도록, 오류로 끊지 않고 그대로 저장한다.
            std::cout << "  바뀐 것 없음 (이미 그 값입니다)\n";
            return true;
        }
        if (auto r = archive.setPlayerSetting(player, setting); !r)
        {
            std::cerr << "플레이어 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  P" << (player + 1) << "  " << raceName(setting.race)
                  << "  " << slotTypeName(setting.slotType)
                  << "  세력 " << (setting.force + 1)
                  << "  " << playerColorName(setting.color) << "\n";
        return true;
    });
}

// --- 세력 ---

int forceList(Args & args)
{
    args.finish();
    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto forces = archive.forceSettings();
        for (std::size_t i = 0; i < forces.size(); ++i)
        {
            const auto & force = forces[i];
            std::cout << "  세력 " << (i + 1) << "  "
                      << (force.name.empty() ? "(이름 없음)" : force.name);
            if (force.alliedVictory)         std::cout << "  [동맹 승리]";
            if (force.sharedVision)          std::cout << "  [시야 공유]";
            if (force.randomAllies)          std::cout << "  [무작위 동맹]";
            if (force.randomizeStartLocation) std::cout << "  [시작 위치 섞기]";
            std::cout << "\n";
        }
        return 0;
    });
}

int forceSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto nameText = args.option("--name");
    const auto alliedVictory = args.toggle("--allied-victory");
    const auto sharedVision = args.toggle("--shared-vision");
    const auto randomAllies = args.toggle("--random-allies");
    const auto randomizeStart = args.toggle("--randomize-start");

    const std::string mapPath = args.at(0);
    const long long forceNumber = args.integer(1);
    if (forceNumber < 1 || forceNumber > 4)
        throw CliError("세력은 1~4 입니다.");
    const auto force = static_cast<std::size_t>(forceNumber - 1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        auto forces = archive.forceSettings();
        if (force >= forces.size())
        {
            std::cerr << "세력 번호가 범위를 벗어났습니다.\n";
            return false;
        }

        bool changed = false;
        if (nameText)
        {
            if (auto r = archive.setForceName(force, *nameText); !r)
            {
                std::cerr << "세력 이름 바꾸기 실패: " << r.message << "\n";
                return false;
            }
            changed = true;
        }

        auto setting = forces[force];
        bool flagsChanged = false;
        if (alliedVictory)  assign(setting.alliedVictory, *alliedVictory, flagsChanged);
        if (sharedVision)   assign(setting.sharedVision, *sharedVision, flagsChanged);
        if (randomAllies)   assign(setting.randomAllies, *randomAllies, flagsChanged);
        if (randomizeStart) assign(setting.randomizeStartLocation, *randomizeStart, flagsChanged);

        if (flagsChanged)
        {
            if (auto r = archive.setForceFlags(force, setting); !r)
            {
                std::cerr << "세력 설정 바꾸기 실패: " << r.message << "\n";
                return false;
            }
            changed = true;
        }

        if (!(nameText || alliedVictory || sharedVision || randomAllies || randomizeStart))
            throw CliError("고칠 값을 옵션으로 주세요 (--name 이나 깃발 옵션).");
        if (!changed)
        {
            // 옵션은 줬는데 이미 그 값이다. 스크립트가 여러 번 돌아도
            // 같은 결과가 되도록, 오류로 끊지 않고 그대로 저장한다.
            std::cout << "  바뀐 것 없음 (이미 그 값입니다)\n";
            return true;
        }

        const auto after = archive.forceSettings();
        const auto & shown = force < after.size() ? after[force] : setting;
        std::cout << "  세력 " << (force + 1) << "  "
                  << (shown.name.empty() ? "(이름 없음)" : shown.name)
                  << (shown.alliedVictory ? "  [동맹 승리]" : "")
                  << (shown.sharedVision ? "  [시야 공유]" : "")
                  << (shown.randomAllies ? "  [무작위 동맹]" : "")
                  << (shown.randomizeStartLocation ? "  [시작 위치 섞기]" : "") << "\n";
        return true;
    });
}

// --- 문자열 ---

int stringList(Args & args)
{
    const auto limit = args.number("--limit");
    const bool usedOnly = args.flag("--used");
    const auto needle = args.option("--find");
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto strings = archive.strings();
        std::size_t shown = 0;
        for (const auto & entry : strings)
        {
            if (usedOnly && !entry.used)
                continue;
            if (needle && entry.text.find(*needle) == std::string::npos)
                continue;
            if (limit && shown >= static_cast<std::size_t>(*limit))
                break;
            ++shown;

            // 줄바꿈이 든 문자열은 한 줄로 눕혀 보여 준다.
            std::string flat = entry.text;
            for (char & c : flat)
                if (c == '\n' || c == '\r') c = ' ';
            std::cout << "  " << std::setw(5) << entry.id
                      << (entry.used ? "  " : " *") << "  " << flat << "\n";
        }
        std::cout << "  문자열 " << strings.size() << "개";
        if (shown != strings.size())
            std::cout << ", " << shown << "개 보임";
        std::cout << "  (* 는 쓰이지 않는 것)\n";
        return 0;
    });
}

int stringSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto stringId = static_cast<std::size_t>(args.integer(1));
    const std::string text = args.at(2);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setString(stringId, text); !r)
        {
            std::cerr << "문자열 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  문자열 " << stringId << " -> " << text << "\n";
        return true;
    });
}

// --- 스위치 ---

int switchList(Args & args)
{
    const bool namedOnly = args.flag("--named");
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto names = archive.switchNames();
        std::size_t named = 0;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (!names[i].empty())
                ++named;
            else if (namedOnly)
                continue;
            std::cout << "  " << std::setw(4) << (i + 1) << "  "
                      << (names[i].empty() ? "(이름 없음)" : names[i]) << "\n";
        }
        std::cout << "  스위치 " << names.size() << "개 가운데 이름 붙은 것 " << named << "개\n";
        return 0;
    });
}

int switchName(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const long long number = args.integer(1);
    if (number < 1 || number > 256)
        throw CliError("스위치는 1~256 입니다.");
    const std::string name = args.at(2);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setSwitchName(static_cast<std::size_t>(number - 1), name); !r)
        {
            std::cerr << "스위치 이름 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  스위치 " << number << " -> " << name << "\n";
        return true;
    });
}

// --- CUWP (유닛 프리셋) ---

int presetList(Args & args)
{
    const bool usedOnly = args.flag("--used");
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto presets = archive.unitPresets();
        for (const auto & preset : presets)
        {
            if (usedOnly && !preset.used)
                continue;
            std::cout << "  " << std::setw(4) << preset.index << (preset.used ? "  " : " *");
            if (preset.setOwner)     std::cout << "  P" << (preset.owner + 1);
            if (preset.setHitpoints) std::cout << "  체력 " << int(preset.hitpointPercent) << "%";
            if (preset.setShields)   std::cout << "  방패 " << int(preset.shieldPercent) << "%";
            if (preset.setEnergy)    std::cout << "  마나 " << int(preset.energyPercent) << "%";
            if (preset.setResources) std::cout << "  자원 " << preset.resourceAmount;
            if (preset.setHangar)    std::cout << "  격납고 " << preset.hangarAmount;
            if (preset.cloaked)      std::cout << "  [은폐]";
            if (preset.burrowed)     std::cout << "  [버로우]";
            if (preset.inTransit)    std::cout << "  [떠 있음]";
            if (preset.hallucinated) std::cout << "  [환영]";
            if (preset.invincible)   std::cout << "  [무적]";
            std::cout << "\n";
        }
        std::cout << "  프리셋 " << presets.size() << "자리  (* 는 트리거가 쓰지 않는 것)\n";
        return 0;
    });
}

int presetSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto ownerText = args.option("--owner");
    const auto hp = args.number("--hp");
    const auto shield = args.number("--shield");
    const auto energy = args.number("--energy");
    const auto resource = args.number("--resource");
    const auto hangar = args.number("--hangar");
    const auto cloaked = args.toggle("--cloaked");
    const auto burrowed = args.toggle("--burrowed");
    const auto lifted = args.toggle("--lifted");
    const auto hallucinated = args.toggle("--hallucinated");
    const auto invincible = args.toggle("--invincible");
    const bool clear = args.flag("--clear");

    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const auto presets = archive.unitPresets();
        if (index >= presets.size())
        {
            std::cerr << "프리셋 번호가 범위를 벗어났습니다 (" << presets.size() << "자리)\n";
            return false;
        }

        auto preset = presets[index];
        bool changed = false;

        if (clear)
        {
            // 어떤 값도 정하지 않는 빈 프리셋으로 되돌린다.
            const std::size_t keep = preset.index;
            const bool used = preset.used;
            preset = io::MapArchive::UnitPreset{};
            preset.index = keep;
            preset.used = used;
            changed = true;
        }

        // 값을 주면 "이 값을 쓴다" 표시도 함께 켠다.
        if (ownerText)
        {
            assign(preset.owner, parseOwner(*ownerText), changed);
            assign(preset.setOwner, true, changed);
        }
        if (hp)       { assign(preset.hitpointPercent, *hp, changed);  assign(preset.setHitpoints, true, changed); }
        if (shield)   { assign(preset.shieldPercent, *shield, changed); assign(preset.setShields, true, changed); }
        if (energy)   { assign(preset.energyPercent, *energy, changed); assign(preset.setEnergy, true, changed); }
        if (resource) { assign(preset.resourceAmount, *resource, changed); assign(preset.setResources, true, changed); }
        if (hangar)   { assign(preset.hangarAmount, *hangar, changed);  assign(preset.setHangar, true, changed); }

        if (cloaked)      assign(preset.cloaked, *cloaked, changed);
        if (burrowed)     assign(preset.burrowed, *burrowed, changed);
        if (lifted)       assign(preset.inTransit, *lifted, changed);
        if (hallucinated) assign(preset.hallucinated, *hallucinated, changed);
        if (invincible)   assign(preset.invincible, *invincible, changed);

        if (!(clear || ownerText || hp || shield || energy || resource || hangar ||
            cloaked || burrowed || lifted || hallucinated || invincible))
            throw CliError("고칠 값을 옵션으로 주세요. 쓸 수 있는 옵션은 도움말에 있습니다.");
        if (!changed)
        {
            // 옵션은 줬는데 이미 그 값이다. 스크립트가 여러 번 돌아도
            // 같은 결과가 되도록, 오류로 끊지 않고 그대로 저장한다.
            std::cout << "  바뀐 것 없음 (이미 그 값입니다)\n";
            return true;
        }
        if (auto r = archive.setUnitPreset(index, preset); !r)
        {
            std::cerr << "프리셋 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  프리셋 " << index << " 을 고쳤습니다.\n";
        return true;
    });
}

// --- 유닛 설정 (UNIS/UNIx) ---

int unitdefGet(Args & args)
{
    args.finish();
    const std::uint16_t type = parseUnitType(args.at(1));

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto stats = archive.unitStats(type);
        if (!stats)
        {
            std::cerr << "유닛 설정을 읽지 못했습니다.\n";
            return 1;
        }
        std::cout << "  유닛      : " << io::unitTypeName(type) << " (" << type << ")\n"
                  << "  기본값    : " << (stats->useDefault ? "예" : "아니오") << "\n"
                  << "  체력      : " << (stats->hitpoints / 256) << " ("
                  << stats->hitpoints << " 내부 단위)\n"
                  << "  방패      : " << stats->shields << "\n"
                  << "  방어력    : " << int(stats->armor) << "\n"
                  << "  생산 시간 : " << stats->buildTime << " (1/15초)\n"
                  << "  미네랄    : " << stats->mineralCost << "\n"
                  << "  가스      : " << stats->gasCost << "\n"
                  << "  기본 생산 : " << (stats->defaultBuildable ? "가능" : "불가") << "\n"
                  << "  생산 가능 : " << playerTableText(stats->buildable) << "\n"
                  << "  기본 따름 : " << playerTableText(stats->playerUsesDefault) << "\n";
        return 0;
    });
}

int unitdefSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto useDefault = args.toggle("--default");
    const auto hitpoints = args.number("--hp");
    const auto shields = args.number("--shields");
    const auto armor = args.number("--armor");
    const auto buildTime = args.number("--build-time");
    const auto minerals = args.number("--minerals");
    const auto gas = args.number("--gas");
    const auto buildable = args.option("--buildable");
    const auto usesDefault = args.option("--uses-default");

    const std::string mapPath = args.at(0);
    const std::uint16_t type = parseUnitType(args.at(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        auto stats = archive.unitStats(type);
        if (!stats)
        {
            std::cerr << "유닛 설정을 읽지 못했습니다.\n";
            return false;
        }

        bool changed = false;
        if (useDefault) assign(stats->useDefault, *useDefault, changed);
        // 체력은 표시값 x 256 으로 저장된다.
        if (hitpoints)  assign(stats->hitpoints, *hitpoints * 256, changed);
        if (shields)    assign(stats->shields, *shields, changed);
        if (armor)      assign(stats->armor, *armor, changed);
        if (buildTime)  assign(stats->buildTime, *buildTime, changed);
        if (minerals)   assign(stats->mineralCost, *minerals, changed);
        if (gas)        assign(stats->gasCost, *gas, changed);
        if (buildable)
        {
            const auto table = parsePlayerTable(*buildable);
            if (stats->buildable != table) { stats->buildable = table; changed = true; }
        }
        if (usesDefault)
        {
            const auto table = parsePlayerTable(*usesDefault);
            if (stats->playerUsesDefault != table)
            {
                stats->playerUsesDefault = table;
                changed = true;
            }
        }

        // 값을 고쳤다면 기본값을 계속 쓸 수는 없다.
        if (changed && !useDefault && (hitpoints || shields || armor || buildTime ||
                                       minerals || gas))
        {
            assign(stats->useDefault, false, changed);
        }

        if (!(useDefault || hitpoints || shields || armor || buildTime || minerals || gas ||
            buildable || usesDefault))
            throw CliError("고칠 값을 옵션으로 주세요. 쓸 수 있는 옵션은 도움말에 있습니다.");
        if (!changed)
        {
            // 옵션은 줬는데 이미 그 값이다. 스크립트가 여러 번 돌아도
            // 같은 결과가 되도록, 오류로 끊지 않고 그대로 저장한다.
            std::cout << "  바뀐 것 없음 (이미 그 값입니다)\n";
            return true;
        }
        if (auto r = archive.setUnitStats(type, *stats); !r)
        {
            std::cerr << "유닛 설정 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  " << io::unitTypeName(type) << "  체력 " << (stats->hitpoints / 256)
                  << "  방패 " << stats->shields << "  방어 " << int(stats->armor)
                  << "  " << stats->mineralCost << "/" << stats->gasCost
                  << (stats->useDefault ? "  [기본값]" : "") << "\n";
        return true;
    });
}

// --- 업그레이드 ---

int upgradeList(Args & args)
{
    args.finish();
    for (std::uint16_t type = 0; type < io::upgradeTypeCount(); ++type)
        std::cout << "  " << std::setw(4) << type << "  " << io::upgradeTypeName(type) << "\n";
    std::cout << "  업그레이드 " << io::upgradeTypeCount() << "종\n";
    return 0;
}

int upgradeGet(Args & args)
{
    args.finish();
    const auto type = static_cast<std::uint16_t>(args.integer(1));

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto settings = archive.upgradeSettings(type);
        if (!settings)
        {
            std::cerr << "업그레이드 설정을 읽지 못했습니다.\n";
            return 1;
        }
        std::cout << "  업그레이드: " << io::upgradeTypeName(type) << " (" << type << ")\n"
                  << "  기본 비용 : " << (settings->useDefaultCosts ? "예" : "아니오") << "\n"
                  << "  미네랄    : " << settings->baseMineralCost
                  << " (+" << settings->mineralCostFactor << "/단계)\n"
                  << "  가스      : " << settings->baseGasCost
                  << " (+" << settings->gasCostFactor << "/단계)\n"
                  << "  연구 시간 : " << settings->baseResearchTime
                  << " (+" << settings->researchTimeFactor << "/단계, 1/15초)\n"
                  << "  기본 단계 : 시작 " << int(settings->defaultStartLevel)
                  << ", 최대 " << int(settings->defaultMaxLevel) << "\n";
        for (std::size_t player = 0; player < 12; ++player)
        {
            if (settings->playerUsesDefault[player])
                continue;
            std::cout << "  P" << (player + 1) << " 시작 "
                      << int(settings->startLevel[player]) << ", 최대 "
                      << int(settings->maxLevel[player]) << "\n";
        }
        return 0;
    });
}

int upgradeSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto useDefault = args.toggle("--default-costs");
    const auto minerals = args.number("--minerals");
    const auto mineralFactor = args.number("--mineral-factor");
    const auto gas = args.number("--gas");
    const auto gasFactor = args.number("--gas-factor");
    const auto time = args.number("--time");
    const auto timeFactor = args.number("--time-factor");
    const auto startLevel = args.number("--start-level");
    const auto maxLevel = args.number("--max-level");
    const auto playerText = args.option("--player");

    const std::string mapPath = args.at(0);
    const auto type = static_cast<std::uint16_t>(args.integer(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        auto settings = archive.upgradeSettings(type);
        if (!settings)
        {
            std::cerr << "업그레이드 설정을 읽지 못했습니다.\n";
            return false;
        }

        bool changed = false;
        if (useDefault)    assign(settings->useDefaultCosts, *useDefault, changed);
        if (minerals)      assign(settings->baseMineralCost, *minerals, changed);
        if (mineralFactor) assign(settings->mineralCostFactor, *mineralFactor, changed);
        if (gas)           assign(settings->baseGasCost, *gas, changed);
        if (gasFactor)     assign(settings->gasCostFactor, *gasFactor, changed);
        if (time)          assign(settings->baseResearchTime, *time, changed);
        if (timeFactor)    assign(settings->researchTimeFactor, *timeFactor, changed);

        if (playerText)
        {
            // 플레이어 하나만 다르게 두려면 기본값 따르기를 꺼야 한다.
            const std::uint8_t player = parseOwner(*playerText);
            if (startLevel) assign(settings->startLevel[player], *startLevel, changed);
            if (maxLevel)   assign(settings->maxLevel[player], *maxLevel, changed);
            if (startLevel || maxLevel)
                assign(settings->playerUsesDefault[player], false, changed);
        }
        else
        {
            if (startLevel) assign(settings->defaultStartLevel, *startLevel, changed);
            if (maxLevel)   assign(settings->defaultMaxLevel, *maxLevel, changed);
        }

        if (changed && !useDefault &&
            (minerals || mineralFactor || gas || gasFactor || time || timeFactor))
        {
            assign(settings->useDefaultCosts, false, changed);
        }

        if (!(useDefault || minerals || mineralFactor || gas || gasFactor || time ||
            timeFactor || startLevel || maxLevel))
            throw CliError("고칠 값을 옵션으로 주세요. 쓸 수 있는 옵션은 도움말에 있습니다.");
        if (!changed)
        {
            // 옵션은 줬는데 이미 그 값이다. 스크립트가 여러 번 돌아도
            // 같은 결과가 되도록, 오류로 끊지 않고 그대로 저장한다.
            std::cout << "  바뀐 것 없음 (이미 그 값입니다)\n";
            return true;
        }
        if (auto r = archive.setUpgradeSettings(type, *settings); !r)
        {
            std::cerr << "업그레이드 설정 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  " << io::upgradeTypeName(type)
                  << "  " << settings->baseMineralCost << "(+" << settings->mineralCostFactor << ")"
                  << " / " << settings->baseGasCost << "(+" << settings->gasCostFactor << ")"
                  << "  " << settings->baseResearchTime << "(+" << settings->researchTimeFactor << ")"
                  << "\n";
        return true;
    });
}

// --- 기술 ---

int techList(Args & args)
{
    args.finish();
    for (std::uint16_t type = 0; type < io::techTypeCount(); ++type)
        std::cout << "  " << std::setw(4) << type << "  " << io::techTypeName(type) << "\n";
    std::cout << "  기술 " << io::techTypeCount() << "종\n";
    return 0;
}

int techGet(Args & args)
{
    args.finish();
    const auto type = static_cast<std::uint16_t>(args.integer(1));

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto settings = archive.techSettings(type);
        if (!settings)
        {
            std::cerr << "기술 설정을 읽지 못했습니다.\n";
            return 1;
        }
        std::cout << "  기술      : " << io::techTypeName(type) << " (" << type << ")\n"
                  << "  기본 비용 : " << (settings->useDefaultCosts ? "예" : "아니오") << "\n"
                  << "  미네랄    : " << settings->mineralCost << "\n"
                  << "  가스      : " << settings->gasCost << "\n"
                  << "  연구 시간 : " << settings->researchTime << " (1/15초)\n"
                  << "  마나 소모 : " << settings->energyCost << "\n"
                  << "  기본 연구 : " << (settings->defaultAvailable ? "가능" : "불가")
                  << ", " << (settings->defaultResearched ? "이미 연구됨" : "안 됨") << "\n"
                  << "  연구 가능 : " << playerTableText(settings->available) << "\n"
                  << "  연구 완료 : " << playerTableText(settings->researched) << "\n"
                  << "  기본 따름 : " << playerTableText(settings->playerUsesDefault) << "\n";
        return 0;
    });
}

int techSet(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto useDefault = args.toggle("--default-costs");
    const auto minerals = args.number("--minerals");
    const auto gas = args.number("--gas");
    const auto time = args.number("--time");
    const auto energy = args.number("--energy");
    const auto available = args.option("--available");
    const auto researched = args.option("--researched");
    const auto usesDefault = args.option("--uses-default");

    const std::string mapPath = args.at(0);
    const auto type = static_cast<std::uint16_t>(args.integer(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        auto settings = archive.techSettings(type);
        if (!settings)
        {
            std::cerr << "기술 설정을 읽지 못했습니다.\n";
            return false;
        }

        bool changed = false;
        if (useDefault) assign(settings->useDefaultCosts, *useDefault, changed);
        if (minerals)   assign(settings->mineralCost, *minerals, changed);
        if (gas)        assign(settings->gasCost, *gas, changed);
        if (time)       assign(settings->researchTime, *time, changed);
        if (energy)     assign(settings->energyCost, *energy, changed);

        const auto applyTable = [&](const std::optional<std::string> & text,
                                    std::array<bool, 12> & field) {
            if (!text)
                return;
            const auto table = parsePlayerTable(*text);
            if (field != table) { field = table; changed = true; }
        };
        applyTable(available, settings->available);
        applyTable(researched, settings->researched);
        applyTable(usesDefault, settings->playerUsesDefault);

        if (changed && !useDefault && (minerals || gas || time || energy))
            assign(settings->useDefaultCosts, false, changed);

        if (!(useDefault || minerals || gas || time || energy || available || researched ||
            usesDefault))
            throw CliError("고칠 값을 옵션으로 주세요. 쓸 수 있는 옵션은 도움말에 있습니다.");
        if (!changed)
        {
            // 옵션은 줬는데 이미 그 값이다. 스크립트가 여러 번 돌아도
            // 같은 결과가 되도록, 오류로 끊지 않고 그대로 저장한다.
            std::cout << "  바뀐 것 없음 (이미 그 값입니다)\n";
            return true;
        }
        if (auto r = archive.setTechSettings(type, *settings); !r)
        {
            std::cerr << "기술 설정 바꾸기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  " << io::techTypeName(type)
                  << "  " << settings->mineralCost << "/" << settings->gasCost
                  << "  시간 " << settings->researchTime
                  << "  마나 " << settings->energyCost << "\n";
        return true;
    });
}

} // namespace

std::vector<Group> settingsGroups()
{
    return {
        Group{"map", "맵 속성 (SPRP·DIM·ERA)", {
            {"info",        "<맵>", "맵 메타데이터를 보여 준다.", mapInfo},
            {"name",        "<맵> <이름> -o <출력맵>", "시나리오 이름을 바꾼다.", mapName},
            {"description", "<맵> <설명> -o <출력맵>", "시나리오 설명을 바꾼다.", mapDescription},
            {"size",        "<맵> <가로> <세로> -o <출력맵>", "맵 크기를 바꾼다.", mapSize},
            {"tileset",     "<맵> <타일셋ID> -o <출력맵>", "타일셋을 바꾼다.", mapTileset},
        }},
        Group{"player", "플레이어 슬롯 (OWNR·SIDE·COLR)", {
            {"list", "<맵>", "플레이어 12칸을 보여 준다.", playerList},
            {"set",  "<맵> <플레이어1-12> [--race 종족] [--slot 슬롯] [--force 1-4] "
                     "[--color 색] -o <출력맵>", "플레이어 슬롯을 바꾼다.", playerSet},
        }},
        Group{"force", "세력 (FORC)", {
            {"list", "<맵>", "세력 넷을 보여 준다.", forceList},
            {"set",  "<맵> <세력1-4> [--name 이름] [--allied-victory on|off] "
                     "[--shared-vision on|off] [--random-allies on|off] "
                     "[--randomize-start on|off] -o <출력맵>", "세력을 바꾼다.", forceSet},
        }},
        Group{"string", "맵 문자열 (STR)", {
            {"list", "<맵> [--limit N] [--used] [--find 글자]", "문자열을 나열한다.", stringList},
            {"set",  "<맵> <번호> <텍스트> -o <출력맵>", "문자열을 바꾼다.", stringSet},
        }},
        Group{"switch", "스위치 (SWNM)", {
            {"list", "<맵> [--named]", "스위치 256개를 보여 준다.", switchList},
            {"name", "<맵> <번호1-256> <이름> -o <출력맵>", "스위치 이름을 붙인다.", switchName},
        }},
        Group{"preset", "유닛 프리셋 (CUWP)", {
            {"list", "<맵> [--used]", "프리셋 64자리를 보여 준다.", presetList},
            {"set",  "<맵> <번호> [--clear] [--owner P] [--hp %] [--shield %] [--energy %] "
                     "[--resource N] [--hangar N] [--cloaked on|off] [--burrowed on|off] "
                     "[--lifted on|off] [--hallucinated on|off] [--invincible on|off] -o <출력맵>",
                     "프리셋 하나를 정한다.", presetSet},
        }},
        Group{"unitdef", "맵이 정하는 유닛 능력치 (UNIS·UNIx)", {
            {"get", "<맵> <유닛>", "유닛 설정을 보여 준다.", unitdefGet},
            {"set", "<맵> <유닛> [--default on|off] [--hp N] [--shields N] [--armor N] "
                    "[--build-time N] [--minerals N] [--gas N] [--buildable 1,2|all|none] "
                    "[--uses-default 1,2|all|none] -o <출력맵>",
                    "유닛 설정을 바꾼다. 체력은 표시값으로 적는다.", unitdefSet},
        }},
        Group{"upgrade", "업그레이드 설정 (UPGS·UPGx)", {
            {"list", "", "업그레이드 번호와 이름을 나열한다.", upgradeList},
            {"get",  "<맵> <번호>", "업그레이드 설정을 보여 준다.", upgradeGet},
            {"set",  "<맵> <번호> [--default-costs on|off] [--minerals N] [--mineral-factor N] "
                     "[--gas N] [--gas-factor N] [--time N] [--time-factor N] "
                     "[--start-level N] [--max-level N] [--player P] -o <출력맵>",
                     "업그레이드 설정을 바꾼다. --player 를 주면 그 플레이어만.", upgradeSet},
        }},
        Group{"tech", "기술 설정 (TECS·TECx)", {
            {"list", "", "기술 번호와 이름을 나열한다.", techList},
            {"get",  "<맵> <번호>", "기술 설정을 보여 준다.", techGet},
            {"set",  "<맵> <번호> [--default-costs on|off] [--minerals N] [--gas N] [--time N] "
                     "[--energy N] [--available 1,2|all|none] [--researched 1,2|all|none] "
                     "[--uses-default 1,2|all|none] -o <출력맵>", "기술 설정을 바꾼다.", techSet},
        }},
    };
}

} // namespace splash::cli
