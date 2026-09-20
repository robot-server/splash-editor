#pragma once

// 맵 지형을 보여 주는 캔버스. 읽기 전용(M2).
//
// 보이는 영역의 타일만 그린다. 맵 전체를 한 장 이미지로 만들지 않는 이유:
// 256x256 맵은 8192x8192 픽셀이라 RGBA 로 268MB 다.
//
// 타일 그림은 타일 ID 별로 캐시한다. 맵 하나가 쓰는 고유 타일은 보통
// 수백~수천 개라, 65536 개 전부를 준비할 일은 없다.

#include <QAbstractScrollArea>
#include <QHash>
#include <QPoint>
#include <QRectF>
#include <QVector>
#include <QPixmap>
#include <QRegion>

#include <cstdint>
#include <utility>
#include <vector>

namespace splash::chk { class MapDocument; }
namespace splash::io  { class GameGraphics; }

namespace splash::ui {

class MapView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    /// 지금 마우스가 무슨 일을 하는지.
    enum class Tool
    {
        Select,      ///< 유닛·로케이션 고르고 옮기기
        Terrain,     ///< 지형 칠하기
        PlaceUnit,   ///< 유닛 놓기
        PlaceSprite, ///< 맵 장식 스프라이트 놓기
        PlaceDoodad,   ///< 두들(지형 장식) 놓기
        SelectTerrain, ///< 지형을 네모로 고르기 (복사·붙여넣기)
        Location,      ///< 로케이션 그리기·크기 조절
        Fog          ///< 시야 가리개 칠하기
    };

    explicit MapView(QWidget * parent = nullptr);
    ~MapView() override;

    /// 표시할 문서. nullptr 이면 빈 캔버스가 된다.
    /// 소유하지 않는다 — 호출부가 수명을 관리한다.
    void setDocument(const chk::MapDocument * document);

    /// 타일셋 그래픽. nullptr 이거나 로드되지 않았으면 안내 문구를 보여 준다.
    void setTileset(const io::GameGraphics * tileset);

    /// 문서나 타일셋의 내용이 바뀌었을 때 호출한다. 캐시를 비우고 다시 그린다.
    void refresh();

    /// 지금 보고 있는 맵 영역(맵 픽셀 좌표).
    QRectF visibleMapRect() const;

    /// 그 지점이 화면 가운데 오도록 옮긴다(맵 픽셀 좌표).
    void centerOnMap(const QPointF & mapPos);

    /// 그 로케이션이 보이도록 화면을 옮기고 고른 상태로 만든다.
    void focusLocation(std::size_t index);

    /// 선택된 유닛 번호. 없으면 -1.
    /// 마지막으로 고른 유닛. 여럿을 골랐으면 그 가운데 하나다.
    int selectedUnit() const { return selectedUnit_; }

    /// 고른 유닛 전부 (마지막 것을 포함한다).
    const std::vector<int> & selectedUnits() const { return selectedUnits_; }

    /// 선택된 로케이션 번호(표시 목록 기준). 없으면 -1.
    int selectedLocation() const { return selectedLocation_; }

    void clearSelection();

    /// 선택된 유닛을 지운다. 지웠으면 true.
    bool deleteSelectedUnit();

    /// 선택된 유닛을 복사한다. 복사했으면 true.
    bool copySelection();

    /// 복사해 둔 유닛을 그 자리에 붙인다. 붙였으면 true.
    bool pasteAt(const QPointF & screenPos);

    /// 붙일 것이 있는지.
    bool hasClipboard() const { return clipboardValid_; }

    /// 화면 가운데에 붙인다 (메뉴에서 부를 때).
    bool pasteAtCentre();

signals:
    /// 문서가 편집되었다. 창이 제목·상태를 갱신하도록 알린다.
    void documentEdited();

    /// 보고 있는 영역이 바뀌었다(스크롤·줌·크기 변경).
    void viewportMoved();

    /// 선택이 바뀌었다 (없으면 -1).
    void selectionChanged(int unitIndex);

    /// 유닛을 두 번 눌렀다 — 속성 창을 열라는 뜻.
    void unitActivated(int unitIndex);

    /// 숫자 키로 소유자를 바꿔 달라는 뜻 (0 부터).
    void ownerRequested(std::uint8_t owner);

    /// 무언가를 오른쪽 단추로 눌렀다 — 맥락 메뉴를 띄우라는 뜻.
    ///
    /// unitIndex·locationIndex 는 눌린 것이 없으면 -1 이다.
    void contextMenuRequested(const QPoint & globalPos, int unitIndex, int locationIndex);

