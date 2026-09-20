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

#include "io/text_encoding.h"
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

    // 애드온·나이더스가 서로 이어져 있으면 상대의 classId 가 들어 있다.
    std::uint16_t relationFlags = 0;   ///< Chk::Unit::RelationFlag
    std::uint32_t relationClassId = 0; ///< 이어진 상대의 classId
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

/// 트리거 텍스트를 쓸 때 등장할 수 있는 낱말들. 편집기의 자동 완성과
/// 문법 검사에 쓴다.
struct TriggerVocabulary
{
    std::vector<std::string> conditions;  ///< 조건 이름
    std::vector<std::string> actions;     ///< 액션 이름
    std::vector<std::string> constants;   ///< 비교·수정자 등 열거값
    std::vector<std::string> locations;   ///< 맵의 위치 이름
    std::vector<std::string> switches;    ///< 스위치 이름
    std::vector<std::string> units;       ///< 유닛 이름
    std::vector<std::string> players;     ///< 플레이어·그룹 이름
    std::vector<std::string> scripts;     ///< AI 스크립트 이름
};

/// 맵이 정하는 유닛 능력치. 기본값을 쓰면 useDefault 가 참이다.
struct UnitStats
{
    bool useDefault = true;
    std::uint32_t hitpoints = 0;   ///< 게임 내부 단위(표시 체력 x 256)
    std::uint16_t shields = 0;
    std::uint8_t armor = 0;
    std::uint16_t buildTime = 0;   ///< 1/15 초 단위
    std::uint16_t mineralCost = 0;
    std::uint16_t gasCost = 0;

    bool defaultBuildable = true;
    std::array<bool, 12> playerUsesDefault {};
    std::array<bool, 12> buildable {};
};

/// 맵이 정하는 업그레이드 설정.
struct UpgradeSettings
{
    bool useDefaultCosts = true;
    std::uint16_t baseMineralCost = 0;
    std::uint16_t mineralCostFactor = 0;  ///< 단계마다 더해지는 값
    std::uint16_t baseGasCost = 0;
    std::uint16_t gasCostFactor = 0;
    std::uint16_t baseResearchTime = 0;   ///< 1/15 초 단위
    std::uint16_t researchTimeFactor = 0;

    std::uint8_t defaultStartLevel = 0;
    std::uint8_t defaultMaxLevel = 0;
    std::array<bool, 12> playerUsesDefault {};
    std::array<std::uint8_t, 12> startLevel {};
    std::array<std::uint8_t, 12> maxLevel {};
};

/// 맵이 정하는 기술 설정.
struct TechSettings
{
    bool useDefaultCosts = true;
    std::uint16_t mineralCost = 0;
    std::uint16_t gasCost = 0;
    std::uint16_t researchTime = 0;       ///< 1/15 초 단위
    std::uint16_t energyCost = 0;

    bool defaultAvailable = false;
    bool defaultResearched = false;
    std::array<bool, 12> playerUsesDefault {};
    std::array<bool, 12> available {};
    std::array<bool, 12> researched {};
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

    // 색은 앞 8칸에만 있다 (CHK 의 COLR).
    std::uint8_t color = 0;    ///< Chk::PlayerColor (0 빨강, 1 파랑, …)

    // 리마스터 맵은 색을 더 자세히 정한다 (CRGB).
    bool remasteredColors = false;  ///< 이 맵이 리마스터 색을 쓰는지
    std::uint8_t colorSetting = 0;  ///< 0 무작위, 1 플레이어 선택, 2 직접 정함, 3 위 색 번호
    std::uint8_t customRed = 0;
    std::uint8_t customGreen = 0;
    std::uint8_t customBlue = 0;
};

/// 세력 하나의 설정 (FORC 의 플래그).
struct ForceSetting
{
    std::string name;
    bool randomizeStartLocation = false; ///< 시작 위치를 섞는다
    bool randomAllies = false;           ///< 무작위로 동맹을 맺는다
    bool alliedVictory = false;          ///< 동맹 승리
    bool sharedVision = false;           ///< 시야를 함께 본다
};

