#include "ui/map_view.h"

#include "chk/map_document.h"
#include "io/tileset_source.h"

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

void MapView::setTileset(const io::TilesetSource * tileset)
{
    tileset_ = tileset;
    tileCache_.clear(); // 타일셋이 바뀌면 그림이 전부 달라진다
    refresh();
}

void MapView::refresh()
{
    tileCache_.clear();
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
}

} // namespace splash::ui
