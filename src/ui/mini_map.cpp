#include "ui/mini_map.h"

#include "chk/map_document.h"
#include "io/game_graphics.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace splash::ui {
namespace {

/// 맵 픽셀 -> 타일. 미니맵은 타일 하나가 픽셀 하나다.
constexpr int kTilePixels = 32;

} // namespace

MiniMap::MiniMap(QWidget * parent) : QWidget(parent)
{
    setMinimumSize(140, 140);
    setMouseTracking(false);
}

MiniMap::~MiniMap() = default;

void MiniMap::setDocument(const chk::MapDocument * document)
{
    document_ = document;
    refresh();
}

void MiniMap::setTileset(const io::GameGraphics * tileset)
{
    tileset_ = tileset;
    refresh();
}

void MiniMap::refresh()
{
    dirty_ = true;
    update();
}

void MiniMap::setViewportRect(const QRectF & mapRect)
{
    if (viewportRect_ == mapRect)
        return;
    viewportRect_ = mapRect;
    update();
}

QSize MiniMap::sizeHint() const
{
    return QSize(180, 180);
}

void MiniMap::rebuild()
{
    dirty_ = false;
    terrain_ = QPixmap();

    if (document_ == nullptr || !document_->isOpen() ||
        tileset_ == nullptr || !tileset_->isLoaded())
    {
        return;
    }

    const auto & info = document_->info();
    const auto pixels = tileset_->renderMinimap(document_->tiles(),
                                                info.width, info.height,
                                                info.tilesetId);
    if (pixels.empty())
        return;

    QImage image(info.width, info.height, QImage::Format_RGB888);
    for (int y = 0; y < info.height; ++y)
    {
        std::copy_n(pixels.data() + static_cast<std::size_t>(y) * info.width * 3,
                    static_cast<std::size_t>(info.width) * 3,
                    image.scanLine(y));
    }

    terrain_ = QPixmap::fromImage(image);
}

QRectF MiniMap::imageRect() const
{
    if (terrain_.isNull())
        return QRectF();

    // 맵은 정사각형이 아닐 수 있다. 위젯 안에 비율을 지켜 넣는다.
    const double scale = std::min(double(width()) / terrain_.width(),
                                  double(height()) / terrain_.height());
    const double w = terrain_.width() * scale;
    const double h = terrain_.height() * scale;
    return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
}

void MiniMap::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 28));

    if (dirty_)
        rebuild();

    if (terrain_.isNull())
    {
        painter.setPen(QColor(150, 150, 155));
        painter.drawText(rect(), Qt::AlignCenter, tr("미니맵 없음"));
        return;
    }

    const QRectF target = imageRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(target, terrain_, QRectF(terrain_.rect()));

    // 유닛을 소유자 색 점으로 얹는다.
    const auto & info = document_->info();
    const double sx = target.width() / (info.width * double(kTilePixels));
    const double sy = target.height() / (info.height * double(kTilePixels));

    for (const auto & unit : document_->units())
    {
        const chk::PlayerColor color = chk::playerColor(unit.owner);
        painter.fillRect(QRectF(target.left() + unit.x * sx - 1,
                                target.top() + unit.y * sy - 1, 2.5, 2.5),
                         QColor(color.r, color.g, color.b));
    }

    // 지금 보고 있는 영역.
    if (!viewportRect_.isEmpty())
    {
        const QRectF box(target.left() + viewportRect_.left() * sx,
                         target.top() + viewportRect_.top() * sy,
                         viewportRect_.width() * sx,
                         viewportRect_.height() * sy);
        painter.setPen(QPen(QColor(255, 255, 255, 200), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box.intersected(target));
    }
}

void MiniMap::mousePressEvent(QMouseEvent * event)
{
    mouseMoveEvent(event);
}

void MiniMap::mouseMoveEvent(QMouseEvent * event)
{
    if (terrain_.isNull() || document_ == nullptr || !document_->isOpen())
        return;
    if (!(event->buttons() & Qt::LeftButton) && event->type() != QEvent::MouseButtonPress)
        return;

    const QRectF target = imageRect();
    if (!target.contains(event->position()))
        return;

    const auto & info = document_->info();
    const double fx = (event->position().x() - target.left()) / target.width();
    const double fy = (event->position().y() - target.top()) / target.height();

    emit navigationRequested(QPointF(fx * info.width * kTilePixels,
                                     fy * info.height * kTilePixels));
    event->accept();
}

} // namespace splash::ui
