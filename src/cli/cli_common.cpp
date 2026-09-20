#include "cli_common.h"

#include "io/game_graphics.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace splash::cli {

namespace {

std::string lower(std::string text)
{
    for (char & c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// 쉼표·빈칸으로 끊는다. 빈 조각은 버린다.
std::vector<std::string> splitList(const std::string & text)
{
    std::vector<std::string> parts;
    std::string current;
    for (char c : text)
    {
        if (c == ',' || c == ' ' || c == '+' || c == '|')
        {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(c);
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

long long toNumber(const std::string & text, const std::string & what)
{
    try
    {
        std::size_t used = 0;
        // 0x 접두어를 받는다 — 타일 값과 플래그는 16진으로 적는 편이 읽기 쉽다.
        const long long value = std::stoll(text, &used, 0);
        if (used != text.size())
            throw std::invalid_argument("남은 글자");
        return value;
    }
    catch (const std::exception &)
    {
        throw CliError(what + "를 수로 읽지 못했습니다: " + text);
    }
}

} // namespace

// --- Args ---

Args::Args(std::vector<std::string> tokens)
    : tokens_(std::move(tokens))
{
    // `--` 뒤는 옵션처럼 생겼어도 값이다. 로케이션 이름이 `-` 로 시작하는
    // 경우가 실제로 있다.
    const auto it = std::find(tokens_.begin(), tokens_.end(), "--");
    if (it != tokens_.end())
    {
        fixedFrom_ = static_cast<std::size_t>(std::distance(tokens_.begin(), it));
        tokens_.erase(it);
    }
}

bool Args::looksLikeOption(std::size_t index) const
{
    if (index >= tokens_.size() || index >= fixedFrom_)
        return false;
    const std::string & token = tokens_[index];
    // 음수는 옵션이 아니다 — 로케이션을 왼쪽으로 옮길 때 -32 를 쓴다.
    return token.size() > 1 && token[0] == '-' &&
           !std::isdigit(static_cast<unsigned char>(token[1])) && token[1] != '.';
}

std::vector<std::string> Args::positionals() const
{
    std::vector<std::string> rest;
    for (std::size_t i = 0; i < tokens_.size(); ++i)
    {
        if (!looksLikeOption(i))
            rest.push_back(tokens_[i]);
    }
    return rest;
}

bool Args::flag(const std::string & name)
{
    for (std::size_t i = 0; i < tokens_.size(); ++i)
    {
        if (looksLikeOption(i) && tokens_[i] == name)
        {
            tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(i));
            if (fixedFrom_ != std::size_t(-1) && i < fixedFrom_)
                --fixedFrom_;
            return true;
        }
    }
    return false;
}

std::optional<std::string> Args::option(const std::string & name)
{
    const std::string prefix = name + "=";
    for (std::size_t i = 0; i < tokens_.size(); ++i)
    {
        if (!looksLikeOption(i))
            continue;

        const auto erase = [&](std::size_t count) {
            const auto first = tokens_.begin() + static_cast<std::ptrdiff_t>(i);
            tokens_.erase(first, first + static_cast<std::ptrdiff_t>(count));
            if (fixedFrom_ != std::size_t(-1) && i < fixedFrom_)
                fixedFrom_ -= count;
        };

        if (tokens_[i].rfind(prefix, 0) == 0)
        {
            std::string value = tokens_[i].substr(prefix.size());
            erase(1);
            return value;
        }
        if (tokens_[i] != name)
            continue;

        if (i + 1 >= tokens_.size())
            throw CliError(name + " 에 값이 필요합니다.");
        std::string value = tokens_[i + 1];
        erase(2);
        return value;
    }
    return std::nullopt;
}

std::size_t Args::count() const
{
    return positionals().size();
}

std::optional<long long> Args::number(const std::string & name)
{
    const auto text = option(name);
    if (!text)
        return std::nullopt;
    return toNumber(*text, name);
}

std::optional<bool> Args::toggle(const std::string & name)
{
    const auto text = option(name);
    if (!text)
        return std::nullopt;
    const std::string value = lower(*text);
    if (value == "on" || value == "true" || value == "yes" || value == "1" || value == "켬")
        return true;
    if (value == "off" || value == "false" || value == "no" || value == "0" || value == "끔")
        return false;
    throw CliError(name + " 는 on 이나 off 여야 합니다: " + *text);
}

const std::string & Args::at(std::size_t index) const
{
    std::size_t seen = 0;
    for (std::size_t i = 0; i < tokens_.size(); ++i)
    {
        if (looksLikeOption(i))
            continue;
        if (seen == index)
            return tokens_[i];
        ++seen;
    }
    throw CliError("인자가 모자랍니다.");
}

long long Args::integer(std::size_t index) const
{
    return toNumber(at(index), std::to_string(index + 1) + "번째 인자");
}

long long Args::integerOr(std::size_t index, long long fallback) const
{
    if (index >= count())
        return fallback;
    return integer(index);
}

std::vector<std::string> Args::from(std::size_t index) const
{
    const auto rest = positionals();
    if (index >= rest.size())
        return {};
    return std::vector<std::string>(rest.begin() + static_cast<std::ptrdiff_t>(index),
                                    rest.end());
}

void Args::finish() const
{
    for (std::size_t i = 0; i < tokens_.size(); ++i)
    {
        if (looksLikeOption(i))
            throw CliError("모르는 옵션입니다: " + tokens_[i]);
    }
}

// --- 저장 ---

SaveTarget takeSaveTarget(Args & args)
{
    auto out = args.option("-o");
    if (!out)
        out = args.option("--out");
    const bool inPlace = args.flag("--in-place");

    if (out && inPlace)
        throw CliError("-o 와 --in-place 는 함께 쓸 수 없습니다.");
    if (out)
        return SaveTarget{*out, false};
    if (inPlace)
        return SaveTarget{std::string(), true};

    throw CliError("저장할 곳을 정해야 합니다: -o <출력맵> 또는 --in-place");
}

int editMap(const std::string & mapPath, const SaveTarget & requested, Args & args,
            const std::function<bool(io::MapArchive &)> & body)
{
    SaveTarget target = requested;
    if (target.inPlace)
        target.path = mapPath;
    args.finish();

    io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }

    if (!body(archive))
        return 1;

    // 원본을 덮어쓸 때는 옆에 먼저 쓰고 바꿔치기한다. 쓰다 멈춰도 원본이
    // 남아 있어야 한다 — 맵은 다시 만들 수 없다.
    std::filesystem::path finalPath(target.path);
    std::filesystem::path writePath = finalPath;
    if (target.inPlace)
        writePath += ".splash-tmp";

    if (auto r = archive.saveAs(writePath.string()); !r)
    {
        std::cerr << "저장 실패: " << r.message << "\n";
        std::error_code ec;
        if (target.inPlace) std::filesystem::remove(writePath, ec);
        return 1;
    }

    if (!archive.lastSaveWarning().empty())
        std::cout << "  알림      : " << archive.lastSaveWarning() << "\n";

    // 저장본을 다시 열어 본다. 여기서 막히면 맵을 망가뜨린 것이다.
    {
        io::MapArchive reopened;
        if (auto r = reopened.open(writePath.string()); !r)
        {
            std::cerr << "저장본을 다시 열지 못했습니다: " << r.message << "\n";
            std::error_code ec;
            if (target.inPlace) std::filesystem::remove(writePath, ec);
            return 1;
        }
        const auto info = reopened.info();
        std::cout << "  다시 열기 : " << info.tileWidth << "x" << info.tileHeight
                  << ", 유닛 " << info.unitCount
                  << ", 로케이션 " << info.locationCount
                  << ", 트리거 " << info.triggerCount
                  << ", 문자열 " << info.stringCount << "\n";
    }

    if (target.inPlace)
    {
        std::error_code ec;
        std::filesystem::rename(writePath, finalPath, ec);
        if (ec)
        {
            std::cerr << "바꿔치기 실패: " << ec.message() << "\n";
            std::filesystem::remove(writePath, ec);
            return 1;
        }
    }

    std::cout << "  -> " << finalPath.string() << "\n";
    return 0;
}

int readMap(const std::string & mapPath,
            const std::function<int(io::MapArchive &)> & body)
{
    io::MapArchive archive;
    if (auto r = archive.open(mapPath); !r)
    {
        std::cerr << "열기 실패: " << r.message << "\n";
        return 1;
    }
    return body(archive);
}

bool loadGraphics(Args & args, io::GameGraphics & graphics)
{
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");

    std::string error;
    if (!graphics.load(*installPath, &error))
    {
        std::cerr << "게임 데이터 로드 실패: " << error << "\n";
        return false;
    }
    return true;
}

// --- 이름 조회 ---

std::uint16_t parseUnitType(const std::string & text)
{
    constexpr std::uint16_t kRealUnitTypes = 228;

    // 수로 적었으면 그대로.
    if (!text.empty() && std::isdigit(static_cast<unsigned char>(text[0])))
    {
        const long long value = toNumber(text, "유닛 번호");
        if (value < 0 || value >= kRealUnitTypes)
            throw CliError("유닛 번호는 0~" + std::to_string(kRealUnitTypes - 1) + " 입니다.");
        return static_cast<std::uint16_t>(value);
    }

    const std::string needle = lower(text);
    std::vector<std::uint16_t> exact;
    std::vector<std::uint16_t> partial;
    for (std::uint16_t type = 0; type < kRealUnitTypes; ++type)
    {
        const std::string name = lower(io::unitTypeName(type));
        if (name == needle)
            exact.push_back(type);
        else if (name.find(needle) != std::string::npos)
            partial.push_back(type);
    }

    if (exact.size() == 1)
        return exact.front();
    if (exact.empty() && partial.size() == 1)
        return partial.front();

    const auto & hits = exact.empty() ? partial : exact;
    if (hits.empty())
        throw CliError("그런 유닛이 없습니다: " + text);

    std::ostringstream why;
    why << "유닛 이름이 여럿 걸립니다 (" << hits.size() << "개): ";
    for (std::size_t i = 0; i < hits.size() && i < 8; ++i)
        why << (i ? ", " : "") << io::unitTypeName(hits[i]) << "(" << hits[i] << ")";
    if (hits.size() > 8) why << ", ...";
    throw CliError(why.str());
}

std::uint8_t parseOwner(const std::string & text)
{
    std::string digits = text;
    if (!digits.empty() && (digits[0] == 'p' || digits[0] == 'P'))
        digits.erase(digits.begin());

    const long long value = toNumber(digits, "플레이어 번호");
    if (value < 1 || value > 12)
        throw CliError("플레이어 번호는 1~12 입니다: " + text);
    return static_cast<std::uint8_t>(value - 1);
}

namespace {

struct NamedBit { std::uint16_t bit; const char * ko; const char * en; };

const NamedBit kElevationBits[6] {
    { 0x01, "저지대", "low" },
    { 0x02, "중지대", "medium" },
    { 0x04, "고지대", "high" },
    { 0x08, "저공",   "low-air" },
    { 0x10, "중공",   "medium-air" },
    { 0x20, "고공",   "high-air" },
};

} // namespace

std::uint16_t parseElevationFlags(const std::string & text)
{
    if (text.empty())
        return 0;
    if (lower(text) == "all")
        return 0x3F;
    if (lower(text) == "none")
        return 0;
    if (std::isdigit(static_cast<unsigned char>(text[0])))
        return static_cast<std::uint16_t>(toNumber(text, "고도 플래그"));

    std::uint16_t flags = 0;
    for (const auto & part : splitList(text))
    {
        const std::string want = lower(part);
        bool found = false;
        for (const auto & bit : kElevationBits)
        {
            if (want == lower(bit.ko) || want == bit.en)
            {
                flags = static_cast<std::uint16_t>(flags | bit.bit);
                found = true;
                break;
            }
        }
        if (!found)
            throw CliError("모르는 고도입니다: " + part +
                           " (저지대/중지대/고지대/저공/중공/고공)");
    }
    return flags;
}

std::string elevationFlagsText(std::uint16_t flags)
{
    if (flags == 0)
        return "(없음)";
    std::string text;
    for (const auto & bit : kElevationBits)
    {
        if ((flags & bit.bit) == 0)
            continue;
        if (!text.empty()) text += ",";
        text += bit.ko;
    }
    return text;
}

std::uint8_t parsePlayerBits(const std::string & text)
{
    const std::string value = lower(text);
    if (value == "all")  return 0xFF;
    if (value == "none") return 0x00;

    std::uint8_t bits = 0;
    for (const auto & part : splitList(text))
    {
        std::string digits = part;
        if (!digits.empty() && (digits[0] == 'p' || digits[0] == 'P'))
            digits.erase(digits.begin());
        const long long player = toNumber(digits, "플레이어 번호");
        if (player < 1 || player > 8)
            throw CliError("가리개는 플레이어 1~8 만 씁니다: " + part);
        bits = static_cast<std::uint8_t>(bits | (1u << (player - 1)));
    }
    return bits;
}

std::string playerBitsText(std::uint8_t bits)
{
    if (bits == 0)
        return "(없음)";
    std::string text;
    for (int player = 0; player < 8; ++player)
    {
        if ((bits & (1u << player)) == 0)
            continue;
        if (!text.empty()) text += ",";
        text += std::to_string(player + 1);
    }
    return text;
}

namespace {

struct NamedValue { std::uint8_t value; const char * ko; const char * en; };

// Chk::Race.
const NamedValue kRaces[8] {
    { 0, "저그",      "zerg" },
    { 1, "테란",      "terran" },
    { 2, "프로토스",  "protoss" },
    { 3, "독립",      "independent" },
    { 4, "중립",      "neutral" },
    { 5, "선택 가능", "userselect" },
    { 6, "무작위",    "random" },
    { 7, "사용 안 함","inactive" },
};

// Sc::Player::SlotType. 4 는 쓰이지 않는다.
const NamedValue kSlotTypes[8] {
    { 0, "사용 안 함",   "inactive" },
    { 1, "컴퓨터(게임)", "computergame" },
    { 2, "사람(게임)",   "humangame" },
    { 3, "구조 대상",    "rescuable" },
    { 5, "컴퓨터",       "computer" },
    { 6, "열림",         "open" },
    { 7, "중립",         "neutral" },
    { 8, "닫힘",         "closed" },
};

std::uint8_t parseNamed(const std::string & text, const NamedValue * table,
                        std::size_t count, const char * what)
{
    if (!text.empty() && std::isdigit(static_cast<unsigned char>(text[0])))
        return static_cast<std::uint8_t>(toNumber(text, what));

    const std::string want = lower(text);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (want == lower(table[i].ko) || want == table[i].en)
            return table[i].value;
    }

    std::ostringstream why;
    why << "모르는 " << what << " 입니다: " << text << " (";
    for (std::size_t i = 0; i < count; ++i)
        why << (i ? ", " : "") << table[i].ko;
    why << ")";
    throw CliError(why.str());
}

std::string namedText(std::uint8_t value, const NamedValue * table, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (table[i].value == value)
            return table[i].ko;
    }
    return std::to_string(value);
}

} // namespace

std::string raceName(std::uint8_t race)
{
    return namedText(race, kRaces, 8);
}

std::uint8_t parseRace(const std::string & text)
{
    return parseNamed(text, kRaces, 8, "종족");
}

std::string slotTypeName(std::uint8_t slotType)
{
    return namedText(slotType, kSlotTypes, 8);
}

std::uint8_t parseSlotType(const std::string & text)
{
    return parseNamed(text, kSlotTypes, 8, "슬롯");
}

std::string playerColorName(std::uint8_t color)
{
    for (const auto & entry : io::playerColors())
    {
        if (entry.value == color)
            return entry.name;
    }
    return std::to_string(color);
}

std::uint8_t parsePlayerColor(const std::string & text)
{
    if (!text.empty() && std::isdigit(static_cast<unsigned char>(text[0])))
        return static_cast<std::uint8_t>(toNumber(text, "색 번호"));

    const std::string want = lower(text);
    for (const auto & entry : io::playerColors())
    {
        if (lower(entry.name) == want)
            return entry.value;
    }
    throw CliError("모르는 색입니다: " + text);
}

// --- 갈래 표 ---

const std::vector<Group> & allGroups()
{
    static const std::vector<Group> kGroups = [] {
        std::vector<Group> groups;
        for (auto * maker : { objectGroups, clipboardGroups, terrainGroups,
                              settingsGroups, scenarioGroups, eudGroups })
        {
            auto part = maker();
            groups.insert(groups.end(), std::make_move_iterator(part.begin()),
                          std::make_move_iterator(part.end()));
        }
        return groups;
    }();
    return kGroups;
}

bool isGroupName(const std::string & name)
{
    const auto & groups = allGroups();
    return std::any_of(groups.begin(), groups.end(),
                       [&](const Group & g) { return name == g.name; });
}

namespace {

/// 표 모양을 맞추려고 이름 칸 너비를 재 둔다.
std::size_t widestGroupName()
{
    std::size_t widest = 0;
    for (const auto & group : allGroups())
        widest = std::max(widest, std::string(group.name).size());
    return widest;
}

} // namespace

void printGroupList(const std::string & argv0)
{
    std::cout << "갈래:\n";
    const std::size_t width = widestGroupName();
    for (const auto & group : allGroups())
    {
        std::cout << "  " << group.name
                  << std::string(width - std::string(group.name).size() + 2, ' ')
                  << group.summary << "\n";
    }
    std::cout << "\n  " << argv0 << " <갈래> 를 치면 그 갈래의 명령을 봅니다.\n"
              << "  " << argv0 << " help 로 전체를 봅니다.\n";
}

void printGroupHelp(const std::string & argv0, const Group & group)
{
    std::cout << group.name << " — " << group.summary << "\n\n";
    for (const auto & command : group.commands)
    {
        std::cout << "  " << argv0 << " " << group.name << " " << command.name;
        if (command.usage && *command.usage)
            std::cout << " " << command.usage;
        std::cout << "\n      " << command.summary << "\n";
    }
    std::cout << "\n  고치는 명령은 저장할 곳이 필요합니다: -o <출력맵> 또는 --in-place\n";
}

int runGroupCommand(const std::string & argv0, const std::vector<std::string> & args)
{
    if (args.empty())
        return kUnknownCommand;

    const auto & groups = allGroups();
    const auto group = std::find_if(groups.begin(), groups.end(),
                                    [&](const Group & g) { return args[0] == g.name; });
    if (group == groups.end())
        return kUnknownCommand;

    if (args.size() < 2 || args[1] == "help" || args[1] == "--help" || args[1] == "-h")
    {
        printGroupHelp(argv0, *group);
        return args.size() < 2 ? 2 : 0;
    }

    const auto command = std::find_if(group->commands.begin(), group->commands.end(),
                                      [&](const Command & c) { return args[1] == c.name; });
    if (command == group->commands.end())
    {
        std::cerr << group->name << " 에 그런 명령이 없습니다: " << args[1] << "\n\n";
        printGroupHelp(argv0, *group);
        return 2;
    }

    Args parsed(std::vector<std::string>(args.begin() + 2, args.end()));
    try
    {
        return command->run(parsed);
    }
    catch (const CliError & error)
    {
        std::cerr << error.what() << "\n\n";
        std::cerr << "사용법: " << argv0 << " " << group->name << " " << command->name;
        if (command->usage && *command->usage)
            std::cerr << " " << command->usage;
        std::cerr << "\n  " << command->summary << "\n";
        return 2;
    }
}

} // namespace splash::cli
