#include "chk/map_document.h"

#include <algorithm>
#include <array>
#include <filesystem>

namespace splash::chk {
namespace {

// 표시용 타일셋 이름. MappingCore 의 Sc::Terrain::TilesetNames 는
// 에셋 파일명("badlands", "platform", "install", ...)이라 UI 라벨로 쓸 수 없다.
// 여기 값은 SCMDraft 2 / Chkdraft 가 사용자에게 보여 주는 표기를 따른다.
constexpr std::array<const char *, 8> kTilesetNames = {
    "Badlands",
    "Space Platform",
    "Installation",
    "Ashworld",
    "Jungle",
    "Desert",
    "Ice",
    "Twilight",
};

} // namespace

PlayerColor playerColor(std::uint8_t owner)
{
    // StarCraft 의 기본 플레이어 색. 맵이 CRGB 로 덮어쓸 수 있지만,
    // 표시용 기본값으로는 이것으로 충분하다.
    static const PlayerColor kColors[] = {
        {244,  4,  4}, // 0 빨강
        { 12, 72,204}, // 1 파랑
        { 44,180,148}, // 2 청록
        {136, 64,156}, // 3 보라
        {248,140, 20}, // 4 주황
        {112, 48, 20}, // 5 갈색
        {204,168,252}, // 6 연보라
        { 16,  0,124}, // 7 남색
        { 56, 12, 44}, // 8
        {248,224,120}, // 9 노랑
        { 16,128, 96}, // 10 초록
        {239,231, 55}, // 11 중립(노랑)
    };
    constexpr std::size_t count = sizeof(kColors) / sizeof(kColors[0]);
    return kColors[owner < count ? owner : count - 1];
}

std::string tilesetDisplayName(std::uint16_t tilesetId)
{
    // 맵은 타일셋 상위 비트에 값을 남겨 두기도 한다. StarCraft 는 8 로 나눈
    // 나머지를 쓴다(MappingCore 의 Sc::Terrain::hasWater 도 동일한 관례).
    const std::size_t index = static_cast<std::size_t>(tilesetId) % kTilesetNames.size();
    std::string name = kTilesetNames[index];

    // 원시값이 0..7 밖이면 표기에 남겨 둔다. 정보를 조용히 버리지 않는다.
    if (tilesetId >= kTilesetNames.size())
        name += " (raw " + std::to_string(tilesetId) + ")";

    return name;
}

std::string versionDisplayName(std::uint16_t versionId)
{
    // Chk::Version (chk.h) 의 값들.
    switch (versionId)
    {
        case 59:  return "StarCraft (original)";
        case 63:  return "Hybrid (1.04+)";
        case 205: return "Brood War";
        case 206: return "Remastered";
        default:  return "알 수 없음 (" + std::to_string(versionId) + ")";
    }
}

MapDocument::MapDocument() = default;
MapDocument::~MapDocument() = default;
MapDocument::MapDocument(MapDocument &&) noexcept = default;
MapDocument & MapDocument::operator=(MapDocument &&) noexcept = default;

bool MapDocument::open(const std::string & filePath)
{
    const io::Result result = archive_.open(filePath);
    if (!result)
    {
        // 열기에 실패하면 이전 문서를 계속 들고 있는 것이 더 혼란스럽다.
        // 확실히 닫힌 상태로 만들고 사유만 남긴다.
        const std::string why = result.message;
        close();
        lastError_ = why;
        return false;
    }

    filePath_ = filePath;
    modified_ = false;
    undoDepth_ = 0;
    redoDepth_ = 0;
    savedDepth_ = 0;
    lastError_.clear();
    refreshInfo();
    return true;
}

bool MapDocument::save()
{
    if (!isOpen())
    {
        lastError_ = "열린 맵이 없습니다.";
        return false;
    }
    return saveAs(filePath_);
}

bool MapDocument::saveAs(const std::string & filePath)
{
    if (!isOpen())
    {
        lastError_ = "열린 맵이 없습니다.";
        return false;
    }

    const io::Result result = archive_.saveAs(filePath);
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }

    filePath_ = filePath;
    modified_ = false;
    savedDepth_ = undoDepth_; // 지금 상태가 저장된 상태다
    lastError_.clear();
    return true;
}

void MapDocument::close()
{
    archive_.close();
    info_ = MapInfo{};
    tiles_.clear();
    units_.clear();
    sprites_.clear();
    locations_.clear();
    filePath_.clear();
    lastError_.clear();
    modified_ = false;
    undoDepth_ = 0;
    redoDepth_ = 0;
    savedDepth_ = 0;
}

