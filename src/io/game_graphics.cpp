#include "io/game_graphics.h"

#include "io/game_assets.h"
#include "mapping_core/casc_archive.h"
#include "mapping_core/archive_cluster.h"
#include "mapping_core/mpq_file.h"
#include "mapping_core/system_io.h"
#include "mapping_core/chk.h"
#include "mapping_core/sc.h"
#include "mapping_core/chk.h"
#include "mapping_core/render/map_animations.h"

#include <algorithm>
#include <array>
#include <map>
#include <cstring>
#include <exception>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace splash::io {
namespace {

/// 전역 난수를 잠깐 고정했다가 되돌린다.
///
/// MappingCore 는 방향이 정해지지 않은 유닛을 그릴 때 std::rand 를 쓴다.
/// 같은 유닛이 늘 같은 방향을 보게 하려면 씨앗을 고정해야 하는데, 전역
/// 난수를 그대로 두고 나오면 **지형 생성이 그 씨앗을 물려받는다** —
/// ISOM 솔버도 같은 std::rand 를 쓰기 때문이다. 그러면 맵 전체가 한
/// 패턴으로 깔려 바둑판처럼 보인다. 그리고 나서는 되돌려 놓는다.
class ScopedRandSeed
{
public:
    explicit ScopedRandSeed(unsigned seed) { std::srand(seed); }
    ~ScopedRandSeed()
    {
        // 원래 상태는 읽을 수 없으므로, 흐르는 값으로 다시 심어 둔다.
        static unsigned counter = 0x9E3779B9u;
        counter = counter * 1664525u + 1013904223u;
        std::srand(counter);
    }
    ScopedRandSeed(const ScopedRandSeed &) = delete;
    ScopedRandSeed & operator=(const ScopedRandSeed &) = delete;
};

} // namespace


struct GameGraphics::Impl
{
    std::shared_ptr<ArchiveCluster> cluster;

    /// 타일 대표색 캐시. [타일셋][타일ID] -> RGB.
    /// 미니맵은 맵을 열거나 지형을 고칠 때마다 다시 그리므로 캐시가 필요하다.
    mutable std::map<std::uint32_t, std::array<std::uint8_t, 3>> miniColors;

    /// 타일셋별 두들 목록. 만들 때마다 정렬까지 해야 해서, 두들을 놓는
    /// 동안 마우스를 움직일 때마다 다시 만들면 눈에 띄게 굼떠진다.
    mutable std::map<std::uint16_t, std::vector<DoodadInfo>> doodadLists;

    // Sc::Data 는 load() 대신 필요한 부분만 직접 채운다. 그쪽 load() 는
    // 파일 브라우저를 요구하고 우리가 이미 연 아카이브를 쓰지 않는다.
    std::unique_ptr<Sc::Data> scData;

    // iscript 실행기. 유닛 하나가 어떤 이미지들로 구성되는지(본체·그림자·부가)
    // 는 iscript 가 정하므로, 그것을 돌려야 그림자와 방향이 맞는다.
    std::unique_ptr<GameClock> clock;
    std::unique_ptr<AnimContext> anim;

    bool loaded = false;
    bool unitsLoaded = false;

    // 명령 카드 아이콘과 업그레이드·기술 표. 설정 창을 열 때 처음 읽는다.
    mutable std::unique_ptr<Sc::Sprite::Grp> icons;
    mutable bool iconsLoaded = false;

    mutable bool upgradesLoaded = false;
    mutable bool techsLoaded = false;
    mutable bool weaponsLoaded = false;

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

bool GameGraphics::load(const std::string & installPath, std::string * error,
                        const std::vector<std::string> & modArchives)
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

        // 모드 자료가 설치본을 덮어쓰도록 먼저 뒤진다. 못 여는 것은
        // 건너뛴다 — 하나가 없다고 게임 자료까지 못 읽을 이유는 없다.
        for (const std::string & archive : modArchives)
        {
            auto mod = std::make_shared<MpqFile>();
            if (mod->open(archive, /*readOnly*/ true, /*createIfNotFound*/ false))
                sources.push_back(mod);
        }

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

        // 두들 이름이 stat_txt.tbl 에 있으므로 함께 넘긴다. 없으면
        // 이름 없이 번호로만 보인다.
        std::shared_ptr<Sc::TblFile> statTxt = std::make_shared<Sc::TblFile>();
        if (!statTxt->load(*fresh->cluster, "rez\\stat_txt.tbl"))
            statTxt.reset();

        if (!fresh->scData->terrain.load(*fresh->cluster, statTxt))
        {
            // 일부 타일셋만 실패해도 false 가 나올 수 있다.
            // 하나라도 쓸 수 있으면 계속 진행하는 편이 낫다 — 실패 판정은
            // renderTile 에서 타일셋 단위로 드러난다.
        }

        // --- AI 스크립트 목록 ---
        // 트리거의 "Run AI Script" 인자에 이름을 붙이는 데 쓴다. 실패해도
        // 나머지는 그대로 쓸 수 있으므로 조용히 넘어간다.
        try
        {
            if (statTxt)
                fresh->scData->ai.load(*fresh->cluster, statTxt);
            else
                fresh->scData->ai.load(*fresh->cluster);
        }
        catch (const std::exception &)
        {
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

std::vector<GameGraphics::TerrainType> GameGraphics::terrainTypes(
    std::uint16_t tilesetId) const
{
    std::vector<TerrainType> out;
    if (!impl_->loaded)
        return out;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);
    for (std::size_t position = 0; position < tiles.terrainTypes.size(); ++position)
    {
        const auto & info = tiles.terrainTypes[position];

        // 브러시로 쓸 수 없는 항목(정렬 순서가 없는 것)은 뺀다.
        if (info.brushSortOrder < 0 || info.name.empty())
            continue;

        TerrainType entry;
        entry.brushIndex = position;
        entry.index = info.index;
        entry.name.assign(info.name.begin(), info.name.end());
        entry.sortOrder = info.brushSortOrder;

        // 대표 그림은 그 지형의 "한가운데" 타일이어야 한다. 아무 타일 그룹이나
        // 고르면 절벽 모서리 같은 전이 타일이 잡혀 실제 지형과 달라 보인다.
        //
        // ISOM 은 마름모의 네 변에 지형 값을 적고, 그 조합의 해시로 타일
        // 그룹을 찾는다. 네 변을 모두 같은 지형으로 채우면 그것이 한가운데다.
        {
            const Span<Sc::Isom::ShapeLinks> isomLinks(
                tiles.isomLinks.empty() ? nullptr : &tiles.isomLinks[0],
                tiles.isomLinks.size());

            const std::uint16_t stored = static_cast<std::uint16_t>(info.isomValue << 4);
            const Chk::IsomRect centre(stored, stored, stored, stored);
            const std::uint32_t hash = centre.getHash(isomLinks);

            const auto found = tiles.hashToTileGroup.find(hash);
            if (found != tiles.hashToTileGroup.end() && !found->second.empty())
            {
                const std::uint16_t group = found->second.front();
                if (group < tiles.tileGroups.size())
                {
                    const auto & tileGroup = tiles.tileGroups[group];
                    for (std::size_t sub = 0; sub < 16; ++sub)
                    {
                        const std::uint16_t megaTileIndex = tileGroup.megaTileIndex[sub];
                        if (megaTileIndex != 0 && megaTileIndex < tiles.tileGraphics.size())
                        {
                            entry.previewTileId = static_cast<std::uint16_t>(group * 16 + sub);
                            entry.hasPreview = true;
                            break;
                        }
                    }
                }
            }
        }

        out.push_back(std::move(entry));
    }

    std::sort(out.begin(), out.end(), [](const TerrainType & a, const TerrainType & b) {
        return a.sortOrder < b.sortOrder;
    });
    return out;
}

std::vector<std::uint8_t> GameGraphics::renderMinimap(
    const std::vector<std::uint16_t> & tiles, int width, int height,
    std::uint16_t tilesetId) const
{
    std::vector<std::uint8_t> out;
    if (!impl_->loaded || width <= 0 || height <= 0)
        return out;
    if (tiles.size() < static_cast<std::size_t>(width) * height)
        return out;

    out.assign(static_cast<std::size_t>(width) * height * 3, 0);

    const Sc::Terrain::Tiles & tileset = impl_->tiles(tilesetId);

    // 타일 하나의 대표색은 그 타일 그림의 평균이다. 타일 종류는 많지만
    // 한 맵이 쓰는 종류는 훨씬 적으므로 필요한 것만 계산해 캐시한다.
    const auto colorOf = [&](std::uint16_t tileId) -> std::array<std::uint8_t, 3> {
        const std::uint32_t key =
            (static_cast<std::uint32_t>(tilesetId % Sc::Terrain::NumTilesets) << 16) | tileId;

        auto found = impl_->miniColors.find(key);
        if (found != impl_->miniColors.end())
            return found->second;

        std::array<std::uint8_t, 3> color {0, 0, 0};

        const std::size_t groupIndex = static_cast<std::size_t>(tileId) / 16;
        const std::size_t subIndex = static_cast<std::size_t>(tileId) % 16;
        if (groupIndex < tileset.tileGroups.size())
        {
            const std::uint16_t megaTileIndex =
                tileset.tileGroups[groupIndex].megaTileIndex[subIndex];
            if (megaTileIndex < tileset.tileGraphics.size())
            {
                const auto & graphics = tileset.tileGraphics[megaTileIndex];
                std::uint32_t r = 0, g = 0, b = 0, count = 0;

                for (int my = 0; my < 4; ++my)
                {
                    for (int mx = 0; mx < 4; ++mx)
                    {
                        const std::uint32_t vr4 = graphics.miniTileGraphics[my][mx].vr4Index();
                        if (vr4 >= tileset.miniTilePixels.size())
                            continue;

                        // 미니타일마다 네 귀퉁이만 본다 — 평균을 내는 데
                        // 64픽셀을 다 훑을 필요가 없다.
                        const auto & pixels = tileset.miniTilePixels[vr4];
                        for (int p = 0; p < 4; ++p)
                        {
                            const int py = (p / 2) * 7;
                            const int px = (p % 2) * 7;
                            const Sc::SystemColor & c =
                                tileset.systemColorPalette[pixels.wpeIndex[py][px]];
                            r += c.red; g += c.green; b += c.blue;
                            ++count;
                        }
                    }
                }

                if (count > 0)
                {
                    color[0] = static_cast<std::uint8_t>(r / count);
                    color[1] = static_cast<std::uint8_t>(g / count);
                    color[2] = static_cast<std::uint8_t>(b / count);
                }
            }
        }

        impl_->miniColors.emplace(key, color);
        return color;
    };

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const auto color = colorOf(tiles[index]);
            out[index * 3 + 0] = color[0];
            out[index * 3 + 1] = color[1];
            out[index * 3 + 2] = color[2];
        }
    }

    return out;
}