    /// 도구가 바뀌었다 (우클릭으로 배치를 그만두는 경우 등).
    void toolChanged(Tool tool);

    /// 놓기를 거절했다 — 왜인지 알린다.
    void placementRejected(const QString & reason);

    /// 유닛을 새로 놓았다 (소리를 내라는 뜻).
    void unitPlaced(std::uint16_t unitType);

    /// 지형 브러시가 집은 타일이 바뀌었다.
    void brushTileChanged(std::uint16_t tileId);

public:

    double zoom() const { return zoom_; }
    void setZoom(double factor);

    Tool tool() const { return tool_; }
    void setTool(Tool tool);

    /// 지형 브러시가 칠할 타일. 스포이드로 바꾼다.
    std::uint16_t brushTile() const { return brushTile_; }
    void setBrushTile(std::uint16_t tileId);

    /// 지형 도구가 어떤 방식으로 놓을지.
    enum class TerrainMode
    {
        Isometric,   ///< 마름모 격자. 절벽·해안이 자동으로 이어진다
        Rectangular, ///< 브러시 크기만큼 사각으로 칠한다
        Subtile      ///< 한 칸씩 정밀하게 칠한다
    };
    TerrainMode terrainMode() const { return terrainMode_; }
    void setTerrainMode(TerrainMode mode);

    /// 지형을 칠할 때 맞은편에도 함께 칠할지.
    ///
    /// 맵 한가운데를 기준으로 좌우·위아래를 뒤집은 자리에 같은 것을
    /// 놓는다. 대칭 맵을 만들 때 한쪽만 그리면 된다.
    enum class Symmetry
    {
        None,       ///< 대칭 없음
        Horizontal, ///< 좌우 (세로축 기준)
        Vertical,   ///< 위아래 (가로축 기준)
        Both        ///< 네 곳
    };
    Symmetry terrainSymmetry() const { return symmetry_; }
    void setTerrainSymmetry(Symmetry symmetry);

    /// ISOM 모드가 놓을 지형 종류(GameGraphics::terrainTypes 의 brushIndex).
    void setIsomTerrainType(std::size_t brushIndex);

    /// 지형을 네모로 골라 복사·붙여넣기 하는 도구.
    ///
    /// SCMDraft 처럼 고른 지형이 곧 브러시가 된다 — 붙여넣기를 켜면 커서를
    /// 따라 미리 보이고, 누르면 그 자리에 찍힌다.
    bool hasTerrainClipboard() const { return !terrainClipboard_.empty(); }

    /// 고른 지형을 클립보드에 담는다. 담았으면 참.
    bool copyTerrainSelection();

    /// 고른 로케이션을 담는다. 담았으면 참.
    bool copySelectedLocation();

    /// 담아 둔 로케이션을 커서 자리에 같은 크기로 만든다.
    bool pasteLocationAt(const QPointF & screenPos);

    bool hasLocationClipboard() const { return locationClipboard_.valid; }

    /// 화면 한가운데에 붙인다.
    bool pasteLocationAtCentre();

    /// 고른 지형을 담고 그 자리를 비운다 (잘라내기).
    ///
    /// 비운 자리는 타일 0 이 된다 — 게임에서 검게 보이는 빈 타일이다.
    bool cutTerrainSelection();

    /// 담아 둔 지형 덩어리. 브러시 팔레트가 주고받는다.
    struct TerrainBrush
    {
        QString name;
        int width = 0;
        int height = 0;
        std::vector<std::uint16_t> tiles;

        /// 칸마다 "이 칸이 브러시에 드는지". 떨어진 네모를 여러 개 골랐을
        /// 때 그 사이의 빈 칸을 찍지 않으려는 것이다. 비어 있으면 모두 든다.
        std::vector<bool> mask;

        std::uint16_t tilesetId = 0;
    };

    /// 지금 담아 둔 지형을 브러시로 꺼낸다. 비어 있으면 이름이 빈 값.
    TerrainBrush terrainClipboardBrush() const;

    /// 브러시를 클립보드에 올려 곧바로 찍을 수 있게 한다.
    void useTerrainBrush(const TerrainBrush & brush);

    /// 담아 둔 지형을 커서 자리에 찍는다.
    bool pasteTerrainAt(const QPointF & screenPos);