bool MapDocument::isOpen() const
{
    return archive_.isOpen();
}

bool MapDocument::isModified() const
{
    return modified_;
}

void MapDocument::markModified()
{
    modified_ = true;
}

bool MapDocument::moveUnit(std::size_t unitIndex, std::uint16_t x, std::uint16_t y)
{
    const io::Result result = archive_.moveUnit(unitIndex, x, y);
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeUnit(std::size_t unitIndex)
{
    const io::Result result = archive_.removeUnit(unitIndex);
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setUnitOwner(std::size_t unitIndex, std::uint8_t owner)
{
    const io::Result result = archive_.setUnitOwner(unitIndex, owner);
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::canUndo() const { return undoDepth_ > 0; }
bool MapDocument::canRedo() const { return redoDepth_ > 0; }

bool MapDocument::undo()
{
    const io::Result result = archive_.undo();
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }
    --undoDepth_;
    ++redoDepth_;
    // 편집을 전부 되돌리면 저장된 상태로 돌아온 것이다.
    modified_ = (undoDepth_ != savedDepth_);
    refreshInfo();
    return true;
}

bool MapDocument::redo()
{
    const io::Result result = archive_.redo();
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }
    ++undoDepth_;
    --redoDepth_;
    modified_ = (undoDepth_ != savedDepth_);
    refreshInfo();
    return true;
}

const MapInfo & MapDocument::info() const
{
    return info_;
}

const std::string & MapDocument::filePath() const
{
    return filePath_;
}

std::string MapDocument::fileName() const
{
    if (filePath_.empty())
        return {};
    return std::filesystem::path(filePath_).filename().string();
}

const std::string & MapDocument::lastError() const
{
    return lastError_;
}

const std::vector<std::uint16_t> & MapDocument::tiles() const
{
    return tiles_;
}

const std::vector<MapUnit> & MapDocument::units() const
{
    return units_;
}

const std::vector<MapSprite> & MapDocument::sprites() const
{
    return sprites_;
}

const std::vector<MapLocation> & MapDocument::locations() const
{
    return locations_;
}

void MapDocument::refreshInfo()
{
    tiles_ = archive_.terrainTiles();

    units_.clear();
    for (const io::RawUnit & raw : archive_.units())
    {
        MapUnit unit;
        unit.x        = raw.x;
        unit.y        = raw.y;
        unit.type     = raw.type;
        unit.owner    = raw.owner;
        unit.resourceAmount = raw.resourceAmount;
        unit.typeName = io::unitTypeName(raw.type);
        units_.push_back(std::move(unit));
    }

    sprites_.clear();
    for (const io::RawSprite & raw : archive_.sprites())
    {
        MapSprite sprite;
        sprite.x             = raw.x;
        sprite.y             = raw.y;
        sprite.type          = raw.type;
        sprite.owner         = raw.owner;
        sprite.drawnAsSprite = raw.drawnAsSprite;
        sprites_.push_back(sprite);
    }

    locations_.clear();
    for (const io::RawLocation & raw : archive_.locations())
    {
        MapLocation location;
        // 사용자가 반대로 끌어 만든 로케이션은 좌우/상하가 뒤집혀 저장된다.
        // 원본 바이트는 건드리지 않고 표시용으로만 정규화한다.
        location.left   = std::min(raw.left, raw.right);
        location.right  = std::max(raw.left, raw.right);
        location.top    = std::min(raw.top, raw.bottom);
        location.bottom = std::max(raw.top, raw.bottom);
        location.name   = raw.name;
        location.index  = raw.index;
        locations_.push_back(std::move(location));
    }

    const io::RawMapInfo raw = archive_.info();

    info_.name          = raw.scenarioName;
    info_.description   = raw.scenarioDescription;
    info_.width         = raw.tileWidth;
    info_.height        = raw.tileHeight;
    info_.tilesetId     = raw.tilesetId;
    info_.tilesetName   = tilesetDisplayName(raw.tilesetId);
    info_.versionId     = raw.versionId;
    info_.versionName   = versionDisplayName(raw.versionId);
    info_.unitCount     = raw.unitCount;
    info_.locationCount = raw.locationCount;
    info_.triggerCount  = raw.triggerCount;
    info_.stringCount   = raw.stringCount;
    info_.isProtected   = raw.isProtected;
}

} // namespace splash::chk
