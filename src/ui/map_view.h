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

    /// 선택된 유닛 번호. 없으면 -1.
    int selectedUnit() const { return selectedUnit_; }

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

    /// ISOM 모드가 놓을 지형 종류(GameGraphics::terrainTypes 의 brushIndex).
    void setIsomTerrainType(std::size_t brushIndex);

    /// 유닛 놓기 도구가 놓을 유닛과 소유자.
    void setPlacementUnit(std::uint16_t unitType, std::uint8_t owner);

    /// 스프라이트 놓기 도구가 놓을 스프라이트.
    void setPlacementSprite(std::uint16_t spriteType, std::uint8_t owner);

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

    /// 건물을 놓을 때 지형을 따질지.
    ///
    /// 켜면 게임이 건물을 지을 수 있는 땅(평지, 같은 높이)에만 놓는다.
    /// 유즈맵은 일부러 물 위나 절벽에 올리는 일이 흔하므로 끌 수 있다.
    bool terrainCheckEnabled() const { return checkTerrain_; }
    void setTerrainCheckEnabled(bool enabled);

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

private:
    void updateScrollRanges();

    /// 타일 그림을 캐시에서 얻는다. 없으면 만들어 넣는다.
    /// 타일셋이 없으면 nullptr.
    const QPixmap * tilePixmap(std::uint16_t tileId);

    /// 유닛 스프라이트. 타입과 소유자 조합으로 캐시한다(플레이어 색이 다르다).
    /// 스프라이트를 구하지 못하면 nullptr — 호출부가 원으로 대신 그린다.
    struct UnitSprite { QPixmap pixmap; int anchorX = 0; int anchorY = 0; };
    const UnitSprite * unitSprite(std::uint16_t type, std::uint8_t owner,
                                  std::uint32_t resourceAmount);

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
    bool showLocations_ = true;
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

    /// 시야 가리개를 겹쳐 그린다.
    void paintFog(QPainter & painter, const QRect & dirty);

    /// 커서 자리의 타일에 가리개를 칠한다.
    void paintFogAt(const QPointF & screenPos);

    /// 커서 자리에 미리보기를 그린다.
    void paintPlacementPreview(QPainter & painter);

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

    bool showFog_ = false;
    std::uint8_t fogPlayers_ = 0x01; ///< 기본은 플레이어 1
    bool fogErase_ = false;
    bool fogPainting_ = false;

    // 놓기 도구가 커서를 따라 보여 주는 미리보기.
    QPoint hoverPos_ {-1, -1};   ///< 맵 좌표로 옮긴 커서 자리
    bool hoverValid_ = false;    ///< 그 자리에 놓을 수 있는지
    bool hasHover_ = false;
    bool placingDrag_ = false;   ///< 버튼을 누른 채 끌며 놓는 중
    QPoint lastPlaced_ {-1, -1}; ///< 끌며 놓을 때 같은 자리에 겹쳐 놓지 않도록

    std::uint16_t placeUnitType_ = 0;
    std::uint8_t placeUnitOwner_ = 0;
    std::uint16_t placeSpriteType_ = 0;
    int brushSize_ = 1;
    bool painting_ = false;
    bool isomPainting_ = false;
    QPoint lastIsomTile_ {-1, -1}; ///< 같은 칸에 거듭 찍지 않도록
    std::vector<std::pair<std::size_t, std::size_t>> strokeTiles_; ///< 이번 획에 칠한 자리

    int selectedUnit_ = -1;
    int selectedLocation_ = -1;

    // 복사해 둔 유닛. 맵 사이에서도 붙일 수 있도록 값으로 들고 있는다.
    bool clipboardValid_ = false;
    std::uint16_t clipboardType_ = 0;
    std::uint8_t clipboardOwner_ = 0;
    std::uint32_t clipboardResource_ = 0;
    bool dragging_ = false;
    QPointF dragStartMap_;      ///< 드래그 시작 시 맵 좌표
    QPoint dragStartUnitPos_;   ///< 드래그 시작 시 유닛 좌표
    QPoint previewPos_;         ///< 드래그 중 미리 보여 줄 위치
    bool hasPreview_ = false;
};

} // namespace splash::ui