    /// 유닛 놓기 도구가 놓을 유닛과 소유자.
    void setPlacementUnit(std::uint16_t unitType, std::uint8_t owner);

    /// 스프라이트 놓기 도구가 놓을 스프라이트.
    void setPlacementSprite(std::uint16_t spriteType, std::uint8_t owner);

    /// 두들 놓기 도구가 놓을 두들 (dddata.bin 번호).
    void setPlacementDoodad(std::uint16_t doodadId);

    /// 유닛·스프라이트를 놓을 때 좌표를 격자에 맞출지.
    ///
    /// 게임은 건물 좌표를 타일 경계에 맞춰 두므로, 맞춰 놓으면 실제
    /// 게임에서 보는 자리와 같아진다. 자유 배치는 SCMDraft 처럼 픽셀
    /// 단위로 어디든 놓는다.
    enum class UnitSnap
    {
        Free,      ///< 픽셀 단위 자유 배치
        Quarter,   ///< 1/4 타일 (8px)
        HalfTile,  ///< 반 타일 (16px)
        Tile       ///< 한 타일 (32px)
    };
    UnitSnap unitSnap() const { return unitSnap_; }
    void setUnitSnap(UnitSnap snap);

    /// 놓을 때 지형을 따질지.
    ///
    /// 건물은 지을 수 있는 땅(평지, 같은 높이)에만, 지상 유닛은 걸을 수
    /// 있는 땅에만 놓는다. 공중 유닛은 어디든 놓는다. 유즈맵은 일부러
    /// 물 위나 절벽에 올리는 일이 흔하므로 따로 끌 수 있다.
    bool terrainCheckEnabled() const { return checkTerrain_; }
    void setTerrainCheckEnabled(bool enabled);

    bool groundUnitCheckEnabled() const { return checkGroundUnits_; }
    void setGroundUnitCheckEnabled(bool enabled);

    /// 이미 유닛이 있는 자리에 겹쳐 놓을 수 있는지.
    ///
    /// 끄면 유닛의 차지 범위(units.dat 크기)가 겹치는 자리에는 놓지도,
    /// 끌어다 옮기지도 못한다.
    bool unitStackingAllowed() const { return allowStack_; }
    void setUnitStackingAllowed(bool allowed);

    /// 브러시 한 변의 타일 수 (1, 2, 4 …).
    int brushSize() const { return brushSize_; }
    void setBrushSize(int size);

    bool unitsVisible() const { return showUnits_; }
    bool locationsVisible() const { return showLocations_; }
    bool creepVisible() const { return showCreep_; }

    /// 이어진 유닛을 선으로 보여 줄지.
    bool unitLinksVisible() const { return showLinks_; }

    /// 지형의 성질을 색으로 겹쳐 보여 준다.
    ///
    /// 지형을 맞추거나 미네랄 자리·길목을 살필 때 쓴다.
    enum class TerrainOverlay
    {
        None,      ///< 겹치지 않음
        Elevation, ///< 높이 (저·중·고)
        Walkable,  ///< 지상 유닛이 지날 수 있는지
        Buildable, ///< 건물을 지을 수 있는지
        Creep      ///< 크립 위에만 지을 수 있는 땅
    };
    TerrainOverlay terrainOverlay() const { return overlay_; }
    void setTerrainOverlay(TerrainOverlay overlay);

    /// 타일 값을 칸마다 적어 보여 줄지 (지형을 맞출 때 쓴다).
    bool tileValuesVisible() const { return showTileValues_; }
    void setTileValuesVisible(bool visible);

    /// 시야 가리개(MASK)를 겹쳐 보여 줄지.
    bool fogVisible() const { return showFog_; }

    /// 가리개 도구가 어느 플레이어를 칠할지 (비트 0~7).
    std::uint8_t fogPlayers() const { return fogPlayers_; }
    void setFogPlayers(std::uint8_t players);

    /// 칠할 때 가릴지 걷을지.
    bool fogErasing() const { return fogErase_; }
    void setFogErasing(bool erasing);

    /// 타일 격자를 그릴지.
    bool gridVisible() const { return showGrid_; }
    void setGridVisible(bool visible);

public slots:
    void setUnitsVisible(bool visible);
    void setLocationsVisible(bool visible);
    void setCreepVisible(bool visible);
    void setFogVisible(bool visible);
    void setUnitLinksVisible(bool visible);