/// 색 번호의 이름과 화면에 보일 색.
struct PlayerColorInfo
{
    std::uint8_t value = 0;
    std::string name;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

/// 고를 수 있는 플레이어 색 목록.
const std::vector<PlayerColorInfo> & playerColors();

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

/// 미션 브리핑 한 줄(브리핑 트리거) 요약.
struct BriefingSummary
{
    std::size_t index = 0;
    std::string players;      ///< 이 브리핑을 보는 플레이어
    std::size_t actions = 0;
    std::string firstAction;  ///< 무엇을 하는지 한 줄
};

/// 브리핑 트리거 하나의 속내.
struct BriefingDetail
{
    std::array<bool, 27> owners {};
    std::vector<std::string> actions; ///< 사람이 읽는 형태
    std::string text;                 ///< 이 브리핑만의 텍스트 트리거
};

/// 조건·액션 인자의 종류. 편집기가 어떤 위젯을 띄울지 고른다.
enum class TriggerArgKind
{
    None,       ///< 쓰이지 않는 자리
    Choice,     ///< 정해진 값 중 고른다 (choices 참고)
    Number,     ///< 자유로운 수
    Text,       ///< 문자열 (맵 문자열로 들어간다)
    Sound,      ///< 사운드 파일 경로
};

/// 그 자리가 트리거에서 무엇을 뜻하는지.
///
/// 이름표는 사람이 읽으라고 있는 것이라 그것으로 자리를 가려내면 말이
/// 바뀔 때마다 깨진다. EUD 처럼 특정 자리를 집어야 하는 기능이 기댈 수
/// 있도록 뜻을 따로 적어 둔다.
enum class TriggerArgRole
{
    Other,
    Player,
    UnitType,
    Location,
    Switch,
    Amount,
    Comparison,
    MemoryOffset,
    MemoryBitmask,
};

/// 고를 수 있는 값 하나.
struct TriggerChoice
{
    std::uint32_t value = 0;
    std::string text;
};

/// 조건이나 액션의 인자 하나.
struct TriggerArg
{
    TriggerArgKind kind = TriggerArgKind::None;
    TriggerArgRole role = TriggerArgRole::Other; ///< 그 자리의 뜻
    std::string label;                   ///< "플레이어", "유닛" 처럼 무슨 자리인지
    std::string text;                    ///< 지금 값을 사람이 읽는 형태
    std::uint32_t value = 0;             ///< 지금 값의 원시 형태
    std::vector<TriggerChoice> choices;  ///< kind == Choice 일 때 고를 수 있는 값
    std::uint32_t maximum = 0xFFFFFFFFu; ///< kind == Number 일 때 최댓값
};

/// 가상 조건·액션 종류. CHK 에는 Deaths / Set Deaths 로 들어가지만
/// 편집기에서는 따로 고를 수 있어야 한다.
///
/// 종류 목록(conditionTypes·actionTypes)과 TriggerElement::typeKey 가
/// 이 값을 쓴다. 실제 종류 번호(0-59)와 겹치지 않는 자리에 둔다.
inline constexpr std::uint32_t kMemoryType = 0x100;        ///< Memory / Set Memory
inline constexpr std::uint32_t kMemoryMaskedType = 0x101;  ///< 비트마스크를 함께 쓰는 쪽

/// 트리거에 든 조건 또는 액션 하나.
struct TriggerElement
{
    std::uint8_t type = 0;      ///< Chk::Condition::Type / Chk::Action::Type
    std::string name;           ///< "Bring", "Set Resources" 처럼 이름만
    std::string text;           ///< 인자까지 붙인 한 줄
    bool disabled = false;
    std::vector<TriggerArg> args;

    /// EUD 인지 — Deaths 를 메모리 접근으로 쓰는 줄인지.
    bool memory = false;
    /// 비트마스크를 함께 쓰는지 (maskFlag 가 "SC").
    bool masked = false;

