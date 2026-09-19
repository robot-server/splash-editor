#include "io/map_archive.h"

// MappingCore 헤더는 이 번역 단위 안에만 존재한다.
#include "cross_cut/logger.h"
#include "mapping_core/map_file.h"

#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>

// MappingCore 의 여러 번역 단위가 `extern Logger logger` 로 참조하는 전역.
// Chkdraft 본체에서는 main.cpp 가 정의한다. 라이브러리로 쓰는 쪽에서는
// 우리가 제공해야 링크가 된다. 기본은 Error — 파싱 중 경고가 stdout 을
// 오염시키면 CLI 의 round-trip 출력과 뒤섞인다.
Logger logger(LogLevel::Error);

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

struct MapArchive::Impl
{
    // MapFile 은 복사도 이동도 되지 않는다(Scenario 가 대입 연산자를 지운다).
    // 새 맵으로 통째로 교체하려면 포인터로 들고 있어야 한다.
    std::unique_ptr<MapFile> mapFile;
    std::string sourcePath;

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

    try
    {
        // 인자 의미 (MappingCore MapFile::save):
        //   overwriting             = true  기존 파일 교체를 허용
        //   updateListFile          = true  MPQ (listfile) 유지 — 게임/에디터 호환
        //   lockAnywhere            = false "Anywhere" 로케이션을 건드리지 않는다
        //   autoDefragmentLocations = false MRGN 을 재배치하지 않는다
        //
        // 뒤의 둘을 끄는 이유: 우리가 편집하지 않은 섹션의 바이트를 바꾸기 때문이다.
        // M1 의 목표는 "열고 다시 저장해도 맵이 그대로"이므로 보존을 우선한다.
        const bool saved = impl_->mapFile->save(
            filePath,
            /*overwriting*/ true,
            /*updateListFile*/ true,
            /*lockAnywhere*/ false,
            /*autoDefragmentLocations*/ false);

        if (!saved)
            return Result::failure("맵을 저장하지 못했습니다: " + filePath);
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
    }
    catch (const std::exception &)
    {
        // 메타데이터 일부를 못 읽어도 나머지는 그대로 돌려준다.
        // 열기 자체는 이미 성공했으므로 전체를 실패로 만들 이유가 없다.
    }

    return out;
}

} // namespace splash::io
