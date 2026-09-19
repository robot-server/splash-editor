#include "io/game_assets.h"

#include "mapping_core/casc_archive.h"
#include "mapping_core/mpq_file.h"
#include "mapping_core/system_io.h"

#include <exception>
#include <filesystem>

namespace splash::io {
namespace {

/// 타일셋 에셋 파일의 기본 이름. MappingCore 의 Sc::Terrain::TilesetNames 와
/// 같은 값이며, 인덱스는 CHK ERA 원시값과 일치한다.
/// (표시용 이름은 splash::chk::tilesetDisplayName 쪽이다 — 여기 값은 파일명이다.)
const std::vector<std::string> kTilesetAssetNames = {
    "badlands", "platform", "install", "ashworld",
    "jungle",   "Desert",   "Ice",     "Twilight",
};

/// 아카이브에서 각 타일셋의 .cv5 가 실제로 읽히는지 확인한다.
/// .cv5 를 고른 이유: 타일 그룹 정의로, 지형을 그리려면 반드시 필요하다.
std::vector<std::string> findTilesets(ArchiveFile & archive)
{
    std::vector<std::string> found;
    for (const std::string & name : kTilesetAssetNames)
    {
        const std::string path =
            makeExtArchiveFilePath(makeArchiveFilePath("tileset", name), "cv5");

        if (archive.getFileSize(path) > 0)
            found.push_back(name);
    }
    return found;
}

} // namespace

const std::vector<std::string> & tilesetAssetNames()
{
    return kTilesetAssetNames;
}

InstallationInfo probeInstallation(const std::string & installPath)
{
    InstallationInfo info;
    info.path = installPath;

    std::error_code ec;
    if (installPath.empty() || !std::filesystem::is_directory(installPath, ec))
    {
        info.detail = "폴더가 아닙니다: " + installPath;
        return info;
    }

    const std::filesystem::path root(installPath);

    // --- 리마스터: .build.info 와 Data/ 가 있으면 CASC ---
    if (std::filesystem::exists(root / ".build.info", ec) &&
        std::filesystem::is_directory(root / "Data", ec))
    {
        try
        {
            CascArchive casc;
            if (!casc.open(installPath, /*readOnly*/ true, /*createIfNotFound*/ false))
            {
                info.detail = "CASC 스토리지를 열지 못했습니다.";
                return info;
            }

            info.kind = InstallationKind::Casc;
            info.tilesetsFound = findTilesets(casc);
            casc.close();

            info.detail = info.tilesetsFound.empty()
                ? "CASC 는 열렸지만 타일셋 데이터를 찾지 못했습니다."
                : "리마스터 설치본 (CASC)";
            return info;
        }
        catch (const std::exception & e)
        {
            info.kind = InstallationKind::None;
            info.detail = std::string("CASC 를 여는 중 예외: ") + e.what();
            return info;
        }
    }

    // --- 구버전: StarDat.mpq / BrooDat.mpq ---
    for (const char * candidate : {"BrooDat.mpq", "StarDat.mpq", "brooDat.mpq", "starDat.mpq"})
    {
        const std::filesystem::path mpqPath = root / candidate;
        if (!std::filesystem::exists(mpqPath, ec))
            continue;

        try
        {
            MpqFile mpq;
            if (!mpq.open(mpqPath.string(), /*readOnly*/ true, /*createIfNotFound*/ false))
                continue;

            info.kind = InstallationKind::Mpq;
            info.tilesetsFound = findTilesets(mpq);
            mpq.close();

            info.detail = info.tilesetsFound.empty()
                ? std::string("MPQ 는 열렸지만 타일셋 데이터를 찾지 못했습니다: ") + candidate
                : std::string("구버전 설치본 (MPQ): ") + candidate;
            return info;
        }
        catch (const std::exception &)
        {
            // 다음 후보를 시도한다
        }
    }

    info.detail = "StarCraft 설치본으로 보이지 않습니다 (.build.info 도 *.mpq 도 없음).";
    return info;
}

} // namespace splash::io