    /// 종류 고르개가 쓰는 값. 보통은 type 과 같고, EUD 면 kMemoryType
    /// 또는 kMemoryMaskedType 이다.
    std::uint32_t typeKey = 0;
};

/// EUD 조건 한 줄에 넣을 값.
struct MemoryConditionSpec
{
    std::uint32_t address = 0;    ///< 1.16.1 기준 메모리 주소
    std::uint8_t comparison = 0;  ///< Chk::Condition::Comparison (0 AtLeast, 1 AtMost, 10 Exactly)
    std::uint32_t amount = 0;
    bool masked = false;          ///< 비트마스크를 쓰는지
    std::uint32_t bitmask = 0xFFFFFFFFu;
};

/// EUD 동작 한 줄에 넣을 값.
struct MemoryActionSpec
{
    std::uint32_t address = 0;
    std::uint8_t modifier = 7;    ///< ValueModifier (7 SetTo, 8 Add, 9 Subtract)
    std::uint32_t amount = 0;
    bool masked = false;
    std::uint32_t bitmask = 0xFFFFFFFFu;
};

/// 맵 안에서 찾은 EUD 자리 하나.
struct EudUsage
{
    std::size_t triggerIndex = 0;
    std::size_t slot = 0;
    bool isCondition = true;
    bool masked = false;
    std::uint32_t epd = 0;        ///< Deaths 표에서 네 바이트 단위로 센 거리
    std::uint32_t address = 0;    ///< 1.16.1 기준 메모리 주소
    std::uint32_t bitmask = 0;    ///< masked 일 때만 뜻이 있다
    std::uint32_t amount = 0;     ///< 견줄 값 / 넣을 값
    std::string text;             ///< 사람이 읽는 한 줄
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

/// 그 자리를 덮는 ISOM 마름모의 한가운데 (맵 픽셀 좌표).
///
/// ISOM 브러시는 마름모 단위로 놓이므로, 미리보기를 이 자리에 맞춰야
/// 실제로 바뀌는 곳과 어긋나지 않는다.
struct IsomDiamondCentre
{
    int x = 0;
    int y = 0;
    int halfWidth = 0;  ///< 마름모의 가로 반지름 (픽셀)
    int halfHeight = 0;
};
IsomDiamondCentre isomDiamondCentre(int pixelX, int pixelY, int brushSize = 1);

/// 업그레이드·기술 종류의 이름과 개수.
std::string upgradeTypeName(std::uint16_t type);
std::size_t upgradeTypeCount();
std::string techTypeName(std::uint16_t type);
std::size_t techTypeCount();

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
    /// 새 맵에 넣을 기본 트리거.
    ///
    /// 옵저버가 있는 세트는 그 인원수만큼 플레이어가 겨루고 나머지는
    /// 구경만 한다 — 대회 맵에서 흔히 쓴다.
    enum class DefaultTriggers
    {
        None = 0,
        Melee = 1,
        MeleeWithObservers2 = 2,
        MeleeWithObservers3 = 3,
        MeleeWithObservers4 = 4,
        MeleeWithObservers5 = 5,
        MeleeWithObservers6 = 6,
        MeleeWithObservers7 = 7,
    };

    /// meleeTriggers 를 켜면 MappingCore 의 기본 melee 트리거 세트를 넣는다.
    /// graphics 를 주면 그 타일셋 자료로 지형을 제대로 채운다.
    /// terrainTypeIndex 는 GameGraphics::terrainTypes() 의 brushIndex 다.
    Result createNew(MapFormat format,
                     std::uint16_t tilesetId,
                     std::uint16_t width,
                     std::uint16_t height,
                     DefaultTriggers defaultTriggers,
                     const GameGraphics * graphics = nullptr,
                     std::size_t terrainTypeIndex = 0);

    /// 지정한 경로로 쓴다. 기존 파일이 있으면 덮어쓴다.
    ///
    /// MappingCore 의 save() 기본값 중 lockAnywhere / autoDefragmentLocations 는
    /// 끈다 — 둘 다 우리가 편집하지 않은 섹션의 바이트를 바꾸기 때문이다.
    /// "아는 섹션만 수정, 나머지 바이트 보존" 제약을 지키기 위한 선택.
    Result saveAs(const std::string & filePath) const;