std::string GameGraphics::unitSoundName(std::uint16_t unitType) const
{
    if (!hasUnitGraphics())
        return {};

    const Sc::Unit & units = impl_->scData->units;
    if (unitType >= units.numUnitTypes())
        return {};

    try
    {
        const auto & dat = units.getUnit(Sc::Unit::Type(unitType));
        std::uint16_t soundIndex = dat.readySound;
        if (soundIndex == 0)
            soundIndex = dat.whatSoundStart;
        if (soundIndex == 0)
            return {};

        auto sfxData = Sc::Data::GetAsset(*impl_->cluster, "arr\\sfxdata.dat", true);
        if (!sfxData || sfxData->size() < 4)
            return {};

        // units.dat 의 소리 번호는 1 부터 센다(0 은 "소리 없음"). sfxdata.dat
        // 의 배열은 0 부터라 한 칸 밀어 읽어야 같은 소리가 나온다 — 실제
        // 파일 이름으로 확인했다(마린 275 -> TMaRdy00.WAV).
        const std::size_t entries = std::min<std::size_t>(1144, sfxData->size() / 4);
        const std::size_t entryIndex = static_cast<std::size_t>(soundIndex) + 1;
        if (entryIndex >= entries)
            return {};

        std::uint32_t stringIndex = 0;
        std::memcpy(&stringIndex, sfxData->data() + entryIndex * 4, sizeof(stringIndex));
        if (stringIndex == 0)
            return {};

        Sc::TblFile sfxTbl;
        if (!sfxTbl.load(*impl_->cluster, "arr\\sfxdata.tbl"))
            return {};
        if (stringIndex > sfxTbl.numStrings())
            return {};

        return sfxTbl.getString(stringIndex - 1);
    }
    catch (const std::exception &)
    {
    }
    return {};
}

std::vector<std::uint8_t> GameGraphics::unitSound(std::uint16_t unitType) const
{
    std::vector<std::uint8_t> out;
    if (!hasUnitGraphics())
        return out;

    const Sc::Unit & units = impl_->scData->units;
    if (unitType >= units.numUnitTypes())
        return out;

    try
    {
        const auto & dat = units.getUnit(Sc::Unit::Type(unitType));

        // 게임에서 유닛이 막 나왔을 때 나는 소리를 쓴다 ("생산 완료").
        // 0~105 번 유닛만 이 소리를 갖는데, 건물·중립 물체처럼 없는 것은
        // "무엇" 소리로 대신한다.
        std::uint16_t soundIndex = dat.readySound;
        if (soundIndex == 0)
            soundIndex = dat.whatSoundStart;
        if (soundIndex == 0)
            return out;

        // sfxdata.dat 의 첫 필드가 sfxdata.tbl 의 몇 번째 문자열인지 알려 준다.
        // MappingCore 는 이 파일을 다루지 않으므로 직접 읽는다.
        static const std::string kSfxDataPath = "arr\\sfxdata.dat";
        auto sfxData = Sc::Data::GetAsset(*impl_->cluster, kSfxDataPath, true);
        if (!sfxData || sfxData->size() < 4)
            return out;

        // 앞부분이 u32 배열(파일 이름 인덱스)이다. 항목 수는 알려진 값을 쓰되
        // 파일 크기를 넘지 않도록 자른다.
        const std::size_t entries = std::min<std::size_t>(1144, sfxData->size() / 4);
        const std::size_t entryIndex = static_cast<std::size_t>(soundIndex) + 1;
        if (entryIndex >= entries)
            return out;

        std::uint32_t stringIndex = 0;
        std::memcpy(&stringIndex, sfxData->data() + entryIndex * 4, sizeof(stringIndex));
        if (stringIndex == 0)
            return out;

        Sc::TblFile sfxTbl;
        if (!sfxTbl.load(*impl_->cluster, "arr\\sfxdata.tbl"))
            return out;
        if (stringIndex > sfxTbl.numStrings())
            return out;

        // tbl 인덱스는 1부터 센다.
        const std::string & relative = sfxTbl.getString(stringIndex - 1);
        if (relative.empty())
            return out;

        const std::string path = makeArchiveFilePath("sound", relative);
        if (auto wav = Sc::Data::GetAsset(*impl_->cluster, path, true))
            out = std::move(*wav);
    }
    catch (const std::exception &)
    {
    }

    return out;
}

std::size_t GameGraphics::unitTypeCount() const
{
    if (impl_ == nullptr || impl_->scData == nullptr)
        return 0;
    return impl_->scData->units.numUnitTypes();
}

