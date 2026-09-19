#pragma once

// 열린 맵 하나를 나타내는 코어 파사드.
//
// 코어 경계 규칙: 이 헤더에는 Qt 타입도 MappingCore 타입도 등장하지 않는다.
// UI 는 이 클래스만 알면 되고, 이 클래스는 UI 를 전혀 모른다.

#include "io/game_graphics.h"
#include "io/map_archive.h"

#include <array>
#include <optional>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace splash::chk {

/// 표시 가능한 형태로 해석된 맵 메타데이터.
struct MapInfo
{
    std::string name;            ///< 시나리오 이름. 없으면 빈 문자열.
    std::string description;     ///< 시나리오 설명. 없으면 빈 문자열.
    std::uint16_t width = 0;     ///< 가로 (타일)
    std::uint16_t height = 0;    ///< 세로 (타일)
    std::uint16_t tilesetId = 0; ///< ERA 원시값
    std::string tilesetName;     ///< "Jungle" 등 표시용 이름
    std::uint16_t versionId = 0; ///< VER 원시값
    std::string versionName;     ///< "Brood War" 등 표시용 이름
    std::size_t unitCount = 0;
    std::size_t locationCount = 0;
    std::size_t triggerCount = 0;
    std::size_t stringCount = 0;

    /// 맵 보호 감지 여부. 보호된 맵은 재저장 시 바이트가 달라지거나
    /// 저장이 거부되는 것이 정상이다.
    bool isProtected = false;
};

/// 맵에 놓인 유닛 하나 (표시용).
struct MapUnit
{
    std::uint16_t x = 0;        ///< 중심 x (픽셀)
    std::uint16_t y = 0;        ///< 중심 y (픽셀)
    std::uint16_t type = 0;
    std::uint8_t  owner = 0;    ///< 0-11
    std::uint32_t resourceAmount = 0; ///< 자원 유닛의 남은 양
    std::string typeName;

    // 애드온·나이더스 연결. 이어져 있으면 상대의 classId 가 들어 있다.
    std::uint32_t classId = 0;
    std::uint16_t relationFlags = 0;
    std::uint32_t relationClassId = 0;

    /// 은폐·버로우·떠 있음·환영·무적 (Chk::Unit::State).
    std::uint16_t stateFlags = 0;
};

/// 맵에 배치된 스프라이트 (표시용).
struct MapSprite
{
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t type = 0;
    std::uint8_t  owner = 0;
    bool drawnAsSprite = false;
};

/// 로케이션 하나 (표시용). 좌표는 정규화되어 left<=right, top<=bottom 이다.
struct MapLocation
{
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;
    std::string name;
    std::size_t index = 0;
    std::uint16_t elevationFlags = 0; ///< Chk::Location::Elevation 비트
};

/// 플레이어 색 (0-11). 표준 8색 + 중립.
struct PlayerColor { std::uint8_t r = 0, g = 0, b = 0; };
PlayerColor playerColor(std::uint8_t owner);

/// 타일셋 원시값 -> 표시 이름. 알 수 없는 값도 문자열로 돌려준다.
std::string tilesetDisplayName(std::uint16_t tilesetId);

/// 버전 원시값 -> 표시 이름. 알 수 없는 값도 문자열로 돌려준다.
std::string versionDisplayName(std::uint16_t versionId);

/// 열린 맵 + 더티 플래그 + save(). M1 코어 경계의 전부다.
///
/// 복사 불가, 이동 가능. 스레드 안전하지 않다.
class MapDocument
{
public:
    MapDocument();
    ~MapDocument();

    MapDocument(const MapDocument &) = delete;
    MapDocument & operator=(const MapDocument &) = delete;
    MapDocument(MapDocument &&) noexcept;
    MapDocument & operator=(MapDocument &&) noexcept;