    /// 마지막 저장에서 알려 둘 것이 있으면 그 내용. 없으면 빈 문자열.
    ///
    /// 저장은 됐지만 원본에 있던 것을 다 옮기지 못한 경우처럼, 실패는
    /// 아니어도 사용자가 알아야 하는 일을 담는다.
    const std::string & lastSaveWarning() const;

private:
    /// 맵 안 소리를 메모리로 챙기고, 저장할 때 아카이브를 새로 쓰게 한다.
    void collectArchiveSounds() const;

public:

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

    /// 스프라이트를 새로 놓는다. 좌표는 픽셀.
    Result addSprite(std::uint16_t spriteType, std::uint8_t owner,
                     std::uint16_t x, std::uint16_t y, bool drawnAsSprite);

    /// 스프라이트를 지운다.
    Result removeSprite(std::size_t spriteIndex);

    /// 스프라이트 하나의 값을 바꾼다 (소유자·그리기 방식·꺼짐).
    struct SpriteProperties
    {
        std::uint8_t owner = 0;
        bool drawnAsSprite = false; ///< 참이면 순수 스프라이트, 거짓이면 유닛 그래픽
        bool disabled = false;      ///< 유닛 스프라이트를 꺼 둔 상태
    };
    std::optional<SpriteProperties> spriteProperties(std::size_t spriteIndex) const;
    Result setSpriteProperties(std::size_t spriteIndex, const SpriteProperties & properties);

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

    /// 칸마다 다른 값을 한 번에 쓴다. 지형을 붙여 넣을 때 쓴다.
    ///
    /// 한 번의 실행 취소로 전부 되돌아간다.
    struct TileWrite
    {
        std::size_t x = 0;
        std::size_t y = 0;
        std::uint16_t value = 0;
    };
    Result writeTiles(const std::vector<TileWrite> & writes);

    /// 로케이션의 범위를 바꾼다. 좌표는 픽셀이며 left<=right, top<=bottom 이어야 한다.
    /// 두 유닛을 잇는다 (애드온이 붙거나 나이더스 굴이 이어진 상태).
    ///
    /// 게임은 classId 로 짝을 찾으므로, 번호가 없는 유닛에는 새로 준다.
    Result linkUnits(std::size_t unitA, std::size_t unitB, bool addon);

    /// 유닛의 연결을 끊는다. 짝이 되는 유닛의 연결도 함께 끊는다.
    Result unlinkUnit(std::size_t unitIndex);

    /// 로케이션을 새로 만든다. 만든 번호를 돌려준다(실패하면 0).
    std::size_t addLocation(std::uint32_t left, std::uint32_t top,
                            std::uint32_t right, std::uint32_t bottom,
                            const std::string & name);

    /// 로케이션을 지운다. 트리거가 쓰고 있으면 force 를 켜야 지워진다.
    Result removeLocation(std::size_t locationIndex, bool force = false);

    /// 로케이션 이름을 바꾼다.
    Result setLocationName(std::size_t locationIndex, const std::string & name);

    /// 로케이션이 어느 높이를 잡을지 (Chk::Location::Elevation 비트).
    Result setLocationElevationFlags(std::size_t locationIndex, std::uint16_t flags);

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

    // --- 미션 브리핑 (MBRF) ---

    /// 브리핑 목록.
    std::vector<BriefingSummary> briefingSummaries(const GameGraphics & graphics) const;

    /// 브리핑 하나의 속내.
    std::optional<BriefingDetail> briefingDetail(std::size_t index,
                                                 const GameGraphics & graphics) const;

    /// 브리핑 전체를 텍스트로.
    std::optional<std::string> briefingText(const GameGraphics & graphics) const;

    /// 브리핑 하나를 텍스트로 고친다.
    Result setBriefingText(std::size_t index, const std::string & text, GameGraphics & graphics);

    /// 브리핑 전체를 텍스트로 갈아 끼운다.
    Result setBriefingText(const std::string & text, GameGraphics & graphics);

    /// 브리핑 하나의 동작을 인자까지 풀어서 돌려준다.
    std::vector<TriggerElement> briefingActions(std::size_t index,
                                                const GameGraphics & graphics) const;