GameGraphics::UnitStats GameGraphics::unitStats(std::uint16_t unitType) const
{
    UnitStats out;
    if (impl_ == nullptr || impl_->scData == nullptr)
        return out;

    const Sc::Unit & units = impl_->scData->units;
    if (unitType >= units.numUnitTypes())
        return out;

    try
    {
        // 무기 자료는 따로 읽는다. 한 번만 읽고 들고 있는다.
        if (!impl_->weaponsLoaded)
        {
            impl_->weaponsLoaded = true;
            impl_->scData->weapons.load(*impl_->cluster);
        }

        const auto & d = units.getUnit(Sc::Unit::Type(unitType));
        out.hitPoints = d.hitPoints >> 8;
        out.shields = d.shieldEnable ? d.shieldAmount : std::uint16_t(0);
        out.armor = d.armor;
        out.armorUpgrade = d.armorUpgrade;
        out.sightRange = d.sightRange;
        out.targetAcquisitionRange = d.targetAcquisitionRange;
        out.unitSize = d.unitSize;
        out.flingy = d.graphics;
        out.groundWeapon = d.groundWeapon;
        out.airWeapon = d.airWeapon;
        out.maxGroundHits = d.maxGroundHits;
        out.maxAirHits = d.maxAirHits;
        out.flags = d.flags;
        const auto has = [&](std::uint32_t bit) { return (d.flags & bit) != 0; };
        out.hero = has(Sc::Unit::Flags::Hero);
        out.invincible = has(Sc::Unit::Flags::Invincible);
        out.autoAttackAndMove = has(Sc::Unit::Flags::AutoAttackAndMove);
        out.regeneratesHp = has(Sc::Unit::Flags::RegeneratesHP);
        out.spellcaster = has(Sc::Unit::Flags::Spellcaster);
        out.detector = has(Sc::Unit::Flags::Detector);
        out.cloakable = has(Sc::Unit::Flags::Cloakable);
        out.permanentCloak = has(Sc::Unit::Flags::PermanentCloak);
        out.flyer = has(Sc::Unit::Flags::Flyer);
        out.mechanical = has(Sc::Unit::Flags::Mechanical);
        out.organic = has(Sc::Unit::Flags::Organicunit);
        out.canAttack = has(Sc::Unit::Flags::CanAttack);
        out.starEditGroupFlags = d.starEditGroupFlags;
        out.starEditAvailability = d.starEditAvailabilityFlags;
        out.subunit1 = static_cast<std::uint16_t>(d.subunit1);
        out.subunit2 = static_cast<std::uint16_t>(d.subunit2);
        out.mineralCost = d.mineralCost;
        out.vespeneCost = d.vespeneCost;
        out.buildTime = d.buildTime;
        out.supplyRequired = d.supplyRequired;
        out.supplyProvided = d.supplyProvided;
        out.aiCompIdle = d.compAIIdle;
        out.aiHumanIdle = d.humanAIIdle;
        out.aiReturnToIdle = d.returntoIdle;
        out.aiAttackUnit = d.attackUnit;
        out.aiAttackMove = d.attackMove;

        // 이동 속도는 유닛이 아니라 flingy.dat 에 있다.
        try
        {
            const auto & fl = units.getFlingy(d.graphics);
            out.topSpeed = fl.topSpeed;
            out.moveControl = fl.moveControl;
        }
        catch (const std::exception &)
        {
        }

        const auto weapon = [&](std::uint8_t id) -> const Sc::Weapon::DatEntry * {
            if (id >= Sc::Weapon::Total)
                return nullptr;
            return &impl_->scData->weapons.get(Sc::Weapon::Type(id));
        };
        if (const auto * gw = weapon(d.groundWeapon))
        {
            out.groundRange = gw->maximumRange;
            out.groundDamage = gw->damageAmount;
            out.groundDamageBonus = gw->damageBonus;
            out.groundCooldown = gw->weaponCooldown;
            out.groundDamageUpgrade = gw->damageUpgrade;
            out.groundDamageType = gw->weaponType;
        }
        if (const auto * aw = weapon(d.airWeapon))
        {
            out.airRange = aw->maximumRange;
            out.airDamage = aw->damageAmount;
            out.airDamageUpgrade = aw->damageUpgrade;
            out.airDamageType = aw->weaponType;
        }
    }
    catch (const std::exception &)
    {
    }
    return out;
}

GameGraphics::UnitRanges GameGraphics::unitRanges(std::uint16_t unitType) const
{
    UnitRanges ranges;
    if (!hasUnitGraphics())
        return ranges;

    const Sc::Unit & units = impl_->scData->units;
    if (unitType >= units.numUnitTypes())
        return ranges;

    try
    {
        // 무기 자료는 따로 읽어야 한다. 한 번만 읽고 들고 있는다.
        if (!impl_->weaponsLoaded)
        {
            impl_->weaponsLoaded = true;
            impl_->scData->weapons.load(*impl_->cluster);
        }

        const auto & dat = units.getUnit(Sc::Unit::Type(unitType));

        // 시야·탐지 범위는 타일 단위로 적혀 있다.
        ranges.sight = dat.sightRange * kTilePixels;

        // 탐지기는 시야만큼 탐지한다.
        if ((dat.flags & Sc::Unit::Flags::Detector) != 0)
            ranges.detection = ranges.sight;

        const auto weaponRange = [&](std::uint8_t weaponId) -> int {
            if (weaponId >= Sc::Weapon::Total)
                return 0;
            const auto & weapon = impl_->scData->weapons.get(Sc::Weapon::Type(weaponId));
            return static_cast<int>(weapon.maximumRange);
        };

        ranges.groundWeapon = weaponRange(dat.groundWeapon);
        ranges.airWeapon = weaponRange(dat.airWeapon);
    }
    catch (const std::exception &)
    {
    }

    return ranges;
}

GameGraphics::UnitBounds GameGraphics::unitBounds(std::uint16_t unitType) const
{
    UnitBounds bounds;
    if (!hasUnitGraphics())
        return bounds;

    const Sc::Unit & units = impl_->scData->units;
    if (unitType >= units.numUnitTypes())
        return bounds;

    const auto & dat = units.getUnit(Sc::Unit::Type(unitType));
    bounds.left = dat.unitSizeLeft;
    bounds.up = dat.unitSizeUp;
    bounds.right = dat.unitSizeRight;
    bounds.down = dat.unitSizeDown;
    return bounds;
}

GameGraphics::PlacementBox GameGraphics::placementBox(std::uint16_t unitType) const
{
    PlacementBox box;
    if (!hasUnitGraphics())
        return box;

    const Sc::Unit & units = impl_->scData->units;
    if (unitType >= units.numUnitTypes())
        return box;

    const auto & dat = units.getUnit(Sc::Unit::Type(unitType));
    box.width = dat.starEditPlacementBoxWidth;
    box.height = dat.starEditPlacementBoxHeight;
    return box;
}

GameGraphics::UnitClass GameGraphics::unitClass(std::uint16_t unitType) const
{
    UnitClass result;
    if (!hasUnitGraphics())
        return result;

    const Sc::Unit & units = impl_->scData->units;
    if (unitType >= units.numUnitTypes())
        return result;

    const auto & dat = units.getUnit(Sc::Unit::Type(unitType));

    // 건물·비행 여부는 units.dat 의 특성 플래그가 알려 준다.
    result.building = (dat.flags & Sc::Unit::Flags::Building) != 0;
    result.flyer = (dat.flags & Sc::Unit::Flags::Flyer) != 0;
    result.addon = (dat.flags & Sc::Unit::Flags::Addon) != 0;

    // 종족은 StarEdit 그룹 플래그에 들어 있다. MappingCore 에 이름이 붙은
    // 열거형이 없어 비트를 직접 읽는다 — 값은 실제 데이터로 확인했다.
    result.groupFlags = dat.starEditGroupFlags;
    constexpr std::uint8_t kZerg = 0x01;
    constexpr std::uint8_t kTerran = 0x02;
    constexpr std::uint8_t kProtoss = 0x04;

    if (dat.starEditGroupFlags & kZerg)
        result.race = UnitClass::Race::Zerg;
    else if (dat.starEditGroupFlags & kTerran)
        result.race = UnitClass::Race::Terran;
    else if (dat.starEditGroupFlags & kProtoss)
        result.race = UnitClass::Race::Protoss;
    else
        result.race = UnitClass::Race::Neutral;

    return result;
}

bool GameGraphics::isCreepBuilding(std::uint16_t unitType) const
{
    if (!hasUnitGraphics())
        return false;

    const Sc::Unit & units = impl_->scData->units;
    if (unitType >= units.numUnitTypes())
        return false;

    const auto & dat = units.getUnit(Sc::Unit::Type(unitType));
    return (dat.flags & Sc::Unit::Flags::CreepBuilding) != 0;
}

