// EUD — 메모리 자리 셈, 오프셋 표, 맵 안 EUD 훑기, euddraft 빌드.
//
// GUI 에서 할 수 있는 EUD 일은 여기서도 다 된다. 화면 없이 확인하려면
// 이쪽이 유일한 길이고, 실제 맵으로 재 보는 일도 여기서 한다.

#include "cli_common.h"

#include "io/eud.h"
#include "io/euddraft.h"
#include "io/game_graphics.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace splash::cli {

namespace {

namespace eud = io::eud;

/// `--db <api.json>` 을 받으면 바깥 오프셋 표를 읽어 둔다.
///
/// EUD Book 의 api.json 은 라이선스가 밝혀져 있지 않아 저장소에 넣지
/// 않는다. 쓰려는 사람이 직접 받아 가리킨다.
void takeOffsetDatabase(Args & args)
{
    const auto path = args.option("--db");
    if (!path)
    {
        // 명령마다 --db 를 적지 않아도 되게 환경 변수를 본다.
        // 못 읽으면 조용히 지나간다 — 붙박이 표만으로도 대부분 된다.
        if (!eud::hasOffsetDatabase())
            eud::loadOffsetDatabaseFromEnvironment();
        return;
    }

    std::string error;
    if (!eud::loadOffsetDatabase(*path, &error))
        throw CliError("오프셋 표를 읽지 못했습니다: " + error);

    std::cout << "  오프셋 표  : " << *path << " (" << eud::offsets().size()
              << "개 자리)\n";
}

/// 여러 줄짜리 설명을 한 줄로 눌러 담는다. 표 설명은 꽤 길 수 있다.
std::string oneLine(const std::string & text, std::size_t limit = 220)
{
    std::string out;
    bool space = false;
    for (const char c : text)
    {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ')
        {
            space = !out.empty();
            continue;
        }
        if (space)
        {
            out.push_back(' ');
            space = false;
        }
        out.push_back(c);
    }
    if (out.size() > limit)
        out = out.substr(0, limit) + "…";
    return out;
}

std::string hex(std::uint32_t value, int width = 8)
{
    std::ostringstream out;
    out << "0x" << std::uppercase << std::setfill('0') << std::setw(width)
        << std::hex << value;
    return out.str();
}

std::uint32_t parseAddressOrThrow(const std::string & text)
{
    // 이름으로도 받는다 — `eud addr 미네랄` 이 되면 손이 덜 간다.
    if (const auto address = eud::parseAddress(text))
        return *address;

    const auto found = eud::findOffsets(text);
    if (found.size() == 1)
        return found.front()->address;
    if (found.size() > 1)
    {
        std::ostringstream why;
        why << "'" << text << "' 에 걸리는 자리가 " << found.size() << "개입니다. ";
        why << "`eud offsets " << text << "` 로 좁혀 보세요.";
        throw CliError(why.str());
    }
    throw CliError("주소로 읽을 수 없습니다: " + text);
}

/// 비교 이름을 Chk::Condition::Comparison 으로.
std::uint8_t parseComparison(const std::string & text)
{
    std::string key;
    for (const char c : text)
    {
        if (c != ' ' && c != '-' && c != '_')
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (key == "atleast" || key == "이상" || key == ">=") return 0;
    if (key == "atmost"  || key == "이하" || key == "<=") return 1;
    if (key == "exactly" || key == "같음" || key == "==" || key == "=") return 10;
    if (key == "set"     || key == "켜짐") return 2;
    if (key == "notset"  || key == "꺼짐") return 3;

    try
    {
        return static_cast<std::uint8_t>(std::stoul(key));
    }
    catch (const std::exception &)
    {
        throw CliError("비교를 알 수 없습니다: " + text +
                       " (atleast·atmost·exactly 또는 수)");
    }
}

/// 수정 방식을 Chk::Trigger::ValueModifier 로.
std::uint8_t parseModifier(const std::string & text)
{
    std::string key;
    for (const char c : text)
    {
        if (c != ' ' && c != '-' && c != '_')
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (key == "setto"    || key == "set" || key == "=" || key == "넣기") return 7;
    if (key == "add"      || key == "+"   || key == "더하기") return 8;
    if (key == "subtract" || key == "sub" || key == "-" || key == "빼기") return 9;

    try
    {
        return static_cast<std::uint8_t>(std::stoul(key));
    }
    catch (const std::exception &)
    {
        throw CliError("수정 방식을 알 수 없습니다: " + text +
                       " (setto·add·subtract 또는 수)");
    }
}

// --- 셈 ---

int eudAddr(Args & args)
{
    takeOffsetDatabase(args);
    args.finish();

    const std::uint32_t address = parseAddressOrThrow(args.at(0));

    std::cout << "  주소      : " << hex(address) << "\n";
    std::cout << "  EPD       : " << eud::signedEpdFor(address)
              << " (부호 없이 " << eud::epdFor(address) << ")\n";

    if (!eud::isAligned(address))
    {
        // 못 읽는 자리가 아니다. Deaths 가 네 바이트 단위라 담긴 칸을
        // 읽고 마스크로 거르면 된다 — Memory Masked 가 그 일을 한다.
        const auto * here = eud::offsetAt(address);
        const std::uint32_t size = here != nullptr ? here->size : 1u;
        std::cout << "  네 바이트 경계가 아닙니다 — 담긴 칸을 마스크와 함께 읽으세요.\n";
        std::cout << "  담긴 칸  : " << hex(eud::containingDword(address)) << "\n";
        std::cout << "  마스크    : " << hex(eud::maskFor(address, size)) << "\n";
        if (here != nullptr)
            std::cout << "  이름      : " << here->name << "\n";
        std::cout << "  보기      : eud set-condition <맵> <트리거> <줄> "
                  << hex(eud::containingDword(address)) << " exactly <값> --mask "
                  << hex(eud::maskFor(address, size)) << "\n";
        return 0;
    }

    std::uint32_t player = 0;
    std::uint32_t unit = 0;
    eud::slotFor(address, &player, &unit);
    std::cout << "  Deaths 자리: 플레이어 " << player << ", 유닛 " << unit;
    if (eud::isInsideDeathTable(player, unit))
        std::cout << "  (표 안 — 진짜 죽은 수를 세는 칸)";
    else
        std::cout << "  (표 밖 — 유닛 0, 플레이어 자리에 EPD 를 통째로)";
    std::cout << "\n";

    if (const auto * entry = eud::offsetAt(address))
    {
        std::cout << "  이름      : " << entry->name;
        const std::uint32_t delta = address - entry->address;
        if (entry->length > 1 && entry->size != 0 && delta % entry->size == 0)
            std::cout << " [" << (delta / entry->size) << "번째]";
        else if (delta != 0)
            std::cout << " [+" << hex(delta, 1) << "]";
        std::cout << "\n";
        std::cout << "  SCR       : " << eud::scrSupportName(entry->scr) << "\n";
        if (!entry->description.empty())
            std::cout << "  설명      : " << oneLine(entry->description) << "\n";
    }
    else
    {
        std::cout << "  이름      : (표에 없음)\n";
    }
    return 0;
}

int eudEpd(Args & args)
{
    takeOffsetDatabase(args);
    args.finish();

    const std::string text = args.at(0);
    long long value = 0;
    try
    {
        value = std::stoll(text, nullptr, 0);
    }
    catch (const std::exception &)
    {
        throw CliError("EPD 를 수로 읽을 수 없습니다: " + text);
    }

    const auto epd = static_cast<std::uint32_t>(value);
    const std::uint32_t address = eud::addressForEpd(epd);

    std::cout << "  EPD       : " << static_cast<std::int32_t>(epd) << "\n";
    std::cout << "  주소      : " << hex(address) << "\n";
    if (const auto * entry = eud::offsetAt(address))
        std::cout << "  이름      : " << entry->name
                  << "  (SCR: " << eud::scrSupportName(entry->scr) << ")\n";
    return 0;
}

int eudOffsets(Args & args)
{
    takeOffsetDatabase(args);
    const auto limitOption = args.number("--limit");
    args.finish();

    const std::string query = args.count() > 0 ? args.at(0) : std::string {};
    const auto found = eud::findOffsets(query);
    const std::size_t limit =
        limitOption ? static_cast<std::size_t>(*limitOption) : std::size_t(60);

    if (found.empty())
    {
        std::cout << "  걸리는 자리가 없습니다.\n";
        return 1;
    }

    std::cout << "  " << found.size() << "개 자리";
    if (found.size() > limit)
        std::cout << " (앞 " << limit << "개만; --limit 으로 늘리세요)";
    std::cout << "\n";

    for (std::size_t i = 0; i < found.size() && i < limit; ++i)
    {
        const auto * entry = found[i];
        std::cout << "    " << hex(entry->address) << "  EPD "
                  << std::setw(8) << eud::signedEpdFor(entry->address) << "  "
                  << entry->name;
        if (entry->length > 1)
            std::cout << " x" << entry->length;
        std::cout << "  [" << eud::scrSupportName(entry->scr) << "]\n";
    }

    if (!eud::hasOffsetDatabase())
    {
        std::cout << "\n  붙박이 표만 쓰고 있습니다. EUD Book 의 api.json 을 받아\n"
                     "  --db 로 가리키면 900개가 넘는 자리를 함께 볼 수 있습니다.\n";
    }
    return 0;
}

int eudWhich(Args & args)
{
    args.finish();

    const std::string path = io::euddraft::findExecutable();
    if (path.empty())
    {
        std::cout << "  euddraft 를 찾지 못했습니다.\n"
                     "  환경 변수 SPLASH_EUDDRAFT 에 실행 파일이나 그 폴더를 적거나,\n"
                     "  PATH 에 넣어 주세요.\n";
        return 1;
    }
    std::cout << "  euddraft  : " << path << "\n";
    return 0;
}

// --- 맵 훑기 ---

int eudList(Args & args)
{
    takeOffsetDatabase(args);
    args.finish();

    return readMap(args.at(0), [](io::MapArchive & archive) {
        const auto usages = archive.eudUsages();
        if (usages.empty())
        {
            std::cout << "  EUD 로 쓰이는 조건·동작이 없습니다.\n";
            return 0;
        }

        std::cout << "  EUD " << usages.size() << "군데\n";
        for (const auto & usage : usages)
        {
            std::cout << "    트리거 " << std::setw(5) << usage.triggerIndex
                      << (usage.isCondition ? "  조건 " : "  동작 ") << usage.slot
                      << "  " << usage.text << "\n";

            std::cout << "          " << eud::describeAddress(usage.address) << "\n";
        }
        return 0;
    });
}

int eudCheck(Args & args)
{
    takeOffsetDatabase(args);
    args.finish();

    return readMap(args.at(0), [](io::MapArchive & archive) {
        const auto usages = archive.eudUsages();
        if (usages.empty())
        {
            std::cout << "  EUD 를 쓰지 않는 맵입니다.\n";
            return 0;
        }

        std::size_t unknown = 0;
        std::size_t readOnly = 0;
        std::size_t unsupported = 0;
        std::size_t misaligned = 0;

        for (const auto & usage : usages)
        {
            if (!eud::isAligned(usage.address))
            {
                ++misaligned;
                std::cout << "  [경계] 트리거 " << usage.triggerIndex << " "
                          << usage.text << "\n";
                continue;
            }

            const auto * entry = eud::offsetAt(usage.address);
            if (entry == nullptr)
            {
                ++unknown;
                continue;
            }

            // 쓰기(동작)만 막히는 자리가 있다. 읽기는 되는데 쓰기가 안 되면
            // 맵이 리마스터에서 아예 열리지 않는다.
            if (entry->scr == eud::ScrSupport::Unsupported)
            {
                ++unsupported;
                std::cout << "  [안 됨] 트리거 " << usage.triggerIndex << " "
                          << usage.text << "  — " << entry->name << "\n";
            }
            else if (!usage.isCondition && entry->scr == eud::ScrSupport::ReadOnly)
            {
                ++readOnly;
                std::cout << "  [읽기만] 트리거 " << usage.triggerIndex << " "
                          << usage.text << "  — " << entry->name << "\n";
            }
        }

        std::cout << "\n  모두      : " << usages.size() << "군데\n";
        std::cout << "  리마스터에서 안 되는 자리 : " << unsupported << "\n";
        std::cout << "  쓰기가 막힌 자리          : " << readOnly << "\n";
        std::cout << "  네 바이트 경계가 아닌 자리: " << misaligned << "\n";
        std::cout << "  표에 없어 모르는 자리     : " << unknown << "\n";

        if (!eud::hasOffsetDatabase() && unknown > 0)
        {
            std::cout << "\n  붙박이 표는 자주 쓰는 자리만 담고 있습니다. --db 로\n"
                         "  EUD Book 의 api.json 을 가리키면 훨씬 많이 가려냅니다.\n";
        }

        return (unsupported > 0 || misaligned > 0) ? 2 : 0;
    });
}

// --- 맵 고치기 ---

int eudSetCondition(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto maskOption = args.option("--mask");
    takeOffsetDatabase(args);

    const std::string mapPath = args.at(0);
    const auto triggerIndex = static_cast<std::size_t>(args.integer(1));
    const auto slot = static_cast<std::size_t>(args.integer(2));
    const std::uint32_t address = parseAddressOrThrow(args.at(3));
    const std::uint8_t comparison = parseComparison(args.at(4));
    const auto amount = static_cast<std::uint32_t>(args.integer(5));

    io::MemoryConditionSpec spec;
    spec.address = address;
    spec.comparison = comparison;
    spec.amount = amount;
    if (maskOption)
    {
        const auto mask = eud::parseAddress(*maskOption);
        if (!mask)
            throw CliError("비트마스크를 수로 읽을 수 없습니다: " + *maskOption);
        spec.masked = true;
        spec.bitmask = *mask;
    }

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setConditionMemory(triggerIndex, slot, spec); !r)
        {
            std::cerr << r.message << "\n";
            return false;
        }
        std::cout << "  넣었음    : 트리거 " << triggerIndex << " 조건 " << slot
                  << "  " << eud::describeAddress(address) << "\n";
        return true;
    });
}