    /// 고를 수 있는 브리핑 동작 종류.
    std::vector<TriggerChoice> briefingActionTypes(const GameGraphics & graphics) const;

    /// 브리핑 동작의 종류·인자를 바꾼다.
    Result setBriefingActionType(std::size_t index, std::size_t slot, std::uint8_t type);
    Result setBriefingActionArg(std::size_t index, std::size_t slot,
                                std::size_t argIndex, std::uint32_t value);
    Result setBriefingActionArgText(std::size_t index, std::size_t slot,
                                    std::size_t argIndex, const std::string & text);
    Result removeBriefingAction(std::size_t index, std::size_t slot);
    Result moveBriefingAction(std::size_t index, std::size_t from, std::size_t to);

    /// 브리핑을 더하거나 지우거나 옮긴다.
    Result addBriefing();
    Result removeBriefing(std::size_t index);
    Result moveBriefing(std::size_t from, std::size_t to);

    /// 어느 플레이어가 이 브리핑을 보는지.
    Result setBriefingOwners(std::size_t index, const std::array<bool, 27> & owners);

    // --- 유닛 속성 프리셋 (CUWP) ---

    /// 트리거의 "유닛 속성으로 유닛 만들기" 가 쓰는 프리셋 하나.
    struct UnitPreset
    {
        std::size_t index = 0;
        bool used = false;          ///< 트리거가 이 자리를 쓰는지

        bool setOwner = false;      ///< 어떤 값을 정할지 (validFieldFlags)
        bool setHitpoints = false;
        bool setShields = false;
        bool setEnergy = false;
        bool setResources = false;
        bool setHangar = false;

        std::uint8_t owner = 0;
        std::uint8_t hitpointPercent = 100;
        std::uint8_t shieldPercent = 100;
        std::uint8_t energyPercent = 100;
        std::uint32_t resourceAmount = 0;
        std::uint16_t hangarAmount = 0;

        bool cloaked = false;
        bool burrowed = false;
        bool inTransit = false;
        bool hallucinated = false;
        bool invincible = false;
    };

    /// 프리셋 64자리를 모두 돌려준다.
    std::vector<UnitPreset> unitPresets() const;

    /// 프리셋 하나를 바꾼다.
    Result setUnitPreset(std::size_t index, const UnitPreset & preset);

    // --- 한꺼번에 손보기 ---

    /// 맵 전체를 리빌러로 덮는다. 몇 개를 놓았는지 돌려준다.
    ///
    /// 리빌러 하나가 시야 몇 타일을 여는지에 맞춰 띄엄띄엄 놓는다.
    std::size_t placeMapRevealers(std::uint8_t owner, int spacingTiles);

    /// 맵의 리빌러를 모두 지운다. 몇 개를 지웠는지 돌려준다.
    std::size_t removeMapRevealers();

    /// 가리개를 전부 씌우거나 걷는다 (플레이어 비트).
    Result setFogEverywhere(std::uint8_t players, bool covered);

    /// 미네랄·가스의 남은 양을 섞는다. 몇 개를 바꿨는지 돌려준다.
    std::size_t randomizeResources(std::uint32_t minimum, std::uint32_t maximum);

    /// AI 스크립트가 타운으로 쓰는 로케이션 번호들.
    ///
    /// 트리거의 "Run AI Script At Location" 이 가리키는 자리다. 컴퓨터가
    /// 어디를 거점으로 삼는지 한눈에 보려는 것이다.
    std::vector<std::size_t> aiTownLocations() const;

    // --- 스위치 이름 (SWNM) ---

    /// 스위치 256개의 이름. 이름이 없으면 빈 문자열.
    std::vector<std::string> switchNames() const;

    /// 스위치 하나의 이름을 바꾼다.
    Result setSwitchName(std::size_t switchIndex, const std::string & name);

    // --- 맵 보호 ---

    /// 이 맵이 보호되어 있는지 (규격을 벗어나 에디터를 막아 둔 상태).
    bool isProtected() const;

    /// 비밀번호가 걸려 있는지.
    bool hasPassword() const;

