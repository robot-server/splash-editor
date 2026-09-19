#include "io/map_archive.h"

#include "io/game_graphics.h"

// MappingCore 헤더는 이 번역 단위 안에만 존재한다.
#include "cross_cut/logger.h"
#include "mapping_core/map_file.h"
#include "mapping_core/sc.h"
#include "mapping_core/text_trig_compiler.h"
#include "mapping_core/text_trig_generator.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iterator>
#include <sstream>

// MappingCore 의 여러 번역 단위가 `extern Logger logger` 로 참조하는 전역.
// Chkdraft 본체에서는 main.cpp 가 정의한다. 라이브러리로 쓰는 쪽에서는
// 우리가 제공해야 링크가 된다.
//
// stderr 로 보내는 이유: MappingCore 는 보호된 맵 등을 만나면 진단을 쏟아낸다.
// 그게 stdout 으로 가면 CLI 출력과 뒤섞여 스크립트가 파싱할 수 없게 된다.
Logger logger(std::cerr, LogLevel::Error);

namespace splash::io {

namespace {

/// ISOM 브러시가 계산한 타일을 실제 맵에 옮겨 적는 캐시.
///
/// Chk::IsomCache 의 setTileValue 는 기본 구현이 비어 있다 — 어디에 쓸지는
/// 쓰는 쪽이 정하라는 뜻이다. 이것을 구현하지 않으면 브러시가 계산만 하고
/// 맵은 그대로다.
struct ScenarioIsomCache : Chk::IsomCache
{
    Scenario & scenario;

    ScenarioIsomCache(Scenario & scenario, Sc::Terrain::Tileset tileset,
                      std::size_t tileWidth, std::size_t tileHeight,
                      const Sc::Terrain::Tiles & tiles)
        : Chk::IsomCache{tileset, tileWidth, tileHeight, tiles}, scenario(scenario) {}

    void setTileValue(std::size_t tileX, std::size_t tileY, std::uint16_t tileValue) final
    {
        // 에디터용(TILE)과 게임용(MTXM)을 함께 쓴다. 둘이 어긋나면 화면과
        // 게임이 달라진다.
        scenario.setTile(tileX, tileY, tileValue, Chk::Scope::Both);
    }
};

/// MPQ 안에서 시나리오가 저장되는 고정 경로.
constexpr const char * kScenarioChkPath = "staredit\\scenario.chk";

bool hasChkExtension(const std::string & filePath)
{
    const std::filesystem::path path(filePath);
    std::string ext = path.extension().string();
    for (char & c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".chk";
}

/// 업그레이드 종류 이름. sc.h 의 열거값을 사람이 읽게 띄어 쓴 것이다.
const std::array<const char *, 61> kUpgradeNames {
    "Terran Infantry Armor",
    "Terran Vehicle Plating",
    "Terran Ship Plating",
    "Zerg Carapace",
    "Zerg Flyer Carapace",
    "Protoss Armor",
    "Protoss Plating",
    "Terran Infantry Weapons",
    "Terran Vehicle Weapons",
    "Terran Ship Weapons",
    "Zerg Melee Attacks",
    "Zerg Missile Attacks",
    "Zerg Flyer Attacks",
    "Protoss Ground Weapons",
    "Protoss Air Weapons",
    "Protoss Plasma Shields",
    "U238 Shells",
    "Ion Thrusters",
    "Burst Lasers Unused",
    "Titan Reactor",
    "Ocular Implants",
    "Moebius Reactor",
    "Apollo Reactor",
    "Colossus Reactor",
    "Ventral Sacs",
    "Antennae",
    "Pneumatized Carapace",
    "Metabolic Boost",
    "Adrenal Glands",
    "Muscular Augments",
    "Grooved Spines",
    "Gamete Meiosis",
    "Metasynaptic Node",
    "Singularity Charge",
    "Leg Enhancements",
    "Scarab Damage",
    "Reaver Capacity",
    "Gravitic Drive",
    "Sensor Array",
    "Gravitic Boosters",
    "Khaydarin Amulet",
    "Apial Sensors",
    "Gravitic Thrusters",
    "Carrier Capacity",
    "Khaydarin Core",
    "UnusedUpgrade 45",
    "UnusedUpgrade 46",
    "Argus Jewel",
    "UnusedUpgrade 48",
    "Argus Talisman",
    "UnusedUpgrade 50",
    "Caduceus Reactor",
    "Chitinous Plating",
    "Anabolic Synthesis",
    "Charon Booster",
    "UnusedUpgrade 55",
    "UnusedUpgrade 56",
    "UnusedUpgrade 57",
    "UnusedUpgrade 58",
    "UnusedUpgrade 59",
    "SpecialUpgrade 60"
};

/// 기술 종류 이름. sc.h 의 열거값을 사람이 읽게 띄어 쓴 것이다.
const std::array<const char *, 44> kTechNames {
    "Stim Packs",
    "Lockdown",
    "EMPShockwave",
    "Spider Mines",
    "Scanner Sweep",
    "Tank Siege Mode",
    "Defensive Matrix",
    "Irradiate",
    "Yamato Gun",
    "Cloaking Field",
    "Personnel Cloaking",
    "Burrowing",
    "Infestation",
    "Spawn Broodlings",
    "Dark Swarm",
    "Plague",
    "Consume",
    "Ensnare",
    "Parasite",
    "Psionic Storm",
    "Hallucination",
    "Recall",
    "Stasis Field",
    "Archon Warp",
    "Restoration",
    "Disruption Web",
    "UnusedTech 26",
    "Mind Control",
    "Dark Archon Meld",
    "Feedback",
    "Optical Flare",
    "Maelstrom",
    "Lurker Aspect",
    "UnusedTech 33",
    "Healing",
    "UnusedTech 35",
    "UnusedTech 36",
    "UnusedTech 37",
    "UnusedTech 38",
    "UnusedTech 39",
    "UnusedTech 40",
    "UnusedTech 41",
    "UnusedTech 42",
    "UnusedTech 43"
};

} // namespace

std::string unitTypeName(std::uint16_t type)
{
    const auto & names = Sc::Unit::defaultDisplayNames;
    if (type < names.size())
        return names[type];

    return "Unit " + std::to_string(type);
}

std::string upgradeTypeName(std::uint16_t type)
{
    if (type < kUpgradeNames.size())
        return kUpgradeNames[type];
    return "Upgrade " + std::to_string(type);
}

std::size_t upgradeTypeCount() { return kUpgradeNames.size(); }

std::string techTypeName(std::uint16_t type)
{
    if (type < kTechNames.size())
        return kTechNames[type];
    return "Tech " + std::to_string(type);
}

std::size_t techTypeCount() { return kTechNames.size(); }

std::optional<std::vector<std::uint8_t>> readScenarioChk(const std::string & filePath)
{
    std::error_code ec;
    if (filePath.empty() || !std::filesystem::exists(filePath, ec))
        return std::nullopt;

    // 확장자가 .chk 면 MPQ 컨테이너가 아니라 시나리오 그 자체다.
    if (hasChkExtension(filePath))
    {
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
            return std::nullopt;
        return std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    try
    {
        MpqFile mpq;
        if (!mpq.open(filePath, /*readOnly*/ true, /*createIfNotFound*/ false))
            return std::nullopt;

        auto chk = mpq.getFile(kScenarioChkPath);
        mpq.close();
        return chk;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

/// Scenario::write 가 만든 CHK 에서 STR 섹션 끝에 strTailData 를 되살린다.
///
/// MappingCore 는 STR 을 읽을 때 정규 문자열 데이터 뒤에 남은 바이트를
/// strTailData 로 보존하지만(syncBytesToStrings), 쓸 때는 그것을 붙이지 않는다
/// (syncStringsToBytes). 그래서 저장할 때마다 그 바이트가 사라진다.
/// 맵 보호나 서명에 쓰이는 경우가 있어 "모르는 바이트는 보존한다" 원칙상
/// 되돌려 놓아야 한다.
///
/// 실패하면 원본을 그대로 두고 false 를 돌려준다 — 불확실하면 건드리지 않는다.
bool restoreStrTailData(std::string & chkBytes, const std::vector<u8> & tail)
{
    if (tail.empty())
        return true;

    constexpr std::size_t kHeaderSize = 8; // 이름 4 + 크기 4
    constexpr std::size_t kMaxSectionSize = 65535;

    // 같은 섹션이 여러 번 나오면 뒤엣것이 이긴다. 마지막 STR 을 찾는다.
    std::size_t target = std::string::npos;
    std::int32_t targetSize = 0;

    std::size_t off = 0;
    while (off + kHeaderSize <= chkBytes.size())
    {
        std::int32_t size = 0;
        std::memcpy(&size, chkBytes.data() + off + 4, sizeof(size));

        if (std::memcmp(chkBytes.data() + off, "STR ", 4) == 0 && size >= 0)
        {
            target = off;
            targetSize = size;
        }

        if (size < 0) // 음수 크기는 앞선 섹션을 되감는다. 여기서는 다루지 않는다.
            return false;

        off += kHeaderSize + static_cast<std::size_t>(size);
    }

    if (target == std::string::npos)
        return false;

    const std::size_t bodyEnd =
        target + kHeaderSize + static_cast<std::size_t>(targetSize);
    if (bodyEnd > chkBytes.size())
        return false;

    const std::size_t newSize = static_cast<std::size_t>(targetSize) + tail.size();
    if (newSize > kMaxSectionSize)
        return false; // 규격을 깨느니 tail 을 포기한다

    chkBytes.insert(bodyEnd, reinterpret_cast<const char *>(tail.data()), tail.size());

    const std::int32_t written = static_cast<std::int32_t>(newSize);
    std::memcpy(&chkBytes[target + 4], &written, sizeof(written));
    return true;
}

struct MapArchive::Impl
{
    // MapFile 은 복사도 이동도 되지 않는다(Scenario 가 대입 연산자를 지운다).
    // 새 맵으로 통째로 교체하려면 포인터로 들고 있어야 한다.
    std::unique_ptr<MapFile> mapFile;
    std::string sourcePath;

    // MappingCore 는 편집을 액션 단위로 기록하는데, 우리 편집 하나가 그쪽
    // 액션 여러 개로 나뉘는 경우가 있다(좌표 변경은 삭제+삽입으로 만든다).
    // 되돌릴 때 한 번만 되돌리면 절반만 취소되므로, 연산마다 몇 액션을
    // 만들었는지 쌓아 두고 그만큼 되돌린다.
    std::vector<int> undoSteps;
    std::vector<int> redoSteps;

    // 맵 문자열이 쓰는 코드 페이지. 열 때 가려내고, 저장할 때 그대로
    // 되돌린다 — 다른 인코딩으로 쓰면 게임에서 글자가 깨진다.
    TextEncoding encoding = TextEncoding::Ascii;

    /// 맵에서 꺼낸 바이트를 UTF-8 로.
    std::string decode(const std::string & raw) const { return decodeText(raw, encoding); }

    /// UTF-8 을 맵에 넣을 바이트로.
    std::string encode(const std::string & utf8) const { return encodeText(utf8, encoding); }

    bool isOpen() const { return mapFile != nullptr; }
};

MapArchive::MapArchive() : impl_(std::make_unique<Impl>()) {}
MapArchive::~MapArchive() = default;
MapArchive::MapArchive(MapArchive &&) noexcept = default;
MapArchive & MapArchive::operator=(MapArchive &&) noexcept = default;

Result MapArchive::open(const std::string & filePath)
{
    if (filePath.empty())
        return Result::failure("파일 경로가 비어 있습니다.");

    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec))
        return Result::failure("파일이 존재하지 않습니다: " + filePath);

    // 이전 상태를 버리고 새 MapFile 로 시작한다. MapFile 은 재사용 시
    // 이전 맵의 잔여 상태가 남을 수 있어 통째로 교체하는 편이 안전하다.
    auto fresh = std::make_unique<Impl>();

    try
    {
        auto candidate = std::make_unique<MapFile>();
        if (!candidate->load(filePath))
            return Result::failure("맵을 파싱하지 못했습니다: " + filePath);
        fresh->mapFile = std::move(candidate);
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("맵을 여는 중 예외가 발생했습니다: ") + e.what());
    }
    catch (...)
    {
        return Result::failure("맵을 여는 중 알 수 없는 예외가 발생했습니다.");
    }

    fresh->sourcePath = filePath;

    // 맵에 든 문자열을 모아 어떤 코드 페이지인지 가려낸다. CHK 는
    // 인코딩을 적어 두지 않으므로 글자 모양으로 판단할 수밖에 없다.
    try
    {
        std::vector<std::string> samples;
        const MapFile & map = *fresh->mapFile;

        // 맵 이름·설명은 늘 있으므로 먼저 넣는다.
        if (auto name = map.getScenarioName<RawString>())
            samples.push_back(*name);
        if (auto desc = map.getScenarioDescription<RawString>())
            samples.push_back(*desc);

        // 나머지 문자열도 모은다. 글자가 많을수록 판정이 정확해지지만,
        // 앞쪽 몇 백 개면 충분하고 보호된 맵에서는 읽다 실패할 수도 있다.
        const std::size_t capacity = std::min<std::size_t>(map.getCapacity(Chk::Scope::Either), 2048);
        for (std::size_t id = 1; id <= capacity; ++id)
        {
            try
            {
                if (auto text = map.getString<RawString>(id))
                    samples.push_back(*text);
            }
            catch (const std::exception &)
            {
                break;
            }
        }
        fresh->encoding = detectEncoding(samples);
    }
    catch (const std::exception &)
    {
    }

    impl_ = std::move(fresh);
    return Result::success();
}

TextEncoding MapArchive::textEncoding() const
{
    return impl_->encoding;
}

void MapArchive::setTextEncoding(TextEncoding encoding)
{
    impl_->encoding = encoding;
}

Result MapArchive::createNew(MapFormat format,
                             std::uint16_t tilesetId,
                             std::uint16_t width,
                             std::uint16_t height,
                             bool meleeTriggers,
                             const GameGraphics * graphics,
                             std::size_t terrainTypeIndex)
{
    if (width == 0 || height == 0)
        return Result::failure("맵 크기는 0 이 될 수 없습니다.");

    SaveType saveType = SaveType::HybridScm;
    switch (format)
    {
        case MapFormat::HybridScm:    saveType = SaveType::HybridScm;     break;
        case MapFormat::ExpansionScx: saveType = SaveType::ExpansionScx;  break;
        case MapFormat::RemasteredScx:saveType = SaveType::RemasteredScx; break;
    }

    const auto triggers = meleeTriggers ? Chk::DefaultTriggers::DefaultMelee
                                        : Chk::DefaultTriggers::NoTriggers;

    auto fresh = std::make_unique<Impl>();
    try
    {
        // 타일셋 자료를 주면 고른 지형으로 바닥을 제대로 채운다.
        // 주지 않으면 기본 타일로만 채워진다.
        const Sc::Terrain::Tiles * tilesetData = nullptr;
        if (graphics != nullptr)
        {
            const auto * scData = static_cast<const Sc::Data *>(graphics->internalScData());
            if (scData != nullptr)
                tilesetData = &scData->terrain.get(Sc::Terrain::Tileset(tilesetId));
        }

        fresh->mapFile = std::make_unique<MapFile>(
            Sc::Terrain::Tileset(tilesetId), width, height,
            terrainTypeIndex, triggers, saveType, tilesetData);
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("새 맵을 만들지 못했습니다: ") + e.what());
    }
    catch (...)
    {
        return Result::failure("새 맵을 만드는 중 알 수 없는 예외가 발생했습니다.");
    }

