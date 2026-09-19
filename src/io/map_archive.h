#pragma once

// MappingCore(Chkdraft) 에 대한 유일한 접점.
//
// MappingCore 헤더는 무겁고(chk.h 만 90KB+) RareCpp 리플렉션 매크로를 끌고 온다.
// 그것이 상위 계층·UI 로 새어 나가지 않도록 이 파일은 pimpl 뒤에 전부 감춘다.
// 이 헤더에는 표준 라이브러리 타입만 등장한다. Qt 타입은 코어 전체에서 금지.

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

/// 성공/실패와 사람이 읽을 메시지를 함께 나르는 결과 타입.
struct Result
{
    bool ok = false;
    std::string message;

    explicit operator bool() const { return ok; }

    static Result success() { return Result{true, {}}; }
    static Result failure(std::string why) { return Result{false, std::move(why)}; }
};

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
    Result createNew(MapFormat format,
                     std::uint16_t tilesetId,
                     std::uint16_t width,
                     std::uint16_t height,
                     bool meleeTriggers);

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

    /// 맵에 놓인 유닛 전부.
    std::vector<RawUnit> units() const;

    /// 로케이션 전부. 비어 있는 슬롯은 건너뛴다.
    std::vector<RawLocation> locations() const;

    /// 지형 타일 값을 행 우선(row-major)으로 복사한다. 길이는 width*height.
    /// 에디터가 보는 값(TILE 섹션)을 쓴다 — 게임이 보는 MTXM 과 다를 수 있고,
    /// 편집기는 관례상 에디터 쪽을 표시한다.
    std::vector<std::uint16_t> terrainTiles() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace splash::io
