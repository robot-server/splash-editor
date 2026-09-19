#pragma once

// 게임 설치본에서 그래픽(지형 타일 · 유닛 스프라이트)을 읽어 픽셀로 펼쳐 주는 계층.
//
// Qt 를 모른다. 픽셀은 생 버퍼로 넘기고, 그것을 QImage 로 감싸는 일은 UI 몫이다.
//
// 타일 하나는 32x32 픽셀이며, 4x4 개의 미니타일(각 8x8)로 이루어진다.
// 변환 경로(전부 MappingCore 가 파싱한 자료를 쓴다):
//   tileId -> CV5 타일 그룹 -> VX4 메가타일 -> VR4 미니타일 픽셀 -> WPE 팔레트

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
    bool load(const std::string & installPath, std::string * error = nullptr);

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
    UnitImage renderSprite(std::uint16_t spriteType,
                           std::uint8_t owner,
                           std::uint16_t tilesetId,
                           bool drawnAsSprite) const;

    /// 이 유닛이 크립을 만드는 저그 건물인지 (units.dat 의 CreepBuilding 특성).
    bool isCreepBuilding(std::uint16_t unitType) const;

    /// 크립이 퍼지는 대략적인 범위(픽셀 반지름). 건물 크기에 따라 달라진다.
    /// 게임의 정확한 확산 규칙은 데이터에 드러나 있지 않아 근사값이다.
    struct CreepRange { double radiusX = 0.0; double radiusY = 0.0; };
    CreepRange creepRange(std::uint16_t unitType) const;

    /// 크립 바닥으로 쓸 수 있는 타일 ID 들. 없으면 빈 벡터.
    std::vector<std::uint16_t> creepTileIds(std::uint16_t tilesetId) const;

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
    UnitImage renderUnit(std::uint16_t unitType,
                         std::uint8_t owner,
                         std::uint16_t tilesetId,
                         std::uint32_t resourceAmount = 0) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace splash::io