    /// 보호를 풀어 저장할 수 있는 상태로 만든다.
    ///
    /// 비밀번호를 지우고, 빠진 필수 구역을 채우고, 규격을 벗어난 문자열
    /// 선언을 정리한다. 무엇을 고쳤는지 message 에 적어 돌려준다.
    Result unprotect();

    // --- 두들 (DD2) ---

    /// 맵에 놓인 두들 하나.
    struct RawDoodad
    {
        std::size_t index = 0;
        std::uint16_t type = 0;   ///< dddata.bin 번호
        std::uint16_t x = 0;      ///< 중심 픽셀
        std::uint16_t y = 0;
        std::uint8_t owner = 0;
        bool enabled = true;
    };

    std::vector<RawDoodad> doodads() const;

    /// 두들을 놓는다. 지형 타일도 함께 바꾼다 — 두들은 타일로 그려진다.
    Result placeDoodad(const GameGraphics & graphics, std::uint16_t doodadId,
                       int tileX, int tileY, std::uint8_t owner = 0);

    /// 자리와 맞지 않는 두들을 찾는다. 두들 번호(DD2 자리)를 돌려준다.
    ///
    /// 지형을 고치다 보면 두들이 깔아 둔 타일이 지워져 DD2 항목만 남는 일이
    /// 생긴다. 게임은 DD2 를 읽지 않으므로 화면에는 표가 안 나지만, 편집기가
    /// 두들로 다루려 하면 어긋난다.
    std::vector<std::size_t> findBrokenDoodads(const GameGraphics & graphics) const;

    /// 어긋난 두들의 타일을 다시 깐다. 고친 개수를 돌려준다.
    std::size_t repairDoodads(const GameGraphics & graphics);

    /// 두들 항목을 모두 지우고 지형만 남긴다.
    ///
    /// 두들은 지형 타일 + DD2 항목으로 이뤄진다. 항목을 지우면 편집기가
    /// 두들로 다루지 않고 보통 지형처럼 고칠 수 있게 된다. 게임에서 보이는
    /// 모습은 그대로다 — 타일은 건드리지 않는다.
    std::size_t convertDoodadsToTerrain(const GameGraphics & graphics);

    /// 두들 항목을 지운다. 지형 타일은 그대로 둔다 (무엇으로 되돌릴지
    /// 알 수 없으므로, 지형은 따로 칠해야 한다).
    Result removeDoodad(const GameGraphics & graphics, std::size_t index);

    /// 두들을 켜고 끈다 (DD2 의 enabled 칸).
    ///
    /// 꺼진 두들은 타일은 그대로 두고 항목만 죽은 것으로 표시된다. 놓을
    /// 때(placeDoodad)만 켜짐으로 박혀 있어 나중에 바꿀 길이 없었다 —
    /// 오려 붙일 때 꺼진 상태를 되살리려면 필요하다.
    Result setDoodadEnabled(std::size_t index, bool enabled);

    // --- 소리 (WAV) ---

    /// 맵에 등록된 소리 하나.
    struct MapSound
    {
        std::size_t index = std::size_t(-1); ///< WAV 구역에서의 자리 (없으면 -1)
        std::size_t stringId = 0;  ///< 경로가 든 문자열 번호 (없으면 0)
        std::string path;          ///< "staredit\\wav\\...".
        bool registered = false;   ///< WAV 구역에 올라 있는지
        bool inArchive = false;    ///< 맵 안에 파일이 실제로 들어 있는지

        /// inArchive 를 실제로 확인했는지.
        ///
        /// 확인하지 않으면 inArchive 는 그냥 false 로 남는다. 그것을 "맵에
        /// 없다"로 읽으면 표시가 거짓말을 한다 — 없는 것과 모르는 것은
        /// 다르다. 보여 주는 쪽은 이 깃발을 먼저 봐야 한다.
        bool archiveChecked = false;
        bool usedByTrigger = false;
        std::size_t bytes = 0;     ///< 맵 안 파일 크기
    };