    /// 빈 맵을 새로 만든다.
    bool createNew(io::MapFormat format, std::uint16_t tilesetId,
                   std::uint16_t width, std::uint16_t height, bool meleeTriggers,
                   const io::GameGraphics * graphics = nullptr,
                   std::size_t terrainTypeIndex = 0);

    /// 맵을 연다. 실패하면 문서는 이전 상태를 잃고 닫힌 상태가 된다.
    /// 실패 사유는 lastError() 로 확인한다.
    bool open(const std::string & filePath);

    /// 현재 경로에 다시 쓴다. 성공하면 더티 플래그가 내려간다.
    bool save();

    /// 새 경로에 쓴다. 성공하면 그 경로가 현재 경로가 되고 더티 플래그가 내려간다.
    bool saveAs(const std::string & filePath);

    void close();

    bool isOpen() const;

    /// 마지막으로 저장한 이후 편집이 있었는지.
    /// M1 에는 편집 기능이 없으므로 항상 false 지만, 경계는 지금 만들어 둔다.
    bool isModified() const;
    void markModified();

    // --- 편집 ---
    //
    // 실패하면 false 를 돌려주고 사유는 lastError() 에 담긴다.

    /// 유닛을 옮긴다. 좌표는 픽셀.
    bool moveUnit(std::size_t unitIndex, std::uint16_t x, std::uint16_t y);

    /// 유닛을 지운다.
    bool removeUnit(std::size_t unitIndex);

    /// 스프라이트를 새로 놓는다. 좌표는 픽셀.
    bool addSprite(std::uint16_t spriteType, std::uint8_t owner,
                   std::uint16_t x, std::uint16_t y, bool drawnAsSprite);

    /// 스프라이트를 지운다.
    bool removeSprite(std::size_t spriteIndex);

    /// 유닛을 새로 놓는다. 좌표는 픽셀.
    bool addUnit(std::uint16_t unitType, std::uint8_t owner,
                 std::uint16_t x, std::uint16_t y);

    /// 유닛의 소유자를 바꾼다 (0-11).
    bool setUnitOwner(std::size_t unitIndex, std::uint8_t owner);

    /// 유닛 속성을 한꺼번에 바꾼다.
    bool setUnitProperties(std::size_t unitIndex, const io::UnitProperties & properties);

    /// 유닛 하나의 현재 속성.
    std::optional<io::UnitProperties> unitProperties(std::size_t unitIndex) const;

    /// ISOM 브러시로 지형을 놓는다 (절벽·경계가 자동으로 이어진다).
    /// 실행 취소 이력이 지워진다 — 타일 여러 개를 한꺼번에 바꾸기 때문이다.
    bool placeIsomTerrain(io::GameGraphics & graphics,
                          std::size_t pixelX, std::size_t pixelY,
                          std::size_t terrainType, std::size_t brushExtent);

    /// 지형 타일 하나를 바꾼다. 좌표는 타일 단위.
    bool setTile(std::size_t tileX, std::size_t tileY, std::uint16_t tileValue);

    /// 여러 타일을 한 번의 편집으로 묶어 바꾼다(브러시 한 획).
    /// 실행 취소도 한 번에 되돌아간다.
    /// 칸마다 다른 값을 한 번에 쓴다 (지형 붙여넣기).
    bool writeTiles(const std::vector<io::MapArchive::TileWrite> & writes);

    bool setTiles(const std::vector<std::pair<std::size_t, std::size_t>> & positions,
                  std::uint16_t tileValue);

    /// 로케이션을 옮긴다(크기는 유지). 좌표는 픽셀.
    /// 애드온·나이더스 연결을 잇고 끊는다.
    bool linkUnits(std::size_t unitA, std::size_t unitB, bool addon);
    bool unlinkUnit(std::size_t unitIndex);

    bool moveLocation(std::size_t locationIndex, std::int64_t dx, std::int64_t dy);

