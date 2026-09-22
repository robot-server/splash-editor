#pragma once

// 게임 설치본에서 그래픽(지형 타일 · 유닛 스프라이트)을 읽어 픽셀로 펼쳐 주는 계층.
//
// Qt 를 모른다. 픽셀은 생 버퍼로 넘기고, 그것을 QImage 로 감싸는 일은 UI 몫이다.
//
// 타일 하나는 32x32 픽셀이며, 4x4 개의 미니타일(각 8x8)로 이루어진다.
// 변환 경로(전부 MappingCore 가 파싱한 자료를 쓴다):
//   tileId -> CV5 타일 그룹 -> VX4 메가타일 -> VR4 미니타일 픽셀 -> WPE 팔레트

#include "io/map_archive.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace splash::io {

/// 타일 한 변의 픽셀 수.
inline constexpr int kTilePixels = 32;

/// 타일 하나를 RGBA 로 펼쳤을 때의 바이트 수.
inline constexpr int kTileRgbaBytes = kTilePixels * kTilePixels * 4;

/// 유닛 한 마리를 그린 결과. 크기는 유닛마다 다르다.
struct UnitImage
{
    int width = 0;
    int height = 0;
    int anchorX = 0;   ///< 이미지 안에서 유닛 중심이 놓인 x
    int anchorY = 0;   ///< 이미지 안에서 유닛 중심이 놓인 y
    std::vector<std::uint8_t> rgba; ///< width*height*4, 투명 픽셀은 알파 0
};

/// 설치본의 게임 그래픽. 열려 있는 동안 게임 아카이브를 붙잡고 있다.
///
/// 복사 불가, 이동 가능. 스레드 안전하지 않다.
class GameGraphics
{
public:
    GameGraphics();
    ~GameGraphics();

    GameGraphics(const GameGraphics &) = delete;
    GameGraphics & operator=(const GameGraphics &) = delete;
    GameGraphics(GameGraphics &&) noexcept;
    GameGraphics & operator=(GameGraphics &&) noexcept;

    /// 설치 폴더에서 타일셋 데이터를 읽는다. 비용이 크므로 한 번만 호출할 것.
    /// 실패하면 false 를 돌려주고 error 에 사유를 담는다.
    /// 게임 자료를 읽는다.
    ///
    /// modArchives 는 설치본보다 먼저 뒤질 MPQ 들이다. 앞에 적은 것이
    /// 우선하므로, 유닛이나 지형을 갈아 끼운 모드 맵을 원래 모습대로 볼 수
    /// 있다. 열리지 않는 것은 건너뛴다.
    bool load(const std::string & installPath, std::string * error = nullptr,
              const std::vector<std::string> & modArchives = {});

    bool isLoaded() const;

    /// 타일 하나를 RGBA8888 로 그린다.
    ///
    /// rgbaOut 은 kTileRgbaBytes 바이트를 담을 수 있어야 한다.
    /// tilesetId 는 CHK 의 ERA 원시값(8 로 나눈 나머지를 쓴다),
    /// tileId 는 MTXM 의 타일 값이다.
    ///
    /// 알 수 없는 타일이면 검게 채우고 false 를 돌려준다 — 렌더링을 멈추지
    /// 않으면서도 호출부가 이상을 알 수 있게 한다.
    bool renderTile(std::uint16_t tilesetId, std::uint16_t tileId, std::uint8_t * rgbaOut) const;

    /// 맵에 배치된 스프라이트(THG2)를 그린다.
    /// drawnAsSprite 가 false 면 유닛 그래픽으로 그려야 하는 항목이다.
    ///
    /// colorIndex 는 COLR 이 정한 색 번호다. 0xFF 면 플레이어 번호를 그대로
    /// 색 번호로 쓴다 (맵이 색을 따로 정하지 않았을 때의 기본값).
    UnitImage renderSprite(std::uint16_t spriteType,
                           std::uint8_t owner,
                           std::uint16_t tilesetId,
                           bool drawnAsSprite,
                           std::uint8_t colorIndex = 0xFF) const;

    /// ISOM 브러시로 놓을 수 있는 지형 종류.
    struct TerrainType
    {
        /// placeIsomTerrain 에 넘길 값. terrainTypes 배열 안의 위치이며
        /// TerrainTypeInfo::index 와는 다르다 — MappingCore 가 배열 위치로
        /// 조회한다.
        std::size_t brushIndex = 0;

