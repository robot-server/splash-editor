// 맵 전체를 한꺼번에 손보는 것들과 소리, 그리고 트리거·브리핑으로 가는 길.

#include "cli_common.h"

#include <algorithm>
#include <array>
#include <functional>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace splash::cli {

namespace {

// --- 한꺼번에 손보기 ---

int scenarioRevealers(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto ownerText = args.option("--owner");
    const auto spacing = args.number("--spacing");

    const std::string mapPath = args.at(0);
    // 리빌러 하나가 여는 시야에 맞춘 기본값. GUI 와 같다.
    const std::uint8_t owner = ownerText ? parseOwner(*ownerText) : 0;
    const int spacingTiles = spacing ? static_cast<int>(*spacing) : 16;

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const std::size_t placed = archive.placeMapRevealers(owner, spacingTiles);
        if (placed == 0)
        {
            std::cerr << "리빌러를 하나도 놓지 못했습니다.\n";
            return false;
        }
        std::cout << "  깔았음    : 리빌러 " << placed << "개  P" << (owner + 1)
                  << ", " << spacingTiles << "타일 간격\n";
        return true;
    });
}

int scenarioRemoveRevealers(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const std::size_t removed = archive.removeMapRevealers();
        std::cout << "  지웠음    : 리빌러 " << removed << "개\n";
        return true;
    });
}

int scenarioRandomizeResources(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);
    const auto minimum = static_cast<std::uint32_t>(args.integer(1));
    const auto maximum = static_cast<std::uint32_t>(args.integer(2));
    if (minimum > maximum)
        throw CliError("최솟값이 최댓값보다 큽니다.");

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const std::size_t changed = archive.randomizeResources(minimum, maximum);
        std::cout << "  섞었음    : 자원 유닛 " << changed << "개를 "
                  << minimum << "~" << maximum << " 로\n";
        return true;
    });
}

int scenarioCleanBounds(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        const std::size_t touched = archive.removeOutOfBounds();
        std::cout << "  치웠음    : 맵 밖에 있던 것 " << touched << "개\n";
        return true;
    });
}

int scenarioUnprotect(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const std::string mapPath = args.at(0);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (!archive.isProtected())
            std::cout << "  알림      : 보호가 감지되지 않았습니다. 그대로 저장합니다.\n";
        if (auto r = archive.unprotect(); !r)
        {
            std::cerr << "보호 해제 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  보호      : 풀었습니다."
                  << (archive.hasPassword() ? " (암호가 걸려 있던 맵)" : "") << "\n";
        return true;
    });
}

int scenarioImage(Args & args)
{
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    const bool units = args.flag("--units");
    const bool locations = args.flag("--locations");
    const bool creep = args.flag("--creep");
    args.finish();

    const std::string mapPath = args.at(0);
    const std::string outPath = args.at(1);
    return renderMapImage(mapPath, *installPath, outPath, units, locations, creep);
}

// --- 소리 ---

int soundList(Args & args)
{
    const bool skipArchive = args.flag("--fast");
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        const auto sounds = archive.sounds(!skipArchive);
        for (const auto & sound : sounds)
        {
            std::cout << "  ";
            if (sound.index == std::size_t(-1))
                std::cout << "   -";
            else
                std::cout << std::setw(4) << sound.index;
            std::cout << "  " << sound.path;
            if (sound.bytes != 0)      std::cout << "  " << sound.bytes << " 바이트";
            if (!sound.registered)     std::cout << "  [WAV 목록에 없음]";
            if (!sound.inArchive && !skipArchive) std::cout << "  [맵 안에 파일 없음]";
            if (sound.usedByTrigger)   std::cout << "  [트리거가 씀]";
            std::cout << "\n";
        }
        std::cout << "  소리 " << sounds.size() << "개\n";
        return 0;
    });
}

