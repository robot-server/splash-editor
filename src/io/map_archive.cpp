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

} // namespace

std::string unitTypeName(std::uint16_t type)
{
    const auto & names = Sc::Unit::defaultDisplayNames;
    if (type < names.size())
        return names[type];

    return "Unit " + std::to_string(type);
}

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
    impl_ = std::move(fresh);
    return Result::success();
}

Result MapArchive::createNew(MapFormat format,
                             std::uint16_t tilesetId,
                             std::uint16_t width,
                             std::uint16_t height,
                             bool meleeTriggers)
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
        // tilesetData 가 null 이면 지형은 기본 타일로 채워진다.
        // 실제 타일셋 에셋(MPQ/CASC)은 M1 범위 밖이다.
        fresh->mapFile = std::make_unique<MapFile>(
            Sc::Terrain::Tileset(tilesetId), width, height,
            /*terrainTypeIndex*/ 0, triggers, saveType, /*tilesetData*/ nullptr);
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
        impl_->mapFile->setScenarioName(RawString(name));
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
        impl_->mapFile->setScenarioDescription(RawString(description));
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
                raw.name = *name;

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

        return text;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
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

        // 컴파일러는 문자열을 다듬으며 읽으므로 사본을 넘긴다.
        std::string working = text;
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
        const auto & editorTiles = map.read.editorTiles;
        const auto & gameTiles   = map.read.tiles;
        const auto & source =
            (editorTiles.size() >= width * height) ? editorTiles : gameTiles;

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
            out.scenarioName = *name;
        if (auto desc = map.getScenarioDescription<RawString>())
            out.scenarioDescription = *desc;

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
