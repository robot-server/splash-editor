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
#include <QVector>
#include <QPixmap>

#include <cstdint>
#include <vector>

namespace splash::chk { class MapDocument; }
namespace splash::io  { class GameGraphics; }

namespace splash::ui {

class MapView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit MapView(QWidget * parent = nullptr);
    ~MapView() override;

    /// 표시할 문서. nullptr 이면 빈 캔버스가 된다.
    /// 소유하지 않는다 — 호출부가 수명을 관리한다.
    void setDocument(const chk::MapDocument * document);

    /// 타일셋 그래픽. nullptr 이거나 로드되지 않았으면 안내 문구를 보여 준다.
    void setTileset(const io::GameGraphics * tileset);

    /// 문서나 타일셋의 내용이 바뀌었을 때 호출한다. 캐시를 비우고 다시 그린다.
    void refresh();

    double zoom() const { return zoom_; }
    void setZoom(double factor);

    bool unitsVisible() const { return showUnits_; }
    bool locationsVisible() const { return showLocations_; }
    bool creepVisible() const { return showCreep_; }

public slots:
    void setUnitsVisible(bool visible);
    void setLocationsVisible(bool visible);
    void setCreepVisible(bool visible);

    void zoomIn();
    void zoomOut();
    void zoomReset();

protected:
    void paintEvent(QPaintEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void wheelEvent(QWheelEvent * event) override;

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
    void paintLocations(QPainter & painter, const QRect & dirty);

    /// 저그 건물 주변의 크립. 지형 위, 유닛 아래에 그린다.
    void paintCreep(QPainter & painter, const QRect & dirty);

    /// 크립 바닥 타일 그림(변형별). 비어 있으면 크립을 그릴 수 없다.
    const QVector<QPixmap> & creepTiles();

    /// 크립 타일 마스크. 문서나 타일셋이 바뀌면 다시 만든다.
    const std::vector<std::uint8_t> & creepMask();

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
    std::vector<std::uint8_t> creepMask_;
    bool creepReady_ = false;
    bool showUnits_ = true;
    bool showLocations_ = true;
    bool showCreep_ = true;
};

} // namespace splash::ui
