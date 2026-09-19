#pragma once

// 맵 전체를 한눈에 보여 주는 미니맵.
//
// 타일 하나가 픽셀 하나다. 클릭하면 그 자리로 화면을 옮긴다.

#include <QPixmap>
#include <QRectF>
#include <QWidget>

#include <cstdint>

namespace splash::chk { class MapDocument; }
namespace splash::io  { class GameGraphics; }

namespace splash::ui {

class MiniMap : public QWidget
{
    Q_OBJECT

public:
    explicit MiniMap(QWidget * parent = nullptr);
    ~MiniMap() override;

    void setDocument(const chk::MapDocument * document);
    void setTileset(const io::GameGraphics * tileset);

    /// 지형이나 유닛이 바뀌었을 때. 미니맵을 다시 만든다.
    void refresh();

    /// 맵 뷰가 지금 보고 있는 영역(맵 픽셀 좌표).
    void setViewportRect(const QRectF & mapRect);

signals:
    /// 미니맵에서 그 지점으로 가자고 요청한다(맵 픽셀 좌표).
    void navigationRequested(const QPointF & mapPos);

protected:
    void paintEvent(QPaintEvent * event) override;
    void mousePressEvent(QMouseEvent * event) override;
    void mouseMoveEvent(QMouseEvent * event) override;
    QSize sizeHint() const override;

private:
    void rebuild();

    /// 미니맵 그림이 위젯 안에서 차지하는 자리(가로세로 비율 유지).
    QRectF imageRect() const;

    const chk::MapDocument * document_ = nullptr;
    const io::GameGraphics * tileset_ = nullptr;

    QPixmap terrain_;
    QRectF viewportRect_;
    bool dirty_ = true;
};

} // namespace splash::ui