    /// 로케이션의 네 모서리를 직접 정한다 (맵 픽셀 좌표).
    bool setLocationBounds(std::size_t locationIndex, std::uint32_t left, std::uint32_t top,
                           std::uint32_t right, std::uint32_t bottom);

    /// 로케이션을 새로 만든다. 만들었으면 참.
    /// 로케이션을 새로 만든다. outIndex 에 목록에서의 자리를 돌려준다 —
    /// 새 로케이션이 목록 끝에 오지는 않는다 (빈 번호를 찾아 넣는다).
    bool addLocation(std::uint32_t left, std::uint32_t top,
                     std::uint32_t right, std::uint32_t bottom, const std::string & name,
                     std::size_t * outIndex = nullptr);

    bool removeLocation(std::size_t locationIndex, bool force = false);
    bool setLocationName(std::size_t locationIndex, const std::string & name);
    bool setLocationElevationFlags(std::size_t locationIndex, std::uint16_t flags);

    /// 로케이션의 안팎을 뒤집는다.
    ///
    /// 게임은 왼쪽이 오른쪽보다 큰 로케이션을 "이 네모 바깥"으로 읽는다.
    /// 트리거에서 "여기 말고 다른 곳" 을 가리킬 때 쓴다.
    bool setLocationInverted(std::size_t locationIndex, bool inverted);

    /// 그 로케이션이 뒤집혀 있는지.
    bool locationInverted(std::size_t locationIndex) const;

    /// 맵 이름·설명을 바꾼다.
    bool setScenarioName(const std::string & name);
    bool setScenarioDescription(const std::string & description);

    /// 타일셋을 바꾼다 (지형 타일 값은 그대로라 그림이 달라진다).
    bool setTileset(std::uint16_t tilesetId);

    /// 맵 크기를 바꾼다. 실행 취소 이력이 지워진다 — 여러 섹션을 한꺼번에
    /// 건드리므로 절반만 되돌리면 맵이 어긋난다.
    bool setDimensions(std::uint16_t width, std::uint16_t height);

    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();

    const MapInfo & info() const;

    /// 현재 파일 경로. 열려 있지 않으면 빈 문자열.
    const std::string & filePath() const;

    /// 창 제목 등에 쓸 이름. 열려 있지 않으면 빈 문자열.
    std::string fileName() const;

    /// 맵 문자열의 코드 페이지. 열 때 가려낸 값이다.
    io::TextEncoding textEncoding() const;

    /// 코드 페이지를 손으로 바꾼다.
    void setTextEncoding(io::TextEncoding encoding);

    // --- 유닛 능력치 (맵이 정하는 값) ---

    std::optional<io::UnitStats> unitStats(std::uint16_t unitType) const;
    bool setUnitStats(std::uint16_t unitType, const io::UnitStats & stats);

    // --- 업그레이드·기술 설정 ---

    std::optional<io::UpgradeSettings> upgradeSettings(std::uint16_t upgradeType) const;
    bool setUpgradeSettings(std::uint16_t upgradeType, const io::UpgradeSettings & settings);

    std::optional<io::TechSettings> techSettings(std::uint16_t techType) const;
    bool setTechSettings(std::uint16_t techType, const io::TechSettings & settings);

    // --- 문자열 ---

    std::vector<io::MapString> strings() const;
    bool setString(std::size_t stringId, const std::string & text);

    // --- 플레이어 ---

    std::vector<io::PlayerSetting> playerSettings() const;
    bool setPlayerSetting(std::size_t player, const io::PlayerSetting & setting);
    std::vector<std::string> forceNames() const;

    /// 세력 넷의 이름과 플래그 (동맹·공유 시야 등).
    std::vector<io::ForceSetting> forceSettings() const;
    bool setForceFlags(std::size_t force, const io::ForceSetting & setting);
    bool setForceName(std::size_t force, const std::string & name);

    // --- 트리거 ---

    // --- 트리거 조건·액션 (GUI 편집용) ---

