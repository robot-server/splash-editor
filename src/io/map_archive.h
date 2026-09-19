#pragma once

// MappingCore(Chkdraft) 에 대한 유일한 접점.
//
// MappingCore 헤더는 무겁고(chk.h 만 90KB+) RareCpp 리플렉션 매크로를 끌고 온다.
// 그것이 상위 계층·UI 로 새어 나가지 않도록 이 파일은 pimpl 뒤에 전부 감춘다.
// 이 헤더에는 표준 라이브러리 타입만 등장한다. Qt 타입은 코어 전체에서 금지.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace splash::io {

/// 맵에서 읽어낸 가공되지 않은 값들. 해석(타일셋 이름 등)은 상위 계층 몫이다.
struct RawMapInfo
{
    std::string scenarioName;        ///< SPRP 시나리오 이름 (UTF-8, 비어 있을 수 있음)
    std::string scenarioDescription; ///< SPRP 설명 (UTF-8, 비어 있을 수 있음)
    std::uint16_t tileWidth = 0;     ///< DIM 가로 (타일)
    std::uint16_t tileHeight = 0;    ///< DIM 세로 (타일)
    std::uint16_t tilesetId = 0;     ///< ERA 타일셋 원시값
    std::uint16_t versionId = 0;     ///< VER 원시값
    std::size_t unitCount = 0;       ///< UNIT 항목 수
    std::size_t locationCount = 0;   ///< MRGN 항목 수
    std::size_t triggerCount = 0;    ///< TRIG 항목 수
    std::size_t stringCount = 0;     ///< STR 에 저장된 문자열 수

    /// 맵 보호가 감지되었는지. 보호된 맵은 섹션이 의도적으로 망가져 있어
    /// (가짜 중복 섹션, 부풀린 STR, 잘린 MTXM 등) 원본 바이트를 그대로
    /// 되돌려 쓸 수 없는 것이 정상이다.
    bool isProtected = false;
};

/// 새 맵을 만들 때의 대상 포맷. MappingCore 의 SaveType 중 우리가 쓰는 것만 노출한다.
enum class MapFormat
{
    HybridScm,    ///< 1.04+ 하이브리드 (.scm)
    ExpansionScx, ///< 브루드워 (.scx)
    RemasteredScx ///< 리마스터 (.scx)
};

/// 맵에 놓인 유닛 하나. 좌표는 픽셀 단위다(타일이 아니다).
struct RawUnit
{
    std::uint32_t classId = 0;
    std::uint16_t x = 0;          ///< 중심 x (픽셀)
    std::uint16_t y = 0;          ///< 중심 y (픽셀)
    std::uint16_t type = 0;       ///< Sc::Unit::Type
    std::uint8_t  owner = 0;      ///< 0-11 (11 은 중립)
    std::uint16_t stateFlags = 0;
    std::uint32_t resourceAmount = 0; ///< 자원 유닛의 남은 양 (그래픽 단계를 가른다)
    std::uint8_t hitpointPercent = 100;
    std::uint8_t shieldPercent = 100;
    std::uint8_t energyPercent = 100;
    std::uint16_t hangarAmount = 0;
};

/// 유닛 하나의 고칠 수 있는 값들. setUnitProperties 에 넘긴다.
struct UnitProperties
{
    std::uint8_t owner = 0;
    std::uint8_t hitpointPercent = 100;
    std::uint8_t shieldPercent = 100;
    std::uint8_t energyPercent = 100;
    std::uint32_t resourceAmount = 0;
    std::uint16_t hangarAmount = 0;
    std::uint16_t stateFlags = 0;
};

/// 맵에 배치된 스프라이트(THG2). 나무·바위 같은 장식이거나,
/// 유닛처럼 보이지만 실제 유닛이 아닌 것들이다.
struct RawSprite
{
    std::uint16_t type = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t  owner = 0;
    std::uint16_t flags = 0;
    bool drawnAsSprite = false; ///< true 면 스프라이트 그래픽, false 면 유닛 그래픽
};