        std::uint16_t index = 0; ///< TerrainTypeInfo::index (표시·식별용)
        std::string name;
        int sortOrder = -1;

        /// 팔레트에 보여 줄 대표 타일. 이 지형에 속한 타일 그룹에서 고른다.
        std::uint16_t previewTileId = 0;
        bool hasPreview = false;
    };

    /// 이 타일셋의 지형 종류 목록 (브러시 정렬 순서대로).
    std::vector<TerrainType> terrainTypes(std::uint16_t tilesetId) const;

    /// 미니맵 한 장을 그린다. 타일 하나가 픽셀 하나가 된다.
    ///
    /// 결과는 width*height*3 바이트(RGB)다. 타일 색은 타일셋에서 한 번만
    /// 계산해 캐시하므로 여러 번 불러도 비용이 크지 않다.
    std::vector<std::uint8_t> renderMinimap(const std::vector<std::uint16_t> & tiles,
                                            int width, int height,
                                            std::uint16_t tilesetId) const;

    /// 유닛이 배치될 때 낼 소리(WAV 원본 바이트). 없으면 빈 벡터.
    ///
    /// units.dat 의 "무엇" 소리 목록에서 첫 번째를 쓴다. 소리 자료는
    /// MappingCore 가 다루지 않아 sfxdata.dat/tbl 을 직접 읽는다.
    std::vector<std::uint8_t> unitSound(std::uint16_t unitType) const;

    /// 그 소리의 파일 이름 (sfxdata.tbl 의 상대 경로). 확인용이다.
    std::string unitSoundName(std::uint16_t unitType) const;

    /// 유닛을 팔레트에서 나누기 위한 분류.
    struct UnitClass
    {
        enum class Race { Zerg, Terran, Protoss, Neutral };
        Race race = Race::Neutral;
        bool building = false;
        bool flyer = false;      ///< 하늘을 나는 유닛 (땅을 따지지 않는다)
        bool addon = false;      ///< 본체 건물에 붙는 부속 건물
        std::uint8_t groupFlags = 0; ///< units.dat 의 starEditGroupFlags 원시값
    };
    UnitClass unitClass(std::uint16_t unitType) const;

    /// 유닛의 사거리·시야 (픽셀). 화면에 원으로 그려 자리를 가늠한다.
    struct UnitRanges
    {
        int sight = 0;        ///< 시야
        int groundWeapon = 0; ///< 지상 공격 사거리 (없으면 0)
        int airWeapon = 0;    ///< 공중 공격 사거리
        int detection = 0;    ///< 탐지 범위 (탐지기만)
    };
    UnitRanges unitRanges(std::uint16_t unitType) const;