    impl_ = std::move(fresh);
    return Result::success();
}

Result MapArchive::saveAs(const std::string & filePath) const
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    if (filePath.empty())
        return Result::failure("저장 경로가 비어 있습니다.");

    MapFile & map = *impl_->mapFile;

    // MappingCore 의 MapFile::save() 를 쓰지 않는 이유:
    //
    // 그 함수는 saveType 에 맞춰 Scenario::changeVersionTo() 를 무조건 호출하고,
    // changeVersionTo() 는 마지막에 deleteUnusedStrings(Scope::Both) 를 실행한다.
    // 그 결과 "쓰이지 않는다"고 판단된 STR 문자열이 저장 때마다 사라진다.
    // 실제 맵에서 고유 문자열이 302 -> 233 개로 줄어드는 것을 확인했다.
    //
    // 편집하지 않은 섹션의 바이트를 보존해야 하고, 보호된 맵에서는 참조를
    // 추적할 수 없는 문자열이 중요할 수 있으므로 그 경로를 피한다.
    // 대신 Scenario::write() 로 CHK 를 직렬화하고 MPQ 에 직접 넣는다.
    // 두 API 모두 public 이며, MapFile::save() 가 내부에서 하는 일과 같다.
    //
    // 결과적으로 버전 변환은 일어나지 않는다. 포맷을 바꾸는 "다른 형식으로
    // 저장"이 필요해지면 그때 명시적인 별도 연산으로 만든다.

    std::stringstream chk(std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    try
    {
        map.Scenario::write(chk);
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("CHK 직렬화에 실패했습니다: ") + e.what());
    }
    catch (...)
    {
        return Result::failure("CHK 직렬화 중 알 수 없는 예외가 발생했습니다.");
    }

    // Scenario::write 는 직렬화 중 예외를 잡아 failbit 로 바꾼다.
    // 따라서 failbit 는 "쓰다 말았다"는 뜻이며, 부분 출력을 저장해서는 안 된다.
    // 구체적 사유는 MappingCore 가 stderr 로 남긴다.
    if (!chk.good())
    {
        return Result::failure(
            "CHK 를 직렬화하지 못했습니다. 맵이 보호되었거나 CHK 가 유효하지 않을 수 있습니다"
            " (자세한 사유는 stderr 의 MappingCore 진단 참고).");
    }

    // MappingCore 가 쓰기에서 누락하는 STR 꼬리 데이터를 되살린다.
    std::string chkBytes = chk.str();
    restoreStrTailData(chkBytes, map.getStrTailData());

    // 대상이 .chk 면 MPQ 로 감싸지 않고 시나리오 자체를 쓴다.
    if (hasChkExtension(filePath))
    {
        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
        if (!out)
            return Result::failure("출력 파일을 열지 못했습니다: " + filePath);
        out.write(chkBytes.data(), static_cast<std::streamsize>(chkBytes.size()));
        if (!out.good())
            return Result::failure("CHK 를 쓰지 못했습니다: " + filePath);
        return Result::success();
    }

    std::error_code ec;
    const bool inPlace =
        !impl_->sourcePath.empty() &&
        std::filesystem::exists(filePath, ec) &&
        std::filesystem::equivalent(impl_->sourcePath, filePath, ec);

    // 다른 경로로 저장할 때는 원본 MPQ 를 먼저 복사한다.
    // 사운드 등 시나리오 밖의 에셋을 잃지 않기 위해서다.
    if (!inPlace)
    {
        std::filesystem::remove(filePath, ec);

        const bool sourceIsMpq =
            !impl_->sourcePath.empty() && !hasChkExtension(impl_->sourcePath);

        if (sourceIsMpq)
        {
            std::filesystem::copy_file(
                impl_->sourcePath, filePath,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
                return Result::failure("원본 MPQ 를 복사하지 못했습니다: " + ec.message());
        }
    }

    try
    {
        if (!map.MpqFile::open(filePath, /*readOnly*/ false, /*createIfNotFound*/ true))
            return Result::failure("MPQ 를 열지 못했습니다: " + filePath);

        std::stringstream finalChk(
            chkBytes, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
        const bool added = map.MpqFile::addFile(kScenarioChkPath, finalChk);
        map.MpqFile::setUpdatingListFile(true);
        map.MpqFile::close();

        if (!added)
            return Result::failure("시나리오를 MPQ 에 넣지 못했습니다: " + filePath);
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("맵을 저장하는 중 예외가 발생했습니다: ") + e.what());
    }
    catch (...)
    {
        return Result::failure("맵을 저장하는 중 알 수 없는 예외가 발생했습니다.");
    }

    return Result::success();
}

bool MapArchive::isOpen() const
{
    return impl_->isOpen();
}

void MapArchive::close()
{
    impl_ = std::make_unique<Impl>();
}

const std::string & MapArchive::sourcePath() const
{
    return impl_->sourcePath;
}

Result MapArchive::moveUnit(std::size_t unitIndex, std::uint16_t x, std::uint16_t y)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (unitIndex >= map.numUnits())
            return Result::failure("유닛 번호가 범위를 벗어났습니다.");