    /// 맵에 등록된 소리 목록.
    ///
    /// checkArchive 를 켜면 파일이 맵 안에 실제로 들어 있는지 MPQ 를 열어
    /// 확인하고 archiveChecked 를 세운다. 끄면 경로만 채운다.
    ///
    /// 끄는 것이 눈에 띄게 빠르지는 않다 — 52MB 맵에서도 차이가 재지지
    /// 않았다(MPQ 를 소리마다가 아니라 한 번만 열기 때문이다). 맵 안에
    /// 있는지까지 보여 줄 자리라면 켜 두는 편이 낫다.
    std::vector<MapSound> sounds(bool checkArchive = true) const;

    /// 바깥 WAV 파일을 맵에 넣고 소리 목록에 올린다.
    ///
    /// mapPath 를 비우면 "staredit\\wav\\<파일 이름>" 으로 넣는다.
    Result addSound(const std::string & sourceFilePath, const std::string & mapPath = {});

    /// 소리를 목록에서 빼고 맵 안 파일도 지운다.
    Result removeSound(std::size_t soundIndex, bool removeIfUsed = false);

    /// 맵 안 소리를 파일로 꺼낸다.
    Result extractSound(std::size_t soundIndex, const std::string & destFilePath) const;

    /// 문자열 번호로 소리를 꺼낸다.
    ///
    /// WAV 구역에 등록하지 않고 트리거가 경로만으로 부르는 소리는 자리
    /// 번호가 없다. 그런 소리는 이쪽으로만 꺼낼 수 있다.
    Result extractSoundByStringId(std::size_t stringId,
                                  const std::string & destFilePath) const;

    // --- 시야 가리개 (MASK) ---

    /// 타일마다 어느 플레이어에게 가려져 있는지 (비트 0~7 = 플레이어 1~8).
    /// 구역이 없으면 빈 벡터.
    std::vector<std::uint8_t> fogTiles() const;

    /// 타일 하나의 가리개를 바꾼다.
    Result setFogTile(int tileX, int tileY, std::uint8_t players);

    /// 여러 타일을 한 번에 바꾼다 (브러시로 칠할 때).
    Result setFogTiles(const std::vector<std::pair<int, int>> & tiles, std::uint8_t players);

    /// 맵 문자열이 쓰는 코드 페이지. 열 때 가려낸 값이다.
    TextEncoding textEncoding() const;

    /// 코드 페이지를 손으로 바꾼다. 가려낸 값이 틀렸을 때 쓴다.
    ///
    /// 바꾼 뒤 저장하면 그 인코딩으로 다시 쓰이므로, 맵을 만든 나라와
    /// 다른 값을 고르면 게임에서 글자가 깨진다.
    void setTextEncoding(TextEncoding encoding);

    /// 트리거 하나를 그대로 베껴 바로 뒤에 넣는다.
    Result duplicateTrigger(std::size_t index);

    /// 트리거 하나의 조건 목록을 인자까지 풀어서 돌려준다.
    std::vector<TriggerElement> triggerConditions(std::size_t index,
                                                  const GameGraphics & graphics) const;

    /// 트리거 하나의 액션 목록을 인자까지 풀어서 돌려준다.
    std::vector<TriggerElement> triggerActions(std::size_t index,
                                               const GameGraphics & graphics) const;

    /// 고를 수 있는 조건·액션 종류 (번호와 이름).
    std::vector<TriggerChoice> conditionTypes(const GameGraphics & graphics) const;
    std::vector<TriggerChoice> actionTypes(const GameGraphics & graphics) const;

    /// 맵 안의 EUD(메모리) 조건·액션을 모두 찾는다.
    ///
    /// 게임 자료가 없어도 된다 — 자리와 수만 읽으면 되기 때문이다.
    std::vector<EudUsage> eudUsages() const;

    /// 조건·액션의 종류를 바꾼다. 인자는 기본값으로 되돌아간다.
    /// 조건·액션의 종류를 바꾼다.
    ///
    /// kMemoryType / kMemoryMaskedType 을 주면 CHK 에는 Deaths 가 들어가고
    /// 플레이어 자리에는 EUD 로 읽히는 기본 자리가 채워진다.
    Result setConditionType(std::size_t triggerIndex, std::size_t slot, std::uint32_t type);
    Result setActionType(std::size_t triggerIndex, std::size_t slot, std::uint32_t type);