    /// units.dat·weapons.dat 에 박혀 있는 한 유닛의 값들.
    ///
    /// **맵이 고칠 수 있는 것과 없는 것을 가르는 자리다.** 맵의 유닛
    /// 설정(UNIS·UNIx)이 건드리는 것은 체력·방패·방어력·생산시간·값
    /// 뿐이고, 여기 있는 **사거리·이동속도·시야·AI 스크립트**는 못
    /// 고친다. 영웅 유닛이 같은 모양의 일반 유닛과 다른 까닭이 대개
    /// 이쪽이므로, 유즈맵에서 유닛을 고를 때 이 값을 봐야 한다.
    struct UnitStats
    {
        std::uint32_t hitPoints = 0;   ///< 표시값 (내부값 >> 8)
        std::uint16_t shields = 0;     ///< 방패를 안 쓰면 0
        std::uint8_t armor = 0;
        /// 이 유닛의 방어력을 올려 주는 업그레이드 번호 (units.dat).
        /// 공격력 업그레이드와 **다른 종족 것일 수 있다** — 감염된
        /// 듀란은 방어력이 저그, 공격력이 테란이다.
        std::uint8_t armorUpgrade = 61;
        std::uint8_t sightRange = 0;            ///< 타일
        std::uint8_t targetAcquisitionRange = 0; ///< 타일. 먼저 무는 거리
        std::uint8_t unitSize = 0;              ///< 1=소형 2=중형 3=대형
        std::uint32_t topSpeed = 0;             ///< flingy.dat. 클수록 빠르다
        std::uint16_t flingy = 0;
        /// flingy.dat 의 이동 제어. 0·1 이면 topSpeed 가 실제 속도고,
        /// **2(iscript) 면 topSpeed 는 1 짜리 자리표시자**다 — 마린·질럿·
        /// 드라군·저글링이 그렇다. 속도를 비교하려면 이 값을 먼저 봐야 한다.
        std::uint8_t moveControl = 0;
        std::uint8_t groundWeapon = 130;        ///< 130 = 없음
        std::uint8_t airWeapon = 130;
        std::uint8_t maxGroundHits = 0;
        std::uint32_t groundRange = 0;          ///< 픽셀
        std::uint16_t groundDamage = 0;
        std::uint16_t groundDamageBonus = 0;    ///< 업그레이드 한 단계당
        std::uint8_t groundCooldown = 0;        ///< 프레임
        std::uint32_t airRange = 0;
        std::uint16_t airDamage = 0;
        /// 이 무기의 피해를 올려 주는 업그레이드 번호. 61 이면 없다.
        /// **영웅 무기는 대개 일반 무기와 다른 번호를 쓴다** — 그래서
        /// 일반 유닛만 업그레이드를 받고 영웅은 못 받는 일이 생긴다.
        /// 공격 형태 (weapons.dat 의 weaponType). **유닛 덩치와 곱해져
        /// 실제 피해가 정해진다** — 적힌 숫자가 그대로 들어가지 않는다.
        /// 0 독립 · 1 폭발형 · 2 진동형 · 3 일반형 · 4 방어무시
        std::uint8_t groundDamageType = 0;
        std::uint8_t airDamageType = 0;
        std::uint8_t groundDamageUpgrade = 61;
        std::uint8_t airDamageUpgrade = 61;
        /// AI 스크립트 번호. 컴퓨터가 이 유닛을 어떻게 굴리는가 —
        /// 쫓아갔다 돌아오는지, 끝까지 쫓아가는지가 여기서 갈린다.
        std::uint8_t aiCompIdle = 0;
        std::uint8_t aiHumanIdle = 0;
        std::uint8_t aiReturnToIdle = 0;
        std::uint8_t aiAttackUnit = 0;
        std::uint8_t aiAttackMove = 0;
        std::uint32_t flags = 0;
        std::uint16_t mineralCost = 0;
        std::uint16_t vespeneCost = 0;
        std::uint16_t buildTime = 0;
        std::uint8_t supplyRequired = 0;
        /// 이 유닛이 **채워 주는** 인구수. 오버로드 영웅 이그드라실처럼
        /// 일반과 크게 다른 것이 있다. supplyRequired 와 헷갈리지 않는다.
        std::uint8_t supplyProvided = 0;
        bool hero = false;        ///< units.dat 의 Hero 깃발
        bool invincible = false;
        bool autoAttackAndMove = false;  ///< 스스로 무는가
        bool regeneratesHp = false;
        bool spellcaster = false;
        bool detector = false;
        bool cloakable = false;
        bool permanentCloak = false;
        bool flyer = false;
        bool mechanical = false;
        bool organic = false;
        bool canAttack = false;   ///< units.dat 의 CanAttack 깃발
        /// 이 유닛에 딸린 아랫유닛(포탑 등). 228 이면 없다.
        std::uint16_t subunit1 = 228;
        std::uint16_t subunit2 = 228;
    };

    /// 한 유닛의 게임 데이터 값. 번호가 범위를 넘으면 기본값이 나온다.
    UnitStats unitStats(std::uint16_t unitType) const;

    /// units.dat 가 아는 유닛 종류 수 (보통 228). 못 읽었으면 0.
    std::size_t unitTypeCount() const;

    /// 유닛이 차지하는 자리. 유닛 좌표에서 각 방향으로 몇 픽셀인지다
    /// (units.dat 의 unitSize*). 겹침 검사와 격자 맞춤에 쓴다.
    struct UnitBounds
    {
        int left = 0;
        int up = 0;
        int right = 0;
        int down = 0;

        int width() const { return left + right + 1; }
        int height() const { return up + down + 1; }
    };
    UnitBounds unitBounds(std::uint16_t unitType) const;

    /// 건물이 차지하는 칸 수 (units.dat 의 StarEdit 배치 상자, 픽셀).
    /// 건물을 놓을 수 있는지 볼 때 이 범위의 타일을 살핀다.
    struct PlacementBox
    {
        int width = 0;   ///< 픽셀
        int height = 0;
    };
    PlacementBox placementBox(std::uint16_t unitType) const;


    /// 이 유닛이 크립을 만드는 저그 건물인지 (units.dat 의 CreepBuilding 특성).
    bool isCreepBuilding(std::uint16_t unitType) const;

