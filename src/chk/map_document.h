#pragma once

// 열린 맵 하나를 나타내는 코어 파사드.
//
// 코어 경계 규칙: 이 헤더에는 Qt 타입도 MappingCore 타입도 등장하지 않는다.
// UI 는 이 클래스만 알면 되고, 이 클래스는 UI 를 전혀 모른다.

#include "io/game_graphics.h"
#include "io/map_archive.h"

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
                   std::uint16_t width, std::uint16_t height, bool meleeTriggers);

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

    /// 유닛을 새로 놓는다. 좌표는 픽셀.
    bool addUnit(std::uint16_t unitType, std::uint8_t owner,
                 std::uint16_t x, std::uint16_t y);

    /// 유닛의 소유자를 바꾼다 (0-11).
    bool setUnitOwner(std::size_t unitIndex, std::uint8_t owner);

    /// 지형 타일 하나를 바꾼다. 좌표는 타일 단위.
    bool setTile(std::size_t tileX, std::size_t tileY, std::uint16_t tileValue);

    /// 여러 타일을 한 번의 편집으로 묶어 바꾼다(브러시 한 획).
    /// 실행 취소도 한 번에 되돌아간다.
    bool setTiles(const std::vector<std::pair<std::size_t, std::size_t>> & positions,
                  std::uint16_t tileValue);

    /// 로케이션을 옮긴다(크기는 유지). 좌표는 픽셀.
    bool moveLocation(std::size_t locationIndex, std::int64_t dx, std::int64_t dy);

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