    std::vector<io::TriggerElement> triggerConditions(std::size_t index,
                                                      const io::GameGraphics & graphics) const;
    std::vector<io::TriggerElement> triggerActions(std::size_t index,
                                                   const io::GameGraphics & graphics) const;
    std::vector<io::TriggerChoice> conditionTypes(const io::GameGraphics & graphics) const;
    std::vector<io::TriggerChoice> actionTypes(const io::GameGraphics & graphics) const;

    bool setConditionType(std::size_t triggerIndex, std::size_t slot, std::uint8_t type);
    bool setActionType(std::size_t triggerIndex, std::size_t slot, std::uint8_t type);
    bool setConditionArg(std::size_t triggerIndex, std::size_t slot,
                         std::size_t argIndex, std::uint32_t value);
    bool setActionArg(std::size_t triggerIndex, std::size_t slot,
                      std::size_t argIndex, std::uint32_t value);
    bool setActionArgText(std::size_t triggerIndex, std::size_t slot,
                          std::size_t argIndex, const std::string & text);
    bool setConditionDisabled(std::size_t triggerIndex, std::size_t slot, bool disabled);
    bool setActionDisabled(std::size_t triggerIndex, std::size_t slot, bool disabled);
    bool removeCondition(std::size_t triggerIndex, std::size_t slot);
    bool removeAction(std::size_t triggerIndex, std::size_t slot);
    bool moveCondition(std::size_t triggerIndex, std::size_t from, std::size_t to);
    bool moveAction(std::size_t triggerIndex, std::size_t from, std::size_t to);

    // --- 유닛 속성 프리셋 (CUWP) ---

    std::vector<io::MapArchive::UnitPreset> unitPresets() const;
    bool setUnitPreset(std::size_t index, const io::MapArchive::UnitPreset & preset);

    // --- 스위치 이름·맵 보호 ---

    std::vector<std::string> switchNames() const;
    bool setSwitchName(std::size_t switchIndex, const std::string & name);

    bool isProtected() const;
    bool hasPassword() const;

    /// 보호를 풀어 저장할 수 있게 만든다. 무엇을 고쳤는지 알려 준다.
    bool unprotect(std::string * report = nullptr);

    // --- 두들 (DD2) ---

    std::vector<io::MapArchive::RawDoodad> doodads() const;
    bool placeDoodad(const io::GameGraphics & graphics, std::uint16_t doodadId,
                     int tileX, int tileY, std::uint8_t owner = 0);
    bool removeDoodad(std::size_t index);

    // --- 소리 (WAV) ---

    std::vector<io::MapArchive::MapSound> sounds(bool checkArchive = true) const;
    bool addSound(const std::string & sourceFilePath, const std::string & mapPath = {});
    bool removeSound(std::size_t soundIndex, bool removeIfUsed = false);
    bool extractSound(std::size_t soundIndex, const std::string & destFilePath) const;

    // --- 시야 가리개 (MASK) ---

    /// 타일마다 어느 플레이어에게 가려져 있는지. 구역이 없으면 빈 벡터.
    const std::vector<std::uint8_t> & fogTiles() const { return fog_; }

    bool setFogTiles(const std::vector<std::pair<int, int>> & tiles, std::uint8_t players);

    // --- 미션 브리핑 ---