namespace {

/// CV5 의 두들 영역은 타일 그룹과 크기가 같은 다른 구조다. 같은 자리를
/// 두들로 읽어야 크기와 이름을 알 수 있다.
const Sc::Terrain::DoodadCv5 & asDoodad(const Sc::Terrain::TileGroup & group)
{
    return reinterpret_cast<const Sc::Terrain::DoodadCv5 &>(group);
}

} // namespace

const std::vector<GameGraphics::DoodadInfo> &
GameGraphics::doodads(std::uint16_t tilesetId) const
{
    static const std::vector<DoodadInfo> kEmpty;
    if (!impl_->loaded)
        return kEmpty;

    if (const auto found = impl_->doodadLists.find(tilesetId);
        found != impl_->doodadLists.end())
    {
        return found->second;
    }

    std::vector<DoodadInfo> out;
    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);

    // doodadIdToTileGroup 이 두들 번호와 시작 그룹을 이어 준다.
    for (const auto & [doodadId, tileGroupIndex] : tiles.doodadIdToTileGroup)
    {
        if (tileGroupIndex >= tiles.tileGroups.size())
            continue;

        const auto & doodad = asDoodad(tiles.tileGroups[tileGroupIndex]);

        DoodadInfo info;
        info.id = static_cast<std::uint16_t>(doodadId);
        info.tileWidth = doodad.tileWidth;
        info.tileHeight = doodad.tileHeight;
        info.startTileGroup = tileGroupIndex;
        info.overlayIndex = doodad.overlayIndex;
        info.spriteOverlay = (doodad.flags & 0x1000) != 0;

        // 팔레트에는 두들의 첫 타일을 보여 준다.
        info.previewTileId = static_cast<std::uint16_t>(tileGroupIndex * 16);

        // 이름은 stat_txt.tbl 에서 온다. 묶음 목록에 이름이 들어 있다.
        for (const auto & group : tiles.doodadGroups)
        {
            if (std::find(group.doodadStartTileGroup.begin(), group.doodadStartTileGroup.end(),
                          tileGroupIndex) != group.doodadStartTileGroup.end())
            {
                info.name = group.name;
                break;
            }
        }

        if (info.name.empty())
            info.name = "Doodad " + std::to_string(info.id);

        if (info.tileWidth > 0 && info.tileHeight > 0)
            out.push_back(std::move(info));
    }

    std::sort(out.begin(), out.end(), [](const DoodadInfo & a, const DoodadInfo & b) {
        if (a.name != b.name)
            return a.name < b.name;
        return a.id < b.id;
    });

    return impl_->doodadLists.emplace(tilesetId, std::move(out)).first->second;
}

bool GameGraphics::doodadFits(std::uint16_t tilesetId, std::uint16_t doodadId,
                              const std::vector<std::uint16_t> & mapTiles,
                              int mapWidth, int mapHeight, int tileX, int tileY) const
{
    if (!impl_->loaded)
        return true;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);

    const auto found = tiles.doodadIdToTileGroup.find(doodadId);
    if (found == tiles.doodadIdToTileGroup.end())
        return true;

    const std::uint16_t startGroup = found->second;
    if (startGroup >= tiles.tileGroups.size())
        return true;

    const auto & doodad = asDoodad(tiles.tileGroups[startGroup]);
    const int width = doodad.tileWidth;
    const int height = doodad.tileHeight;
    if (width <= 0 || height <= 0)
        return true;

    // 배치 가능 표가 없으면 따질 것이 없다.
    if (doodad.ddDataIndex >= tiles.doodadPlacibility.size())
        return true;

    const auto & placibility = tiles.doodadPlacibility[doodad.ddDataIndex];

    const int left = tileX - width / 2;
    const int top = tileY - height / 2;

    for (int y = 0; y < height; ++y)
    {
        const std::size_t group = static_cast<std::size_t>(startGroup) + y;
        for (int x = 0; x < width; ++x)
        {
            const std::size_t slot = static_cast<std::size_t>(y) * width + x;
            if (slot >= 256)
                continue;

            // 두들에 속하지 않는 빈 칸은 어떤 지형이든 상관없다.
            if (group >= tiles.tileGroups.size() || x >= 16 ||
                tiles.tileGroups[group].megaTileIndex[x] == 0)
                continue;

            const std::uint16_t required = placibility.tileGroup[slot];
            if (required == 0)
                continue; // 아무 지형이나 좋다

            const int mapX = left + x;
            const int mapY = top + y;
            if (mapX < 0 || mapY < 0 || mapX >= mapWidth || mapY >= mapHeight)
                return false;

            const std::size_t index = static_cast<std::size_t>(mapY) * mapWidth + mapX;
            if (index >= mapTiles.size())
                return false;

            // 타일 값 위쪽이 그룹 번호다.
            if (static_cast<std::uint16_t>(mapTiles[index] / 16) != required)
                return false;
        }
    }

    return true;
}

std::vector<std::uint16_t> GameGraphics::doodadTiles(std::uint16_t tilesetId,
                                                     std::uint16_t doodadId) const
{
    std::vector<std::uint16_t> out;
    if (!impl_->loaded)
        return out;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);

    const auto found = tiles.doodadIdToTileGroup.find(doodadId);
    if (found == tiles.doodadIdToTileGroup.end())
        return out;

    const std::uint16_t startGroup = found->second;
    if (startGroup >= tiles.tileGroups.size())
        return out;

    const auto & doodad = asDoodad(tiles.tileGroups[startGroup]);
    const int width = doodad.tileWidth;
    const int height = doodad.tileHeight;
    if (width <= 0 || height <= 0)
        return out;

    // 두들은 줄 하나가 CV5 그룹 하나다. 그룹 안에서 칸 번호가 곧 x 이고,
    // 그 자리의 메가타일이 0 이면 그 칸은 두들에 속하지 않는다 — 바위
    // 그림의 네 귀퉁이처럼 뚫린 자리라 원래 지형을 그대로 둬야 한다.
    out.reserve(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y)
    {
        const std::size_t group = static_cast<std::size_t>(startGroup) + y;
        for (int x = 0; x < width; ++x)
        {
            if (group >= tiles.tileGroups.size() || x >= 16 ||
                tiles.tileGroups[group].megaTileIndex[x] == 0)
            {
                out.push_back(0);
                continue;
            }
            out.push_back(static_cast<std::uint16_t>(group * 16 + x));
        }
    }
    return out;
}

std::vector<std::uint16_t> GameGraphics::doodadMegaTiles(std::uint16_t tilesetId,
                                                         std::uint16_t doodadId) const
{
    std::vector<std::uint16_t> out;
    if (!impl_->loaded)
        return out;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);

    const auto found = tiles.doodadIdToTileGroup.find(doodadId);
    if (found == tiles.doodadIdToTileGroup.end())
        return out;

    const std::uint16_t startGroup = found->second;
    if (startGroup >= tiles.tileGroups.size())
        return out;

    const auto & doodad = asDoodad(tiles.tileGroups[startGroup]);
    const int width = doodad.tileWidth;
    const int height = doodad.tileHeight;
    if (width <= 0 || height <= 0)
        return out;

    out.reserve(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y)
    {
        const std::size_t group = static_cast<std::size_t>(startGroup) + y;
        for (int x = 0; x < width; ++x)
        {
            if (group >= tiles.tileGroups.size() || x >= 16)
                out.push_back(0);
            else
                out.push_back(tiles.tileGroups[group].megaTileIndex[x]);
        }
    }
    return out;
}

std::uint16_t GameGraphics::tileMegaTile(std::uint16_t tilesetId, std::uint16_t tileId) const
{
    if (!impl_->loaded)
        return 0;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);
    const std::size_t group = tileId / 16;
    if (group >= tiles.tileGroups.size())
        return 0;

    return tiles.tileGroups[group].megaTileIndex[tileId % 16];
}

