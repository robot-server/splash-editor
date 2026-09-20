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

bool MapDocument::createNew(io::MapFormat format, std::uint16_t tilesetId,
                            std::uint16_t width, std::uint16_t height,
                            io::MapArchive::DefaultTriggers defaultTriggers,
                            const io::GameGraphics * graphics,
                            std::size_t terrainTypeIndex)
{
    const io::Result result = archive_.createNew(format, tilesetId, width, height,
                                                 defaultTriggers, graphics, terrainTypeIndex);
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }

    // 새 맵은 아직 저장된 적이 없다 — 경로가 없고 저장하면 물어봐야 한다.
    filePath_.clear();
    modified_ = true;
    undoDepth_ = 0;
    redoDepth_ = 0;
    savedDepth_ = -1;
    lastError_.clear();
    refreshInfo();
    return true;
}

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

bool MapDocument::saveCopy(const std::string & filePath) const
{
    if (!isOpen())
    {
        const_cast<MapDocument *>(this)->lastError_ = "열린 맵이 없습니다.";
        return false;
    }

    const io::Result result = archive_.saveAs(filePath);
    if (!result)
    {
        const_cast<MapDocument *>(this)->lastError_ = result.message;
        return false;
    }
    return true;
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

bool MapDocument::addSprite(std::uint16_t spriteType, std::uint8_t owner,
                            std::uint16_t x, std::uint16_t y, bool drawnAsSprite)
{
    const io::Result result = archive_.addSprite(spriteType, owner, x, y, drawnAsSprite);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeSprite(std::size_t spriteIndex)
{
    const io::Result result = archive_.removeSprite(spriteIndex);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::optional<io::MapArchive::SpriteProperties> MapDocument::spriteProperties(
    std::size_t spriteIndex) const
{
    return archive_.spriteProperties(spriteIndex);
}

bool MapDocument::setSpriteProperties(std::size_t spriteIndex,
                                      const io::MapArchive::SpriteProperties & properties)
{
    const io::Result result = archive_.setSpriteProperties(spriteIndex, properties);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::addUnit(std::uint16_t unitType, std::uint8_t owner,
                          std::uint16_t x, std::uint16_t y)
{
    const io::Result result = archive_.addUnit(unitType, owner, x, y);
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

bool MapDocument::placeIsomTerrain(io::GameGraphics & graphics,
                                   std::size_t pixelX, std::size_t pixelY,
                                   std::size_t terrainType, std::size_t brushExtent)
{
    const io::Result result =
        archive_.placeIsomTerrain(graphics, pixelX, pixelY, terrainType, brushExtent);
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::setTile(std::size_t tileX, std::size_t tileY, std::uint16_t tileValue)
{
    const io::Result result = archive_.setTile(tileX, tileY, tileValue);
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

bool MapDocument::writeTiles(const std::vector<io::MapArchive::TileWrite> & writes)
{
    const io::Result result = archive_.writeTiles(writes);
    if (!result) { lastError_ = result.message; return false; }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setTiles(const std::vector<std::pair<std::size_t, std::size_t>> & positions,
                           std::uint16_t tileValue)
{
    if (positions.empty())
        return false;

    // 브러시 한 획은 실행 취소도 한 번에 되돌아가야 한다.
    // 타일마다 액션이 하나씩 생기므로, 획 전체를 한 묶음으로 센다.
    int applied = 0;
    for (const auto & [x, y] : positions)
    {
        if (archive_.setTile(x, y, tileValue))
            ++applied;
    }

    if (applied == 0)
        return false;

    // archive_ 는 타일마다 1 액션으로 세어 두었다. 그것들을 한 묶음으로 합친다.
    archive_.mergeLastEdits(applied);

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::linkUnits(std::size_t unitA, std::size_t unitB, bool addon)
{
    const io::Result result = archive_.linkUnits(unitA, unitB, addon);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::unlinkUnit(std::size_t unitIndex)
{
    const io::Result result = archive_.unlinkUnit(unitIndex);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setLocationBounds(std::size_t locationIndex, std::uint32_t left,
                                    std::uint32_t top, std::uint32_t right,
                                    std::uint32_t bottom)
{
    if (locationIndex >= locations_.size())
    {
        lastError_ = "로케이션 번호가 범위를 벗어났습니다.";
        return false;
    }

    // 모서리가 뒤집히면 게임이 빈 로케이션으로 본다. 정렬해 둔다.
    if (left > right)
        std::swap(left, right);
    if (top > bottom)
        std::swap(top, bottom);

    const io::Result result = archive_.setLocationBounds(
        locations_[locationIndex].index, left, top, right, bottom);
    if (!result) { lastError_ = result.message; return false; }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::addLocation(std::uint32_t left, std::uint32_t top,
                              std::uint32_t right, std::uint32_t bottom,
                              const std::string & name,
                              std::size_t * outIndex)
{
    if (left > right)
        std::swap(left, right);
    if (top > bottom)
        std::swap(top, bottom);

    const std::size_t created = archive_.addLocation(left, top, right, bottom, name);
    if (created == 0)
    {
        lastError_ = "로케이션을 만들지 못했습니다 (자리가 가득 찼을 수 있습니다).";
        return false;
    }

    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();

    // MRGN 번호를 목록에서의 자리로 옮긴다 — 새 로케이션이 끝에 오지 않는다.
    if (outIndex != nullptr)
    {
        *outIndex = locations_.size();
        for (std::size_t i = 0; i < locations_.size(); ++i)
        {
            if (locations_[i].index == created)
            {
                *outIndex = i;
                break;
            }
        }
    }
    return true;
}

bool MapDocument::removeLocation(std::size_t locationIndex, bool force)
{
    if (locationIndex >= locations_.size())
    {
        lastError_ = "로케이션 번호가 범위를 벗어났습니다.";
        return false;
    }

    const io::Result result = archive_.removeLocation(locations_[locationIndex].index, force);
    if (!result) { lastError_ = result.message; return false; }

    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::setLocationName(std::size_t locationIndex, const std::string & name)
{
    if (locationIndex >= locations_.size())
    {
        lastError_ = "로케이션 번호가 범위를 벗어났습니다.";
        return false;
    }

    const io::Result result = archive_.setLocationName(locations_[locationIndex].index, name);
    if (!result) { lastError_ = result.message; return false; }

    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::locationInverted(std::size_t locationIndex) const
{
    if (locationIndex >= locations_.size())
        return false;

    // locations_ 는 보기 좋게 정규화해 두므로 원본을 다시 본다.
    const auto raw = archive_.locations();
    for (const auto & entry : raw)
    {
        if (entry.index == locations_[locationIndex].index)
            return entry.left > entry.right || entry.top > entry.bottom;
    }
    return false;
}

bool MapDocument::setLocationInverted(std::size_t locationIndex, bool inverted)
{
    if (locationIndex >= locations_.size())
    {
        lastError_ = "로케이션 번호가 범위를 벗어났습니다.";
        return false;
    }

    const auto & location = locations_[locationIndex];

    // 정규화된 값을 기준으로, 뒤집을지 말지에 따라 모서리를 넣는다.
    const std::uint32_t left = inverted ? location.right : location.left;
    const std::uint32_t right = inverted ? location.left : location.right;

    const io::Result result = archive_.setLocationBounds(
        location.index, left, location.top, right, location.bottom);

    if (!result) { lastError_ = result.message; return false; }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setLocationElevationFlags(std::size_t locationIndex, std::uint16_t flags)
{
    if (locationIndex >= locations_.size())
    {
        lastError_ = "로케이션 번호가 범위를 벗어났습니다.";
        return false;
    }

    const io::Result result =
        archive_.setLocationElevationFlags(locations_[locationIndex].index, flags);
    if (!result) { lastError_ = result.message; return false; }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::moveLocation(std::size_t locationIndex, std::int64_t dx, std::int64_t dy)
{
    if (locationIndex >= locations_.size())
    {
        lastError_ = "로케이션 번호가 범위를 벗어났습니다.";
        return false;
    }

    const MapLocation & current = locations_[locationIndex];

    // 맵 밖으로 나가지 않도록 잘라 낸다. 좌표는 부호 없는 값이라
    // 음수로 내려가면 거대한 값이 되어 버린다.
    const std::int64_t width  = static_cast<std::int64_t>(current.right) - current.left;
    const std::int64_t height = static_cast<std::int64_t>(current.bottom) - current.top;
    const std::int64_t maxX = static_cast<std::int64_t>(info_.width) * 32 - width;
    const std::int64_t maxY = static_cast<std::int64_t>(info_.height) * 32 - height;

    const std::int64_t newLeft = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(current.left) + dx, 0, std::max<std::int64_t>(0, maxX));
    const std::int64_t newTop = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(current.top) + dy, 0, std::max<std::int64_t>(0, maxY));

    const io::Result result = archive_.setLocationBounds(
        current.index,
        static_cast<std::uint32_t>(newLeft),
        static_cast<std::uint32_t>(newTop),
        static_cast<std::uint32_t>(newLeft + width),
        static_cast<std::uint32_t>(newTop + height));

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

bool MapDocument::setScenarioName(const std::string & name)
{
    const io::Result result = archive_.setScenarioName(name);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setScenarioDescription(const std::string & description)
{
    const io::Result result = archive_.setScenarioDescription(description);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setTileset(std::uint16_t tilesetId)
{
    const io::Result result = archive_.setTileset(tilesetId);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setDimensions(std::uint16_t width, std::uint16_t height)
{
    const io::Result result = archive_.setDimensions(width, height);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::setUnitProperties(std::size_t unitIndex,
                                    const io::UnitProperties & properties)
{
    const io::Result result = archive_.setUnitProperties(unitIndex, properties);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::optional<io::UnitProperties> MapDocument::unitProperties(std::size_t unitIndex) const
{
    return archive_.unitProperties(unitIndex);
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

io::TextEncoding MapDocument::textEncoding() const
{
    return archive_.textEncoding();
}

void MapDocument::setTextEncoding(io::TextEncoding encoding)
{
    archive_.setTextEncoding(encoding);
    refreshInfo();
}

std::optional<io::UnitStats> MapDocument::unitStats(std::uint16_t unitType) const
{
    return archive_.unitStats(unitType);
}

bool MapDocument::setUnitStats(std::uint16_t unitType, const io::UnitStats & stats)
{
    const io::Result result = archive_.setUnitStats(unitType, stats);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

std::optional<io::UpgradeSettings> MapDocument::upgradeSettings(std::uint16_t upgradeType) const
{
    return archive_.upgradeSettings(upgradeType);
}

bool MapDocument::setUpgradeSettings(std::uint16_t upgradeType,
                                     const io::UpgradeSettings & settings)
{
    const io::Result result = archive_.setUpgradeSettings(upgradeType, settings);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

std::optional<io::TechSettings> MapDocument::techSettings(std::uint16_t techType) const
{
    return archive_.techSettings(techType);
}

bool MapDocument::setTechSettings(std::uint16_t techType, const io::TechSettings & settings)
{
    const io::Result result = archive_.setTechSettings(techType, settings);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

std::vector<io::MapString> MapDocument::strings() const
{
    return archive_.strings();
}

bool MapDocument::setString(std::size_t stringId, const std::string & text)
{
    const io::Result result = archive_.setString(stringId, text);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<io::PlayerSetting> MapDocument::playerSettings() const
{
    return archive_.playerSettings();
}

bool MapDocument::setPlayerSetting(std::size_t player, const io::PlayerSetting & setting)
{
    const io::Result result = archive_.setPlayerSetting(player, setting);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<io::ForceSetting> MapDocument::forceSettings() const
{
    return archive_.forceSettings();
}

bool MapDocument::setForceFlags(std::size_t force, const io::ForceSetting & setting)
{
    const io::Result result = archive_.setForceFlags(force, setting);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<std::string> MapDocument::forceNames() const
{
    return archive_.forceNames();
}

bool MapDocument::setForceName(std::size_t force, const std::string & name)
{
    const io::Result result = archive_.setForceName(force, name);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}


bool MapDocument::duplicateTrigger(std::size_t index)
{
    const io::Result result = archive_.duplicateTrigger(index);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<io::TriggerElement> MapDocument::triggerConditions(
    std::size_t index, const io::GameGraphics & graphics) const
{
    return archive_.triggerConditions(index, graphics);
}

std::vector<io::TriggerElement> MapDocument::triggerActions(
    std::size_t index, const io::GameGraphics & graphics) const
{
    return archive_.triggerActions(index, graphics);
}

std::vector<io::TriggerChoice> MapDocument::conditionTypes(const io::GameGraphics & graphics) const
{
    return archive_.conditionTypes(graphics);
}

std::vector<io::TriggerChoice> MapDocument::actionTypes(const io::GameGraphics & graphics) const
{
    return archive_.actionTypes(graphics);
}

bool MapDocument::setConditionType(std::size_t triggerIndex, std::size_t slot, std::uint8_t type)
{
    const io::Result result = archive_.setConditionType(triggerIndex, slot, type);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setActionType(std::size_t triggerIndex, std::size_t slot, std::uint8_t type)
{
    const io::Result result = archive_.setActionType(triggerIndex, slot, type);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setConditionArg(std::size_t triggerIndex, std::size_t slot, std::size_t argIndex, std::uint32_t value)
{
    const io::Result result = archive_.setConditionArg(triggerIndex, slot, argIndex, value);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setActionArg(std::size_t triggerIndex, std::size_t slot, std::size_t argIndex, std::uint32_t value)
{
    const io::Result result = archive_.setActionArg(triggerIndex, slot, argIndex, value);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setActionArgText(std::size_t triggerIndex, std::size_t slot, std::size_t argIndex, const std::string & text)
{
    const io::Result result = archive_.setActionArgText(triggerIndex, slot, argIndex, text);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setConditionDisabled(std::size_t triggerIndex, std::size_t slot, bool disabled)
{
    const io::Result result = archive_.setConditionDisabled(triggerIndex, slot, disabled);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setActionDisabled(std::size_t triggerIndex, std::size_t slot, bool disabled)
{
    const io::Result result = archive_.setActionDisabled(triggerIndex, slot, disabled);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeCondition(std::size_t triggerIndex, std::size_t slot)
{
    const io::Result result = archive_.removeCondition(triggerIndex, slot);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeAction(std::size_t triggerIndex, std::size_t slot)
{
    const io::Result result = archive_.removeAction(triggerIndex, slot);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::moveCondition(std::size_t triggerIndex, std::size_t from, std::size_t to)
{
    const io::Result result = archive_.moveCondition(triggerIndex, from, to);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::moveAction(std::size_t triggerIndex, std::size_t from, std::size_t to)
{
    const io::Result result = archive_.moveAction(triggerIndex, from, to);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<io::MapArchive::UnitPreset> MapDocument::unitPresets() const
{
    return archive_.unitPresets();
}

bool MapDocument::setUnitPreset(std::size_t index, const io::MapArchive::UnitPreset & preset)
{
    const io::Result result = archive_.setUnitPreset(index, preset);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::placeMapRevealers(std::uint8_t owner, int spacingTiles, std::size_t * outCount)
{
    const std::size_t placed = archive_.placeMapRevealers(owner, spacingTiles);
    if (outCount != nullptr)
        *outCount = placed;

    if (placed == 0)
    {
        lastError_ = "리빌러를 놓지 못했습니다.";
        return false;
    }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeMapRevealers(std::size_t * outCount)
{
    const std::size_t removed = archive_.removeMapRevealers();
    if (outCount != nullptr)
        *outCount = removed;

    if (removed == 0)
    {
        lastError_ = "지울 리빌러가 없습니다.";
        return false;
    }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setFogEverywhere(std::uint8_t players, bool covered)
{
    const io::Result result = archive_.setFogEverywhere(players, covered);
    if (!result) { lastError_ = result.message; return false; }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::randomizeResources(std::uint32_t minimum, std::uint32_t maximum,
                                     std::size_t * outCount)
{
    const std::size_t changed = archive_.randomizeResources(minimum, maximum);
    if (outCount != nullptr)
        *outCount = changed;

    if (changed == 0)
    {
        lastError_ = "바꿀 자원 유닛이 없습니다.";
        return false;
    }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<std::size_t> MapDocument::aiTownLocations() const
{
    return archive_.aiTownLocations();
}

std::vector<std::string> MapDocument::switchNames() const
{
    return archive_.switchNames();
}

bool MapDocument::setSwitchName(std::size_t switchIndex, const std::string & name)
{
    const io::Result result = archive_.setSwitchName(switchIndex, name);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::isProtected() const
{
    return archive_.isProtected();
}

bool MapDocument::hasPassword() const
{
    return archive_.hasPassword();
}

bool MapDocument::unprotect(std::string * report)
{
    const io::Result result = archive_.unprotect();
    if (report != nullptr)
        *report = result.message;

    if (!result) { lastError_ = result.message; return false; }

    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

std::vector<io::MapArchive::RawDoodad> MapDocument::doodads() const
{
    return archive_.doodads();
}

bool MapDocument::placeDoodad(const io::GameGraphics & graphics, std::uint16_t doodadId,
                              int tileX, int tileY, std::uint8_t owner)
{
    const io::Result result = archive_.placeDoodad(graphics, doodadId, tileX, tileY, owner);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<std::size_t> MapDocument::findBrokenDoodads(const io::GameGraphics & graphics) const
{
    return archive_.findBrokenDoodads(graphics);
}

bool MapDocument::repairDoodads(const io::GameGraphics & graphics, std::size_t * outCount)
{
    const std::size_t repaired = archive_.repairDoodads(graphics);
    if (outCount != nullptr)
        *outCount = repaired;

    if (repaired == 0)
    {
        lastError_ = "고칠 두들이 없습니다.";
        return false;
    }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::convertDoodadsToTerrain(const io::GameGraphics & graphics,
                                          std::size_t * outCount)
{
    const std::size_t converted = archive_.convertDoodadsToTerrain(graphics);
    if (outCount != nullptr)
        *outCount = converted;

    if (converted == 0)
    {
        lastError_ = "풀어 낼 두들이 없습니다.";
        return false;
    }

    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeDoodad(const io::GameGraphics & graphics, std::size_t index)
{
    const io::Result result = archive_.removeDoodad(graphics, index);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<io::MapArchive::MapSound> MapDocument::sounds(bool checkArchive) const
{
    return archive_.sounds(checkArchive);
}

bool MapDocument::addSound(const std::string & sourceFilePath, const std::string & mapPath)
{
    const io::Result result = archive_.addSound(sourceFilePath, mapPath);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::removeSound(std::size_t soundIndex, bool removeIfUsed)
{
    const io::Result result = archive_.removeSound(soundIndex, removeIfUsed);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::extractSoundByStringId(std::size_t stringId,
                                         const std::string & destFilePath) const
{
    const io::Result result = archive_.extractSoundByStringId(stringId, destFilePath);
    if (!result)
    {
        // 꺼내기는 문서를 바꾸지 않으므로 const 다. 실패 사유만 남긴다.
        const_cast<MapDocument *>(this)->lastError_ = result.message;
        return false;
    }
    return true;
}

bool MapDocument::extractSound(std::size_t soundIndex, const std::string & destFilePath) const
{
    const io::Result result = archive_.extractSound(soundIndex, destFilePath);
    if (!result)
    {
        // 꺼내기는 문서를 바꾸지 않으므로 const 다. 실패 사유만 남긴다.
        const_cast<MapDocument *>(this)->lastError_ = result.message;
        return false;
    }
    return true;
}

bool MapDocument::setFogTiles(const std::vector<std::pair<int, int>> & tiles,
                              std::uint8_t players)
{
    const io::Result result = archive_.setFogTiles(tiles, players);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

std::vector<io::BriefingSummary> MapDocument::briefingSummaries(
    const io::GameGraphics & graphics) const
{
    return archive_.briefingSummaries(graphics);
}

std::optional<io::BriefingDetail> MapDocument::briefingDetail(
    std::size_t index, const io::GameGraphics & graphics) const
{
    return archive_.briefingDetail(index, graphics);
}

std::optional<std::string> MapDocument::briefingText(const io::GameGraphics & graphics) const
{
    return archive_.briefingText(graphics);
}

bool MapDocument::setBriefingText(std::size_t index, const std::string & text,
                                  io::GameGraphics & graphics)
{
    const io::Result result = archive_.setBriefingText(index, text, graphics);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

bool MapDocument::setBriefingText(const std::string & text, io::GameGraphics & graphics)
{
    const io::Result result = archive_.setBriefingText(text, graphics);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}


std::vector<io::TriggerElement> MapDocument::briefingActions(
    std::size_t index, const io::GameGraphics & graphics) const
{
    return archive_.briefingActions(index, graphics);
}

std::vector<io::TriggerChoice> MapDocument::briefingActionTypes(
    const io::GameGraphics & graphics) const
{
    return archive_.briefingActionTypes(graphics);
}

bool MapDocument::setBriefingActionType(std::size_t index, std::size_t slot, std::uint8_t type)
{
    const io::Result result = archive_.setBriefingActionType(index, slot, type);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setBriefingActionArg(std::size_t index, std::size_t slot, std::size_t argIndex, std::uint32_t value)
{
    const io::Result result = archive_.setBriefingActionArg(index, slot, argIndex, value);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setBriefingActionArgText(std::size_t index, std::size_t slot, std::size_t argIndex, const std::string & text)
{
    const io::Result result = archive_.setBriefingActionArgText(index, slot, argIndex, text);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeBriefingAction(std::size_t index, std::size_t slot)
{
    const io::Result result = archive_.removeBriefingAction(index, slot);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::moveBriefingAction(std::size_t index, std::size_t from, std::size_t to)
{
    const io::Result result = archive_.moveBriefingAction(index, from, to);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::addBriefing()
{
    const io::Result result = archive_.addBriefing();
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeBriefing(std::size_t index)
{
    const io::Result result = archive_.removeBriefing(index);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::moveBriefing(std::size_t from, std::size_t to)
{
    const io::Result result = archive_.moveBriefing(from, to);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setBriefingOwners(std::size_t index, const std::array<bool, 27> & owners)
{
    const io::Result result = archive_.setBriefingOwners(index, owners);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    ++undoDepth_;
    redoDepth_ = 0;
    refreshInfo();
    return true;
}

io::TriggerVocabulary MapDocument::triggerVocabulary(const io::GameGraphics & graphics) const
{
    return archive_.triggerVocabulary(graphics);
}

std::vector<io::TriggerSummary> MapDocument::triggerSummaries(
    const io::GameGraphics & graphics) const
{
    return archive_.triggerSummaries(graphics);
}

std::optional<io::TriggerDetail> MapDocument::triggerDetail(
    std::size_t index, const io::GameGraphics & graphics) const
{
    return archive_.triggerDetail(index, graphics);
}

bool MapDocument::setTriggerOwners(std::size_t index, const std::array<bool, 27> & owners)
{
    const io::Result result = archive_.setTriggerOwners(index, owners);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::setTriggerEnabled(std::size_t index, bool enabled)
{
    const io::Result result = archive_.setTriggerEnabled(index, enabled);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::removeTrigger(std::size_t index)
{
    const io::Result result = archive_.removeTrigger(index);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::addTrigger()
{
    const io::Result result = archive_.addTrigger();
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true; ++undoDepth_; redoDepth_ = 0;
    refreshInfo();
    return true;
}

bool MapDocument::applyTriggerText(std::size_t index, const std::string & text,
                                   io::GameGraphics & graphics)
{
    const io::Result result = archive_.setTriggerText(index, text, graphics);
    if (!result) { lastError_ = result.message; return false; }
    modified_ = true;
    undoDepth_ = 0; redoDepth_ = 0; savedDepth_ = -1;
    refreshInfo();
    return true;
}

std::optional<std::string> MapDocument::triggerText(const io::GameGraphics & graphics) const
{
    return archive_.triggerText(graphics);
}

bool MapDocument::applyTriggerText(const std::string & text, io::GameGraphics & graphics)
{
    const io::Result result = archive_.setTriggerText(text, graphics);
    if (!result)
    {
        lastError_ = result.message;
        return false;
    }

    modified_ = true;
    undoDepth_ = 0;
    redoDepth_ = 0;
    savedDepth_ = -1; // 저장 전까지는 변경된 상태
    refreshInfo();
    return true;
}

const std::string & MapDocument::lastError() const
{
    return lastError_;
}

const std::string & MapDocument::lastSaveWarning() const
{
    return archive_.lastSaveWarning();
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
    fog_ = archive_.fogTiles();

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
        unit.classId = raw.classId;
        unit.relationFlags = raw.relationFlags;
        unit.relationClassId = raw.relationClassId;
        unit.stateFlags = raw.stateFlags;
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
        location.elevationFlags = raw.elevationFlags;
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