    /// 타일셋이 가진 두들(나무·바위 같은 장식) 하나.
    struct DoodadInfo
    {
        std::uint16_t id = 0;         ///< dddata.bin 번호 (CHK 의 DD2 에 들어간다)
        std::string name;             ///< 사람이 읽는 이름
        int tileWidth = 0;
        int tileHeight = 0;
        std::uint16_t startTileGroup = 0; ///< 두들 타일이 시작하는 CV5 그룹
        std::uint16_t previewTileId = 0;  ///< 팔레트에 보일 타일

        /// 두들에 딸린 그림 조각. 0 이면 없다.
        ///
        /// 불타는 잔해나 깜빡이는 불빛처럼 움직이는 두들은 타일만으로는
        /// 표현되지 않아 스프라이트를 하나 더 얹는다.
        std::uint16_t overlayIndex = 0;
        bool spriteOverlay = false; ///< 참이면 sprites.dat, 거짓이면 units.dat
    };

    /// 그 타일셋의 두들 목록. 처음 부를 때 만들어 두고 그대로 돌려준다.
    const std::vector<DoodadInfo> & doodads(std::uint16_t tilesetId) const;

    /// 그 두들을 그 자리에 놓을 수 있는지 (dddata.bin 의 배치 가능 표).
    ///
    /// 표는 칸마다 "여기에 이 타일 그룹이 있어야 한다"를 적어 둔다. 0 이면
    /// 어떤 지형이든 좋다.
    bool doodadFits(std::uint16_t tilesetId, std::uint16_t doodadId,
                    const std::vector<std::uint16_t> & mapTiles,
                    int mapWidth, int mapHeight, int tileX, int tileY) const;

    /// 두들이 덮는 타일 값들 (왼쪽 위부터 가로 순서).
    std::vector<std::uint16_t> doodadTiles(std::uint16_t tilesetId, std::uint16_t doodadId) const;

    /// 두들이 덮는 칸들의 메가타일 번호 (왼쪽 위부터 가로 순서).
    ///
    /// 타일 값은 에디터마다 다르게 적히지만 — StarEdit 는 두들을 일반 타일로
    /// 구워 넣고 SCMDraft 는 두들 그룹 타일을 그대로 남긴다 — 화면에 보이는
    /// 그림은 메가타일이 정한다. 두들이 멀쩡한지 따질 때는 이쪽을 본다.
    std::vector<std::uint16_t> doodadMegaTiles(std::uint16_t tilesetId,
                                               std::uint16_t doodadId) const;

    /// 한 타일 값이 가리키는 메가타일 번호.
    std::uint16_t tileMegaTile(std::uint16_t tilesetId, std::uint16_t tileId) const;

    /// 한 타일의 지형 성질. 크립이 퍼질 수 있는지 판단하는 데 쓴다.
    struct TileTerrain
    {
        bool buildable = false; ///< 건물을 놓을 수 있는 평지인지
        int elevation = 0;      ///< 0 저지대, 1 중지대, 2 고지대

        /// 지상 유닛이 지나다닐 수 있는지. 한 타일은 4x4 미니타일로
        /// 나뉘고 각 칸마다 걷기 여부가 따로 있어서, 그중 얼마나
        /// 걸을 수 있는지를 함께 준다 (VF4).
        bool walkable = false;      ///< 한 칸이라도 걸을 수 있는지
        bool fullyWalkable = false; ///< 열여섯 칸 모두 걸을 수 있는지

        /// VF4 의 "시야 막음" 비트. 실제 타일셋에는 쓰이지 않는다 — 정글
        /// 8192 타일을 세어 0개였다. 게임은 높이 차이로 시야를 가린다.
        bool blocksView = false;

        /// VF4 의 램프 비트. 높이가 다른 땅을 잇는 비탈이다. ISOM 브러시에는
        /// 램프가 없으므로(지형 종류 표에 없다), 램프를 놓으려면 이 비트가
        /// 선 타일을 찾아 직접 써야 한다.
        bool ramp = false;

        /// 미니타일 16칸의 걷기 여부를 한 비트씩 담는다. 비트 y*4+x 가
        /// (x, y) 칸이다. 게임은 타일이 아니라 이 칸 단위로 길을 찾으므로,
        /// "여기서 저기로 걸어갈 수 있는가" 를 따지려면 이것이 있어야 한다.
        std::uint16_t walkMask = 0;