GameGraphics::TileTerrain GameGraphics::tileTerrain(std::uint16_t tilesetId,
                                                   std::uint16_t tileId) const
{
    TileTerrain terrain;
    if (!impl_->loaded)
        return terrain;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);
    const std::size_t groupIndex = static_cast<std::size_t>(tileId) / 16;
    if (groupIndex >= tiles.tileGroups.size())
        return terrain;

    const std::uint16_t flags = tiles.tileGroups[groupIndex].flags;

    using Flags = Sc::Terrain::TileGroup::Flags;
    terrain.buildable = (flags & Flags::Unbuildable) == 0;
    terrain.creep = (flags & (Flags::Creep | Flags::TemporaryCreep)) != 0;

    if (flags & Flags::HighGround)
        terrain.elevation = 2;
    else if (flags & Flags::MidGround)
        terrain.elevation = 1;
    else
        terrain.elevation = 0;

    // 걷기 여부는 미니타일마다 따로 있다. 타일 하나는 4x4 = 16칸이다.
    const std::size_t megaTileIndex =
        tiles.tileGroups[groupIndex].megaTileIndex[static_cast<std::size_t>(tileId) % 16];
    if (megaTileIndex < tiles.tileFlags.size())
    {
        const auto & tileFlags = tiles.tileFlags[megaTileIndex];

        std::size_t walkableCount = 0;
        std::size_t blockingCount = 0;
        std::size_t rampCount = 0;
        for (std::size_t y = 0; y < 4; ++y)
        {
            for (std::size_t x = 0; x < 4; ++x)
            {
                if (tileFlags.miniTileFlags[y][x].isWalkable())
                {
                    ++walkableCount;
                    terrain.walkMask |= static_cast<std::uint16_t>(1u << (y * 4 + x));
                }
                if (tileFlags.miniTileFlags[y][x].blocksView())
                    ++blockingCount;
                if (tileFlags.miniTileFlags[y][x].isRamp())
                    ++rampCount;
            }
        }

        terrain.walkable = walkableCount > 0;
        terrain.fullyWalkable = walkableCount == 16;
        terrain.blocksView = blockingCount > 0;
        terrain.ramp = rampCount > 0;
    }

    return terrain;
}

std::vector<std::uint8_t> GameGraphics::computeCreepMask(
    const std::vector<RawUnit> & units,
    const std::vector<std::uint16_t> & tiles,
    int tileWidth,
    int tileHeight,
    std::uint16_t tilesetId) const
{
    std::vector<std::uint8_t> mask;
    if (!hasUnitGraphics() || tileWidth <= 0 || tileHeight <= 0)
        return mask;
    if (tiles.size() < static_cast<std::size_t>(tileWidth) * tileHeight)
        return mask;

    mask.assign(static_cast<std::size_t>(tileWidth) * tileHeight, 0);

    for (const RawUnit & unit : units)
    {
        const CreepRange range = creepRange(unit.type);
        if (range.radiusX <= 0.0 || range.radiusY <= 0.0)
            continue;

        // 건물이 선 자리의 높이를 기준으로 삼는다. 크립은 같은 높이로만 퍼진다.
        const int centerTileX = unit.x / kTilePixels;
        const int centerTileY = unit.y / kTilePixels;
        if (centerTileX < 0 || centerTileY < 0 ||
            centerTileX >= tileWidth || centerTileY >= tileHeight)
        {
            continue;
        }

        const std::size_t centerIndex =
            static_cast<std::size_t>(centerTileY) * tileWidth + centerTileX;
        const int baseElevation = tileTerrain(tilesetId, tiles[centerIndex]).elevation;

        const int minX = std::max(0, static_cast<int>((unit.x - range.radiusX) / kTilePixels));
        const int maxX = std::min(tileWidth - 1,
                                  static_cast<int>((unit.x + range.radiusX) / kTilePixels));
        const int minY = std::max(0, static_cast<int>((unit.y - range.radiusY) / kTilePixels));
        const int maxY = std::min(tileHeight - 1,
                                  static_cast<int>((unit.y + range.radiusY) / kTilePixels));

        for (int ty = minY; ty <= maxY; ++ty)
        {
            for (int tx = minX; tx <= maxX; ++tx)
            {
                // 타일 중심이 범위 안에 드는지로 판정한다.
                const double px = tx * kTilePixels + kTilePixels / 2.0;
                const double py = ty * kTilePixels + kTilePixels / 2.0;
                const double dx = (px - unit.x) / range.radiusX;
                const double dy = (py - unit.y) / range.radiusY;
                if (dx * dx + dy * dy > 1.0)
                    continue;

                const std::size_t index =
                    static_cast<std::size_t>(ty) * tileWidth + tx;
                const TileTerrain terrain = tileTerrain(tilesetId, tiles[index]);

                // 평지가 아니거나 높이가 다르면 크립이 넘어가지 않는다.
                if (!terrain.buildable || terrain.elevation != baseElevation)
                    continue;

                mask[index] = 1;
            }
        }
    }

    return mask;
}

GameGraphics::CreepRange GameGraphics::creepRange(std::uint16_t unitType) const
{
    CreepRange range;
    if (!isCreepBuilding(unitType))
        return range;

    const Sc::Unit & units = impl_->scData->units;
    const auto & dat = units.getUnit(Sc::Unit::Type(unitType));

    // 건물이 차지하는 크기에서 출발해 바깥으로 얼마쯤 더 퍼진다고 본다.
    // 게임은 건물마다 정해진 패턴으로 크립을 놓지만 그 표는 데이터 파일이
    // 아니라 게임 내부에 있다. 크기에 비례시키는 것이 가장 가까운 근사다.
    constexpr double kMarginX = 4.0 * kTilePixels;
    constexpr double kMarginY = 3.0 * kTilePixels;

    range.radiusX = dat.starEditPlacementBoxWidth / 2.0 + kMarginX;
    range.radiusY = dat.starEditPlacementBoxHeight / 2.0 + kMarginY;
    return range;
}

std::vector<std::uint16_t> GameGraphics::paletteTileIds(std::uint16_t tilesetId) const
{
    std::vector<std::uint16_t> out;
    if (!impl_->loaded)
        return out;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);

    // 타일 ID 는 16비트이므로 그룹은 4096개까지만 표현된다.
    const std::size_t groupLimit = std::min<std::size_t>(tiles.tileGroups.size(), 4096);

    std::vector<std::uint32_t> seenMegaTiles;
    seenMegaTiles.reserve(groupLimit);

    // 타일 0 은 "null" 타일이다. 맵 가장자리나 검은 영역을 만들 때 쓰므로
    // 팔레트 맨 앞에 넣는다. 아래 루프는 배정되지 않은 칸을 걸러내느라
    // megaTileIndex 0 을 모두 빼므로 여기서 따로 챙긴다.
    out.push_back(0);
    seenMegaTiles.push_back(0);

    for (std::size_t group = 0; group < groupLimit; ++group)
    {
        const auto & tileGroup = tiles.tileGroups[group];
        for (std::size_t sub = 0; sub < 16; ++sub)
        {
            const std::uint16_t megaTileIndex = tileGroup.megaTileIndex[sub];
            if (megaTileIndex == 0 || megaTileIndex >= tiles.tileGraphics.size())
                continue;

            // 같은 그림을 여러 칸이 가리키면 한 번만 싣는다.
            if (std::find(seenMegaTiles.begin(), seenMegaTiles.end(), megaTileIndex)
                != seenMegaTiles.end())
            {
                continue;
            }
            seenMegaTiles.push_back(megaTileIndex);
            out.push_back(static_cast<std::uint16_t>(group * 16 + sub));
        }
    }

    return out;
}