int soundAdd(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto mapInnerPath = args.option("--map-path");

    const std::string mapPath = args.at(0);
    const std::string wavPath = args.at(1);

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.addSound(wavPath, mapInnerPath ? *mapInnerPath : std::string()); !r)
        {
            std::cerr << "소리 넣기 실패: " << r.message << "\n";
            return false;
        }
        std::cout << "  넣었음    : " << wavPath << "\n";
        return true;
    });
}

int soundRemove(Args & args)
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
            throw CliError("소리 번호를 하나 이상 적어야 합니다.");

        // 큰 번호부터 지워야 앞 번호가 밀리지 않는다.
        std::sort(indices.begin(), indices.end(), std::greater<>());
        for (std::size_t index : indices)
        {
            if (auto r = archive.removeSound(index, force); !r)
            {
                std::cerr << "소리 빼기 실패(" << index << "): " << r.message << "\n";
                if (!force)
                    std::cerr << "  트리거가 쓰고 있다면 --force 로 밀어붙일 수 있습니다.\n";
                return false;
            }
        }
        std::cout << "  뺐음      : 소리 " << indices.size() << "개\n";
        return true;
    });
}

int soundExtract(Args & args)
{
    const auto stringId = args.number("--string-id");
    args.finish();

    return readMap(args.at(0), [&](io::MapArchive & archive) {
        // 트리거가 경로만으로 부르는 소리는 WAV 자리 번호가 없다.
        // 그런 것은 문자열 번호로만 꺼낼 수 있다.
        if (stringId)
        {
            const std::string outPath = args.at(1);
            if (auto r = archive.extractSoundByStringId(
                    static_cast<std::size_t>(*stringId), outPath); !r)
            {
                std::cerr << "꺼내기 실패: " << r.message << "\n";
                return 1;
            }
            std::cout << "  꺼냈음    : 문자열 " << *stringId << " -> " << outPath << "\n";
            return 0;
        }

        const auto index = static_cast<std::size_t>(args.integer(1));
        const std::string outPath = args.at(2);
        if (auto r = archive.extractSound(index, outPath); !r)
        {
            std::cerr << "꺼내기 실패: " << r.message << "\n";
            return 1;
        }
        std::cout << "  꺼냈음    : 소리 " << index << " -> " << outPath << "\n";
        return 0;
    });
}

// --- 트리거·브리핑 ---
//
// 알맹이는 예전부터 main.cpp 에 있다. 여기서는 갈래 이름으로도 부를 수
// 있도록 길만 낸다.

int triggerShow(Args & args)
{
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    args.finish();
    return triggerTextCommand(args.at(0), *installPath,
                              args.count() > 1 ? args.at(1) : std::string());
}

int triggerApply(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    if (target.inPlace)
        throw CliError("트리거는 -o <출력맵> 으로만 씁니다.");
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    args.finish();
    return setTriggersCommand(args.at(0), *installPath, args.at(1), target.path);
}

int triggerList(Args & args)
{
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    args.finish();
    return triggerListCommand(args.at(0), *installPath,
                              args.count() > 1 ? static_cast<int>(args.integer(1)) : -1);
}

int triggerArgs(Args & args)
{
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    args.finish();
    return triggerArgsCommand(args.at(0), *installPath,
                              static_cast<std::size_t>(args.integer(1)));
}

int triggerSetArg(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    if (target.inPlace)
        throw CliError("트리거는 -o <출력맵> 으로만 씁니다.");
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    args.finish();
    return setTriggerArgCommand(args.at(0), *installPath, args.at(1),
                                static_cast<std::size_t>(args.integer(2)),
                                static_cast<std::size_t>(args.integer(3)),
                                static_cast<std::size_t>(args.integer(4)),
                                args.at(5), target.path);
}

// --- 트리거 자체를 늘리고 줄이기 ---
//
// 인자 하나를 고치려면 먼저 고칠 트리거가 있어야 한다. GUI 의 트리거
// 편집기에는 있던 일이 CLI 에는 없어서, EUD 를 넣으려면 여기서 막혔다.

