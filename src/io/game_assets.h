#pragma once

// StarCraft 설치본에서 그래픽 에셋을 읽기 위한 계층.
//
// M1 에는 쓰이지 않는다. M2(지형 렌더링)의 토대이며, 우선 "설치본을 찾고
// 열 수 있는가"를 확인하는 기능만 둔다.
//
// 맵 파일은 MPQ, 게임 설치본은 리마스터면 CASC / 구버전이면 MPQ 다.
// 여기서도 MappingCore 헤더는 .cpp 안에만 존재한다.

#include <cstdint>
#include <string>
#include <vector>

namespace splash::io {

/// 설치본이 어떤 형태로 에셋을 담고 있는지.
enum class InstallationKind
{
    None,      ///< 설치본으로 보이지 않음
    Casc,      ///< 리마스터 (Data/ CASC 스토리지)
    Mpq        ///< 구버전 (StarDat.mpq / BrooDat.mpq)
};

/// 설치본 조사 결과.
struct InstallationInfo
{
    InstallationKind kind = InstallationKind::None;
    std::string path;
    std::string detail;                    ///< 사람이 읽을 요약 또는 실패 사유
    std::vector<std::string> tilesetsFound; ///< .cv5 를 찾은 타일셋 이름들

    bool ok() const { return kind != InstallationKind::None && !tilesetsFound.empty(); }
};

/// 설치 폴더를 조사한다. 아카이브를 열고 타일셋 데이터가 실제로 읽히는지까지 본다.
///
/// 비용이 크므로(CASC 인덱스 로딩) 호출 빈도를 낮게 유지할 것.
InstallationInfo probeInstallation(const std::string & installPath);

/// 8개 타일셋의 내부 이름. 인덱스는 CHK 의 ERA 값과 같다.
const std::vector<std::string> & tilesetAssetNames();

} // namespace splash::io