std::vector<std::uint16_t> GameGraphics::creepTileIds(std::uint16_t tilesetId) const
{
    std::vector<std::uint16_t> out;
    if (!impl_->loaded)
        return out;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);

    // 크립 바닥이 어느 그룹인지는 타일셋 데이터가 알려 준다 — Creep 플래그가
    // 선 타일 그룹이 그것이다. 인덱스를 고정하지 않는 이유는 타일셋마다
    // 배치가 다를 수 있기 때문이다.
    std::size_t kCreepGroup = 0;
    bool found = false;
    for (std::size_t i = 0; i < tiles.tileGroups.size(); ++i)
    {
        if (tiles.tileGroups[i].flags & Sc::Terrain::TileGroup::Flags::Creep)
        {
            kCreepGroup = i;
            found = true;
            break;
        }
    }
    if (!found)
        return out;

    const auto & group = tiles.tileGroups[kCreepGroup];

    // 같은 메가타일을 가리키는 칸이 여러 개다(빈 칸은 보통 같은 값으로 채워진다).
    // 서로 다른 메가타일을 가리키는 칸만 골라야 변형이 실제로 다르다.
    std::vector<std::uint16_t> seen;
    for (std::size_t sub = 0; sub < 16; ++sub)
    {
        const std::uint16_t megaTileIndex = group.megaTileIndex[sub];
        // 0 은 "배정 없음"이다. 그룹 안의 남는 칸이 0 으로 채워져 있으므로
        // 이것을 크립 타일로 쓰면 검은 칸이 섞인다.
        if (megaTileIndex == 0 || megaTileIndex >= tiles.tileGraphics.size())
            continue;
        if (std::find(seen.begin(), seen.end(), megaTileIndex) != seen.end())
            continue;

        seen.push_back(megaTileIndex);
        out.push_back(static_cast<std::uint16_t>(kCreepGroup * 16 + sub));
    }

    if (std::getenv("SPLASH_DEBUG_CREEP") != nullptr)
    {
        std::cerr << "    creep group=" << kCreepGroup << " tiles=" << out.size() << " :";
        for (std::size_t sub = 0; sub < 16; ++sub)
            std::cerr << " " << group.megaTileIndex[sub];
        std::cerr << "\n";
    }

    return out;
}

GameGraphics::MegaTileInfo GameGraphics::describeMegaTile(
    std::uint16_t tilesetId, std::uint32_t megaTileIndex) const
{
    MegaTileInfo info;
    if (!impl_->loaded)
        return info;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);
    if (megaTileIndex >= tiles.tileGraphics.size())
        return info;

    const auto & graphics = tiles.tileGraphics[megaTileIndex];
    for (int my = 0; my < 4; ++my)
    {
        for (int mx = 0; mx < 4; ++mx)
        {
            const std::uint32_t vr4 = graphics.miniTileGraphics[my][mx].vr4Index();
            info.vr4[my * 4 + mx] = vr4;
            if (vr4 == 0)
                ++info.emptyMiniTiles;
        }
    }
    return info;
}

std::size_t GameGraphics::megaTileCount(std::uint16_t tilesetId) const
{
    if (!impl_->loaded)
        return 0;
    return impl_->tiles(tilesetId).tileGraphics.size();
}

bool GameGraphics::renderMegaTile(std::uint16_t tilesetId, std::uint32_t megaTileIndex,
                                  std::uint8_t * rgbaOut) const
{
    if (rgbaOut == nullptr || !impl_->loaded)
        return false;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);
    if (megaTileIndex >= tiles.tileGraphics.size())
        return false;

    const auto & graphics = tiles.tileGraphics[megaTileIndex];
    for (int my = 0; my < 4; ++my)
    {
        for (int mx = 0; mx < 4; ++mx)
        {
            const auto & mini = graphics.miniTileGraphics[my][mx];
            const std::uint32_t vr4Index = mini.vr4Index();
            const bool flipped = mini.isFlipped();
            if (vr4Index >= tiles.miniTilePixels.size())
                continue;

            const auto & pixels = tiles.miniTilePixels[vr4Index];
            for (int py = 0; py < 8; ++py)
            {
                for (int px = 0; px < 8; ++px)
                {
                    const int srcX = flipped ? (7 - px) : px;
                    const std::uint8_t paletteIndex = pixels.wpeIndex[py][srcX];
                    const Sc::SystemColor & color = tiles.systemColorPalette[paletteIndex];
                    const std::size_t at =
                        ((static_cast<std::size_t>(my * 8 + py)) * kTilePixels + mx * 8 + px) * 4;
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

const void * GameGraphics::internalScData() const
{
    return impl_->scData.get();
}

void * GameGraphics::internalScData()
{
    return impl_->scData.get();
}

std::vector<std::string> GameGraphics::imageFileNames() const
{
    std::vector<std::string> out;
    if (!hasUnitGraphics())
        return out;

    // Sc::Sprite 가 들고 있는 images.tbl 을 직접 읽을 수는 없으므로
    // 아카이브에서 다시 읽는다. 진단 목적이라 비용은 문제되지 않는다.
    try
    {
        Sc::TblFile tbl;
        if (!tbl.load(*impl_->cluster, "arr\\images.tbl"))
            return out;

        out.reserve(tbl.numStrings());
        for (std::size_t i = 0; i < tbl.numStrings(); ++i)
            out.push_back(tbl.getString(i));
    }
    catch (const std::exception &)
    {
    }

    return out;
}

GameGraphics::TilesetInfo GameGraphics::describeTileset(std::uint16_t tilesetId) const
{
    TilesetInfo info;
    if (!impl_->loaded)
        return info;

    const Sc::Terrain::Tiles & tiles = impl_->tiles(tilesetId);
    info.tileGroupCount = tiles.tileGroups.size();
    info.megaTileCount = tiles.tileGraphics.size();

    for (std::size_t i = 0; i < tiles.tileGroups.size(); ++i)
    {
        const std::uint16_t flags = tiles.tileGroups[i].flags;
        if (flags & Sc::Terrain::TileGroup::Flags::Creep)
            info.creepGroups.push_back(static_cast<std::uint16_t>(i));
        if (flags & Sc::Terrain::TileGroup::Flags::TemporaryCreep)
            info.tempCreepGroups.push_back(static_cast<std::uint16_t>(i));
        if (flags & Sc::Terrain::TileGroup::Flags::RecedingCreep)
            info.recedingGroups.push_back(static_cast<std::uint16_t>(i));
    }

    return info;
}

bool GameGraphics::hasUnitGraphics() const
{
    return impl_->loaded && impl_->unitsLoaded;
}

namespace {

/// 초기화된 액터가 가리키는 이미지 레이어들을 하나의 RGBA 이미지로 합성한다.
/// 유닛과 스프라이트가 같은 경로를 쓴다 — 차이는 액터를 어떻게 초기화하느냐뿐이다.
UnitImage composeActor(Sc::Data & sc,
                       AnimContext & anim,
                       MapActor & actor,
                       const Sc::Terrain::Tiles & tiles,
                       std::uint8_t colorIndex)
{
    UnitImage out;
    // --- 팔레트 두 벌 ---
    // 일반 이미지는 지형 팔레트를 쓰되 8-15 구간을 플레이어 색으로 바꾼다.
    // 그림자는 dark.pcx 팔레트로 그린다(Chkdraft 도 shadowPalette 로 같은 것을 쓴다).
    std::array<Sc::SystemColor, Sc::NumColors> palette = tiles.systemColorPalette;
    // 색 번호는 플레이어 번호가 아니라 COLR 이 정한다. tunit.pcx 는 색마다
    // 여덟 단계 램프를 담고 있고, 그 여덟 칸이 팔레트 8-15 를 덮는다.
    //
    // 램프에 든 것은 색이 아니라 팔레트 자리 번호라, 실제 색은 타일셋
    // 팔레트에서 꺼내야 한다. 그래서 같은 색 번호라도 지형에 따라 다르게
    // 보인다 — 얼음 지형의 흰색이 초록빛인 것이 그 예다.
    const auto & rampIndex = sc.tunit.paletteIndex;
    const std::size_t rampBase = static_cast<std::size_t>(colorIndex % 16) * 8;
    for (std::size_t i = 0; i < 8; ++i)
    {
        if (rampBase + i >= rampIndex.size())
            continue;

        const std::size_t slot = rampIndex[rampBase + i];
        if (slot < tiles.systemColorPalette.size())
            palette[8 + i] = tiles.systemColorPalette[slot];
    }
    // 그림자는 색을 칠하는 것이 아니라 배경을 어둡게 하는 효과다.
    // dark.pcx 는 "배경색 -> 어두운 색" 매핑표라 배경을 알아야 정확한데,
    // 스프라이트를 따로 그리는 우리는 배경을 모른다. 반투명 검정으로 근사한다.
    constexpr std::uint8_t kShadowAlpha = 110;



    struct Layer
    {
        const Sc::Sprite::GrpFile * grp = nullptr;
        std::size_t frame = 0;
        int left = 0;   ///< 유닛 중심 기준
        int top = 0;
        bool flipped = false;
        bool shadow = false;

        // 은폐·환영은 게임에서 왜곡·색 입힘으로 그린다. 그 효과를 그대로
        // 흉내 낼 수는 없어, 눈에 띄게 다르되 모습은 남도록 옮긴다.
        bool cloaked = false;
        bool hallucinated = false;
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
        layer.cloaked = (image.drawFunction == MapImage::DrawFunction::Cloaked ||
                         image.drawFunction == MapImage::DrawFunction::Cloak ||
                         image.drawFunction == MapImage::DrawFunction::Decloak);
        layer.hallucinated = (image.drawFunction == MapImage::DrawFunction::Hallucination);

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

                            if (layer.hallucinated)
                            {
                                // 환영은 게임에서 푸르게 물든다. 밝기는 두고
                                // 색만 파랑 쪽으로 끌어당긴다.
                                const int grey = (color.red + color.green + color.blue) / 3;
                                out.rgba[at + 0] = static_cast<std::uint8_t>(grey / 3);
                                out.rgba[at + 1] = static_cast<std::uint8_t>(grey / 2);
                                out.rgba[at + 2] = static_cast<std::uint8_t>(
                                    std::min(255, grey + 80));
                                out.rgba[at + 3] = 220;
                            }
                            else if (layer.cloaked)
                            {
                                // 은폐는 반투명하게 — 게임의 왜곡 효과를
                                // 그대로 낼 수는 없지만 숨은 상태는 드러난다.
                                out.rgba[at + 0] = color.red;
                                out.rgba[at + 1] = color.green;
                                out.rgba[at + 2] = color.blue;
                                out.rgba[at + 3] = 110;
                            }
                            else
                            {
                                out.rgba[at + 0] = color.red;
                                out.rgba[at + 1] = color.green;
                                out.rgba[at + 2] = color.blue;
                                out.rgba[at + 3] = 255;
                            }
                        }
                    }
                }

                x += length;
                lineOffset += line.sizeInBytes();
            }
        }
    }

    return out;
}

} // namespace