/// 로케이션 하나. 좌표는 픽셀 단위이며, 좌상단이 우하단보다 클 수 있다
/// (사용자가 반대로 끌어 만든 경우 — 게임은 그대로 받아들인다).
struct RawLocation
{
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;
    std::uint16_t stringId = 0;
    std::uint16_t elevationFlags = 0;
    std::string name;             ///< 없으면 빈 문자열
    std::size_t index = 0;        ///< MRGN 인덱스 (1-based 로 쓰이는 번호)
};

/// 맵에 든 문자열 하나.
struct MapString
{
    std::size_t id = 0;
    std::string text;
    bool used = false; ///< 어딘가에서 쓰이고 있는지
};

/// 플레이어 슬롯 하나의 설정.
struct PlayerSetting
{
    std::uint8_t race = 0;     ///< Chk::Race (0 저그, 1 테란, 2 프로토스, …)
    std::uint8_t slotType = 0; ///< Sc::Player::SlotType
    std::uint8_t force = 0;    ///< 0~3
};

/// 트리거 하나의 요약. 목록에 늘어놓을 때 쓴다.
struct TriggerSummary
{
    std::size_t index = 0;
    std::string players;      ///< "플레이어 1, 2" 처럼 읽을 수 있는 형태
    std::size_t conditions = 0;
    std::size_t actions = 0;
    std::string firstAction;  ///< 무엇을 하는 트리거인지 한눈에 보이도록
    bool disabled = false;
};

/// 트리거 하나의 자세한 내용.
struct TriggerDetail
{
    std::array<bool, 27> owners {};   ///< 어느 플레이어가 실행하는지
    std::vector<std::string> conditions; ///< 사람이 읽는 형태
    std::vector<std::string> actions;
    std::uint32_t flags = 0;
    std::string text;                 ///< 이 트리거만의 텍스트 트리거
};

/// 성공/실패와 사람이 읽을 메시지를 함께 나르는 결과 타입.
struct Result
{
    bool ok = false;
    std::string message;

    explicit operator bool() const { return ok; }

    static Result success() { return Result{true, {}}; }
    static Result failure(std::string why) { return Result{false, std::move(why)}; }
};

class GameGraphics;

/// 유닛 타입의 기본 표시 이름 (예: 12 -> "Terran Marine").
/// 알 수 없는 번호면 "Unit <번호>" 를 돌려준다.
std::string unitTypeName(std::uint16_t type);

/// 맵 파일에서 시나리오 청크(CHK)의 원본 바이트를 꺼낸다.
///
/// .scm/.scx 는 MPQ 컨테이너이므로 "staredit\\scenario.chk" 를 추출하고,
/// .chk 는 파일 내용을 그대로 돌려준다.
///
/// round-trip 검증은 이 함수로 얻은 바이트를 비교한다. 맵 파일 전체를 비교하면
/// 안 되는 이유: MPQ 컨테이너는 해시 테이블 배치·압축 결과가 달라질 수 있어
/// 의미가 같아도 바이트가 달라진다. 보존해야 하는 것은 그 안의 CHK 다.
std::optional<std::vector<std::uint8_t>> readScenarioChk(const std::string & filePath);

/// 열린 맵 하나. MappingCore 의 MapFile 을 소유한다.
///
/// 복사 불가, 이동 가능. 스레드 안전하지 않다.
class MapArchive
{
public:
    MapArchive();
    ~MapArchive();

    MapArchive(const MapArchive &) = delete;
    MapArchive & operator=(const MapArchive &) = delete;
    MapArchive(MapArchive &&) noexcept;
    MapArchive & operator=(MapArchive &&) noexcept;

    /// .scm / .scx / .chk 를 연다. 실패해도 이 객체는 유효한 빈 상태로 남는다.
    Result open(const std::string & filePath);

