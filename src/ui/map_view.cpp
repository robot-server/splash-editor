#include "ui/map_view.h"

#include "chk/map_document.h"
#include "io/game_graphics.h"

#include <QFontMetrics>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace splash::ui {
namespace {

constexpr double kMinZoom = 0.125;
constexpr double kMaxZoom = 4.0;

/// 캐시가 지나치게 커지는 것을 막는 상한.
/// 타일 하나가 32x32 RGBA(4KB)이므로 4096개면 약 16MB 다.
constexpr int kMaxCachedTiles = 4096;

} // namespace

MapView::MapView(QWidget * parent) : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(true);
}

MapView::~MapView() = default;

void MapView::setDocument(const chk::MapDocument * document)
{
    document_ = document;
    refresh();
}

void MapView::setTileset(const io::GameGraphics * tileset)
{
    tileset_ = tileset;
    tileCache_.clear(); // 타일셋이 바뀌면 그림이 전부 달라진다
    unitCache_.clear();
    refresh();
}

void MapView::refresh()
{
    tileCache_.clear();
    unitCache_.clear();
    updateScrollRanges();
    viewport()->update();
}

double MapView::scaledTileSize() const
{
    return io::kTilePixels * zoom_;
}

QSize MapView::contentSize() const
{
    if (document_ == nullptr || !document_->isOpen())
        return QSize(0, 0);

    const auto & info = document_->info();
    const double tile = scaledTileSize();
    return QSize(static_cast<int>(std::ceil(info.width * tile)),
                 static_cast<int>(std::ceil(info.height * tile)));
}