std::vector<std::pair<std::uint8_t, std::size_t>>
GameGraphics::iconPaletteHistogram(std::uint16_t iconIndex) const
{
    std::map<std::uint8_t, std::size_t> counts;
    const UnitImage image = renderIcon(iconIndex, 4);
    (void)image; // 그리는 김에 아이콘 묶음이 준비된다

    if (!impl_->icons)
        return {};

    try
    {
        const Sc::Sprite::GrpFile & grp = impl_->icons->get();
        if (iconIndex >= grp.numFrames)
            return {};

        const Sc::Sprite::GrpFrameHeader & header = grp.frameHeaders[iconIndex];
        const auto * frameStart = reinterpret_cast<const std::uint8_t *>(&grp) + header.frameOffset;
        const auto * rowOffsets = reinterpret_cast<const std::uint16_t *>(frameStart);

        for (int row = 0; row < header.frameHeight; ++row)
        {
            const std::uint8_t * lineBytes = frameStart + rowOffsets[row];
            int x = 0;
            std::size_t lineOffset = 0;
            while (x < header.frameWidth)
            {
                const Sc::Sprite::PixelLine & line =
                    reinterpret_cast<const Sc::Sprite::PixelLine &>(lineBytes[lineOffset]);

                int length = static_cast<int>(line.lineLength());
                if (x + length > header.frameWidth)
                    length = header.frameWidth - x;
                if (length <= 0)
                    break;

                if (line.isSpeckled() || line.isSolidLine())
                {
                    for (int i = 0; i < length; ++i)
                    {
                        const std::uint8_t index = line.isSpeckled()
                            ? line.paletteIndex[i] : line.paletteIndex[0];
                        ++counts[index];
                    }
                }

                x += length;
                lineOffset += line.sizeInBytes();
            }
        }
    }
    catch (const std::exception &)
    {
    }

    std::vector<std::pair<std::uint8_t, std::size_t>> out(counts.begin(), counts.end());
    std::sort(out.begin(), out.end(),
              [](const auto & a, const auto & b) { return a.second > b.second; });
    return out;
}

std::size_t GameGraphics::assetSize(const std::string & archivePath) const
{
    if (!isLoaded())
        return 0;

    try
    {
        if (auto data = Sc::Data::GetAsset(*impl_->cluster, archivePath, true))
            return data->size();
    }
    catch (const std::exception &)
    {
    }
    return 0;
}

UnitImage GameGraphics::renderIcon(std::uint16_t iconIndex, std::uint16_t tilesetId) const
{
    UnitImage out;
    if (!isLoaded())
        return out;

    try
    {
        // 아이콘 묶음은 한 번만 읽어 둔다 — 창을 열 때마다 수백 번 부른다.
        if (!impl_->iconsLoaded)
        {
            impl_->iconsLoaded = true;
            // 아이콘 묶음의 경로는 설치본마다 다르다. 리마스터 CASC 는
            // 옛 MPQ 와 다른 자리에 두는 경우가 있어 알려진 자리를 차례로
            // 두드려 본다.
            // 구분 기호는 아카이브 종류에 따라 다르므로 MappingCore 의
            // 조립 함수를 쓴다.
            const std::string kIconPaths[] {
                makeArchiveFilePath("unit\\cmdicons", "cmdicons.grp"),
                makeArchiveFilePath("unit\\cmdbtns", "cmdicons.grp"),
                makeArchiveFilePath("game", "cmdicons.grp"),
                makeArchiveFilePath("unit\\cmdbtns", "icons.grp"),
                "unit\\cmdbtns\\cmdicons.grp",
                "unit/cmdbtns/cmdicons.grp",
                "SD/unit/cmdbtns/cmdicons.grp",
                "sd/unit/cmdbtns/cmdicons.grp",
                "unit\\cmdbtns\\cmdicons.pcx",
            };

            // Grp::load 는 파일이 없어도 참을 돌려주므로(빈 데이터를 그대로
            // 둔다) 먼저 자산이 있는지 직접 확인한다.
            for (const std::string & path : kIconPaths)
            {
                if (!Sc::Data::GetAsset(*impl_->cluster, path, true))
                    continue;

                auto candidate = std::make_unique<Sc::Sprite::Grp>();
                if (candidate->load(*impl_->cluster, path))
                {
                    impl_->icons = std::move(candidate);
                    break;
                }
            }
        }
        if (!impl_->icons)
            return out;

        const Sc::Sprite::GrpFile & grp = impl_->icons->get();
        if (iconIndex >= grp.numFrames)
            return out;

        // 아이콘 색은 콘솔 팔레트에서 온다. 지형 팔레트로 그리면 보라빛이
        // 도는 엉뚱한 색이 되므로, 콘솔 팔레트를 먼저 찾아보고 없을 때만
        // 지형 팔레트로 물러선다.
        // 명령 카드 아이콘은 팔레트 앞쪽 16칸만 쓴다 — 그림 자료를 세어
        // 확인했다(아이콘 0 은 인덱스 1~15 만 사용). 게임 콘솔 팔레트의
        // 그 구간은 검정에서 밝은 회색으로 가는 램프인데, 지형·유닛
        // 팔레트의 같은 구간은 플레이어 색이라 그대로 쓰면 자홍빛이 된다.
        //
        // 콘솔 팔레트 파일은 리마스터 설치본에서 찾지 못했으므로 램프를
        // 직접 만들어 쓴다. 게임처럼 아주 옅은 푸른빛을 준다.
        std::array<Sc::SystemColor, 16> ramp {};
        for (std::size_t i = 0; i < ramp.size(); ++i)
        {
            const double t = double(i) / double(ramp.size() - 1);
            ramp[i] = Sc::SystemColor(
                static_cast<std::uint8_t>(t * 224.0),
                static_cast<std::uint8_t>(t * 228.0),
                static_cast<std::uint8_t>(t * 244.0));
        }

        (void)tilesetId;

        const Sc::Sprite::GrpFrameHeader & header = grp.frameHeaders[iconIndex];
        out.width = header.frameWidth;
        out.height = header.frameHeight;
        if (out.width <= 0 || out.height <= 0)
        {
            out.width = out.height = 0;
            return out;
        }
        out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
        out.anchorX = out.width / 2;
        out.anchorY = out.height / 2;

        const auto * frameStart = reinterpret_cast<const std::uint8_t *>(&grp) + header.frameOffset;
        const auto * rowOffsets = reinterpret_cast<const std::uint16_t *>(frameStart);

        for (int row = 0; row < out.height; ++row)
        {
            const std::uint8_t * lineBytes = frameStart + rowOffsets[row];

            int x = 0;
            std::size_t lineOffset = 0;
            while (x < out.width)
            {
                const Sc::Sprite::PixelLine & line =
                    reinterpret_cast<const Sc::Sprite::PixelLine &>(lineBytes[lineOffset]);

                int length = static_cast<int>(line.lineLength());
                if (x + length > out.width)
                    length = out.width - x;
                if (length <= 0)
                    break;

                if (line.isSpeckled() || line.isSolidLine())
                {
                    for (int i = 0; i < length; ++i)
                    {
                        const std::uint8_t index = line.isSpeckled()
                            ? line.paletteIndex[i]
                            : line.paletteIndex[0];

                        const std::size_t at =
                            (static_cast<std::size_t>(row) * out.width + (x + i)) * 4;

                        // 아이콘이 16칸 밖을 가리키면 램프 끝으로 자른다.
                        const Sc::SystemColor & color = ramp[std::min<std::size_t>(index, 15)];
                        out.rgba[at + 0] = color.red;
                        out.rgba[at + 1] = color.green;
                        out.rgba[at + 2] = color.blue;
                        out.rgba[at + 3] = 255;
                    }
                }

                x += length;
                lineOffset += line.sizeInBytes();
            }
        }
    }
    catch (const std::exception &)
    {
        out = UnitImage {};
    }

    return out;
}