int triggerAdd(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    const auto countOption = args.number("--count");
    args.finish();

    const std::string mapPath = args.at(0);
    const auto count = static_cast<std::size_t>(countOption ? *countOption : 1);
    if (count == 0)
        throw CliError("--count 는 1 이상이어야 합니다.");

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (auto r = archive.addTrigger(); !r)
            {
                std::cerr << r.message << "\n";
                return false;
            }
        }
        std::cout << "  더했음    : 빈 트리거 " << count << "개 (이제 "
                  << archive.info().triggerCount << "개)\n";
        return true;
    });
}

int triggerRemove(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    args.finish();

    const std::string mapPath = args.at(0);

    // 뒤에서부터 지운다 — 앞을 먼저 지우면 뒤 번호가 밀린다.
    std::vector<std::size_t> indices;
    for (std::size_t i = 1; i < args.count(); ++i)
        indices.push_back(static_cast<std::size_t>(args.integer(i)));
    if (indices.empty())
        throw CliError("지울 트리거 번호를 하나 이상 주세요.");
    std::sort(indices.begin(), indices.end(), std::greater<std::size_t>());

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        for (const std::size_t index : indices)
        {
            if (auto r = archive.removeTrigger(index); !r)
            {
                std::cerr << r.message << "\n";
                return false;
            }
        }
        std::cout << "  지웠음    : 트리거 " << indices.size() << "개 (이제 "
                  << archive.info().triggerCount << "개)\n";
        return true;
    });
}

int triggerDuplicate(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    args.finish();

    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.duplicateTrigger(index); !r)
        {
            std::cerr << r.message << "\n";
            return false;
        }
        std::cout << "  베꼈음    : 트리거 " << index << " (이제 "
                  << archive.info().triggerCount << "개)\n";
        return true;
    });
}

int triggerOwners(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    args.finish();

    const std::string mapPath = args.at(0);
    const auto index = static_cast<std::size_t>(args.integer(1));
    const std::string list = args.at(2);

    // `1,3,5` 또는 `all`. 번호는 사람이 세는 1~12 이고 속으로는 0~11 이다.
    std::array<bool, 27> owners {};
    if (list == "all" || list == "모두")
    {
        for (std::size_t i = 0; i < 12; ++i)
            owners[i] = true;
    }
    else if (list != "none" && list != "없음")
    {
        std::stringstream stream(list);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            if (token.empty())
                continue;
            const long long number = std::stoll(token);
            if (number < 1 || number > 27)
                throw CliError("플레이어 번호는 1~27 입니다: " + token);
            owners[static_cast<std::size_t>(number - 1)] = true;
        }
    }

    return editMap(mapPath, target, args, [&](io::MapArchive & archive) {
        if (auto r = archive.setTriggerOwners(index, owners); !r)
        {
            std::cerr << r.message << "\n";
            return false;
        }
        std::cout << "  고쳤음    : 트리거 " << index << " 실행 플레이어\n";
        return true;
    });
}

int briefingShow(Args & args)
{
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    args.finish();
    return briefingTextCommand(args.at(0), *installPath,
                               args.count() > 1 ? args.at(1) : std::string());
}

int briefingApply(Args & args)
{
    const SaveTarget target = takeSaveTarget(args);
    if (target.inPlace)
        throw CliError("브리핑은 -o <출력맵> 으로만 씁니다.");
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    args.finish();
    return setBriefingCommand(args.at(0), *installPath, args.at(1), target.path);
}

int briefingArgs(Args & args)
{
    const auto installPath = args.option("--install");
    if (!installPath)
        throw CliError("게임 자료가 필요합니다: --install <StarCraft 설치폴더>");
    args.finish();
    return briefingArgsCommand(args.at(0), *installPath,
                               static_cast<std::size_t>(args.integer(1)));
}

} // namespace

