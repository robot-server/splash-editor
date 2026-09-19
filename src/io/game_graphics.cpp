#include "io/game_graphics.h"

#include "io/game_assets.h"
#include "mapping_core/casc_archive.h"
#include "mapping_core/archive_cluster.h"
#include "mapping_core/mpq_file.h"
#include "mapping_core/sc.h"
#include "mapping_core/chk.h"
#include "mapping_core/render/map_animations.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace splash::io {

struct GameGraphics::Impl
{
    std::shared_ptr<ArchiveCluster> cluster;

    // Sc::Data 는 load() 대신 필요한 부분만 직접 채운다. 그쪽 load() 는
    // 파일 브라우저를 요구하고 우리가 이미 연 아카이브를 쓰지 않는다.
    std::unique_ptr<Sc::Data> scData;

    // iscript 실행기. 유닛 하나가 어떤 이미지들로 구성되는지(본체·그림자·부가)
    // 는 iscript 가 정하므로, 그것을 돌려야 그림자와 방향이 맞는다.
    std::unique_ptr<GameClock> clock;
    std::unique_ptr<AnimContext> anim;

    bool loaded = false;
    bool unitsLoaded = false;

    const Sc::Terrain::Tiles & tiles(std::uint16_t tilesetId) const
    {
        return scData->terrain.get(
            Sc::Terrain::Tileset(tilesetId % Sc::Terrain::NumTilesets));
    }
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
        fresh->scData = std::make_unique<Sc::Data>();

        // statTxt 는 생략한다(기본값 nullptr). 두들 이름 표시에만 쓰이며
        // 지형 픽셀을 그리는 데는 필요 없다.
        if (!fresh->scData->terrain.load(*fresh->cluster, nullptr))
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
            // images.tbl 은 GRP 파일 이름표다. 이것이 없으면 스프라이트를
            // 파일과 이어 붙일 수 없다.
            auto imagesTbl = std::make_shared<Sc::TblFile>();
            const bool tblOk = imagesTbl->load(*fresh->cluster, "arr\\images.tbl");

            if (tblOk &&
                fresh->scData->units.load(*fresh->cluster) &&
                fresh->scData->sprites.load(*fresh->cluster, imagesTbl))
            {
                // 플레이어 색 치환표. 없으면 지형 팔레트 색이 그대로 남는다.
                fresh->scData->tunit.load(*fresh->cluster, "game\\tunit.pcx");

                fresh->clock = std::make_unique<GameClock>();
                fresh->anim = std::make_unique<AnimContext>(*fresh->scData, *fresh->clock);
                fresh->unitsLoaded = true;
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
                                   std::uint16_t tilesetId,
                                   std::uint32_t resourceAmount) const
{
    UnitImage out;
    if (!hasUnitGraphics())
        return out;

    Sc::Data & sc = *impl_->scData;
    AnimContext & anim = *impl_->anim;
    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);