void MapView::setZoom(double factor)
{
    const double clamped = std::clamp(factor, kMinZoom, kMaxZoom);
    if (std::abs(clamped - zoom_) < 1e-9)
        return;

    // 확대/축소 후에도 화면 중앙이 같은 지점을 가리키도록 스크롤을 맞춘다.
    const double oldTile = scaledTileSize();
    const QPointF center(horizontalScrollBar()->value() + viewport()->width() / 2.0,
                         verticalScrollBar()->value() + viewport()->height() / 2.0);
    const QPointF centerInTiles = oldTile > 0 ? center / oldTile : QPointF(0, 0);

    zoom_ = clamped;
    updateScrollRanges();

    const double newTile = scaledTileSize();
    horizontalScrollBar()->setValue(
        static_cast<int>(centerInTiles.x() * newTile - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(
        static_cast<int>(centerInTiles.y() * newTile - viewport()->height() / 2.0));

    viewport()->update();
}

void MapView::setUnitsVisible(bool visible)
{
    if (showUnits_ == visible)
        return;
    showUnits_ = visible;
    viewport()->update();
}

void MapView::setLocationsVisible(bool visible)
{
    if (showLocations_ == visible)
        return;
    showLocations_ = visible;
    viewport()->update();
}

QPointF MapView::mapToScreen(double mapX, double mapY) const
{
    return QPointF(mapX * zoom_ - horizontalScrollBar()->value(),
                   mapY * zoom_ - verticalScrollBar()->value());
}

void MapView::zoomIn()    { setZoom(zoom_ * 2.0); }
void MapView::zoomOut()   { setZoom(zoom_ / 2.0); }
void MapView::zoomReset() { setZoom(1.0); }

void MapView::updateScrollRanges()
{
    const QSize content = contentSize();
    const QSize view = viewport()->size();

    horizontalScrollBar()->setRange(0, std::max(0, content.width()  - view.width()));
    verticalScrollBar()->setRange(0, std::max(0, content.height() - view.height()));
    horizontalScrollBar()->setPageStep(view.width());
    verticalScrollBar()->setPageStep(view.height());
    horizontalScrollBar()->setSingleStep(static_cast<int>(scaledTileSize()));
    verticalScrollBar()->setSingleStep(static_cast<int>(scaledTileSize()));
}

void MapView::resizeEvent(QResizeEvent * event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollRanges();
}

void MapView::wheelEvent(QWheelEvent * event)
{
    // Ctrl(또는 macOS 의 Command)을 누른 채 굴리면 확대/축소.
    if (event->modifiers() & Qt::ControlModifier)
    {
        if (event->angleDelta().y() > 0)
            zoomIn();
        else if (event->angleDelta().y() < 0)
            zoomOut();
        event->accept();
        return;
    }

    QAbstractScrollArea::wheelEvent(event);
}

const QPixmap * MapView::tilePixmap(std::uint16_t tileId)
{
    if (tileset_ == nullptr || !tileset_->isLoaded())
        return nullptr;

    auto found = tileCache_.find(tileId);
    if (found != tileCache_.end())
        return &found.value();

    if (tileCache_.size() >= kMaxCachedTiles)
        tileCache_.clear(); // 단순한 정책 — 맵 하나가 상한을 넘는 일은 드물다

    std::vector<std::uint8_t> rgba(io::kTileRgbaBytes);
    tileset_->renderTile(document_->info().tilesetId, tileId, rgba.data());

    // QImage 는 버퍼를 복사하지 않으므로, QPixmap 으로 변환해 소유권을 넘긴다.
    const QImage image(rgba.data(), io::kTilePixels, io::kTilePixels,
                       io::kTilePixels * 4, QImage::Format_RGBA8888);

    auto inserted = tileCache_.insert(tileId, QPixmap::fromImage(image.copy()));
    return &inserted.value();
}

void MapView::paintEvent(QPaintEvent * event)
{
    QPainter painter(viewport());
    painter.fillRect(event->rect(), palette().dark());

    if (document_ == nullptr || !document_->isOpen())
        return;

    const auto & info = document_->info();
    const auto & tiles = document_->tiles();
    if (tiles.empty())
        return;

    if (tileset_ == nullptr || !tileset_->isLoaded())
    {
        painter.setPen(palette().color(QPalette::BrightText));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         tr("지형을 그리려면 StarCraft 설치 폴더가 필요합니다.\n"
                            "파일 › StarCraft 설치 폴더 지정…"));
        return;
    }

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    // 다시 그려야 하는 영역에 걸치는 타일 범위만 계산한다.
    const QRect dirty = event->rect();
    const int firstX = std::max(0, static_cast<int>((originX + dirty.left()) / tile));
    const int firstY = std::max(0, static_cast<int>((originY + dirty.top()) / tile));
    const int lastX = std::min<int>(info.width  - 1,
                                    static_cast<int>((originX + dirty.right())  / tile));
    const int lastY = std::min<int>(info.height - 1,
                                    static_cast<int>((originY + dirty.bottom()) / tile));

    // 축소 상태에서는 부드러운 보간이 비싸고 이득이 적다.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);

    for (int ty = firstY; ty <= lastY; ++ty)
    {
        for (int tx = firstX; tx <= lastX; ++tx)
        {
            const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
            if (index >= tiles.size())
                continue;

            const QPixmap * pixmap = tilePixmap(tiles[index]);
            if (pixmap == nullptr)
                continue;

            const QRectF target(tx * tile - originX, ty * tile - originY, tile, tile);
            painter.drawPixmap(target, *pixmap, QRectF(0, 0, io::kTilePixels, io::kTilePixels));
        }
    }

    if (showLocations_)
        paintLocations(painter, dirty);
    if (showUnits_)
        paintUnits(painter, dirty);
}

const MapView::UnitSprite * MapView::unitSprite(std::uint16_t type, std::uint8_t owner)
{
    if (tileset_ == nullptr || !tileset_->hasUnitGraphics() || document_ == nullptr)
        return nullptr;

    const std::uint32_t key = (static_cast<std::uint32_t>(type) << 8) | owner;
    auto found = unitCache_.find(key);
    if (found != unitCache_.end())
        return found.value().pixmap.isNull() ? nullptr : &found.value();

    const io::UnitImage image =
        tileset_->renderUnit(type, owner, document_->info().tilesetId);

    UnitSprite sprite;
    if (image.width > 0 && image.height > 0)
    {
        const QImage qimage(image.rgba.data(), image.width, image.height,
                            image.width * 4, QImage::Format_RGBA8888);
        sprite.pixmap = QPixmap::fromImage(qimage.copy());
        sprite.anchorX = image.anchorX;
        sprite.anchorY = image.anchorY;
    }

    auto inserted = unitCache_.insert(key, sprite);
    return inserted.value().pixmap.isNull() ? nullptr : &inserted.value();
}

void MapView::paintUnits(QPainter & painter, const QRect & dirty)
{
    const auto & units = document_->units();
    if (units.empty())
        return;

    painter.save();

    // 스프라이트가 없는 유닛(또는 그래픽 미로드)을 위한 대체 표시 크기.
    const double diameter = std::clamp(16.0 * zoom_, 3.0, 48.0);
    const double radius = diameter / 2.0;

    for (const auto & unit : units)
    {
        const UnitSprite * sprite = unitSprite(unit.type, unit.owner);

        if (sprite != nullptr)
        {
            // 스프라이트는 유닛 중심(anchor)을 기준으로 놓인다.
            const QPointF topLeft =
                mapToScreen(unit.x - sprite->anchorX, unit.y - sprite->anchorY);
            const QRectF bounds(topLeft.x(), topLeft.y(),
                                sprite->pixmap.width() * zoom_,
                                sprite->pixmap.height() * zoom_);

            // 화면 밖이면 건너뛴다 — 유닛이 수천 개인 맵이 흔하다.
            if (!dirty.intersects(bounds.toAlignedRect().adjusted(-1, -1, 1, 1)))
                continue;

            painter.drawPixmap(bounds, sprite->pixmap,
                               QRectF(0, 0, sprite->pixmap.width(), sprite->pixmap.height()));
            continue;
        }

        const QPointF center = mapToScreen(unit.x, unit.y);
        const QRectF bounds(center.x() - radius, center.y() - radius, diameter, diameter);
        if (!dirty.intersects(bounds.toAlignedRect().adjusted(-1, -1, 1, 1)))
            continue;

        const chk::PlayerColor color = chk::playerColor(unit.owner);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(QColor(color.r, color.g, color.b));
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
        painter.drawEllipse(bounds);
    }

    painter.restore();
}

void MapView::paintLocations(QPainter & painter, const QRect & dirty)
{
    const auto & locations = document_->locations();
    if (locations.empty())
        return;

    painter.save();

    const QColor edge(255, 220, 90);
    QFont font = painter.font();
    font.setPointSizeF(std::max(7.0, font.pointSizeF()));
    painter.setFont(font);
    const QFontMetrics metrics(font);

    for (const auto & location : locations)
    {
        const QPointF topLeft = mapToScreen(location.left, location.top);
        const QPointF bottomRight = mapToScreen(location.right, location.bottom);
        const QRectF bounds(topLeft, bottomRight);

        if (!dirty.intersects(bounds.toAlignedRect().adjusted(-1, -1, 1, 1)))
            continue;

        painter.setPen(QPen(edge, 1.0, Qt::DashLine));
        painter.setBrush(QColor(255, 220, 90, 28));
        painter.drawRect(bounds);

        // 이름은 사각형이 글자를 담을 만큼 클 때만 그린다.
        const QString label = location.name.empty()
            ? tr("로케이션 %1").arg(location.index)
            : QString::fromStdString(location.name);

        if (bounds.width() > metrics.horizontalAdvance(label) + 6 &&
            bounds.height() > metrics.height() + 4)
        {
            painter.setPen(edge);
            painter.drawText(bounds.adjusted(3, 2, -3, -2),
                             Qt::AlignTop | Qt::AlignLeft, label);
        }
    }

    painter.restore();
}

} // namespace splash::ui