    std::vector<io::BriefingSummary> briefingSummaries(const io::GameGraphics & graphics) const;
    std::optional<io::BriefingDetail> briefingDetail(std::size_t index,
                                                     const io::GameGraphics & graphics) const;
    std::optional<std::string> briefingText(const io::GameGraphics & graphics) const;
    bool setBriefingText(std::size_t index, const std::string & text, io::GameGraphics & graphics);
    bool setBriefingText(const std::string & text, io::GameGraphics & graphics);
    std::vector<io::TriggerElement> briefingActions(std::size_t index,
                                                    const io::GameGraphics & graphics) const;
    std::vector<io::TriggerChoice> briefingActionTypes(const io::GameGraphics & graphics) const;
    bool setBriefingActionType(std::size_t index, std::size_t slot, std::uint8_t type);
    bool setBriefingActionArg(std::size_t index, std::size_t slot,
                              std::size_t argIndex, std::uint32_t value);
    bool setBriefingActionArgText(std::size_t index, std::size_t slot,
                                  std::size_t argIndex, const std::string & text);
    bool removeBriefingAction(std::size_t index, std::size_t slot);
    bool moveBriefingAction(std::size_t index, std::size_t from, std::size_t to);
    bool addBriefing();
    bool removeBriefing(std::size_t index);
    bool moveBriefing(std::size_t from, std::size_t to);
    bool setBriefingOwners(std::size_t index, const std::array<bool, 27> & owners);

    /// 트리거 편집기의 자동 완성·문법 검사에 쓸 낱말 목록.
    io::TriggerVocabulary triggerVocabulary(const io::GameGraphics & graphics) const;

    std::vector<io::TriggerSummary> triggerSummaries(const io::GameGraphics & graphics) const;
    std::optional<io::TriggerDetail> triggerDetail(std::size_t index,
                                                   const io::GameGraphics & graphics) const;

    bool setTriggerOwners(std::size_t index, const std::array<bool, 27> & owners);
    bool setTriggerEnabled(std::size_t index, bool enabled);
    bool removeTrigger(std::size_t index);
    bool addTrigger();

    /// 트리거를 그대로 베껴 바로 뒤에 넣는다.
    bool duplicateTrigger(std::size_t index);

    /// 트리거 하나만 텍스트로 바꿔 적용한다.
    /// 실행 취소 이력이 지워진다 — 컴파일이 TRIG 과 STR 을 함께 바꾼다.
    bool applyTriggerText(std::size_t index, const std::string & text,
                          io::GameGraphics & graphics);

    /// 트리거를 사람이 읽는 텍스트로 옮긴다. 게임 데이터가 필요하다.
    std::optional<std::string> triggerText(const io::GameGraphics & graphics) const;

    /// 텍스트 트리거를 컴파일해 적용한다. 트리거 전체가 교체된다.
    ///
    /// 성공하면 실행 취소 이력이 지워진다 — 컴파일이 TRIG 과 STR 을 한꺼번에
    /// 바꾸기 때문에 부분적으로 되돌리면 맵이 어긋난다.
    bool applyTriggerText(const std::string & text, io::GameGraphics & graphics);

    /// 마지막 실패 사유. 성공했다면 빈 문자열.
    const std::string & lastError() const;

    /// 맵에 놓인 유닛. 열 때 한 번 읽어 둔다.
    const std::vector<MapUnit> & units() const;

    /// 맵에 배치된 스프라이트(THG2).
    const std::vector<MapSprite> & sprites() const;

    /// 쓰이고 있는 로케이션.
    const std::vector<MapLocation> & locations() const;

    /// 지형 타일 값. 행 우선이며 길이는 width*height.
    /// 열 때 한 번 읽어 둔다 — 256x256 맵도 128KB 라 들고 있어도 부담이 없고,
    /// 렌더링 때마다 코어를 두드리지 않아도 된다.
    const std::vector<std::uint16_t> & tiles() const;

private:
    void refreshInfo();

    io::MapArchive archive_;
    MapInfo info_;
    std::vector<std::uint16_t> tiles_;
    std::vector<std::uint8_t> fog_;
    std::vector<MapUnit> units_;
    std::vector<MapSprite> sprites_;
    std::vector<MapLocation> locations_;
    std::string filePath_;
    std::string lastError_;
    bool modified_ = false;

    // 실행 취소 깊이. savedDepth_ 와 같으면 저장된 상태와 일치한다.
    int undoDepth_ = 0;
    int redoDepth_ = 0;
    int savedDepth_ = 0;
};

} // namespace splash::chk