int eudSetAction(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto maskOption = args.option("--mask");
    takeOffsetDatabase(args);

    const std::string mapPath = args.at(0);
    const auto triggerIndex = static_cast<std::size_t>(args.integer(1));
    const auto slot = static_cast<std::size_t>(args.integer(2));
    const std::uint32_t address = parseAddressOrThrow(args.at(3));
    const std::uint8_t modifier = parseModifier(args.at(4));
    const auto amount = static_cast<std::uint32_t>(args.integer(5));

    io::MemoryActionSpec spec;
    spec.address = address;
    spec.modifier = modifier;
    spec.amount = amount;
    if (maskOption)
    {
        const auto mask = eud::parseAddress(*maskOption);
        if (!mask)
            throw CliError("비트마스크를 수로 읽을 수 없습니다: " + *maskOption);
        spec.masked = true;
        spec.bitmask = *mask;
    }

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setActionMemory(triggerIndex, slot, spec); !r)
        {
            std::cerr << r.message << "\n";
            return false;
        }
        std::cout << "  넣었음    : 트리거 " << triggerIndex << " 동작 " << slot
                  << "  " << eud::describeAddress(address) << "\n";
        return true;
    });
}

// --- euddraft 빌드 ---

int eudBuild(Args & args)
{
    // 여기서는 editMap 을 쓰지 않는다. 맵을 만드는 것은 euddraft 이고,
    // 우리는 원본을 건드리지 않는다.
    const auto outOption = args.option("-o");
    const auto executable = args.option("--euddraft");
    const auto workDir = args.option("--work-dir");
    const bool freeze = args.flag("--freeze");
    const bool dryRun = args.flag("--dry-run");
    const bool quiet = args.flag("--quiet");

    std::vector<std::string> scripts;
    while (const auto script = args.option("--script"))
        scripts.push_back(*script);

    std::vector<io::euddraft::Plugin> plugins;
    while (const auto plugin = args.option("--plugin"))
    {
        // `이름` 또는 `이름: 키=값, 키=값`. 화면 쪽과 같은 표기다.
        io::euddraft::Plugin parsed = io::euddraft::parsePlugin(*plugin);
        if (parsed.name.empty())
            throw CliError("--plugin 에 이름이 없습니다: " + *plugin);
        plugins.push_back(std::move(parsed));
    }

    std::vector<std::pair<std::string, std::string>> mainOptions;
    while (const auto option = args.option("--main"))
    {
        const std::size_t equals = option->find('=');
        if (equals == std::string::npos)
            throw CliError("--main 은 키=값 꼴이어야 합니다: " + *option);
        mainOptions.emplace_back(option->substr(0, equals), option->substr(equals + 1));
    }

    args.finish();

    io::euddraft::BuildRequest request;
    request.inputMap = args.at(0);
    request.outputMap = outOption ? *outOption : std::string {};
    request.scripts = std::move(scripts);
    request.plugins = std::move(plugins);
    request.mainOptions = std::move(mainOptions);
    request.freeze = freeze;
    if (executable)
        request.executable = *executable;
    if (workDir)
        request.workingDirectory = *workDir;

    if (request.outputMap.empty())
        throw CliError("만들 맵을 -o <출력맵> 으로 주세요.");
    if (request.scripts.empty() && request.plugins.empty())
        throw CliError("--script 나 --plugin 을 적어도 하나는 주세요.");

    if (dryRun)
    {
        // 무엇을 돌릴지만 보여 준다. 실제로 쓰일 설정과 같은 글이 나오도록
        // 경로 펴기까지 마친 뒤 찍는다.
        if (const std::string why = io::euddraft::normalize(request); !why.empty())
            throw CliError(why);
        std::cout << io::euddraft::settingsText(request);
        return 0;
    }

    const auto result = io::euddraft::build(request);

    if (!quiet && !result.log.empty())
    {
        std::cout << "--- euddraft ---\n" << result.log;
        if (result.log.back() != '\n')
            std::cout << "\n";
        std::cout << "----------------\n";
    }

    std::cout << "  설정      : " << result.settingsPath << "\n";
    std::cout << (result.ok ? "  " : "  ") << result.message << "\n";

    if (!result.ok)
        return 1;

    // 만든 맵을 우리 코어로 다시 열어 본다. AGENTS.md 의 "저장 → 다시
    // 열기" 를 여기서도 지킨다 — euddraft 가 뱉은 것도 맵이어야 한다.
    io::MapArchive reopened;
    if (auto r = reopened.open(request.outputMap); !r)
    {
        std::cerr << "  다시 열기 실패: " << r.message << "\n";
        return 1;
    }

    const auto info = reopened.info();
    std::cout << "  다시 열기 : 트리거 " << info.triggerCount << "개, 문자열 "
              << info.stringCount << "개, 보호 " << (info.isProtected ? "예" : "아니오")
              << "\n";

    const auto usages = reopened.eudUsages();
    std::cout << "  EUD       : " << usages.size() << "군데\n";
    return 0;
}

} // namespace