std::vector<Group> scenarioGroups()
{
    return {
        Group{"scenario", "맵 전체를 한꺼번에 손보기", {
            {"revealers",           "<맵> [--owner P] [--spacing 타일] -o <출력맵>",
                                    "맵 전체를 리빌러로 덮는다.", scenarioRevealers},
            {"remove-revealers",    "<맵> -o <출력맵>", "리빌러를 모두 지운다.",
                                    scenarioRemoveRevealers},
            {"randomize-resources", "<맵> <최소> <최대> -o <출력맵>",
                                    "미네랄·가스의 남은 양을 섞는다.", scenarioRandomizeResources},
            {"clean-bounds",        "<맵> -o <출력맵>",
                                    "맵 밖으로 나간 것을 치운다.", scenarioCleanBounds},
            {"unprotect",           "<맵> -o <출력맵>", "맵 보호를 푼다.", scenarioUnprotect},
            {"image",               "<맵> <출력.ppm> --install 설치폴더 [--units] [--locations] [--creep]",
                                    "맵을 그림으로 저장한다.", scenarioImage},
        }},
        Group{"sound", "맵에 든 소리 (WAV)", {
            {"list",    "<맵> [--fast]", "소리를 나열한다. --fast 는 맵 안 확인을 건너뛴다.", soundList},
            {"add",     "<맵> <WAV파일> [--map-path 맵안경로] -o <출력맵>",
                        "WAV 를 맵에 넣고 목록에 올린다.", soundAdd},
            {"remove",  "<맵> <번호...> [--force] -o <출력맵>", "소리를 뺀다.", soundRemove},
            {"extract", "<맵> <번호> <출력.wav>  |  <맵> <출력.wav> --string-id N",
                        "맵 안 소리를 파일로 꺼낸다.", soundExtract},
        }},
        Group{"trigger", "트리거 (TRIG)", {
            {"show",    "<맵> [출력.txt] --install 설치폴더",
                        "트리거를 텍스트로 옮긴다.", triggerShow},
            {"apply",   "<맵> <텍스트파일> --install 설치폴더 -o <출력맵>",
                        "텍스트 트리거를 컴파일해 적용한다.", triggerApply},
            {"list",    "<맵> [번호] --install 설치폴더",
                        "트리거 목록과, 번호를 주면 그 조건·동작을 보여 준다.", triggerList},
            {"args",    "<맵> <번호> --install 설치폴더",
                        "트리거 하나를 인자 단위로 풀어 보여 준다.", triggerArgs},
            {"set-arg", "<맵> <condition|action> <트리거> <줄> <인자> <값> --install 설치폴더 -o <출력맵>",
                        "조건·동작의 인자 하나를 바꾼다.", triggerSetArg},
            {"add",       "<맵> [--count N] -o <출력맵>", "빈 트리거를 더한다.", triggerAdd},
            {"remove",    "<맵> <번호...> -o <출력맵>", "트리거를 지운다.", triggerRemove},
            {"duplicate", "<맵> <번호> -o <출력맵>", "트리거를 베낀다.", triggerDuplicate},
            {"owners",    "<맵> <번호> <1,3,5|all|none> -o <출력맵>",
                          "그 트리거를 실행할 플레이어를 정한다.", triggerOwners},
        }},
        Group{"briefing", "미션 브리핑 (MBRF)", {
            {"show",  "<맵> [출력.txt] --install 설치폴더", "브리핑을 텍스트로 옮긴다.", briefingShow},
            {"apply", "<맵> <텍스트파일> --install 설치폴더 -o <출력맵>",
                      "텍스트 브리핑을 컴파일해 적용한다.", briefingApply},
            {"args",  "<맵> <번호> --install 설치폴더",
                      "브리핑 하나를 인자 단위로 풀어 보여 준다.", briefingArgs},
        }},
    };
}

} // namespace splash::cli