std::uint16_t GameGraphics::upgradeIcon(std::uint16_t upgradeType) const
{
    if (!isLoaded())
        return 0;

    try
    {
        if (!impl_->upgradesLoaded)
        {
            impl_->upgradesLoaded = true;
            if (!impl_->scData->upgrades.load(*impl_->cluster))
                return 0;
        }
        return impl_->scData->upgrades.getUpgrade(Sc::Upgrade::Type(upgradeType)).icon;
    }
    catch (const std::exception &)
    {
    }
    return 0;
}

std::uint16_t GameGraphics::techIcon(std::uint16_t techType) const
{
    if (!isLoaded())
        return 0;

    try
    {
        if (!impl_->techsLoaded)
        {
            impl_->techsLoaded = true;
            if (!impl_->scData->techs.load(*impl_->cluster))
                return 0;
        }
        return impl_->scData->techs.getTech(Sc::Tech::Type(techType)).icon;
    }
    catch (const std::exception &)
    {
    }
    return 0;
}

UnitImage GameGraphics::renderUnit(std::uint16_t unitType,
                                   std::uint8_t owner,
                                   std::uint16_t tilesetId,
                                   std::uint32_t resourceAmount,
                                   std::uint16_t stateFlags,
                                   std::uint16_t relationFlags,
                                   std::uint8_t colorIndex) const
{
    if (!hasUnitGraphics())
        return UnitImage{};

    try
    {
        Chk::Unit chkUnit {};
        chkUnit.type = Sc::Unit::Type(unitType);
        chkUnit.owner = owner;
        chkUnit.xc = 0;
        chkUnit.yc = 0;
        // 자원 유닛은 남은 양에 따라 그래픽이 달라진다(미네랄 3단계 등).
        chkUnit.resourceAmount = resourceAmount;

        // 상태에 따라 iscript 가 다른 자세·그리기 방식을 고른다 — 은폐와
        // 환영은 반투명하게, 버로우는 묻힌 모습, 떠 있는 건물은 뜬 모습,
        // 애드온이 붙은 건물은 붙은 모습이 된다.
        chkUnit.stateFlags = stateFlags;
        chkUnit.relationFlags = relationFlags;

        // MappingCore 는 방향이 정해지지 않은 유닛(units.dat 의 unitDirection
        // 이 32)의 방향을 std::rand 로 뽑는다. 그대로 두면 다시 그릴 때마다
        // 방향이 달라져서, 유닛을 하나 놓을 때마다 화면에 있는 유닛이 전부
        // 돌아간다. 씨앗을 유닛 종류·소유자로 고정해 같은 유닛은 언제나
        // 같은 방향으로 그린다.
        const ScopedRandSeed facingSeed(
            static_cast<unsigned>(unitType) * 2654435761u +
            static_cast<unsigned>(owner));

        MapActor actor {};
        impl_->anim->initializeUnitActor(actor, /*isClipboard*/ false, /*unitIndex*/ 0,
                                         chkUnit, 0, 0);

        // 버로우·이륙은 "움직이는 동작"이라 첫 프레임은 아직 서 있는
        // 모습이다. iscript 를 몇 틱 돌려 자세가 자리를 잡게 한다. 건물이
        // 뜰 때 그림자가 아래로 밀리는 것도 이 틱 동안 일어난다.
        const bool settles =
            (stateFlags & (0x02 /*버로우*/ | 0x04 /*떠 있음*/)) != 0 ||
            (relationFlags & 0x0400 /*애드온 붙음*/) != 0;

        if (settles)
        {
            // 한 동작이 끝나기에 넉넉한 만큼 돌린다. 동작이 끝나면 그 자리에
            // 머무르므로 더 돌려도 모습은 그대로다.
            std::uint64_t tick = impl_->clock->currentTick();
            for (int step = 0; step < 150; ++step)
                actor.animate(++tick, /*isUnit*/ true, *impl_->anim);
        }

        return composeActor(*impl_->scData, *impl_->anim, actor, impl_->tiles(tilesetId),
                            tilesetPlayerColor(tilesetId,
                                               colorIndex == 0xFF ? owner : colorIndex));
    }
    catch (const std::exception &)
    {
        return UnitImage{};
    }
}

UnitImage GameGraphics::renderSprite(std::uint16_t spriteType,
                                     std::uint8_t owner,
                                     std::uint16_t tilesetId,
                                     bool drawnAsSprite,
                                     std::uint8_t colorIndex) const
{
    if (!hasUnitGraphics())
        return UnitImage{};

    try
    {
        Chk::Sprite chkSprite {};
        chkSprite.type = Sc::Sprite::Type(spriteType);
        chkSprite.owner = owner;
        chkSprite.xc = 0;
        chkSprite.yc = 0;
        // 스프라이트로 그릴지 유닛 그래픽으로 그릴지는 플래그가 정한다.
        chkSprite.flags = drawnAsSprite
            ? Chk::Sprite::toPureSpriteFlags(0)
            : Chk::Sprite::toSpriteUnitFlags(0);

        // 유닛과 같은 이유로 씨앗을 고정한다 (아래 renderUnit 의 설명 참고).
        const ScopedRandSeed facingSeed(
            static_cast<unsigned>(spriteType) * 2654435761u +
            static_cast<unsigned>(owner));

        MapActor actor {};
        impl_->anim->initializeSpriteActor(actor, /*isClipboard*/ false, /*spriteIndex*/ 0,
                                           chkSprite, 0, 0);

        return composeActor(*impl_->scData, *impl_->anim, actor, impl_->tiles(tilesetId),
                            tilesetPlayerColor(tilesetId,
                                               colorIndex == 0xFF ? owner : colorIndex));
    }
    catch (const std::exception &)
    {
        return UnitImage{};
    }
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