    try
    {
        // --- 팔레트 두 벌 ---
        // 일반 이미지는 지형 팔레트를 쓰되 8-15 구간을 플레이어 색으로 바꾼다.
        // 그림자는 dark.pcx 팔레트로 그린다(Chkdraft 도 shadowPalette 로 같은 것을 쓴다).
        std::array<Sc::SystemColor, Sc::NumColors> palette = tiles.systemColorPalette;
        const auto & ramp = sc.tunit.bgraPalette;
        const std::size_t rampBase = static_cast<std::size_t>(owner) * 8;
        for (std::size_t i = 0; i < 8; ++i)
        {
            if (rampBase + i < ramp.size())
                palette[8 + i] = ramp[rampBase + i];
        }
        // 그림자는 색을 칠하는 것이 아니라 배경을 어둡게 하는 효과다.
        // dark.pcx 는 "배경색 -> 어두운 색" 매핑표라 배경을 알아야 정확한데,
        // 스프라이트를 따로 그리는 우리는 배경을 모른다. 반투명 검정으로 근사한다.
        constexpr std::uint8_t kShadowAlpha = 110;

        // --- iscript 를 돌려 이 유닛이 어떤 이미지들로 구성되는지 얻는다 ---
        Chk::Unit chkUnit {};
        chkUnit.type = Sc::Unit::Type(unitType);
        chkUnit.owner = owner;
        chkUnit.xc = 0;
        chkUnit.yc = 0;
        // 자원 유닛은 남은 양에 따라 그래픽이 달라진다(미네랄 3단계 등).
        chkUnit.resourceAmount = resourceAmount;

        MapActor actor {};
        anim.initializeUnitActor(actor, /*isClipboard*/ false, /*unitIndex*/ 0, chkUnit, 0, 0);

        struct Layer
        {
            const Sc::Sprite::GrpFile * grp = nullptr;
            std::size_t frame = 0;
            int left = 0;   ///< 유닛 중심 기준
            int top = 0;
            bool flipped = false;
            bool shadow = false;
        };

        std::vector<Layer> layers;
        int minLeft = 0, minTop = 0, maxRight = 0, maxBottom = 0;
        bool first = true;

        for (std::size_t slot = 0; slot < MapActor::MaxSlots; ++slot)
        {
            const std::uint16_t imageIndex = actor.usedImages[slot];
            if (imageIndex == 0 || imageIndex >= anim.images.size())
                continue;
            if (!anim.images[imageIndex].has_value())
                continue;

            const MapImage & image = *anim.images[imageIndex];
            if (image.hidden || image.drawFunction == MapImage::DrawFunction::None)
                continue;
            if (image.imageId >= sc.sprites.numImages())
                continue;

            const std::size_t grpIndex = sc.sprites.getImage(image.imageId).grpFile;
            const Sc::Sprite::GrpFile & grp = sc.sprites.getGrp(grpIndex).get();
            if (grp.numFrames == 0)
                continue;

            const std::size_t frame =
                image.frame < grp.numFrames ? image.frame : 0;
            const Sc::Sprite::GrpFrameHeader & header = grp.frameHeaders[frame];
            if (header.frameWidth == 0 || header.frameHeight == 0)
                continue;

            Layer layer;
            layer.grp = &grp;
            layer.frame = frame;
            layer.flipped = image.flipped;
            layer.shadow = (image.drawFunction == MapImage::DrawFunction::Shadow);

            // GRP 프레임은 스프라이트 원점(그림 중앙) 기준 오프셋을 갖는다.
            // 좌우 반전 시에는 프레임이 반대쪽에서 시작하므로 x 기준이 달라진다
            // (Chkdraft 의 drawClassicImage 와 같은 계산이다).
            layer.left = image.xc + image.xOffset +
                (layer.flipped
                     ? (grp.grpWidth / 2 - header.frameWidth - header.xOffset)
                     : (-grp.grpWidth / 2 + header.xOffset));
            layer.top  = image.yc + image.yOffset - grp.grpHeight / 2 + header.yOffset;

            const int right = layer.left + header.frameWidth;
            const int bottom = layer.top + header.frameHeight;

            if (first)
            {
                minLeft = layer.left; minTop = layer.top;
                maxRight = right; maxBottom = bottom;
                first = false;
            }
            else
            {
                minLeft = std::min(minLeft, layer.left);
                minTop = std::min(minTop, layer.top);
                maxRight = std::max(maxRight, right);
                maxBottom = std::max(maxBottom, bottom);
            }

            if (std::getenv("SPLASH_DEBUG_LAYERS") != nullptr)
            {
                std::cerr << "    layer slot=" << slot
                          << " imageId=" << image.imageId
                          << " grp=" << grpIndex
                          << " frame=" << frame << "/" << grp.numFrames
                          << " grpWH=" << grp.grpWidth << "x" << grp.grpHeight
                          << " frameWH=" << int(header.frameWidth) << "x" << int(header.frameHeight)
                          << " hdrOff=(" << int(header.xOffset) << "," << int(header.yOffset) << ")"
                          << " imgOff=(" << int(image.xOffset) << "," << int(image.yOffset) << ")"
                          << " xc,yc=(" << image.xc << "," << image.yc << ")"
                          << " flip=" << image.flipped
                          << " draw=" << int(image.drawFunction)
                          << " -> left=" << layer.left << " top=" << layer.top << "\n";
            }

            layers.push_back(layer);
        }

        anim.clearActor(actor); // 다음 호출을 위해 이미지 슬롯을 돌려준다

        if (layers.empty())
            return out;

        out.width = maxRight - minLeft;
        out.height = maxBottom - minTop;
        if (out.width <= 0 || out.height <= 0)
            return out;

        out.anchorX = -minLeft;
        out.anchorY = -minTop;
        out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);

        // --- 레이어를 순서대로 합성 (iscript 가 넣은 순서 = 아래에서 위) ---
        for (const Layer & layer : layers)
        {
            const Sc::Sprite::GrpFile & grp = *layer.grp;
            const Sc::Sprite::GrpFrameHeader & header = grp.frameHeaders[layer.frame];
            const int frameWidth = header.frameWidth;
            const int frameHeight = header.frameHeight;

            const std::uint8_t * grpBytes = reinterpret_cast<const std::uint8_t *>(&grp);
            const std::size_t frameOffset = header.frameOffset;
            const Sc::Sprite::GrpFrame & frameData =
                reinterpret_cast<const Sc::Sprite::GrpFrame &>(grpBytes[frameOffset]);

            const int baseX = layer.left - minLeft;
            const int baseY = layer.top - minTop;

            for (int row = 0; row < frameHeight; ++row)
            {
                const std::size_t rowOffset = frameData.rowOffsets[row];
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

                    const bool transparent =
                        !line.isSpeckled() && !line.isSolidLine();

                    if (!transparent)
                    {
                        for (int i = 0; i < length; ++i)
                        {
                            const std::uint8_t index = line.isSpeckled()
                                ? line.paletteIndex[i]
                                : line.paletteIndex[0];

                            // 좌우 반전은 프레임 안에서만 일어난다.
                            const int srcX = layer.flipped ? (frameWidth - 1 - (x + i)) : (x + i);
                            const int dstX = baseX + srcX;
                            const int dstY = baseY + row;
                            if (dstX < 0 || dstY < 0 || dstX >= out.width || dstY >= out.height)
                                continue;

                            const std::size_t at =
                                (static_cast<std::size_t>(dstY) * out.width + dstX) * 4;

                            if (layer.shadow)
                            {
                                // 이미 칠해진 픽셀(먼저 그린 본체)이 있으면 덮지 않는다.
                                if (out.rgba[at + 3] != 0)
                                    continue;
                                out.rgba[at + 0] = 0;
                                out.rgba[at + 1] = 0;
                                out.rgba[at + 2] = 0;
                                out.rgba[at + 3] = kShadowAlpha;
                            }
                            else
                            {
                                const Sc::SystemColor & color = palette[index];
                                out.rgba[at + 0] = color.red;
                                out.rgba[at + 1] = color.green;
                                out.rgba[at + 2] = color.blue;
                                out.rgba[at + 3] = 255;
                            }
                        }
                    }

                    x += length;
                    lineOffset += line.sizeInBytes();
                }
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

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);

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
