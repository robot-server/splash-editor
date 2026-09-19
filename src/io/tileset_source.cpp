#include "io/tileset_source.h"

#include "io/game_assets.h"
#include "mapping_core/casc_archive.h"
#include "mapping_core/archive_cluster.h"
#include "mapping_core/mpq_file.h"
#include "mapping_core/sc.h"

#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <vector>

namespace splash::io {

struct TilesetSource::Impl
{
    std::shared_ptr<ArchiveCluster> cluster;
    std::unique_ptr<Sc::Terrain> terrain;
    bool loaded = false;
};

TilesetSource::TilesetSource() : impl_(std::make_unique<Impl>()) {}
TilesetSource::~TilesetSource() = default;
TilesetSource::TilesetSource(TilesetSource &&) noexcept = default;
TilesetSource & TilesetSource::operator=(TilesetSource &&) noexcept = default;

bool TilesetSource::isLoaded() const
{
    return impl_->loaded;
}

bool TilesetSource::load(const std::string & installPath, std::string * error)
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

bool TilesetSource::renderTile(std::uint16_t tilesetId,
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