std::vector<Group> eudGroups()
{
    return {
        Group{"eud", "EUD — 메모리 자리·오프셋 표·euddraft", {
            {"addr",          "<주소|이름> [--db api.json]",
                              "주소를 EPD·Deaths 자리·이름으로 푼다.", eudAddr},
            {"epd",           "<EPD> [--db api.json]", "EPD 를 주소로 되돌린다.", eudEpd},
            {"offsets",       "[검색어] [--db api.json] [--limit N]",
                              "이름 붙은 메모리 자리를 찾아본다. "
                              "--db 대신 환경 변수 SPLASH_EUD_OFFSETS 도 된다.",
                              eudOffsets},
            {"list",          "<맵> [--db api.json]",
                              "맵 안의 EUD 조건·동작을 모두 보여 준다.", eudList},
            {"check",         "<맵> [--db api.json]",
                              "리마스터에서 될 자리인지 가려낸다.", eudCheck},
            {"set-condition", "<맵> <트리거> <줄> <주소> <비교> <값> [--mask M] -o <출력맵>",
                              "EUD 조건 한 줄을 넣는다.", eudSetCondition},
            {"set-action",    "<맵> <트리거> <줄> <주소> <수정> <값> [--mask M] -o <출력맵>",
                              "EUD 동작 한 줄을 넣는다.", eudSetAction},
            {"build",         "<맵> -o <출력맵> [--script a.eps]... "
                              "[--plugin '이름: 키=값']... [--main 키=값]... "
                              "[--freeze] [--euddraft 경로] [--dry-run]",
                              "euddraft 로 epScript 를 맵에 얹는다.", eudBuild},
            {"which",         "", "euddraft 를 어디서 찾았는지 보여 준다.", eudWhich},
        }},
    };
}

} // namespace splash::cli