        /// 크립이 퍼질 수 있는 땅인지 (저그 건물을 놓을 수 있다).
        bool creep = false;
    };
    TileTerrain tileTerrain(std::uint16_t tilesetId, std::uint16_t tileId) const;

    /// 크립이 퍼지는 대략적인 범위(픽셀 반지름). 건물 크기에 따라 달라진다.
    /// 게임의 정확한 확산 규칙은 데이터에 드러나 있지 않아 근사값이다.
    struct CreepRange { double radiusX = 0.0; double radiusY = 0.0; };
    CreepRange creepRange(std::uint16_t unitType) const;

    /// 팔레트에 늘어놓을 타일 ID 목록.
    ///
    /// 타일 그룹의 칸 중 실제 메가타일이 배정된 것만 고른다 — 빈 칸을 그대로
    /// 늘어놓으면 검은 칸이 잔뜩 끼어 고르기 어렵다. 같은 그림을 가리키는
    /// 중복 칸도 뺀다.
    std::vector<std::uint16_t> paletteTileIds(std::uint16_t tilesetId) const;

    /// 크립 바닥으로 쓸 수 있는 타일 ID 들. 없으면 빈 벡터.
    std::vector<std::uint16_t> creepTileIds(std::uint16_t tilesetId) const;

    /// 크립이 깔릴 타일을 계산한다. 결과는 width*height 크기의 행 우선 마스크다.
    ///
    /// 게임에서 크립은 건물과 같은 높이의 평지에만 퍼진다 — 물·절벽·다른
    /// 고도로는 넘어가지 않는다. 타일 단위로 판정하므로 경계도 타일에 맞는다.
    std::vector<std::uint8_t> computeCreepMask(
        const std::vector<RawUnit> & units,
        const std::vector<std::uint16_t> & tiles,
        int tileWidth,
        int tileHeight,
        std::uint16_t tilesetId) const;

    /// 메가타일 하나를 직접 그린다(타일 그룹을 거치지 않는다).
    /// 타일셋에 어떤 그림이 들어 있는지 훑어볼 때 쓴다.
    bool renderMegaTile(std::uint16_t tilesetId, std::uint32_t megaTileIndex,
                        std::uint8_t * rgbaOut) const;

    /// 메가타일의 미니타일 구성 진단. 크립 가장자리 타일이 실제로
    /// 비어 있는(투명) 미니타일을 갖는지 확인할 때 쓴다.
    struct MegaTileInfo
    {
        int emptyMiniTiles = 0;  ///< vr4Index 가 0 인 칸 수 (0~16)
        std::uint32_t vr4[16] {};
    };
    MegaTileInfo describeMegaTile(std::uint16_t tilesetId, std::uint32_t megaTileIndex) const;

    /// 타일셋의 메가타일 개수.
    std::size_t megaTileCount(std::uint16_t tilesetId) const;

    /// 내부용. MappingCore 의 Sc::Data 를 가리킨다.
    ///
    /// 트리거를 사람이 읽는 텍스트로 옮기려면 유닛·업그레이드 이름표가 필요한데,
    /// 그것을 들고 있는 것이 이 객체다. 헤더에 MappingCore 타입을 노출하지
    /// 않으려고 불투명 포인터로 넘긴다 — io 계층 안에서만 쓴다.
    const void * internalScData() const;

    /// 내부용(쓰기). 트리거 컴파일은 Sc::Data 를 비-const 로 요구한다.
    void * internalScData();

    /// images.tbl 에 든 GRP 파일 이름들. 게임 데이터에 어떤 그래픽이 있는지
    /// 직접 확인할 때 쓴다(추측 대신 데이터를 보기 위한 통로).
    std::vector<std::string> imageFileNames() const;

    /// 타일셋 진단 정보. 크립 타일이 어디에 있는지 등을 조사하는 데 쓴다.
    struct TilesetInfo
    {
        std::size_t tileGroupCount = 0;
        std::size_t megaTileCount = 0;
        std::vector<std::uint16_t> creepGroups;      ///< Creep 플래그가 선 그룹
        std::vector<std::uint16_t> tempCreepGroups;  ///< TemporaryCreep 플래그
        std::vector<std::uint16_t> recedingGroups;   ///< RecedingCreep 플래그
    };
    TilesetInfo describeTileset(std::uint16_t tilesetId) const;

    /// 유닛 스프라이트를 읽을 수 있는지. 지형만 읽혔을 수도 있다.
    bool hasUnitGraphics() const;