    /// EUD 조건 한 줄을 통째로 쓴다.
    ///
    /// 주소는 1.16.1 기준이며 네 바이트 경계여야 한다. 자리를 나눠 여러 번
    /// 쓰면 그 사이에 엉뚱한 곳을 가리키는 순간이 생기므로 한 번에 넣는다.
    Result setConditionMemory(std::size_t triggerIndex, std::size_t slot,
                              const MemoryConditionSpec & spec);

    /// EUD 동작 한 줄을 통째로 쓴다.
    Result setActionMemory(std::size_t triggerIndex, std::size_t slot,
                           const MemoryActionSpec & spec);

    /// 인자 하나의 값을 바꾼다. 문자열 인자는 setConditionArgText 를 쓴다.
    Result setConditionArg(std::size_t triggerIndex, std::size_t slot,
                           std::size_t argIndex, std::uint32_t value);
    Result setActionArg(std::size_t triggerIndex, std::size_t slot,
                        std::size_t argIndex, std::uint32_t value);

    /// 문자열·사운드 인자를 글자로 바꾼다. 맵 문자열 표에 새로 넣는다.
    Result setActionArgText(std::size_t triggerIndex, std::size_t slot,
                            std::size_t argIndex, const std::string & text);

    /// 조건·액션 한 줄을 켜고 끈다.
    Result setConditionDisabled(std::size_t triggerIndex, std::size_t slot, bool disabled);
    Result setActionDisabled(std::size_t triggerIndex, std::size_t slot, bool disabled);

    /// 조건·액션 한 줄을 지운다. 뒤의 줄이 앞으로 당겨진다.
    Result removeCondition(std::size_t triggerIndex, std::size_t slot);
    Result removeAction(std::size_t triggerIndex, std::size_t slot);

    /// 조건·액션 순서를 바꾼다.
    Result moveCondition(std::size_t triggerIndex, std::size_t from, std::size_t to);
    Result moveAction(std::size_t triggerIndex, std::size_t from, std::size_t to);

    /// 업그레이드 하나의 설정.
    std::optional<UpgradeSettings> upgradeSettings(std::uint16_t upgradeType) const;
    Result setUpgradeSettings(std::uint16_t upgradeType, const UpgradeSettings & settings);

    /// 기술 하나의 설정.
    std::optional<TechSettings> techSettings(std::uint16_t techType) const;
    Result setTechSettings(std::uint16_t techType, const TechSettings & settings);

    /// 트리거 편집기에 줄 낱말 목록.
    TriggerVocabulary triggerVocabulary(const GameGraphics & graphics) const;

    /// 유닛 하나의 능력치.
    std::optional<UnitStats> unitStats(std::uint16_t unitType) const;

    /// 유닛 능력치를 바꾼다.
    Result setUnitStats(std::uint16_t unitType, const UnitStats & stats);

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

    /// 세력 넷의 이름과 플래그.
    std::vector<ForceSetting> forceSettings() const;

    /// 세력 하나의 플래그를 바꾼다 (이름은 setForceName 으로).
    Result setForceFlags(std::size_t force, const ForceSetting & setting);

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

    /// 맵 밖으로 나간 유닛·두들을 지우고 로케이션을 안으로 들인다.
    ///
    /// 맵을 줄이거나 남의 맵에서 베껴 붙이다 보면 경계 밖에 놓인 것이
    /// 남는다. 게임은 그런 것을 무시하거나 이상하게 다룬다. 몇 개를
    /// 손봤는지 돌려준다.
    std::size_t removeOutOfBounds();

    /// 두들을 걷어낸 밑 지형 (TILE 구역).
    ///
    /// 화면에 보이는 것은 MTXM 이지만, 두들 아래에 무슨 땅이 있었는지는
    /// 여기 남아 있다. 두들을 빼고 지형만 베낄 때 쓴다. TILE 이 없으면
    /// MTXM 을 돌려준다.
    std::vector<std::uint16_t> underlyingTiles() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace splash::io
