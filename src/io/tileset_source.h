#pragma once

// 게임 설치본에서 타일셋 그래픽을 읽어 타일 하나를 픽셀로 펼쳐 주는 계층.
//
// Qt 를 모른다. 픽셀은 생 버퍼로 넘기고, 그것을 QImage 로 감싸는 일은 UI 몫이다.
//
// 타일 하나는 32x32 픽셀이며, 4x4 개의 미니타일(각 8x8)로 이루어진다.
// 변환 경로(전부 MappingCore 가 파싱한 자료를 쓴다):
//   tileId -> CV5 타일 그룹 -> VX4 메가타일 -> VR4 미니타일 픽셀 -> WPE 팔레트

#include <cstdint>
#include <memory>
#include <string>

namespace splash::io {

/// 타일 한 변의 픽셀 수.
inline constexpr int kTilePixels = 32;

/// 타일 하나를 RGBA 로 펼쳤을 때의 바이트 수.
inline constexpr int kTileRgbaBytes = kTilePixels * kTilePixels * 4;

/// 설치본의 타일셋 그래픽. 열려 있는 동안 게임 아카이브를 붙잡고 있다.
///
/// 복사 불가, 이동 가능. 스레드 안전하지 않다.
class TilesetSource
{
public:
    TilesetSource();
    ~TilesetSource();

    TilesetSource(const TilesetSource &) = delete;
    TilesetSource & operator=(const TilesetSource &) = delete;
    TilesetSource(TilesetSource &&) noexcept;
    TilesetSource & operator=(TilesetSource &&) noexcept;

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace splash::io