    /// 빈 맵을 새로 만든다. 저작권 자료 없이 테스트 픽스처를 만들기 위한 경로이며,
    /// M2 이후 "새 맵" 기능의 토대이기도 하다.
    ///
    /// meleeTriggers 를 켜면 MappingCore 의 기본 melee 트리거 세트를 넣는다.
    /// graphics 를 주면 그 타일셋 자료로 지형을 제대로 채운다.
    /// terrainTypeIndex 는 GameGraphics::terrainTypes() 의 brushIndex 다.
    Result createNew(MapFormat format,
                     std::uint16_t tilesetId,
                     std::uint16_t width,
                     std::uint16_t height,
                     bool meleeTriggers,
                     const GameGraphics * graphics = nullptr,
                     std::size_t terrainTypeIndex = 0);

    /// 지정한 경로로 쓴다. 기존 파일이 있으면 덮어쓴다.
    ///
    /// MappingCore 의 save() 기본값 중 lockAnywhere / autoDefragmentLocations 는
    /// 끈다 — 둘 다 우리가 편집하지 않은 섹션의 바이트를 바꾸기 때문이다.
    /// "아는 섹션만 수정, 나머지 바이트 보존" 제약을 지키기 위한 선택.
    Result saveAs(const std::string & filePath) const;

    bool isOpen() const;
    void close();

    /// 열려 있지 않으면 기본값으로 채워진 구조체를 돌려준다.
    RawMapInfo info() const;

    /// 열 때 사용한 경로. 열려 있지 않으면 빈 문자열.
    const std::string & sourcePath() const;

    // --- 편집 ---
    //
    // MappingCore 의 Scenario 는 변경을 추적한다(nf::tracked). 그래서 편집은
    // 그쪽 API 를 통해야 실행 취소가 성립한다.

    /// 유닛을 옮긴다. 좌표는 픽셀.
    Result moveUnit(std::size_t unitIndex, std::uint16_t x, std::uint16_t y);

    /// 유닛을 지운다.
    Result removeUnit(std::size_t unitIndex);

    /// 유닛을 새로 놓는다. 좌표는 픽셀.
    ///
    /// 자원 유닛(미네랄·베스핀)은 기본 자원량을 함께 넣는다 — 0 으로 두면
    /// 게임에서 고갈된 상태로 나온다.
    Result addUnit(std::uint16_t unitType, std::uint8_t owner,
                   std::uint16_t x, std::uint16_t y);

    /// 유닛의 소유자를 바꾼다 (0-11).
    Result setUnitOwner(std::size_t unitIndex, std::uint8_t owner);

    /// 유닛의 속성을 한꺼번에 바꾼다.
    Result setUnitProperties(std::size_t unitIndex, const UnitProperties & properties);

    /// 유닛 하나의 현재 속성.
    std::optional<UnitProperties> unitProperties(std::size_t unitIndex) const;

    /// ISOM 브러시로 지형을 놓는다. 절벽·경계 타일이 자동으로 이어진다.
    ///
    /// 좌표는 맵 픽셀 단위다 — ISOM 마름모 격자는 타일 격자와 어긋나 있어
    /// MappingCore 의 변환 함수를 그대로 쓴다.
    /// terrainType 은 GameGraphics::terrainTypes() 의 brushIndex 다.
    Result placeIsomTerrain(GameGraphics & graphics,
                            std::size_t pixelX, std::size_t pixelY,
                            std::size_t terrainType,
                            std::size_t brushExtent);

    /// 지형 타일 하나를 바꾼다. 좌표는 타일 단위.
    ///
    /// 에디터용(TILE)과 게임용(MTXM)을 함께 쓴다 — 둘이 어긋나면 에디터에
    /// 보이는 것과 게임에서 도는 것이 달라진다.
    Result setTile(std::size_t tileX, std::size_t tileY, std::uint16_t tileValue);

    /// 로케이션의 범위를 바꾼다. 좌표는 픽셀이며 left<=right, top<=bottom 이어야 한다.
    Result setLocationBounds(std::size_t locationIndex,
                             std::uint32_t left, std::uint32_t top,
                             std::uint32_t right, std::uint32_t bottom);