    void zoomIn();
    void zoomOut();
    void zoomReset();

protected:
    void paintEvent(QPaintEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void wheelEvent(QWheelEvent * event) override;
    void mousePressEvent(QMouseEvent * event) override;
    void mouseDoubleClickEvent(QMouseEvent * event) override;
    void mouseMoveEvent(QMouseEvent * event) override;
    void mouseReleaseEvent(QMouseEvent * event) override;
    void keyPressEvent(QKeyEvent * event) override;
    void leaveEvent(QEvent * event) override;
    void focusOutEvent(QFocusEvent * event) override;

private:
    void updateScrollRanges();

    /// 타일 그림을 캐시에서 얻는다. 없으면 만들어 넣는다.
    /// 타일셋이 없으면 nullptr.
    const QPixmap * tilePixmap(std::uint16_t tileId);

    /// 유닛 스프라이트. 타입과 소유자 조합으로 캐시한다(플레이어 색이 다르다).
    /// 스프라이트를 구하지 못하면 nullptr — 호출부가 원으로 대신 그린다.
    struct UnitSprite { QPixmap pixmap; int anchorX = 0; int anchorY = 0; };
    /// stateFlags·relationFlags 는 은폐·버로우·떠 있음·애드온 붙음을
    /// 그림에 반영하는 데 쓴다.
    const UnitSprite * unitSprite(std::uint16_t type, std::uint8_t owner,
                                  std::uint32_t resourceAmount,
                                  std::uint16_t stateFlags = 0,
                                  std::uint16_t relationFlags = 0);

    /// 맵 스프라이트(THG2). 유닛과 캐시를 나눠 쓴다.
    const UnitSprite * mapSprite(std::uint16_t type, std::uint8_t owner, bool drawnAsSprite);

    /// 맵 픽셀 좌표를 화면 좌표로 옮긴다.
    QPointF mapToScreen(double mapX, double mapY) const;

    void paintUnits(QPainter & painter, const QRect & dirty);

    /// 화면 좌표에서 맵 픽셀 좌표로.
    QPointF screenToMap(const QPointF & screen) const;

    /// 그 자리에 있는 유닛 번호. 없으면 -1. 위에 그려진 것이 우선한다.
    int unitAt(const QPointF & screenPos);

    /// ISOM 모드에서 그 자리에 지형을 찍는다(끌고 다닐 때도 쓴다).
    void applyIsomAt(const QPointF & screenPos);

    /// 지형 모드에서 그 자리에 브러시를 찍는다.
    void paintTerrainAt(const QPointF & screenPos);

    /// 그 자리에 있는 로케이션 번호. 없으면 -1. 작은 것이 우선한다 —
    /// 큰 로케이션 안에 작은 것이 겹쳐 있을 때 작은 쪽을 집어야 쓸모 있다.
    int locationAt(const QPointF & screenPos) const;
    void paintLocations(QPainter & painter, const QRect & dirty);

    /// 저그 건물 주변의 크립. 지형 위, 유닛 아래에 그린다.
    void paintCreep(QPainter & painter, const QRect & dirty);

    /// 크립 바닥 타일 그림(변형별). 비어 있으면 크립을 그릴 수 없다.
    const QVector<QPixmap> & creepTiles();

    /// 크립 타일 마스크. 문서나 타일셋이 바뀌면 다시 만든다.
    const std::vector<std::uint8_t> & creepMask();

    /// 크립을 미리 합성해 둔 이미지(맵 좌표계, 알파 포함).
    /// 가장자리를 흐려 두므로 타일 경계가 드러나지 않는다.
    /// 큰 맵에서는 메모리를 아끼려고 축소해 만든다.
    const QPixmap * creepLayer();

    /// 현재 줌에서 타일 한 변의 화면 픽셀 수.
    double scaledTileSize() const;

    /// 지형이 차지하는 전체 화면 크기.
    QSize contentSize() const;

    const chk::MapDocument * document_ = nullptr;
    const io::GameGraphics * tileset_ = nullptr;

    QHash<std::uint16_t, QPixmap> tileCache_;
    QHash<std::uint32_t, UnitSprite> unitCache_;
    QHash<std::uint32_t, UnitSprite> spriteCache_;
    double zoom_ = 1.0;
    QVector<QPixmap> creepTiles_;
    QPixmap creepLayer_;
    int creepLayerScale_ = 1; ///< 원본 대비 축소 배수 (1 = 등배)
    bool creepLayerReady_ = false;
    std::vector<std::uint8_t> creepMask_;
    bool creepReady_ = false;
    bool showUnits_ = true;
    bool showLocations_ = false; ///< 로케이션은 겹쳐 보이면 지형을 가려 기본은 꺼 둔다
    bool showCreep_ = true;
    bool showGrid_ = false;

    // 선택과 드래그
    /// 격자에 맞춘 좌표. 유닛의 왼쪽·위 모서리를 격자에 붙인다.
    QPoint snapUnitPos(std::uint16_t unitType, int x, int y) const;

    /// 그 자리에 놓으면 다른 유닛과 겹치는지. skipIndex 는 검사에서 뺀다.
    bool unitWouldOverlap(std::uint16_t unitType, int x, int y,
                          int skipIndex = -1) const;

    /// 그 자리의 땅이 이 유닛을 받아 줄 수 있는지 (건물만 따진다).
    bool terrainAccepts(std::uint16_t unitType, int x, int y) const;

    /// 지금 놓기 도구가 그 자리에 놓을 수 있는지.
    bool canPlaceAt(int x, int y) const;

    /// 방금 놓은 유닛을 이웃과 이어 준다 (애드온·나이더스).
    void autoLinkPlaced(std::size_t placedIndex);

    /// 애드온·나이더스로 이어진 유닛을 선으로 잇는다.
    void paintUnitLinks(QPainter & painter);

    /// 지형 성질을 색으로 겹쳐 그린다.
    void paintTerrainOverlay(QPainter & painter, const QRect & dirty);

    /// 타일 값을 칸마다 적는다.
    void paintTileValues(QPainter & painter, const QRect & dirty);

    /// 시야 가리개를 겹쳐 그린다.
    void paintFog(QPainter & painter, const QRect & dirty);

    /// 커서 자리의 타일에 가리개를 칠한다.
    void paintFogAt(const QPointF & screenPos);

    /// 커서 자리에 미리보기를 그린다.
    void paintPlacementPreview(QPainter & painter);

public:
    /// 유닛·스프라이트만 바뀌었을 때 다시 그린다.
    ///
    /// 타일과 유닛 그림 캐시는 그대로 두고 크립만 다시 셈한다. 유닛을
    /// 놓을 때마다 통째로 비우면 그림을 다시 그리느라 느리기도 하고,
    /// 방향이 정해지지 않은 유닛이 매번 다른 쪽을 보게 된다.
    void refreshUnits();

    /// 대칭이 켜져 있을 때 함께 칠할 자리들 (자기 자신을 포함한다).
    ///
    /// 타일 좌표를 받아 타일 좌표를 돌려준다. 겹치는 자리는 한 번만 담는다.
    std::vector<QPoint> mirrorTiles(int tileX, int tileY) const;

    /// 픽셀 좌표판. ISOM 처럼 픽셀로 다루는 것에 쓴다.
    std::vector<QPoint> mirrorPixels(int pixelX, int pixelY) const;

    /// 커서가 로케이션의 어느 모서리에 닿았는지 (Edge 비트, 없으면 0).
    int locationEdgeAt(const QPointF & screenPos, int locationIndex) const;

    /// 고른 지형과 붙여넣기 미리보기를 그린다.
    void paintTerrainSelection(QPainter & painter);

    /// 고르는 사각형을 그린다.
    void paintSelectionBox(QPainter & painter);

    /// 그 사각형 안의 유닛을 모두 고른다.
    void selectUnitsInBox(const QPointF & fromMap, const QPointF & toMap, bool add);

    /// 지형 브러시가 덮을 자리를 커서 둘레에 그린다.
    void paintTerrainCursor(QPainter & painter);

    /// 놓기 도구로 한 번 놓는다. 놓았으면 참.
    bool placeAt(const QPointF & screenPos);

    Tool tool_ = Tool::Select;
    std::uint16_t brushTile_ = 0;
    TerrainMode terrainMode_ = TerrainMode::Rectangular;
    std::size_t isomTerrainType_ = 0;
    UnitSnap unitSnap_ = UnitSnap::Tile;
    bool allowStack_ = false;
    bool checkTerrain_ = false;
    bool checkGroundUnits_ = false;
    Symmetry symmetry_ = Symmetry::None;

    bool showFog_ = false;
    bool showLinks_ = true;
    TerrainOverlay overlay_ = TerrainOverlay::None;
    bool showTileValues_ = false;
    std::uint8_t fogPlayers_ = 0x01; ///< 기본은 플레이어 1
    bool fogErase_ = false;
    bool fogPainting_ = false;

    // 놓기 도구가 커서를 따라 보여 주는 미리보기.
    QPoint hoverPos_ {-1, -1};   ///< 맵 좌표로 옮긴 커서 자리
    bool hoverValid_ = false;    ///< 그 자리에 놓을 수 있는지
    bool hasHover_ = false;
    bool placingDrag_ = false;   ///< 버튼을 누른 채 끌며 놓는 중
    QPoint lastPlaced_ {-1, -1}; ///< 끌며 놓을 때 같은 자리에 겹쳐 놓지 않도록
    bool dragSoundPlayed_ = false; ///< 한 번 끄는 동안 소리는 한 번만

    /// 이어서 놓은 나이더스 굴을 잇기 위해 마지막 자리를 기억한다.
    int lastNydusUnit_ = -1;

    std::uint16_t placeUnitType_ = 0;
    std::uint16_t placeDoodadId_ = 0;
    std::uint8_t placeUnitOwner_ = 0;
    std::uint16_t placeSpriteType_ = 0;
    int brushSize_ = 1;
    bool painting_ = false;
    bool isomPainting_ = false;
    QPoint lastIsomTile_ {-1, -1}; ///< 같은 칸에 거듭 찍지 않도록
    std::vector<std::pair<std::size_t, std::size_t>> strokeTiles_; ///< 이번 획에 칠한 자리

    int selectedUnit_ = -1;
    int selectedLocation_ = -1;

    /// 여럿 고르기. selectedUnit_ 은 이 가운데 마지막 것이다.
    std::vector<int> selectedUnits_;

    // 빈 곳에서 끌면 고르는 사각형이 된다.
    bool boxSelecting_ = false;
    QPointF boxStart_;   ///< 맵 좌표
    QPointF boxEnd_;

    /// 담아 둔 로케이션. 크기와 이름·높이를 그대로 베낀다.
    struct LocationClipboard
    {
        bool valid = false;
        int width = 0;
        int height = 0;
        std::string name;
        std::uint16_t elevationFlags = 0;
        bool inverted = false;
    };
    LocationClipboard locationClipboard_;

    // 로케이션 그리기·모서리 조절.
    bool drawingLocation_ = false;   ///< 빈 곳을 끌어 새로 그리는 중
    int resizingLocation_ = -1;      ///< 모서리를 잡아 늘이는 중인 로케이션
    int resizeEdges_ = 0;            ///< 잡은 모서리 (아래 Edge 비트)
    QRect locationStart_;            ///< 늘이기 전 자리 (맵 픽셀)

    enum Edge { EdgeLeft = 1, EdgeTop = 2, EdgeRight = 4, EdgeBottom = 8 };

    // 지형 고르기·클립보드.
    bool terrainSelecting_ = false;
    QRect terrainSelection_;  ///< 지금 끌고 있는 네모 (타일 좌표)

    /// 고른 영역 전체. Shift 로 떨어진 네모를 여러 개 더할 수 있다.
    QRegion terrainRegion_;

    int clipboardWidth_ = 0;  ///< 담아 둔 지형의 타일 크기
    int clipboardHeight_ = 0;
    std::vector<std::uint16_t> terrainClipboard_;
    std::vector<bool> clipboardMask_; ///< 빈 칸이 있으면 채워진다
    bool pastingTerrain_ = false; ///< 붙여넣기 브러시가 켜져 있는지

    // 복사해 둔 유닛. 맵 사이에서도 붙일 수 있도록 값으로 들고 있는다.
    bool clipboardValid_ = false;
    std::uint16_t clipboardType_ = 0;
    std::uint8_t clipboardOwner_ = 0;
    std::uint32_t clipboardResource_ = 0;

    /// 여럿을 복사하면 서로의 자리 관계를 지켜야 한다. 첫 유닛을 기준으로
    /// 나머지의 상대 좌표를 담아 둔다.
    struct ClipboardUnit
    {
        std::uint16_t type = 0;
        std::uint8_t owner = 0;
        std::uint32_t resourceAmount = 0;
        int dx = 0;
        int dy = 0;
    };
    std::vector<ClipboardUnit> clipboard_;
    bool dragging_ = false;
    QPointF dragStartMap_;      ///< 드래그 시작 시 맵 좌표
    QPoint dragStartUnitPos_;   ///< 드래그 시작 시 유닛 좌표
    QPoint previewPos_;         ///< 드래그 중 미리 보여 줄 위치
    bool hasPreview_ = false;
};

} // namespace splash::ui
