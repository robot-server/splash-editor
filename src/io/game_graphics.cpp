#include "io/game_graphics.h"

#include "io/game_assets.h"
#include "mapping_core/casc_archive.h"
#include "mapping_core/archive_cluster.h"
#include "mapping_core/mpq_file.h"
#include "mapping_core/sc.h"

#include <array>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <vector>

namespace splash::io {

struct GameGraphics::Impl
{
    std::shared_ptr<ArchiveCluster> cluster;
    std::unique_ptr<Sc::Terrain> terrain;
    std::unique_ptr<Sc::Unit> units;     ///< units.dat + flingy.dat
    std::unique_ptr<Sc::Sprite> sprites; ///< sprites.dat + images.dat + GRP
    std::unique_ptr<Sc::Pcx> tunit;      ///< 플레이어 색 치환 팔레트
    bool loaded = false;
    bool unitsLoaded = false;
};

GameGraphics::GameGraphics() : impl_(std::make_unique<Impl>()) {}
GameGraphics::~GameGraphics() = default;
GameGraphics::GameGraphics(GameGraphics &&) noexcept = default;
GameGraphics & GameGraphics::operator=(GameGraphics &&) noexcept = default;

bool GameGraphics::isLoaded() const
{
    return impl_->loaded;
}

bool GameGraphics::load(const std::string & installPath, std::string * error)
{
    const auto setError = [error](const std::string & why) {
        if (error != nullptr)
            *error = why;
        return false;
    };

    const InstallationInfo info = probeInstallation(installPath);
    if (!info.ok())
        return setError(info.detail);

    auto fresh = std::make_unique<Impl>();

    try
    {
        // 설치 형태에 맞는 아카이브를 열어 클러스터에 담는다.
        // 클러스터는 여러 아카이브를 우선순위대로 뒤지는 인터페이스라,
        // 구버전의 patch_rt/BrooDat/StarDat 조합을 나중에 확장하기 좋다.
        std::vector<ArchiveFilePtr> sources;

        if (info.kind == InstallationKind::Casc)
        {
            auto casc = std::make_shared<CascArchive>();
            if (!casc->open(installPath, /*readOnly*/ true, /*createIfNotFound*/ false))
                return setError("CASC 스토리지를 열지 못했습니다: " + installPath);
            sources.push_back(casc);
        }
        else
        {
            // 구버전: BrooDat 을 먼저, 없으면 StarDat 을 쓴다.
            const std::filesystem::path root(installPath);
            bool opened = false;
            for (const char * candidate : {"BrooDat.mpq", "StarDat.mpq"})
            {
                std::error_code ec;
                const auto path = root / candidate;
                if (!std::filesystem::exists(path, ec))
                    continue;

                auto mpq = std::make_shared<MpqFile>();
                if (mpq->open(path.string(), /*readOnly*/ true, /*createIfNotFound*/ false))
                {
                    sources.push_back(mpq);
                    opened = true;
                }
            }
            if (!opened)
                return setError("게임 MPQ 를 열지 못했습니다: " + installPath);
        }

        fresh->cluster = std::make_shared<ArchiveCluster>(std::move(sources));
        fresh->terrain = std::make_unique<Sc::Terrain>();

        // statTxt 는 생략한다(기본값 nullptr). 두들 이름 표시에만 쓰이며
        // 지형 픽셀을 그리는 데는 필요 없다.
        if (!fresh->terrain->load(*fresh->cluster, nullptr))
        {
            // 일부 타일셋만 실패해도 false 가 나올 수 있다.
            // 하나라도 쓸 수 있으면 계속 진행하는 편이 낫다 — 실패 판정은
            // renderTile 에서 타일셋 단위로 드러난다.
        }

        // --- 유닛 그래픽 ---
        // 지형만 있어도 맵은 볼 수 있으므로, 여기서 실패해도 전체를 실패로
        // 만들지 않는다. hasUnitGraphics() 로 구분한다.
        try
        {
            auto units = std::make_unique<Sc::Unit>();
            auto sprites = std::make_unique<Sc::Sprite>();

            // images.tbl 은 GRP 파일 이름표다. 이것이 없으면 스프라이트를
            // 파일과 이어 붙일 수 없다.
            auto imagesTbl = std::make_shared<Sc::TblFile>();
            const bool tblOk = imagesTbl->load(*fresh->cluster, "arr\\images.tbl");

            if (tblOk && units->load(*fresh->cluster) &&
                sprites->load(*fresh->cluster, imagesTbl))
            {
                fresh->units = std::move(units);
                fresh->sprites = std::move(sprites);
                fresh->unitsLoaded = true;

                // 플레이어 색 치환표. 없으면 근사 색으로 대신한다.
                auto tunit = std::make_unique<Sc::Pcx>();
                if (tunit->load(*fresh->cluster, "game\\tunit.pcx"))
                    fresh->tunit = std::move(tunit);
            }
        }
        catch (const std::exception &)
        {
            // 유닛 그래픽 없이 지형만으로 계속한다
        }
    }
    catch (const std::exception & e)
    {
        return setError(std::string("타일셋을 읽는 중 예외: ") + e.what());
    }
    catch (...)
    {
        return setError("타일셋을 읽는 중 알 수 없는 예외가 발생했습니다.");
    }

    fresh->loaded = true;
    impl_ = std::move(fresh);
    return true;
}

bool GameGraphics::hasUnitGraphics() const
{
    return impl_->loaded && impl_->unitsLoaded;
}

UnitImage GameGraphics::renderUnit(std::uint16_t unitType,
                                   std::uint8_t owner,
                                   std::uint16_t tilesetId) const
{
    UnitImage out;
    if (!hasUnitGraphics())
        return out;

    const Sc::Unit & units = *impl_->units;
    const Sc::Sprite & sprites = *impl_->sprites;

    try
    {
        // unitType -> flingy -> sprite -> image -> GRP
        // (MapGraphics::getImageId 와 같은 경로다)
        const std::size_t safeType = unitType < units.numUnitTypes() ? unitType : 0;
        const std::uint32_t flingyId = units.getUnit(Sc::Unit::Type(safeType)).graphics;
        const std::size_t safeFlingy = flingyId < units.numFlingies() ? flingyId : 0;
        const std::uint32_t spriteId = units.getFlingy(safeFlingy).sprite;
        const std::size_t safeSprite = spriteId < sprites.numSprites() ? spriteId : 0;
        const std::size_t imageId = sprites.getSprite(safeSprite).imageFile;
        if (imageId >= sprites.numImages())
            return out;

        const std::size_t grpIndex = sprites.getImage(imageId).grpFile;
        const Sc::Sprite::GrpFile & grp =
            const_cast<Sc::Sprite &>(sprites).getGrp(grpIndex).get();

        if (grp.numFrames == 0)
            return out;

        // 프레임 0 = 정면 기본 자세. 애니메이션은 M3 범위 밖이다.
        const Sc::Sprite::GrpFrameHeader & header = grp.frameHeaders[0];
        const int frameWidth = header.frameWidth;
        const int frameHeight = header.frameHeight;
        if (frameWidth <= 0 || frameHeight <= 0)
            return out;

        // 팔레트: 지형 팔레트를 쓰되 8-15 구간은 플레이어 색으로 바꾼다.
        const Sc::Terrain::Tiles & tiles =
            impl_->terrain->get(Sc::Terrain::Tileset(tilesetId % Sc::Terrain::NumTilesets));
        std::array<Sc::SystemColor, Sc::NumColors> palette = tiles.systemColorPalette;

        if (impl_->tunit)
        {
            // tunit.pcx 는 플레이어당 8색 그라데이션을 순서대로 담는다
            // (Chkdraft 도 rgbaPalette[8*player] 로 같은 규약을 쓴다).
            const auto & ramp = impl_->tunit->bgraPalette;
            const std::size_t base = static_cast<std::size_t>(owner) * 8;
            for (std::size_t i = 0; i < 8; ++i)
            {
                const std::size_t at = base + i;
                if (at < ramp.size())
                    palette[8 + i] = ramp[at];
            }
        }

        out.width = frameWidth;
        out.height = frameHeight;
        // GRP 프레임은 스프라이트 원점 기준 오프셋을 갖는다.
        // 유닛 중심은 GRP 전체 크기의 절반에서 프레임 오프셋을 뺀 자리다.
        out.anchorX = grp.grpWidth / 2 - header.xOffset;
        out.anchorY = grp.grpHeight / 2 - header.yOffset;
        out.rgba.assign(static_cast<std::size_t>(frameWidth) * frameHeight * 4, 0);

        // --- GRP 프레임 디코딩 ---
        // 행마다 PixelLine 이 이어진다. 규약은 MappingCore 의 PixelLine 이
        // 캡슐화하고 있다(투명/단색/얼룩 라인).
        const std::uint8_t * grpBytes = reinterpret_cast<const std::uint8_t *>(&grp);
        const std::size_t frameOffset = header.frameOffset;
        const Sc::Sprite::GrpFrame & frame =
            reinterpret_cast<const Sc::Sprite::GrpFrame &>(grpBytes[frameOffset]);

        for (int row = 0; row < frameHeight; ++row)
        {
            const std::size_t rowOffset = frame.rowOffsets[row];
            const std::uint8_t * lineBytes = &grpBytes[frameOffset + rowOffset];

            int x = 0;
            std::size_t lineOffset = 0;
            while (x < frameWidth)
            {
                const Sc::Sprite::PixelLine & line =
                    reinterpret_cast<const Sc::Sprite::PixelLine &>(lineBytes[lineOffset]);

                int length = static_cast<int>(line.lineLength());
                if (x + length > frameWidth)
                    length = frameWidth - x;
                if (length <= 0)
                    break;

                if (line.isSpeckled())
                {
                    for (int i = 0; i < length; ++i)
                    {
                        const Sc::SystemColor & c = palette[line.paletteIndex[i]];
                        const std::size_t at =
                            (static_cast<std::size_t>(row) * frameWidth + x + i) * 4;
                        out.rgba[at + 0] = c.red;
                        out.rgba[at + 1] = c.green;
                        out.rgba[at + 2] = c.blue;
                        out.rgba[at + 3] = 255;
                    }
                }
                else if (line.isSolidLine())
                {
                    const Sc::SystemColor & c = palette[line.paletteIndex[0]];
                    for (int i = 0; i < length; ++i)
                    {
                        const std::size_t at =
                            (static_cast<std::size_t>(row) * frameWidth + x + i) * 4;
                        out.rgba[at + 0] = c.red;
                        out.rgba[at + 1] = c.green;
                        out.rgba[at + 2] = c.blue;
                        out.rgba[at + 3] = 255;
                    }
                }
                // 투명 라인은 알파 0 그대로 둔다

                x += length;
                lineOffset += line.sizeInBytes();
            }
        }
    }
    catch (const std::exception &)
    {
        return UnitImage{};
    }

    return out;
}

bool GameGraphics::renderTile(std::uint16_t tilesetId,
                               std::uint16_t tileId,
                               std::uint8_t * rgbaOut) const
{
    if (rgbaOut == nullptr)
        return false;

    const auto fillBlack = [rgbaOut] {
        for (int i = 0; i < kTilePixels * kTilePixels; ++i)
        {
            rgbaOut[i * 4 + 0] = 0;
            rgbaOut[i * 4 + 1] = 0;
            rgbaOut[i * 4 + 2] = 0;
            rgbaOut[i * 4 + 3] = 255;
        }
    };

    if (!impl_->loaded)
    {
        fillBlack();
        return false;
    }

    const Sc::Terrain::Tiles & tiles =
        impl_->terrain->get(Sc::Terrain::Tileset(tilesetId % Sc::Terrain::NumTilesets));

    // tileId 상위 12비트가 타일 그룹, 하위 4비트가 그룹 내 위치다.
    const std::size_t groupIndex = static_cast<std::size_t>(tileId) / 16;
    const std::size_t subIndex   = static_cast<std::size_t>(tileId) % 16;

    if (groupIndex >= tiles.tileGroups.size())
    {
        fillBlack();
        return false;
    }

    const std::uint16_t megaTileIndex =
        tiles.tileGroups[groupIndex].megaTileIndex[subIndex];

    if (megaTileIndex >= tiles.tileGraphics.size())
    {
        fillBlack();
        return false;
    }

    const auto & graphics = tiles.tileGraphics[megaTileIndex];

    for (int my = 0; my < 4; ++my)
    {
        for (int mx = 0; mx < 4; ++mx)
        {
            const auto & mini = graphics.miniTileGraphics[my][mx];
            const std::uint32_t vr4Index = mini.vr4Index();
            const bool flipped = mini.isFlipped();

            if (vr4Index >= tiles.miniTilePixels.size())
                continue; // 이 미니타일만 검게 남는다

            const auto & pixels = tiles.miniTilePixels[vr4Index];

            for (int py = 0; py < 8; ++py)
            {
                for (int px = 0; px < 8; ++px)
                {
                    // 좌우 반전 플래그가 서면 미니타일을 가로로 뒤집어 그린다.
                    const int srcX = flipped ? (7 - px) : px;
                    const std::uint8_t paletteIndex = pixels.wpeIndex[py][srcX];
                    const Sc::SystemColor & color = tiles.systemColorPalette[paletteIndex];

                    const int outX = mx * 8 + px;
                    const int outY = my * 8 + py;
                    const std::size_t at =
                        (static_cast<std::size_t>(outY) * kTilePixels + outX) * 4;

                    rgbaOut[at + 0] = color.red;
                    rgbaOut[at + 1] = color.green;
                    rgbaOut[at + 2] = color.blue;
                    rgbaOut[at + 3] = 255;
                }
            }
        }
    }

    return true;
}

} // namespace splash::io