    /// 맵 이름을 바꾼다.
    Result setScenarioName(const std::string & name);

    /// 맵 설명을 바꾼다.
    Result setScenarioDescription(const std::string & description);

    /// 타일셋을 바꾼다. 지형 타일은 그대로 두므로 그림이 달라진다.
    Result setTileset(std::uint16_t tilesetId);

    /// 맵 크기를 바꾼다. 늘리면 빈 지형이, 줄이면 잘려 나간다.
    Result setDimensions(std::uint16_t width, std::uint16_t height);

    /// 마지막 n 개의 편집을 하나로 묶는다. 브러시 한 획처럼 여러 번 고친
    /// 것을 한 번에 되돌리기 위해 쓴다.
    void mergeLastEdits(int count);

    /// 마지막 편집을 되돌린다. 되돌릴 것이 없으면 실패.
    Result undo();

    /// 되돌린 편집을 다시 적용한다.
    Result redo();

    /// 맵에 놓인 유닛 전부.
    std::vector<RawUnit> units() const;

    /// 맵에 배치된 스프라이트 전부.
    std::vector<RawSprite> sprites() const;

    /// 로케이션 전부. 비어 있는 슬롯은 건너뛴다.
    std::vector<RawLocation> locations() const;

    /// 트리거 전체를 사람이 읽는 텍스트로 옮긴다.
    ///
    /// 유닛·업그레이드 이름표가 필요해서 게임 데이터(GameGraphics)를 받는다.
    /// 그것이 준비되지 않았으면 빈 값을 돌려준다.
    std::optional<std::string> triggerText(const GameGraphics & graphics) const;

    /// 맵의 문자열 목록. 비어 있는 자리는 건너뛴다.
    std::vector<MapString> strings() const;

    /// 문자열 하나를 바꾼다.
    Result setString(std::size_t stringId, const std::string & text);

    /// 플레이어 12칸의 설정.
    std::vector<PlayerSetting> playerSettings() const;

    /// 플레이어 하나의 설정을 바꾼다.
    Result setPlayerSetting(std::size_t player, const PlayerSetting & setting);

    /// 세력 이름 네 개.
    std::vector<std::string> forceNames() const;

    /// 세력 이름을 바꾼다 (0~3).
    Result setForceName(std::size_t force, const std::string & name);

    /// 트리거 목록 요약.
    std::vector<TriggerSummary> triggerSummaries(const GameGraphics & graphics) const;

    /// 트리거 하나의 자세한 내용.
    std::optional<TriggerDetail> triggerDetail(std::size_t index,
                                               const GameGraphics & graphics) const;

    /// 트리거를 실행할 플레이어를 바꾼다.
    Result setTriggerOwners(std::size_t index, const std::array<bool, 27> & owners);

    /// 트리거를 켜고 끈다.
    Result setTriggerEnabled(std::size_t index, bool enabled);

    /// 트리거를 지운다.
    Result removeTrigger(std::size_t index);

    /// 빈 트리거를 맨 뒤에 더한다.
    Result addTrigger();

    /// 트리거 하나만 텍스트로 바꿔 적용한다.
    Result setTriggerText(std::size_t index, const std::string & text,
                          GameGraphics & graphics);

    /// 텍스트 트리거를 컴파일해 TRIG 섹션을 통째로 교체한다.
    ///
    /// 성공하면 트리거 전체가 새 내용으로 바뀐다 — 일부만 고치는 것이 아니다.
    /// 실패하면 맵은 건드리지 않는다.
    Result setTriggerText(const std::string & text, GameGraphics & graphics);

    /// 지형 타일 값을 행 우선(row-major)으로 복사한다. 길이는 width*height.
    /// 에디터가 보는 값(TILE 섹션)을 쓴다 — 게임이 보는 MTXM 과 다를 수 있고,
    /// 편집기는 관례상 에디터 쪽을 표시한다.
    std::vector<std::uint16_t> terrainTiles() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace splash::io