        // 필드를 직접 쓰는 통로(edit 프록시)는 Scenario 내부 전용이라
        // 밖에서는 쓸 수 없다. 대신 public API 인 삭제+삽입으로 같은 결과를
        // 만든다 — 이쪽도 변경이 기록되므로 실행 취소가 성립한다.
        Chk::Unit unit = map.getUnit(unitIndex);
        unit.xc = x;
        unit.yc = y;
        map.deleteUnit(unitIndex);
        map.insertUnit(unitIndex, unit);
        impl_->undoSteps.push_back(2);
        impl_->redoSteps.clear(); // 새 편집이 들어오면 되돌린 이력은 버린다
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("유닛을 옮기지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::removeUnit(std::size_t unitIndex)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (unitIndex >= map.numUnits())
            return Result::failure("유닛 번호가 범위를 벗어났습니다.");
        map.deleteUnit(unitIndex);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("유닛을 지우지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::addUnit(std::uint16_t unitType, std::uint8_t owner,
                           std::uint16_t x, std::uint16_t y)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        Chk::Unit unit {};
        unit.type = Sc::Unit::Type(unitType);
        unit.owner = owner;
        unit.xc = x;
        unit.yc = y;
        unit.hitpointPercent = 100;
        unit.shieldPercent = 100;
        unit.energyPercent = 100;

        // 자원 유닛은 남은 양이 그래픽과 게임 양쪽에 영향을 준다.
        // StarCraft 의 기본값을 쓴다.
        switch (unitType)
        {
            case 176: case 177: case 178: // Mineral Field 1~3
                unit.resourceAmount = 1500;
                break;
            case 188: // Vespene Geyser
                unit.resourceAmount = 5000;
                break;
            default:
                break;
        }

        // classId 는 맵 안에서 유닛을 가리키는 번호다. 겹치지 않게 뒤에서 잇는다.
        std::uint32_t nextClassId = 0;
        for (std::size_t i = 0; i < map.numUnits(); ++i)
            nextClassId = std::max(nextClassId, map.getUnit(i).classId + 1);
        unit.classId = nextClassId;
        unit.relationClassId = nextClassId;

        map.addUnit(unit);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("유닛을 놓지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::addSprite(std::uint16_t spriteType, std::uint8_t owner,
                             std::uint16_t x, std::uint16_t y, bool drawnAsSprite)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    try
    {
        Chk::Sprite sprite {};
        sprite.type = Sc::Sprite::Type(spriteType);
        sprite.owner = owner;
        sprite.xc = x;
        sprite.yc = y;
        sprite.flags = drawnAsSprite ? Chk::Sprite::toPureSpriteFlags(0)
                                     : Chk::Sprite::toSpriteUnitFlags(0);

        impl_->mapFile->addSprite(sprite);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("스프라이트를 놓지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::removeSprite(std::size_t spriteIndex)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    try
    {
        if (spriteIndex >= impl_->mapFile->numSprites())
            return Result::failure("스프라이트 번호가 범위를 벗어났습니다.");
        impl_->mapFile->deleteSprite(spriteIndex);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("스프라이트를 지우지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setUnitOwner(std::size_t unitIndex, std::uint8_t owner)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (unitIndex >= map.numUnits())
            return Result::failure("유닛 번호가 범위를 벗어났습니다.");
        Chk::Unit unit = map.getUnit(unitIndex);
        unit.owner = owner;
        map.deleteUnit(unitIndex);
        map.insertUnit(unitIndex, unit);
        impl_->undoSteps.push_back(2);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("소유자를 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::placeIsomTerrain(GameGraphics & graphics,
                                    std::size_t pixelX, std::size_t pixelY,
                                    std::size_t terrainType,
                                    std::size_t brushExtent)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return Result::failure("게임 데이터가 준비되지 않았습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        const std::size_t tileWidth = map.getTileWidth();
        const std::size_t tileHeight = map.getTileHeight();
        if (pixelX >= tileWidth * 32 || pixelY >= tileHeight * 32)
            return Result::failure("좌표가 맵 범위를 벗어났습니다.");

        const Sc::Terrain::Tiles & tilesetData =
            scData->terrain.get(map.getTileset());

        // ISOM 은 타일 격자가 아니라 마름모 격자 위에서 움직인다.
        // 가로는 타일 두 칸이 마름모 한 칸이다(isomWidth = tileWidth/2 + 1).
        ScenarioIsomCache cache(map, map.getTileset(), tileWidth, tileHeight, tilesetData);

        // 마름모 격자는 타일 격자와 어긋나 있다. 직접 나누면 엉뚱한 칸이
        // 잡히므로 MappingCore 의 변환을 쓴다.
        const Chk::IsomDiamond diamond =
            Chk::IsomDiamond::fromMapCoordinates(pixelX, pixelY);
        if (!map.placeIsomTerrain(diamond, terrainType, brushExtent, cache))
            return Result::failure("그 자리에는 이 지형을 놓을 수 없습니다.");

        if (std::getenv("SPLASH_DEBUG_ISOM") != nullptr)
        {
            std::cerr << "    isom diamond=(" << diamond.x << "," << diamond.y << ")"
                      << " isomWH=" << cache.isomWidth << "x" << cache.isomHeight
                      << " changed=(" << cache.changedArea.left << "," << cache.changedArea.top
                      << ")-(" << cache.changedArea.right << "," << cache.changedArea.bottom << ")"
                      << " isomRects=" << map.read.isomRects.size()
                      << " editorTiles=" << map.read.editorTiles.size() << "\n";
        }

        // ISOM 을 고쳤으면 실제 타일로 펼쳐야 화면과 게임에 반영된다.
        map.updateTilesFromIsom(cache);

        // ISOM 한 번이 타일 여러 개를 바꾼다. 몇 액션이 생기는지 알 수 없어
        // 이력을 비운다 — 절반만 되돌리면 지형이 어긋난다.
        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("지형을 놓지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setTile(std::size_t tileX, std::size_t tileY, std::uint16_t tileValue)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (tileX >= map.getTileWidth() || tileY >= map.getTileHeight())
            return Result::failure("타일 좌표가 맵 범위를 벗어났습니다.");

        // Scope::Both 는 MTXM 과 TILE 을 각각 고치므로 액션이 두 개 생긴다
        // (Scenario::setTile 이 create_action 을 두 번 호출한다).
        // TILE 섹션이 없는 맵에서는 하나만 생긴다.
        const bool hasEditorTiles =
            map.read.editorTiles.size() > (tileY * map.getTileWidth() + tileX);

        map.setTile(tileX, tileY, tileValue, Chk::Scope::Both);
        impl_->undoSteps.push_back(hasEditorTiles ? 2 : 1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("타일을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setLocationBounds(std::size_t locationIndex,
                                     std::uint32_t left, std::uint32_t top,
                                     std::uint32_t right, std::uint32_t bottom)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (locationIndex >= map.numLocations())
            return Result::failure("로케이션 번호가 범위를 벗어났습니다.");

        Chk::Location location = map.getLocation(locationIndex);
        location.left = left;
        location.top = top;
        location.right = right;
        location.bottom = bottom;

        // replaceLocation 은 한 번의 변경으로 기록된다.
        map.replaceLocation(locationIndex, location);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("로케이션을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setScenarioName(const std::string & name)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    try
    {
        impl_->mapFile->setScenarioName(RawString(impl_->encode(name)));
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("이름을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setScenarioDescription(const std::string & description)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    try
    {
        impl_->mapFile->setScenarioDescription(RawString(impl_->encode(description)));
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("설명을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setTileset(std::uint16_t tilesetId)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    try
    {
        impl_->mapFile->setTileset(Sc::Terrain::Tileset(tilesetId));
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("타일셋을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setDimensions(std::uint16_t width, std::uint16_t height)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    if (width == 0 || height == 0)
        return Result::failure("맵 크기는 0 이 될 수 없습니다.");

    try
    {
        impl_->mapFile->setDimensions(width, height);

        // 크기 변경은 여러 섹션을 한꺼번에 건드린다. 몇 액션이 생기는지
        // 알 수 없어 실행 취소 이력을 비운다 — 절반만 되돌리면 맵이 어긋난다.
        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("크기를 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

void MapArchive::mergeLastEdits(int count)
{
    if (count <= 1)
        return;

    int total = 0;
    for (int i = 0; i < count && !impl_->undoSteps.empty(); ++i)
    {
        total += impl_->undoSteps.back();
        impl_->undoSteps.pop_back();
    }
    if (total > 0)
        impl_->undoSteps.push_back(total);
}

std::optional<UnitProperties> MapArchive::unitProperties(std::size_t unitIndex) const
{
    if (!impl_->isOpen())
        return std::nullopt;

    const MapFile & map = *impl_->mapFile;
    try
    {
        if (unitIndex >= map.numUnits())
            return std::nullopt;

        const Chk::Unit & unit = map.getUnit(unitIndex);
        UnitProperties properties;
        properties.owner = unit.owner;
        properties.hitpointPercent = unit.hitpointPercent;
        properties.shieldPercent = unit.shieldPercent;
        properties.energyPercent = unit.energyPercent;
        properties.resourceAmount = unit.resourceAmount;
        properties.hangarAmount = unit.hangarAmount;
        properties.stateFlags = unit.stateFlags;
        return properties;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

Result MapArchive::setUnitProperties(std::size_t unitIndex,
                                     const UnitProperties & properties)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (unitIndex >= map.numUnits())
            return Result::failure("유닛 번호가 범위를 벗어났습니다.");

        Chk::Unit unit = map.getUnit(unitIndex);
        unit.owner = properties.owner;
        unit.hitpointPercent = properties.hitpointPercent;
        unit.shieldPercent = properties.shieldPercent;
        unit.energyPercent = properties.energyPercent;
        unit.resourceAmount = properties.resourceAmount;
        unit.hangarAmount = properties.hangarAmount;
        unit.stateFlags = properties.stateFlags;

        // 바꾼 값이 실제로 쓰이도록 "이 필드는 유효하다" 표시를 켠다.
        // 이 비트가 없으면 게임이 기본값을 쓴다.
        unit.validFieldFlags |= Chk::Unit::ValidField::Owner
                              | Chk::Unit::ValidField::Hitpoints
                              | Chk::Unit::ValidField::Shields
                              | Chk::Unit::ValidField::Energy
                              | Chk::Unit::ValidField::Resources
                              | Chk::Unit::ValidField::Hangar;
        unit.validStateFlags |= properties.stateFlags;

        map.deleteUnit(unitIndex);
        map.insertUnit(unitIndex, unit);
        impl_->undoSteps.push_back(2);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("유닛 속성을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::undo()
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    if (impl_->undoSteps.empty())
        return Result::failure("되돌릴 편집이 없습니다.");

    try
    {
        const int steps = impl_->undoSteps.back();
        for (int i = 0; i < steps; ++i)
            impl_->mapFile->undoAction();

        impl_->undoSteps.pop_back();
        impl_->redoSteps.push_back(steps);
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("되돌리지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::redo()
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    if (impl_->redoSteps.empty())
        return Result::failure("다시 실행할 편집이 없습니다.");

    try
    {
        const int steps = impl_->redoSteps.back();
        for (int i = 0; i < steps; ++i)
            impl_->mapFile->redoAction();

        impl_->redoSteps.pop_back();
        impl_->undoSteps.push_back(steps);
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("다시 실행하지 못했습니다: ") + e.what());
    }
    return Result::success();
}

std::vector<RawUnit> MapArchive::units() const
{
    if (!impl_->isOpen())
        return {};

    const MapFile & map = *impl_->mapFile;
    std::vector<RawUnit> out;

    try
    {
        const std::size_t count = map.numUnits();
        out.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            const Chk::Unit & unit = map.getUnit(i);
            RawUnit raw;
            raw.classId    = unit.classId;
            raw.x          = unit.xc;
            raw.y          = unit.yc;
            raw.type       = static_cast<std::uint16_t>(unit.type);
            raw.owner      = unit.owner;
            raw.stateFlags = unit.stateFlags;
            raw.resourceAmount = unit.resourceAmount;
            raw.hitpointPercent = unit.hitpointPercent;
            raw.shieldPercent = unit.shieldPercent;
            raw.energyPercent = unit.energyPercent;
            raw.hangarAmount = unit.hangarAmount;
            out.push_back(raw);
        }
    }
    catch (const std::exception &)
    {
        return out; // 읽은 데까지는 돌려준다
    }

    return out;
}

std::vector<RawSprite> MapArchive::sprites() const
{
    if (!impl_->isOpen())
        return {};

    const MapFile & map = *impl_->mapFile;
    std::vector<RawSprite> out;

    try
    {
        const std::size_t count = map.numSprites();
        out.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            const Chk::Sprite & sprite = map.getSprite(i);
            RawSprite raw;
            raw.type          = static_cast<std::uint16_t>(sprite.type);
            raw.x             = sprite.xc;
            raw.y             = sprite.yc;
            raw.owner         = sprite.owner;
            raw.flags         = sprite.flags;
            raw.drawnAsSprite = sprite.isDrawnAsSprite();
            out.push_back(raw);
        }
    }
    catch (const std::exception &)
    {
        return out;
    }

    return out;
}

std::vector<RawLocation> MapArchive::locations() const
{
    if (!impl_->isOpen())
        return {};

    const MapFile & map = *impl_->mapFile;
    std::vector<RawLocation> out;

    try
    {
        const std::size_t count = map.numLocations();
        out.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            const Chk::Location & location = map.getLocation(i);

            // 좌표가 전부 0 이고 이름도 없는 슬롯은 쓰이지 않는 자리다.
            // MRGN 은 항상 255개(또는 확장 시 더) 슬롯을 갖지만 대부분 비어 있다.
            if (location.isBlank())
                continue;

            RawLocation raw;
            raw.left           = location.left;
            raw.top            = location.top;
            raw.right          = location.right;
            raw.bottom         = location.bottom;
            raw.stringId       = location.stringId;
            raw.elevationFlags = location.elevationFlags;
            raw.index          = i;

            if (auto name = map.getLocationName<RawString>(i))
                raw.name = impl_->decode(*name);

            out.push_back(raw);
        }
    }
    catch (const std::exception &)
    {
        return out;
    }

    return out;
}

std::optional<std::string> MapArchive::triggerText(const GameGraphics & graphics) const
{
    if (!impl_->isOpen())
        return std::nullopt;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return std::nullopt;

    try
    {
        // 인자 의미: 메모리를 주소로 표기할지, death table 오프셋, 빈 문자열 표기 방식.
        // Chkdraft 기본값과 맞춘다.
        TextTrigGenerator generator(false, 0);

        std::string text;
        // 템플릿은 Scenario 로만 인스턴스화되어 있다. MapFile 이 그것을
        // 상속하므로 기반 클래스로 넘긴다.
        const Scenario & scenario = *impl_->mapFile;
        if (!generator.generateTextTrigs(scenario, text, *scData))
            return std::nullopt;

        return impl_->decode(text);
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

namespace {

/// 트리거를 실행하는 플레이어들을 읽기 좋은 문장으로.
std::string describeOwners(const Chk::Trigger & trigger)
{
    // 앞 8개가 플레이어 1~8, 그 뒤는 All players 같은 특수 항목이다.
    std::string out;
    for (std::size_t i = 0; i < 8; ++i)
    {
        if (trigger.owners[i] == Chk::Trigger::Owned::Yes)
        {
            if (!out.empty())
                out += ", ";
            out += std::to_string(i + 1);
        }
    }

    if (trigger.owners[17] == Chk::Trigger::Owned::Yes) // All players
        return "모든 플레이어";

    return out.empty() ? std::string("(없음)") : ("플레이어 " + out);
}

} // namespace

std::optional<UpgradeSettings> MapArchive::upgradeSettings(std::uint16_t upgradeType) const
{
    if (!impl_->isOpen())
        return std::nullopt;

    const MapFile & map = *impl_->mapFile;
    try
    {
        const auto type = Sc::Upgrade::Type(upgradeType);
        UpgradeSettings settings;

        settings.useDefaultCosts = map.upgradeUsesDefaultCosts(type);
        settings.baseMineralCost = map.getUpgradeBaseMineralCost(type);
        settings.mineralCostFactor = map.getUpgradeMineralCostFactor(type);
        settings.baseGasCost = map.getUpgradeBaseGasCost(type);
        settings.gasCostFactor = map.getUpgradeGasCostFactor(type);
        settings.baseResearchTime = map.getUpgradeBaseResearchTime(type);
        settings.researchTimeFactor = map.getUpgradeResearchTimeFactor(type);

        settings.defaultStartLevel =
            static_cast<std::uint8_t>(map.getDefaultStartUpgradeLevel(type));
        settings.defaultMaxLevel =
            static_cast<std::uint8_t>(map.getDefaultMaxUpgradeLevel(type));

        for (std::size_t player = 0; player < 12; ++player)
        {
            settings.playerUsesDefault[player] = map.playerUsesDefaultUpgradeLeveling(type, player);
            settings.startLevel[player] =
                static_cast<std::uint8_t>(map.getStartUpgradeLevel(type, player));
            settings.maxLevel[player] =
                static_cast<std::uint8_t>(map.getMaxUpgradeLevel(type, player));
        }
        return settings;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

Result MapArchive::setUpgradeSettings(std::uint16_t upgradeType, const UpgradeSettings & settings)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        const auto type = Sc::Upgrade::Type(upgradeType);

        map.setUpgradeUsesDefaultCosts(type, settings.useDefaultCosts);
        if (!settings.useDefaultCosts)
        {
            map.setUpgradeBaseMineralCost(type, settings.baseMineralCost);
            map.setUpgradeMineralCostFactor(type, settings.mineralCostFactor);
            map.setUpgradeBaseGasCost(type, settings.baseGasCost);
            map.setUpgradeGasCostFactor(type, settings.gasCostFactor);
            map.setUpgradeBaseResearchTime(type, settings.baseResearchTime);
            map.setUpgradeResearchTimeFactor(type, settings.researchTimeFactor);
        }

        map.setDefaultStartUpgradeLevel(type, settings.defaultStartLevel);
        map.setDefaultMaxUpgradeLevel(type, settings.defaultMaxLevel);

        for (std::size_t player = 0; player < 12; ++player)
        {
            map.setPlayerUsesDefaultUpgradeLeveling(type, player, settings.playerUsesDefault[player]);
            if (!settings.playerUsesDefault[player])
            {
                map.setStartUpgradeLevel(type, player, settings.startLevel[player]);
                map.setMaxUpgradeLevel(type, player, settings.maxLevel[player]);
            }
        }

        // 여러 값을 한꺼번에 쓰므로 실행 취소 단위를 셀 수 없다.
        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("업그레이드 설정을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

std::optional<TechSettings> MapArchive::techSettings(std::uint16_t techType) const
{
    if (!impl_->isOpen())
        return std::nullopt;

    const MapFile & map = *impl_->mapFile;
    try
    {
        const auto type = Sc::Tech::Type(techType);
        TechSettings settings;

        settings.useDefaultCosts = map.techUsesDefaultSettings(type);
        settings.mineralCost = map.getTechMineralCost(type);
        settings.gasCost = map.getTechGasCost(type);
        settings.researchTime = map.getTechResearchTime(type);
        settings.energyCost = map.getTechEnergyCost(type);

        settings.defaultAvailable = map.techDefaultAvailable(type);
        settings.defaultResearched = map.techDefaultResearched(type);

        for (std::size_t player = 0; player < 12; ++player)
        {
            settings.playerUsesDefault[player] = map.playerUsesDefaultTechSettings(type, player);
            settings.available[player] = map.techAvailable(type, player);
            settings.researched[player] = map.techResearched(type, player);
        }
        return settings;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

Result MapArchive::setTechSettings(std::uint16_t techType, const TechSettings & settings)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        const auto type = Sc::Tech::Type(techType);

        map.setTechUsesDefaultSettings(type, settings.useDefaultCosts);
        if (!settings.useDefaultCosts)
        {
            map.setTechMineralCost(type, settings.mineralCost);
            map.setTechGasCost(type, settings.gasCost);
            map.setTechResearchTime(type, settings.researchTime);
            map.setTechEnergyCost(type, settings.energyCost);
        }

        map.setDefaultTechAvailable(type, settings.defaultAvailable);
        map.setDefaultTechResearched(type, settings.defaultResearched);

        for (std::size_t player = 0; player < 12; ++player)
        {
            map.setPlayerUsesDefaultTechSettings(type, player, settings.playerUsesDefault[player]);
            if (!settings.playerUsesDefault[player])
            {
                map.setTechAvailable(type, player, settings.available[player]);
                map.setTechResearched(type, player, settings.researched[player]);
            }
        }

        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("기술 설정을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

TriggerVocabulary MapArchive::triggerVocabulary(const GameGraphics & graphics) const
{
    TriggerVocabulary out;
    if (!impl_->isOpen())
        return out;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return out;

    try
    {
        const MapFile & map = *impl_->mapFile;
        const Scenario & scenario = map;

        TextTrigGenerator generator(false, 0);
        if (!generator.loadScenario(scenario, *scData))
            return out;

        // 이름표가 붙은 타입만 거둔다. 알 수 없는 번호는 생성기가 숫자를
        // 그대로 돌려주므로 글자가 섞였는지로 가린다.
        const auto named = [](const std::string & name) {
            return !name.empty() &&
                name.find_first_not_of("0123456789") != std::string::npos;
        };

        for (int i = 0; i < 256; ++i)
        {
            std::string name = generator.getConditionName(Chk::Condition::Type(i));
            if (named(name))
                out.conditions.push_back(std::move(name));
        }
        for (int i = 0; i < 256; ++i)
        {
            std::string name = generator.getActionName(Chk::Action::Type(i));
            if (named(name))
                out.actions.push_back(std::move(name));
        }

        const auto push = [&named](std::vector<std::string> & into, std::string name) {
            if (!named(name))
                return;
            if (std::find(into.begin(), into.end(), name) == into.end())
                into.push_back(std::move(name));
        };

        for (int i = 0; i < 12; ++i)
            push(out.constants, generator.getTrigNumericComparison(Chk::Condition::Comparison(i)));
        for (int i = 0; i < 12; ++i)
            push(out.constants, generator.getTrigNumericModifier(Chk::Trigger::ValueModifier(i)));
        for (int i = 0; i < 12; ++i)
            push(out.constants, generator.getTrigSwitchState(Chk::Trigger::ValueModifier(i)));
        for (int i = 0; i < 12; ++i)
            push(out.constants, generator.getTrigSwitchModifier(Chk::Trigger::ValueModifier(i)));
        for (int i = 0; i < 8; ++i)
            push(out.constants, generator.getTrigAllyState(Chk::Action::AllianceStatus(i)));
        for (int i = 0; i < 8; ++i)
            push(out.constants, generator.getTrigOrder(Chk::Action::Order(i)));
        for (int i = 0; i < 12; ++i)
            push(out.constants, generator.getTrigScoreType(Chk::Trigger::ScoreType(i)));
        for (int i = 0; i < 4; ++i)
            push(out.constants, generator.getTrigResourceType(Chk::Trigger::ResourceType(i)));
        for (int i = 0; i < 8; ++i)
            push(out.constants, generator.getTrigTextFlags(Chk::Action::Flags(i)));
        push(out.constants, generator.getTrigNumUnits(Chk::Action::NumUnits(0)));

        for (std::size_t i = 1; i <= map.numLocations(); ++i)
        {
            std::string name = impl_->decode(generator.getTrigLocation(i));
            if (!name.empty() && name != "No Location")
                push(out.locations, std::move(name));
        }
        for (std::size_t i = 0; i < 256; ++i)
            push(out.switches, impl_->decode(generator.getTrigSwitch(i)));
        for (int i = 0; i < int(Sc::Unit::TotalReferenceTypes); ++i)
            push(out.units, impl_->decode(generator.getTrigUnit(Sc::Unit::Type(i))));
        for (std::size_t i = 0; i < 27; ++i)
            push(out.players, impl_->decode(generator.getTrigPlayer(i)));
        for (std::size_t i = 0; i < scData->ai.numEntries(); ++i)
            push(out.scripts, generator.getTrigScript(Sc::Ai::ScriptId(scData->ai.getEntry(i).identifier)));
    }
    catch (const std::exception &)
    {
    }

    return out;
}

std::optional<UnitStats> MapArchive::unitStats(std::uint16_t unitType) const
{
    if (!impl_->isOpen())
        return std::nullopt;

    const MapFile & map = *impl_->mapFile;
    try
    {
        const auto type = Sc::Unit::Type(unitType);
        UnitStats stats;
        stats.useDefault = map.unitUsesDefaultSettings(type);
        stats.hitpoints = map.getUnitHitpoints(type);
        stats.shields = map.getUnitShieldPoints(type);
        stats.armor = map.getUnitArmorLevel(type);
        stats.buildTime = map.getUnitBuildTime(type);
        stats.mineralCost = map.getUnitMineralCost(type);
        stats.gasCost = map.getUnitGasCost(type);

        stats.defaultBuildable = map.isUnitDefaultBuildable(type);
        for (std::size_t player = 0; player < 12; ++player)
        {
            stats.playerUsesDefault[player] = map.playerUsesDefaultUnitBuildability(type, player);
            stats.buildable[player] = map.isUnitBuildable(type, player);
        }
        return stats;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

Result MapArchive::setUnitStats(std::uint16_t unitType, const UnitStats & stats)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        const auto type = Sc::Unit::Type(unitType);

        map.setUnitUsesDefaultSettings(type, stats.useDefault);
        if (!stats.useDefault)
        {
            map.setUnitHitpoints(type, stats.hitpoints);
            map.setUnitShieldPoints(type, stats.shields);
            map.setUnitArmorLevel(type, stats.armor);
            map.setUnitBuildTime(type, stats.buildTime);
            map.setUnitMineralCost(type, stats.mineralCost);
            map.setUnitGasCost(type, stats.gasCost);
        }

        map.setUnitDefaultBuildable(type, stats.defaultBuildable);
        for (std::size_t player = 0; player < 12; ++player)
        {
            map.setPlayerUsesDefaultUnitBuildability(type, player, stats.playerUsesDefault[player]);
            if (!stats.playerUsesDefault[player])
                map.setUnitBuildable(type, player, stats.buildable[player]);
        }

        // 여러 필드를 각각 기록한다. 몇 액션이 생기는지 세기 어려워 이력을
        // 비운다 — 절반만 되돌리면 능력치가 뒤섞인다.
        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("유닛 능력치를 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

std::vector<MapString> MapArchive::strings() const
{
    std::vector<MapString> out;
    if (!impl_->isOpen())
        return out;

    const MapFile & map = *impl_->mapFile;
    try
    {
        const std::size_t capacity = map.getCapacity();
        for (std::size_t id = 1; id <= capacity; ++id)
        {
            if (!map.stringStored(id))
                continue;

            auto text = map.getString<RawString>(id);
            if (!text)
                continue;

            MapString entry;
            entry.id = id;
            entry.text = impl_->decode(*text);
            entry.used = map.stringUsed(id);
            out.push_back(std::move(entry));
        }
    }
    catch (const std::exception &)
    {
    }
    return out;
}

Result MapArchive::setString(std::size_t stringId, const std::string & text)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    try
    {
        impl_->mapFile->replaceString(stringId, RawString(impl_->encode(text)));
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("문자열을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

std::vector<PlayerSetting> MapArchive::playerSettings() const
{
    std::vector<PlayerSetting> out;
    if (!impl_->isOpen())
        return out;

    const MapFile & map = *impl_->mapFile;
    try
    {
        // 12칸이지만 앞 8칸만 사람이 고를 수 있다. 나머지는 중립·구조물용이다.
        for (std::size_t player = 0; player < 12; ++player)
        {
            PlayerSetting setting;
            setting.race = static_cast<std::uint8_t>(map.getPlayerRace(player));
            setting.slotType = static_cast<std::uint8_t>(map.getSlotType(player));
            setting.force = (player < 8)
                ? static_cast<std::uint8_t>(map.getPlayerForce(player)) : 0;
            out.push_back(setting);
        }
    }
    catch (const std::exception &)
    {
    }
    return out;
}

Result MapArchive::setPlayerSetting(std::size_t player, const PlayerSetting & setting)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    if (player >= 12)
        return Result::failure("플레이어 번호가 범위를 벗어났습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        map.setPlayerRace(player, Chk::Race(setting.race));
        map.setSlotType(player, Sc::Player::SlotType(setting.slotType));
        if (player < 8)
            map.setPlayerForce(player, Chk::Force(setting.force));

        // 세 가지를 각각 기록하므로 액션도 그만큼 생긴다.
        impl_->undoSteps.push_back(player < 8 ? 3 : 2);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("플레이어 설정을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

std::vector<std::string> MapArchive::forceNames() const
{
    std::vector<std::string> out;
    if (!impl_->isOpen())
        return out;

    const MapFile & map = *impl_->mapFile;
    try
    {
        for (std::size_t force = 0; force < 4; ++force)
        {
            if (auto name = map.getForceName<RawString>(Chk::Force(force)))
                out.push_back(impl_->decode(*name));
            else
                out.push_back(std::string());
        }
    }
    catch (const std::exception &)
    {
    }
    return out;
}

Result MapArchive::setForceName(std::size_t force, const std::string & name)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    if (force >= 4)
        return Result::failure("세력 번호가 범위를 벗어났습니다.");

    try
    {
        impl_->mapFile->setForceName(Chk::Force(force), RawString(impl_->encode(name)));
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("세력 이름을 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

std::vector<TriggerSummary> MapArchive::triggerSummaries(const GameGraphics & graphics) const
{
    std::vector<TriggerSummary> out;
    if (!impl_->isOpen())
        return out;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    const MapFile & map = *impl_->mapFile;

    try
    {
        TextTrigGenerator generator(false, 0);
        const std::size_t count = map.numTriggers();
        out.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            const Chk::Trigger & trigger = map.getTrigger(i);

            TriggerSummary summary;
            summary.index = i;
            summary.players = describeOwners(trigger);
            summary.disabled = (trigger.flags & Chk::Trigger::Flags::Disabled) != 0;

            for (const auto & condition : trigger.conditions)
            {
                if (condition.conditionType != Chk::Condition::Type::NoCondition)
                    ++summary.conditions;
            }
            for (const auto & action : trigger.actions)
            {
                if (action.actionType != Chk::Action::Type::NoAction)
                    ++summary.actions;
            }

            // 무엇을 하는 트리거인지 한 줄로 보여 준다. 이름표는 텍스트
            // 생성기가 채우므로 그쪽에서 첫 동작 줄을 가져온다.
            if (scData != nullptr && summary.actions > 0)
            {
                std::string text;
                const Scenario & scenario = map;
                if (generator.generateTextTrigs(scenario, i, text, *scData))
                {
                    const std::size_t actionsAt = text.find("Actions:");
                    if (actionsAt != std::string::npos)
                    {
                        std::size_t lineStart = text.find_first_not_of(" \t\r\n",
                            actionsAt + std::strlen("Actions:"));
                        if (lineStart != std::string::npos)
                        {
                            std::size_t lineEnd = text.find_first_of(";\n", lineStart);
                            if (lineEnd == std::string::npos)
                                lineEnd = text.size();
                            summary.firstAction =
                                impl_->decode(text.substr(lineStart, lineEnd - lineStart));
                        }
                    }
                }
            }

            out.push_back(std::move(summary));
        }
    }
    catch (const std::exception &)
    {
    }

    return out;
}

std::optional<TriggerDetail> MapArchive::triggerDetail(std::size_t index,
                                                       const GameGraphics & graphics) const
{
    if (!impl_->isOpen())
        return std::nullopt;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return std::nullopt;

    const MapFile & map = *impl_->mapFile;

    try
    {
        if (index >= map.numTriggers())
            return std::nullopt;

        const Chk::Trigger & trigger = map.getTrigger(index);

        TriggerDetail detail;
        detail.flags = trigger.flags;
        for (std::size_t i = 0; i < 27 && i < Chk::Trigger::MaxOwners; ++i)
            detail.owners[i] = (trigger.owners[i] == Chk::Trigger::Owned::Yes);

        TextTrigGenerator generator(false, 0);

        // 이 트리거만의 텍스트. 자세히 고칠 때 쓰고, 조건·동작 목록도
        // 여기서 뽑는다 — 이름만 따로 물으면 인자가 빠져 "Bring" 처럼
        // 반쪽짜리가 되고, 이름표가 준비되지 않으면 숫자만 나온다.
        std::string text;
        const Scenario & scenario = map;
        if (generator.generateTextTrigs(scenario, index, text, *scData))
            detail.text = impl_->decode(text);

        // 텍스트는 다음 모양이다:
        //   Trigger("...")\n{\nConditions:\n\t...;\n\nActions:\n\t...;\n}
        // 탭으로 들여쓴 줄이 조건·동작 한 줄씩이다.
        const auto collect = [](const std::string & body,
                                std::vector<std::string> & out) {
            std::size_t start = 0;
            while (start < body.size())
            {
                std::size_t end = body.find('\n', start);
                if (end == std::string::npos)
                    end = body.size();

                std::string line = body.substr(start, end - start);
                start = end + 1;

                // 앞뒤 공백·탭을 걷어낸다.
                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos)
                    continue;
                const auto last = line.find_last_not_of(" \t\r");
                line = line.substr(first, last - first + 1);

                if (line.empty() || line == "{" || line == "}")
                    continue;
                if (line.rfind("Trigger(", 0) == 0)
                    continue;
                // 트리거 사이 구분선 주석은 목록에 넣지 않는다.
                if (line.rfind("//", 0) == 0)
                    continue;

                // 끝의 세미콜론은 목록에서 군더더기다.
                if (!line.empty() && line.back() == ';')
                    line.pop_back();

                out.push_back(std::move(line));
            }
        };

        const std::size_t conditionsAt = detail.text.find("Conditions:");
        const std::size_t actionsAt = detail.text.find("Actions:");

        if (conditionsAt != std::string::npos)
        {
            const std::size_t from = conditionsAt + std::strlen("Conditions:");
            const std::size_t to = (actionsAt != std::string::npos && actionsAt > from)
                ? actionsAt : detail.text.size();
            collect(detail.text.substr(from, to - from), detail.conditions);
        }
        if (actionsAt != std::string::npos)
        {
            const std::size_t from = actionsAt + std::strlen("Actions:");
            collect(detail.text.substr(from), detail.actions);
        }

        return detail;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

// ------------------------------------------- 트리거 조건·액션의 인자 다루기

namespace {

/// 조건 구조체에서 필드 하나를 읽는다.
std::uint32_t readField(const Chk::Condition & condition, Chk::Condition::ArgField field)
{
    switch (field)
    {
        case Chk::Condition::ArgField::LocationId:    return condition.locationId;
        case Chk::Condition::ArgField::Player:        return condition.player;
        case Chk::Condition::ArgField::Amount:        return condition.amount;
        case Chk::Condition::ArgField::UnitType:      return std::uint32_t(condition.unitType);
        case Chk::Condition::ArgField::Comparison:    return std::uint32_t(condition.comparison);
        case Chk::Condition::ArgField::ConditionType: return std::uint32_t(condition.conditionType);
        case Chk::Condition::ArgField::TypeIndex:     return condition.typeIndex;
        case Chk::Condition::ArgField::Flags:         return condition.flags;
        case Chk::Condition::ArgField::MaskFlag:      return std::uint32_t(condition.maskFlag);
        case Chk::Condition::ArgField::NoField:       break;
    }
    return 0;
}

/// 조건 구조체의 필드 하나에 값을 쓴다.
void writeField(Chk::Condition & condition, Chk::Condition::ArgField field, std::uint32_t value)
{
    switch (field)
    {
        case Chk::Condition::ArgField::LocationId:    condition.locationId = value; break;
        case Chk::Condition::ArgField::Player:        condition.player = value; break;
        case Chk::Condition::ArgField::Amount:        condition.amount = value; break;
        case Chk::Condition::ArgField::UnitType:
            condition.unitType = Sc::Unit::Type(std::uint16_t(value)); break;
        case Chk::Condition::ArgField::Comparison:
            condition.comparison = Chk::Condition::Comparison(std::uint8_t(value)); break;
        case Chk::Condition::ArgField::ConditionType:
            condition.conditionType = Chk::Condition::Type(std::uint8_t(value)); break;
        case Chk::Condition::ArgField::TypeIndex:
            condition.typeIndex = std::uint8_t(value); break;
        case Chk::Condition::ArgField::Flags:
            condition.flags = std::uint8_t(value); break;
        case Chk::Condition::ArgField::MaskFlag:
            condition.maskFlag = Chk::Condition::MaskFlag(std::uint16_t(value)); break;
        case Chk::Condition::ArgField::NoField: break;
    }
}

std::uint32_t readField(const Chk::Action & action, Chk::Action::ArgField field)
{
    switch (field)
    {
        case Chk::Action::ArgField::LocationId:    return action.locationId;
        case Chk::Action::ArgField::StringId:      return action.stringId;
        case Chk::Action::ArgField::SoundStringId: return action.soundStringId;
        case Chk::Action::ArgField::Time:          return action.time;
        case Chk::Action::ArgField::Group:         return action.group;
        case Chk::Action::ArgField::Number:        return action.number;
        case Chk::Action::ArgField::Type:          return action.type;
        case Chk::Action::ArgField::ActionType:    return std::uint32_t(action.actionType);
        case Chk::Action::ArgField::Type2:         return action.type2;
        case Chk::Action::ArgField::Flags:         return action.flags;
        case Chk::Action::ArgField::Padding:       return action.padding;
        case Chk::Action::ArgField::MaskFlag:      return std::uint32_t(action.maskFlag);
        case Chk::Action::ArgField::NoField:       break;
    }
    return 0;
}

void writeField(Chk::Action & action, Chk::Action::ArgField field, std::uint32_t value)
{
    switch (field)
    {
        case Chk::Action::ArgField::LocationId:    action.locationId = value; break;
        case Chk::Action::ArgField::StringId:      action.stringId = value; break;
        case Chk::Action::ArgField::SoundStringId: action.soundStringId = value; break;
        case Chk::Action::ArgField::Time:          action.time = value; break;
        case Chk::Action::ArgField::Group:         action.group = value; break;
        case Chk::Action::ArgField::Number:        action.number = value; break;
        case Chk::Action::ArgField::Type:          action.type = std::uint16_t(value); break;
        case Chk::Action::ArgField::ActionType:
            action.actionType = Chk::Action::Type(std::uint8_t(value)); break;
        case Chk::Action::ArgField::Type2:         action.type2 = std::uint8_t(value); break;
        case Chk::Action::ArgField::Flags:         action.flags = std::uint8_t(value); break;
        case Chk::Action::ArgField::Padding:       action.padding = std::uint8_t(value); break;
        case Chk::Action::ArgField::MaskFlag:
            action.maskFlag = Chk::Action::MaskFlag(std::uint16_t(value)); break;
        case Chk::Action::ArgField::NoField: break;
    }
}

/// 이름이 붙은 값만 선택지로 삼는다. 이름표가 없으면 생성기가 숫자를
/// 그대로 돌려주므로 글자가 섞였는지로 가린다.
bool namedValue(const std::string & text)
{
    return !text.empty() && text.find_first_not_of("0123456789") != std::string::npos;
}

} // namespace

namespace {

/// 조건 인자 한 자리를 편집기가 쓸 수 있는 꼴로 푼다.
TriggerArg describeConditionArg(const TextTrigGenerator & generator,
                                const Chk::Condition & condition,
                                std::size_t argIndex,
                                const Scenario & scenario,
                                const Sc::Data & scData)
{
    using ArgType = Chk::Condition::ArgType;

    const Chk::Condition::Argument & argument =
        Chk::Condition::getTextArg(condition.conditionType, argIndex);

    TriggerArg out;
    if (argument.type == ArgType::NoType)
        return out;

    out.value = readField(condition, argument.field);
    out.text = generator.getConditionArgument(condition, argIndex);

    const auto addChoice = [&out](std::uint32_t value, std::string text) {
        if (!namedValue(text))
            return;
        out.choices.push_back(TriggerChoice{value, std::move(text)});
    };

    switch (argument.type)
    {
        case ArgType::Unit:
            out.kind = TriggerArgKind::Choice;
            out.label = "유닛";
            for (int i = 0; i < int(Sc::Unit::TotalReferenceTypes); ++i)
                addChoice(std::uint32_t(i), generator.getTrigUnit(Sc::Unit::Type(i)));
            break;

        case ArgType::Location:
            out.kind = TriggerArgKind::Choice;
            out.label = "위치";
            for (std::size_t i = 0; i <= scenario.numLocations(); ++i)
                addChoice(std::uint32_t(i), generator.getTrigLocation(i));
            break;

        case ArgType::Player:
            out.kind = TriggerArgKind::Choice;
            out.label = "플레이어";
            for (std::size_t i = 0; i < 27; ++i)
                addChoice(std::uint32_t(i), generator.getTrigPlayer(i));
            break;

        case ArgType::Amount:
            out.kind = TriggerArgKind::Number;
            out.label = "수량";
            break;

        case ArgType::NumericComparison:
        case ArgType::Comparison:
            out.kind = TriggerArgKind::Choice;
            out.label = "비교";
            for (int i = 0; i < 16; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigNumericComparison(Chk::Condition::Comparison(i)));
            break;

        case ArgType::SwitchState:
            out.kind = TriggerArgKind::Choice;
            out.label = "스위치 상태";
            for (int i = 0; i < 16; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigSwitchState(Chk::Trigger::ValueModifier(i)));
            break;

        case ArgType::ResourceType:
            out.kind = TriggerArgKind::Choice;
            out.label = "자원";
            for (int i = 0; i < 4; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigResourceType(Chk::Trigger::ResourceType(i)));
            break;

        case ArgType::ScoreType:
            out.kind = TriggerArgKind::Choice;
            out.label = "점수 종류";
            for (int i = 0; i < 12; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigScoreType(Chk::Trigger::ScoreType(i)));
            break;

        case ArgType::Switch:
            out.kind = TriggerArgKind::Choice;
            out.label = "스위치";
            for (std::size_t i = 0; i < 256; ++i)
                addChoice(std::uint32_t(i), generator.getTrigSwitch(i));
            break;

        case ArgType::TypeIndex:
            out.kind = TriggerArgKind::Number;
            out.label = "종류 번호";
            out.maximum = 255;
            break;

        case ArgType::MemoryOffset:
            out.kind = TriggerArgKind::Number;
            out.label = "메모리 위치";
            break;

        default:
            // ConditionType·Flags·MaskFlag 는 편집기가 따로 다루므로 숨긴다.
            out.kind = TriggerArgKind::None;
            break;
    }

    (void)scData;
    return out;
}

/// 액션 인자 한 자리를 편집기가 쓸 수 있는 꼴로 푼다.
TriggerArg describeActionArg(const TextTrigGenerator & generator,
                             const Chk::Action & action,
                             std::size_t argIndex,
                             const Scenario & scenario,
                             const Sc::Data & scData)
{
    using ArgType = Chk::Action::ArgType;

    const Chk::Action::Argument & argument =
        Chk::Action::getTextArg(action.actionType, argIndex);

    TriggerArg out;
    if (argument.type == ArgType::NoType)
        return out;

    out.value = readField(action, argument.field);
    out.text = generator.getActionArgument(action, argIndex);

    const auto addChoice = [&out](std::uint32_t value, std::string text) {
        if (!namedValue(text))
            return;
        out.choices.push_back(TriggerChoice{value, std::move(text)});
    };

    switch (argument.type)
    {
        case ArgType::Location:
            out.kind = TriggerArgKind::Choice;
            out.label = "위치";
            for (std::size_t i = 0; i <= scenario.numLocations(); ++i)
                addChoice(std::uint32_t(i), generator.getTrigLocation(i));
            break;

        case ArgType::String:
            out.kind = TriggerArgKind::Text;
            out.label = "문자열";
            break;

        case ArgType::Sound:
            out.kind = TriggerArgKind::Sound;
            out.label = "소리 파일";
            break;

        case ArgType::Player:
            out.kind = TriggerArgKind::Choice;
            out.label = "플레이어";
            for (std::size_t i = 0; i < 27; ++i)
                addChoice(std::uint32_t(i), generator.getTrigPlayer(i));
            break;

        case ArgType::Unit:
            out.kind = TriggerArgKind::Choice;
            out.label = "유닛";
            for (int i = 0; i < int(Sc::Unit::TotalReferenceTypes); ++i)
                addChoice(std::uint32_t(i), generator.getTrigUnit(Sc::Unit::Type(i)));
            break;

        case ArgType::NumUnits:
            // 0 은 "All" 이고 나머지는 개수다. 숫자로 두되 0 의 뜻을 적어 준다.
            out.kind = TriggerArgKind::Number;
            out.label = "유닛 수 (0=모두)";
            out.maximum = 255;
            break;

        case ArgType::CUWP:
            out.kind = TriggerArgKind::Number;
            out.label = "유닛 속성 번호";
            out.maximum = 63;
            break;

        case ArgType::TextFlags:
            out.kind = TriggerArgKind::Choice;
            out.label = "표시 방식";
            for (int i = 0; i < 8; ++i)
                addChoice(std::uint32_t(i), generator.getTrigTextFlags(Chk::Action::Flags(i)));
            break;

        case ArgType::Amount:
        case ArgType::Number:
            out.kind = TriggerArgKind::Number;
            out.label = "값";
            break;

        case ArgType::Percent:
            out.kind = TriggerArgKind::Number;
            out.label = "퍼센트";
            break;

        case ArgType::Duration:
            out.kind = TriggerArgKind::Number;
            out.label = "시간 (밀리초)";
            break;

        case ArgType::ScoreType:
            out.kind = TriggerArgKind::Choice;
            out.label = "점수 종류";
            for (int i = 0; i < 12; ++i)
                addChoice(std::uint32_t(i), generator.getTrigScoreType(Chk::Trigger::ScoreType(i)));
            break;

        case ArgType::ResourceType:
            out.kind = TriggerArgKind::Choice;
            out.label = "자원";
            for (int i = 0; i < 4; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigResourceType(Chk::Trigger::ResourceType(i)));
            break;

        case ArgType::StateMod:
            out.kind = TriggerArgKind::Choice;
            out.label = "상태";
            for (int i = 0; i < 16; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigStateModifier(Chk::Trigger::ValueModifier(i)));
            break;

        case ArgType::NumericMod:
            out.kind = TriggerArgKind::Choice;
            out.label = "수정 방식";
            for (int i = 0; i < 16; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigNumericModifier(Chk::Trigger::ValueModifier(i)));
            break;

        case ArgType::SwitchMod:
            out.kind = TriggerArgKind::Choice;
            out.label = "스위치 동작";
            for (int i = 0; i < 16; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigSwitchModifier(Chk::Trigger::ValueModifier(i)));
            break;

        case ArgType::Switch:
            out.kind = TriggerArgKind::Choice;
            out.label = "스위치";
            for (std::size_t i = 0; i < 256; ++i)
                addChoice(std::uint32_t(i), generator.getTrigSwitch(i));
            break;

        case ArgType::Order:
            out.kind = TriggerArgKind::Choice;
            out.label = "명령";
            for (int i = 0; i < 4; ++i)
                addChoice(std::uint32_t(i), generator.getTrigOrder(Chk::Action::Order(i)));
            break;

        case ArgType::AllyState:
            out.kind = TriggerArgKind::Choice;
            out.label = "동맹 상태";
            for (int i = 0; i < 4; ++i)
                addChoice(std::uint32_t(i),
                          generator.getTrigAllyState(Chk::Action::AllianceStatus(i)));
            break;

        case ArgType::Script:
            out.kind = TriggerArgKind::Choice;
            out.label = "AI 스크립트";
            for (std::size_t i = 0; i < scData.ai.numEntries(); ++i)
            {
                const std::uint32_t id = scData.ai.getEntry(i).identifier;
                addChoice(id, generator.getTrigScript(Sc::Ai::ScriptId(id)));
            }
            break;

        case ArgType::TypeIndex:
        case ArgType::SecondaryTypeIndex:
            out.kind = TriggerArgKind::Number;
            out.label = "종류 번호";
            out.maximum = 65535;
            break;

        case ArgType::MemoryOffset:
            out.kind = TriggerArgKind::Number;
            out.label = "메모리 위치";
            break;

        default:
            out.kind = TriggerArgKind::None;
            break;
    }

    return out;
}

} // namespace

std::vector<TriggerElement> MapArchive::triggerConditions(std::size_t index,
                                                          const GameGraphics & graphics) const
{
    std::vector<TriggerElement> out;
    if (!impl_->isOpen())
        return out;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return out;

    const MapFile & map = *impl_->mapFile;
    try
    {
        if (index >= map.numTriggers())
            return out;

        const Scenario & scenario = map;
        TextTrigGenerator generator(false, 0);
        if (!generator.loadScenario(scenario, *scData))
            return out;

        const Chk::Trigger & trigger = map.getTrigger(index);
        for (std::size_t slot = 0; slot < Chk::Trigger::MaxConditions; ++slot)
        {
            const Chk::Condition & condition = trigger.conditions[slot];

            TriggerElement element;
            element.type = std::uint8_t(condition.conditionType);
            element.disabled = condition.isDisabled();
            element.name = impl_->decode(generator.getConditionName(condition.conditionType));

            if (condition.conditionType != Chk::Condition::Type::NoCondition)
            {
                std::string text = element.name + "(";
                bool first = true;
                for (std::size_t i = 0; i < Chk::Condition::MaxArguments; ++i)
                {
                    TriggerArg arg = describeConditionArg(generator, condition, i,
                                                          scenario, *scData);
                    if (arg.kind == TriggerArgKind::None)
                        continue;

                    arg.text = impl_->decode(arg.text);
                    arg.label = arg.label;
                    for (auto & choice : arg.choices)
                        choice.text = impl_->decode(choice.text);

                    if (!first)
                        text += ", ";
                    text += arg.text;
                    first = false;

                    element.args.push_back(std::move(arg));
                }
                text += ")";
                element.text = text;
            }

            out.push_back(std::move(element));
        }
    }
    catch (const std::exception &)
    {
    }
    return out;
}

std::vector<TriggerElement> MapArchive::triggerActions(std::size_t index,
                                                       const GameGraphics & graphics) const
{
    std::vector<TriggerElement> out;
    if (!impl_->isOpen())
        return out;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return out;

    const MapFile & map = *impl_->mapFile;
    try
    {
        if (index >= map.numTriggers())
            return out;

        const Scenario & scenario = map;
        TextTrigGenerator generator(false, 0);
        if (!generator.loadScenario(scenario, *scData))
            return out;

        const Chk::Trigger & trigger = map.getTrigger(index);
        for (std::size_t slot = 0; slot < Chk::Trigger::MaxActions; ++slot)
        {
            const Chk::Action & action = trigger.actions[slot];

            TriggerElement element;
            element.type = std::uint8_t(action.actionType);
            element.disabled = action.isDisabled();
            element.name = impl_->decode(generator.getActionName(action.actionType));

            if (action.actionType != Chk::Action::Type::NoAction)
            {
                std::string text = element.name + "(";
                bool first = true;
                for (std::size_t i = 0; i < Chk::Action::MaxArguments; ++i)
                {
                    TriggerArg arg = describeActionArg(generator, action, i, scenario, *scData);
                    if (arg.kind == TriggerArgKind::None)
                        continue;

                    arg.text = impl_->decode(arg.text);
                    for (auto & choice : arg.choices)
                        choice.text = impl_->decode(choice.text);

                    if (!first)
                        text += ", ";
                    text += arg.text;
                    first = false;

                    element.args.push_back(std::move(arg));
                }
                text += ")";
                element.text = text;
            }

            out.push_back(std::move(element));
        }
    }
    catch (const std::exception &)
    {
    }
    return out;
}

std::vector<TriggerChoice> MapArchive::conditionTypes(const GameGraphics & graphics) const
{
    std::vector<TriggerChoice> out;
    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (!impl_->isOpen() || scData == nullptr)
        return out;

    try
    {
        const Scenario & scenario = *impl_->mapFile;
        TextTrigGenerator generator(false, 0);
        if (!generator.loadScenario(scenario, *scData))
            return out;

        for (std::size_t i = 0; i < Chk::Condition::NumConditionTypes; ++i)
        {
            std::string name = generator.getConditionName(Chk::Condition::Type(i));
            if (namedValue(name))
                out.push_back(TriggerChoice{std::uint32_t(i), impl_->decode(name)});
        }
    }
    catch (const std::exception &)
    {
    }
    return out;
}

std::vector<TriggerChoice> MapArchive::actionTypes(const GameGraphics & graphics) const
{
    std::vector<TriggerChoice> out;
    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (!impl_->isOpen() || scData == nullptr)
        return out;

    try
    {
        const Scenario & scenario = *impl_->mapFile;
        TextTrigGenerator generator(false, 0);
        if (!generator.loadScenario(scenario, *scData))
            return out;

        for (std::size_t i = 0; i < Chk::Action::NumActionTypes; ++i)
        {
            std::string name = generator.getActionName(Chk::Action::Type(i));
            if (namedValue(name))
                out.push_back(TriggerChoice{std::uint32_t(i), impl_->decode(name)});
        }
    }
    catch (const std::exception &)
    {
    }
    return out;
}

namespace {

/// 조건 하나를 고친 뒤 트리거를 다시 넣는다. MappingCore 는 트리거를
/// 통째로 다루므로, 지우고 넣는 두 액션이 한 편집이 된다.
template <typename Mutator>
Result mutateTrigger(MapFile & map, std::vector<int> & undoSteps, std::vector<int> & redoSteps,
                     std::size_t index, Mutator && mutate)
{
    if (index >= map.numTriggers())
        return Result::failure("트리거 번호가 범위를 벗어났습니다.");

    Chk::Trigger trigger = map.getTrigger(index);
    if (!mutate(trigger))
        return Result::failure("고칠 수 없는 자리입니다.");

    map.deleteTrigger(index);
    map.insertTrigger(index, trigger);
    undoSteps.push_back(2);
    redoSteps.clear();
    return Result::success();
}

} // namespace

Result MapArchive::setConditionType(std::size_t triggerIndex, std::size_t slot, std::uint8_t type)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [slot, type](Chk::Trigger & trigger) {
            if (slot >= Chk::Trigger::MaxConditions)
                return false;

            // 종류가 바뀌면 인자 자리의 뜻도 바뀐다. 남은 값이 엉뚱하게
            // 읽히지 않도록 비우고 기본 플래그를 새로 준다.
            Chk::Condition fresh {};
            fresh.conditionType = Chk::Condition::Type(type);
            fresh.flags = Chk::Condition::getDefaultFlags(fresh.conditionType);
            trigger.conditions[slot] = fresh;
            return true;
        });
}

Result MapArchive::setActionType(std::size_t triggerIndex, std::size_t slot, std::uint8_t type)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [slot, type](Chk::Trigger & trigger) {
            if (slot >= Chk::Trigger::MaxActions)
                return false;

            Chk::Action fresh {};
            fresh.actionType = Chk::Action::Type(type);
            fresh.flags = Chk::Action::getDefaultFlags(fresh.actionType);
            trigger.actions[slot] = fresh;
            return true;
        });
}

Result MapArchive::setConditionArg(std::size_t triggerIndex, std::size_t slot,
                                   std::size_t argIndex, std::uint32_t value)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [slot, argIndex, value](Chk::Trigger & trigger) {
            if (slot >= Chk::Trigger::MaxConditions)
                return false;

            Chk::Condition & condition = trigger.conditions[slot];

            // 편집기가 세는 인자 번호는 "쓰이는 자리" 기준이다. 빈 자리를
            // 건너뛰며 같은 번호를 찾는다.
            std::size_t seen = 0;
            for (std::size_t i = 0; i < Chk::Condition::MaxArguments; ++i)
            {
                const auto & argument = Chk::Condition::getTextArg(condition.conditionType, i);
                if (argument.type == Chk::Condition::ArgType::NoType ||
                    argument.field == Chk::Condition::ArgField::NoField)
                    continue;

                if (seen == argIndex)
                {
                    writeField(condition, argument.field, value);
                    return true;
                }
                ++seen;
            }
            return false;
        });
}

Result MapArchive::setActionArg(std::size_t triggerIndex, std::size_t slot,
                                std::size_t argIndex, std::uint32_t value)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [slot, argIndex, value](Chk::Trigger & trigger) {
            if (slot >= Chk::Trigger::MaxActions)
                return false;

            Chk::Action & action = trigger.actions[slot];

            std::size_t seen = 0;
            for (std::size_t i = 0; i < Chk::Action::MaxArguments; ++i)
            {
                const auto & argument = Chk::Action::getTextArg(action.actionType, i);
                if (argument.type == Chk::Action::ArgType::NoType ||
                    argument.field == Chk::Action::ArgField::NoField)
                    continue;

                if (seen == argIndex)
                {
                    writeField(action, argument.field, value);
                    return true;
                }
                ++seen;
            }
            return false;
        });
}

Result MapArchive::setActionArgText(std::size_t triggerIndex, std::size_t slot,
                                    std::size_t argIndex, const std::string & text)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        // 글자를 맵 문자열 표에 넣고, 그 번호를 인자에 적는다. 같은 글자가
        // 이미 있으면 그것을 다시 쓴다 — STR 이 쓸데없이 불어나지 않는다.
        const std::string encoded = impl_->encode(text);
        std::size_t stringId = map.findString(RawString(encoded));
        if (stringId == 0 || stringId == std::size_t(-1))
            stringId = map.addString(RawString(encoded));

        if (stringId == 0 || stringId == std::size_t(-1))
            return Result::failure("문자열을 넣지 못했습니다 (STR 이 가득 찼을 수 있습니다).");

        return setActionArg(triggerIndex, slot, argIndex, std::uint32_t(stringId));
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("문자열을 넣지 못했습니다: ") + e.what());
    }
}

Result MapArchive::setConditionDisabled(std::size_t triggerIndex, std::size_t slot, bool disabled)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [slot, disabled](Chk::Trigger & trigger) {
            if (slot >= Chk::Trigger::MaxConditions)
                return false;

            Chk::Condition & condition = trigger.conditions[slot];
            if (condition.isDisabled() != disabled)
                condition.toggleDisabled();
            return true;
        });
}

Result MapArchive::setActionDisabled(std::size_t triggerIndex, std::size_t slot, bool disabled)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [slot, disabled](Chk::Trigger & trigger) {
            if (slot >= Chk::Trigger::MaxActions)
                return false;

            Chk::Action & action = trigger.actions[slot];
            if (action.isDisabled() != disabled)
                action.toggleDisabled();
            return true;
        });
}

Result MapArchive::removeCondition(std::size_t triggerIndex, std::size_t slot)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [slot](Chk::Trigger & trigger) {
            if (slot >= Chk::Trigger::MaxConditions)
                return false;

            for (std::size_t i = slot; i + 1 < Chk::Trigger::MaxConditions; ++i)
                trigger.conditions[i] = trigger.conditions[i + 1];
            trigger.conditions[Chk::Trigger::MaxConditions - 1] = Chk::Condition {};
            return true;
        });
}

Result MapArchive::removeAction(std::size_t triggerIndex, std::size_t slot)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [slot](Chk::Trigger & trigger) {
            if (slot >= Chk::Trigger::MaxActions)
                return false;

            for (std::size_t i = slot; i + 1 < Chk::Trigger::MaxActions; ++i)
                trigger.actions[i] = trigger.actions[i + 1];
            trigger.actions[Chk::Trigger::MaxActions - 1] = Chk::Action {};
            return true;
        });
}

Result MapArchive::moveCondition(std::size_t triggerIndex, std::size_t from, std::size_t to)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [from, to](Chk::Trigger & trigger) {
            if (from >= Chk::Trigger::MaxConditions || to >= Chk::Trigger::MaxConditions)
                return false;

            std::swap(trigger.conditions[from], trigger.conditions[to]);
            return true;
        });
}

Result MapArchive::moveAction(std::size_t triggerIndex, std::size_t from, std::size_t to)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    return mutateTrigger(*impl_->mapFile, impl_->undoSteps, impl_->redoSteps, triggerIndex,
        [from, to](Chk::Trigger & trigger) {
            if (from >= Chk::Trigger::MaxActions || to >= Chk::Trigger::MaxActions)
                return false;

            std::swap(trigger.actions[from], trigger.actions[to]);
            return true;
        });
}

// ------------------------------------------------------------ 미션 브리핑

std::vector<BriefingSummary> MapArchive::briefingSummaries(const GameGraphics & graphics) const
{
    std::vector<BriefingSummary> out;
    if (!impl_->isOpen())
        return out;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    const MapFile & map = *impl_->mapFile;

    try
    {
        BriefingTextTrigGenerator generator;
        const std::size_t count = map.numBriefingTriggers();
        out.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            const Chk::Trigger & trigger = map.getBriefingTrigger(i);

            BriefingSummary summary;
            summary.index = i;
            summary.players = describeOwners(trigger);

            for (const auto & action : trigger.actions)
            {
                if (action.actionType != Chk::Action::Type::BriefingNoAction)
                    ++summary.actions;
            }

            // 첫 동작 한 줄은 텍스트 생성기에서 가져온다 — 이름표가 붙어야
            // "Display Speaking Portrait" 처럼 읽히기 때문이다.
            if (scData != nullptr && summary.actions > 0)
            {
                std::string text;
                const Scenario & scenario = map;
                if (generator.generateBriefingTextTrigs(scenario, i, text, *scData))
                {
                    const std::size_t bodyAt = text.find('{');
                    if (bodyAt != std::string::npos)
                    {
                        const std::size_t lineStart = text.find_first_not_of(" \t\r\n", bodyAt + 1);
                        if (lineStart != std::string::npos)
                        {
                            std::size_t lineEnd = text.find_first_of(";\n", lineStart);
                            if (lineEnd == std::string::npos)
                                lineEnd = text.size();
                            summary.firstAction =
                                impl_->decode(text.substr(lineStart, lineEnd - lineStart));
                        }
                    }
                }
            }

            out.push_back(std::move(summary));
        }
    }
    catch (const std::exception &)
    {
    }

    return out;
}

std::optional<BriefingDetail> MapArchive::briefingDetail(std::size_t index,
                                                        const GameGraphics & graphics) const
{
    if (!impl_->isOpen())
        return std::nullopt;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return std::nullopt;

    const MapFile & map = *impl_->mapFile;

    try
    {
        if (index >= map.numBriefingTriggers())
            return std::nullopt;

        const Chk::Trigger & trigger = map.getBriefingTrigger(index);

        BriefingDetail detail;
        for (std::size_t i = 0; i < 27 && i < Chk::Trigger::MaxOwners; ++i)
            detail.owners[i] = (trigger.owners[i] == Chk::Trigger::Owned::Yes);

        BriefingTextTrigGenerator generator;
        std::string text;
        const Scenario & scenario = map;
        if (generator.generateBriefingTextTrigs(scenario, index, text, *scData))
            detail.text = impl_->decode(text);

        // 본문의 들여쓴 줄이 동작 하나씩이다.
        const std::size_t bodyAt = detail.text.find('{');
        if (bodyAt != std::string::npos)
        {
            std::size_t start = bodyAt + 1;
            while (start < detail.text.size())
            {
                std::size_t end = detail.text.find('\n', start);
                if (end == std::string::npos)
                    end = detail.text.size();

                std::string line = detail.text.substr(start, end - start);
                start = end + 1;

                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos)
                    continue;
                line = line.substr(first);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.pop_back();

                if (line.empty() || line == "}" || line.rfind("//", 0) == 0)
                    continue;

                detail.actions.push_back(std::move(line));
            }
        }

        return detail;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

std::optional<std::string> MapArchive::briefingText(const GameGraphics & graphics) const
{
    if (!impl_->isOpen())
        return std::nullopt;

    const auto * scData = static_cast<const Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return std::nullopt;

    try
    {
        BriefingTextTrigGenerator generator;
        std::string text;
        const Scenario & scenario = *impl_->mapFile;
        if (!generator.generateBriefingTextTrigs(scenario, text, *scData))
            return std::nullopt;

        return impl_->decode(text);
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

Result MapArchive::setBriefingText(std::size_t index, const std::string & text,
                                   GameGraphics & graphics)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    auto * scData = static_cast<Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return Result::failure("게임 데이터가 준비되지 않았습니다.");

    try
    {
        if (index >= impl_->mapFile->numBriefingTriggers())
            return Result::failure("브리핑 번호가 범위를 벗어났습니다.");

        BriefingTextTrigCompiler compiler;
        std::string working = impl_->encode(text);
        Scenario & scenario = *impl_->mapFile;

        if (!compiler.compileBriefingTrigger(working, scenario, *scData, index))
            return Result::failure("브리핑 문법에 오류가 있습니다.");

        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("브리핑을 컴파일하지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setBriefingText(const std::string & text, GameGraphics & graphics)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    auto * scData = static_cast<Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return Result::failure("게임 데이터가 준비되지 않았습니다.");

    try
    {
        BriefingTextTrigCompiler compiler;
        std::string working = impl_->encode(text);
        Scenario & scenario = *impl_->mapFile;

        const std::size_t count = scenario.numBriefingTriggers();
        if (!compiler.compileBriefingTriggers(working, scenario, *scData, 0, count))
            return Result::failure("브리핑 문법에 오류가 있습니다.");

        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("브리핑을 컴파일하지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::addBriefing()
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    try
    {
        // 빈 브리핑 한 줄. 아무도 보지 않는 상태로 시작하고, 플레이어는
        // 편집기에서 고른다.
        Chk::Trigger briefing {};
        impl_->mapFile->addBriefingTrigger(briefing);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("브리핑을 더하지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::removeBriefing(std::size_t index)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    try
    {
        if (index >= impl_->mapFile->numBriefingTriggers())
            return Result::failure("브리핑 번호가 범위를 벗어났습니다.");

        impl_->mapFile->deleteBriefingTrigger(index);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("브리핑을 지우지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::moveBriefing(std::size_t from, std::size_t to)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    try
    {
        const std::size_t count = impl_->mapFile->numBriefingTriggers();
        if (from >= count || to >= count)
            return Result::failure("브리핑 번호가 범위를 벗어났습니다.");

        impl_->mapFile->moveBriefingTrigger(from, to);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("브리핑 순서를 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setBriefingOwners(std::size_t index, const std::array<bool, 27> & owners)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (index >= map.numBriefingTriggers())
            return Result::failure("브리핑 번호가 범위를 벗어났습니다.");

        Chk::Trigger briefing = map.getBriefingTrigger(index);
        for (std::size_t i = 0; i < 27 && i < Chk::Trigger::MaxOwners; ++i)
        {
            briefing.owners[i] = owners[i] ? Chk::Trigger::Owned::Yes
                                           : Chk::Trigger::Owned::No;
        }

        map.deleteBriefingTrigger(index);
        map.insertBriefingTrigger(index, briefing);
        impl_->undoSteps.push_back(2);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("브리핑 플레이어를 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setTriggerOwners(std::size_t index, const std::array<bool, 27> & owners)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (index >= map.numTriggers())
            return Result::failure("트리거 번호가 범위를 벗어났습니다.");

        Chk::Trigger trigger = map.getTrigger(index);
        for (std::size_t i = 0; i < 27 && i < Chk::Trigger::MaxOwners; ++i)
        {
            trigger.owners[i] = owners[i] ? Chk::Trigger::Owned::Yes
                                          : Chk::Trigger::Owned::No;
        }

        map.deleteTrigger(index);
        map.insertTrigger(index, trigger);
        impl_->undoSteps.push_back(2);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("플레이어를 바꾸지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setTriggerEnabled(std::size_t index, bool enabled)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    MapFile & map = *impl_->mapFile;
    try
    {
        if (index >= map.numTriggers())
            return Result::failure("트리거 번호가 범위를 벗어났습니다.");

        Chk::Trigger trigger = map.getTrigger(index);
        if (enabled)
            trigger.flags &= ~std::uint32_t(Chk::Trigger::Flags::Disabled);
        else
            trigger.flags |= std::uint32_t(Chk::Trigger::Flags::Disabled);

        map.deleteTrigger(index);
        map.insertTrigger(index, trigger);
        impl_->undoSteps.push_back(2);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("트리거를 켜고 끄지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::removeTrigger(std::size_t index)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    try
    {
        if (index >= impl_->mapFile->numTriggers())
            return Result::failure("트리거 번호가 범위를 벗어났습니다.");

        impl_->mapFile->deleteTrigger(index);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("트리거를 지우지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::addTrigger()
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");
    try
    {
        Chk::Trigger trigger {};
        // 새 트리거는 플레이어 1이 실행하도록 둔다 — 아무도 실행하지 않는
        // 트리거는 만들어 놓고 잊기 쉽다.
        trigger.owners[0] = Chk::Trigger::Owned::Yes;

        impl_->mapFile->addTrigger(trigger);
        impl_->undoSteps.push_back(1);
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("트리거를 더하지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setTriggerText(std::size_t index, const std::string & text,
                                  GameGraphics & graphics)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    auto * scData = static_cast<Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return Result::failure("게임 데이터가 준비되지 않았습니다.");

    try
    {
        if (index >= impl_->mapFile->numTriggers())
            return Result::failure("트리거 번호가 범위를 벗어났습니다.");

        TextTrigCompiler compiler(false, 0);
        // 편집기는 UTF-8 로 넘겨 준다. 맵이 쓰는 코드 페이지로 되돌려야
        // 게임이 같은 글자로 읽는다.
        std::string working = impl_->encode(text);
        Scenario & scenario = *impl_->mapFile;

        if (!compiler.compileTrigger(working, scenario, *scData, index))
            return Result::failure("트리거 문법에 오류가 있습니다.");

        // 컴파일은 TRIG 과 STR 을 함께 바꾼다. 몇 액션이 생기는지 알 수 없어
        // 이력을 비운다.
        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("트리거를 컴파일하지 못했습니다: ") + e.what());
    }
    return Result::success();
}

Result MapArchive::setTriggerText(const std::string & text, GameGraphics & graphics)
{
    if (!impl_->isOpen())
        return Result::failure("열린 맵이 없습니다.");

    auto * scData = static_cast<Sc::Data *>(graphics.internalScData());
    if (scData == nullptr)
        return Result::failure("게임 데이터가 준비되지 않았습니다.");

    try
    {
        TextTrigCompiler compiler(false, 0);

        // 컴파일러는 문자열을 다듬으며 읽으므로 사본을 넘긴다. 맵이 쓰는
        // 코드 페이지로 되돌려 넘긴다.
        std::string working = impl_->encode(text);
        Scenario & scenario = *impl_->mapFile;

        // 트리거 전체를 교체한다. 범위를 0~현재개수로 주면 그 구간이 새 내용이 된다.
        const std::size_t count = scenario.numTriggers();
        if (!compiler.compileTriggers(working, scenario, *scData, 0, count))
            return Result::failure("트리거 문법에 오류가 있습니다.");

        // 컴파일은 TRIG 과 STR 을 한꺼번에 바꾼다. 몇 액션이 생기는지 알 수
        // 없으므로, 실행 취소 단위로 묶지 않고 이력을 비운다 — 잘못 묶어
        // 엉뚱한 곳까지 되돌리는 것보다 안전하다.
        impl_->undoSteps.clear();
        impl_->redoSteps.clear();
    }
    catch (const std::exception & e)
    {
        return Result::failure(std::string("트리거를 컴파일하지 못했습니다: ") + e.what());
    }
    return Result::success();
}

std::vector<std::uint16_t> MapArchive::terrainTiles() const
{
    if (!impl_->isOpen())
        return {};

    const MapFile & map = *impl_->mapFile;

    try
    {
        const std::size_t width  = map.getTileWidth();
        const std::size_t height = map.getTileHeight();
        if (width == 0 || height == 0)
            return {};

        // TILE(에디터용)을 우선 쓰고, 없으면 MTXM(게임용)으로 내려간다.
        // 보호된 맵은 TILE 을 비워 두거나 잘라 놓는 경우가 흔하다.
        //
        // TILE 이 아예 없는 맵에서도 MappingCore 는 자리만 0 으로 채워 둔다.
        // 크기만 보고 고르면 지형이 통째로 검게 나오므로, 섹션이 실제로
        // 들어 있는지와 내용이 비어 있지 않은지를 함께 본다.
        const auto & editorTiles = map.read.editorTiles;
        const auto & gameTiles   = map.read.tiles;

        const bool editorUsable =
            map.hasSection(Chk::SectionName::TILE) &&
            editorTiles.size() >= width * height &&
            std::any_of(editorTiles.begin(), editorTiles.end(),
                        [](std::uint16_t tile) { return tile != 0; });

        const auto & source = editorUsable ? editorTiles : gameTiles;

        std::vector<std::uint16_t> out(width * height, 0);
        const std::size_t available = std::min(source.size(), out.size());
        for (std::size_t i = 0; i < available; ++i)
            out[i] = source[i];

        return out;
    }
    catch (const std::exception &)
    {
        return {};
    }
}

RawMapInfo MapArchive::info() const
{
    RawMapInfo out;
    if (!impl_->isOpen())
        return out;

    const MapFile & map = *impl_->mapFile;

    try
    {
        if (auto name = map.getScenarioName<RawString>())
            out.scenarioName = impl_->decode(*name);
        if (auto desc = map.getScenarioDescription<RawString>())
            out.scenarioDescription = impl_->decode(*desc);

        out.tileWidth  = static_cast<std::uint16_t>(map.getTileWidth());
        out.tileHeight = static_cast<std::uint16_t>(map.getTileHeight());
        out.tilesetId  = static_cast<std::uint16_t>(map.getTileset());
        out.versionId  = static_cast<std::uint16_t>(map.getVersion());

        out.unitCount     = map.numUnits();
        out.locationCount = map.numLocations();
        out.triggerCount  = map.numTriggers();
        out.stringCount   = map.getCapacity();
        out.isProtected   = map.isProtected();
    }
    catch (const std::exception &)
    {
        // 메타데이터 일부를 못 읽어도 나머지는 그대로 돌려준다.
        // 열기 자체는 이미 성공했으므로 전체를 실패로 만들 이유가 없다.
    }

    return out;
}

} // namespace splash::io