    /// 유닛 한 마리를 그린다. 실패하면 비어 있는 이미지를 돌려준다.
    ///
    /// owner 는 플레이어 색 치환에 쓰인다(GRP 팔레트 인덱스 8-15 구간).
    /// tilesetId 는 나머지 색에 쓰일 팔레트를 고른다 — StarCraft 는 유닛도
    /// 지형 팔레트를 쓴다.
    /// resourceAmount 는 자원 유닛(미네랄·베스핀)의 그래픽 단계를 가른다.
    /// 0 이면 고갈된 모습이 되므로 실제 맵 값을 넘겨야 한다.
    /// 아이콘 하나가 쓰는 팔레트 인덱스의 분포. 어떤 팔레트를 써야
    /// 하는지 가리기 위한 진단용이다.
    std::vector<std::pair<std::uint8_t, std::size_t>> iconPaletteHistogram(
        std::uint16_t iconIndex) const;

    /// 아카이브에 그 파일이 있는지, 있으면 크기. 진단용이다.
    std::size_t assetSize(const std::string & archivePath) const;

    /// 명령 카드 아이콘 하나 (unit\\cmdbtns\\cmdicons.grp 의 프레임).
    ///
    /// 유닛·업그레이드·기술 설정 창에서 무엇을 고치는 중인지 한눈에
    /// 보이라고 쓴다. 없으면 빈 이미지를 준다.
    UnitImage renderIcon(std::uint16_t iconIndex, std::uint16_t tilesetId) const;

    /// 업그레이드·기술의 아이콘 번호 (dat 의 icon 필드).
    std::uint16_t upgradeIcon(std::uint16_t upgradeType) const;
    std::uint16_t techIcon(std::uint16_t techType) const;

    /// stateFlags 는 은폐·버로우·환영·떠 있음을, relationFlags 는 애드온이
    /// 붙었는지를 알려 준다 (Chk::Unit 의 같은 이름 필드). 그대로 넘기면
    /// 게임과 같은 모습으로 그린다.
    UnitImage renderUnit(std::uint16_t unitType,
                         std::uint8_t owner,
                         std::uint16_t tilesetId,
                         std::uint32_t resourceAmount = 0,
                         std::uint16_t stateFlags = 0,
                         std::uint16_t relationFlags = 0,
                         std::uint8_t colorIndex = 0xFF) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};


/// 그 지형에서 실제로 보이는 색 번호.
///
/// 초록은 원래 고를 수 있는 색이 아니라 두 자리를 메우려고 있는 색이다.
/// 얼음 지형에서는 흰색이 눈밭에 묻히고, 사막 지형에서는 갈색이 모래에
/// 묻혀 미니맵에서 분간이 안 된다. 그래서 게임이 그 두 경우에만 초록으로
/// 바꿔 그린다. 색 번호(COLR) 자체는 그대로고 보이는 색만 달라진다.
///
/// 이 규칙은 게임 실행 파일 안에 있어 자료 파일에는 없다 — 여덟 지형의
/// 팔레트를 다 비교해 봤지만 플레이어 색 자리는 모두 같았다.
inline std::uint8_t tilesetPlayerColor(std::uint16_t tilesetId, std::uint8_t colorIndex)
{
    constexpr std::uint16_t kDesert = 5;
    constexpr std::uint16_t kArctic = 6; // 얼음
    constexpr std::uint8_t kBrown = 5;
    constexpr std::uint8_t kWhite = 6;
    constexpr std::uint8_t kGreen = 8;

    switch (tilesetId % 8)
    {
        case kArctic: return colorIndex == kWhite ? kGreen : colorIndex;
        case kDesert: return colorIndex == kBrown ? kGreen : colorIndex;
        default:      return colorIndex;
    }
}

/// 두들 가운데 픽셀에서 왼쪽 위 타일 좌표를 구한다.
///
/// 칸 수가 짝수인 두들은 타일 경계에, 홀수인 두들은 타일 한가운데에 중심이
/// 온다. 두 경우를 섞어 쓰면 한 칸씩 밀린다.
inline int doodadOriginTile(int centerPixel, int tileCount)
{
    return tileCount % 2 == 0 ? (centerPixel + 16) / 32 - tileCount / 2
                              : centerPixel / 32 - (tileCount - 1) / 2;
}

/// 왼쪽 위 타일에서 DD2 에 적을 가운데 픽셀을 구한다.
inline int doodadCenterPixel(int originTile, int tileCount)
{
    return originTile * 32 + tileCount * 16;
}

} // namespace splash::io
