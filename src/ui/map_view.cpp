#include "ui/map_view.h"

#include "chk/map_document.h"
#include "io/game_graphics.h"
#include "io/map_archive.h"

#include <QFontMetrics>
#include <QImage>
#include <QPainterPath>
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
    spriteCache_.clear();
    refresh();
}

void MapView::refresh()
{
    tileCache_.clear();
    unitCache_.clear();
    spriteCache_.clear();
    creepTiles_.clear();
    creepMask_.clear();
    creepLayer_ = QPixmap();
    creepLayerReady_ = false;
    creepReady_ = false;
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

void MapView::setCreepVisible(bool visible)
{
    if (showCreep_ == visible)
        return;
    showCreep_ = visible;
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

    // 크립은 지형 위, 유닛 아래.
    if (showCreep_)
        paintCreep(painter, dirty);
    if (showLocations_)
        paintLocations(painter, dirty);
    if (showUnits_)
        paintUnits(painter, dirty);
}

const QVector<QPixmap> & MapView::creepTiles()
{
    if (!creepReady_)
        creepMask(); // 둘을 함께 준비한다
    return creepTiles_;
}

const std::vector<std::uint8_t> & MapView::creepMask()
{
    if (creepReady_)
        return creepMask_;

    creepReady_ = true;
    creepTiles_.clear();
    creepMask_.clear();

    if (tileset_ == nullptr || !tileset_->hasUnitGraphics() || document_ == nullptr)
        return creepMask_;

    const auto & info = document_->info();
    const std::uint16_t tilesetId = info.tilesetId;

    // 크립 바닥 타일 그림
    std::vector<std::uint8_t> rgba(io::kTileRgbaBytes);
    for (const std::uint16_t id : tileset_->creepTileIds(tilesetId))
    {
        if (!tileset_->renderTile(tilesetId, id, rgba.data()))
            continue;
        const QImage image(rgba.data(), io::kTilePixels, io::kTilePixels,
                           io::kTilePixels * 4, QImage::Format_RGBA8888);
        creepTiles_.push_back(QPixmap::fromImage(image.copy()));
    }
    if (creepTiles_.isEmpty())
        return creepMask_;

    // 크립이 깔릴 타일. 코어가 지형(평지·높이)까지 따져서 계산해 준다.
    std::vector<io::RawUnit> units;
    units.reserve(document_->units().size());
    for (const auto & unit : document_->units())
    {
        io::RawUnit raw;
        raw.type = unit.type;
        raw.x = unit.x;
        raw.y = unit.y;
        raw.owner = unit.owner;
        units.push_back(raw);
    }

    creepMask_ = tileset_->computeCreepMask(units, document_->tiles(),
                                            info.width, info.height, tilesetId);
    return creepMask_;
}

const QPixmap * MapView::creepLayer()
{
    if (creepLayerReady_)
        return creepLayer_.isNull() ? nullptr : &creepLayer_;

    creepLayerReady_ = true;

    const auto & tiles = creepTiles();
    const auto & mask = creepMask();
    if (tiles.isEmpty() || mask.empty() || document_ == nullptr)
        return nullptr;

    const auto & info = document_->info();

    // 맵 전체를 픽셀 해상도로 합성하면 256x256 맵이 8192x8192 가 된다.
    // 화면에 보이는 정밀도면 충분하므로, 큰 맵은 축소해 만든다.
    int scale = 1;
    while ((static_cast<long long>(info.width) * io::kTilePixels / scale) *
           (static_cast<long long>(info.height) * io::kTilePixels / scale) > 16LL * 1024 * 1024)
    {
        scale *= 2;
    }
    creepLayerScale_ = scale;

    const int layerW = info.width * io::kTilePixels / scale;
    const int layerH = info.height * io::kTilePixels / scale;
    if (layerW <= 0 || layerH <= 0)
        return nullptr;

    // 1) 크립 바닥을 깔고
    QImage layer(layerW, layerH, QImage::Format_RGBA8888);
    layer.fill(Qt::transparent);

    const int tilePx = io::kTilePixels / scale;
    {
        QPainter tilePainter(&layer);
        for (int ty = 0; ty < info.height; ++ty)
        {
            for (int tx = 0; tx < info.width; ++tx)
            {
                if (mask[static_cast<std::size_t>(ty) * info.width + tx] == 0)
                    continue;

                const std::size_t hash = (static_cast<std::size_t>(tx) * 73856093u) ^
                                         (static_cast<std::size_t>(ty) * 19349663u);
                const std::size_t plainCount =
                    std::max<std::size_t>(1, static_cast<std::size_t>(tiles.size()) / 3);
                const bool useDecor =
                    (hash % 11 == 0) && static_cast<std::size_t>(tiles.size()) > plainCount;
                const std::size_t pick = useDecor
                    ? plainCount + (hash / 11) % (static_cast<std::size_t>(tiles.size()) - plainCount)
                    : hash % plainCount;

                tilePainter.drawPixmap(QRect(tx * tilePx, ty * tilePx, tilePx, tilePx),
                                       tiles[static_cast<int>(pick)]);
            }
        }
    }

    // 2) 가장자리를 흐린다.
    //
    // 크립 타일에는 가장자리 전이 변형이 없어서(이 타일셋 기준 전부 가득 찬
    // 질감이다) 마스크 경계가 그대로 드러나면 네모나게 보인다. 알파만 흐려
    // 서서히 사라지게 만든다. 색은 건드리지 않는다.
    const int blur = std::max(1, 10 / scale);
    std::vector<std::uint16_t> alpha(
        static_cast<std::size_t>(layerW) * layerH, 0);
    for (int y = 0; y < layerH; ++y)
    {
        const uchar * line = layer.constScanLine(y);
        for (int x = 0; x < layerW; ++x)
            alpha[static_cast<std::size_t>(y) * layerW + x] = line[x * 4 + 3];
    }

    std::vector<std::uint16_t> tmp(alpha.size(), 0);
    const int window = 2 * blur + 1;
    for (int y = 0; y < layerH; ++y)
    {
        int sum = 0;
        const std::size_t row = static_cast<std::size_t>(y) * layerW;
        for (int x = -blur; x < layerW; ++x)
        {
            if (x + blur < layerW) sum += alpha[row + x + blur];
            if (x - blur - 1 >= 0)  sum -= alpha[row + x - blur - 1];
            if (x >= 0) tmp[row + x] = static_cast<std::uint16_t>(sum / window);
        }
    }
    for (int x = 0; x < layerW; ++x)
    {
        int sum = 0;
        for (int y = -blur; y < layerH; ++y)
        {
            if (y + blur < layerH) sum += tmp[static_cast<std::size_t>(y + blur) * layerW + x];
            if (y - blur - 1 >= 0)  sum -= tmp[static_cast<std::size_t>(y - blur - 1) * layerW + x];
            if (y >= 0)
                alpha[static_cast<std::size_t>(y) * layerW + x] =
                    static_cast<std::uint16_t>(sum / window);
        }
    }

    // 블러 결과를 그대로 알파로 쓰면 크립 전체가 흐리멍덩해진다.
    // 안쪽은 불투명하게 되돌리고 경계만 좁게 남긴다.
    constexpr int kSolidAt = 170; // 이 이상이면 완전 불투명
    constexpr int kClearAt = 60;  // 이 이하면 완전 투명
    for (int y = 0; y < layerH; ++y)
    {
        uchar * line = layer.scanLine(y);
        for (int x = 0; x < layerW; ++x)
        {
            const int v = alpha[static_cast<std::size_t>(y) * layerW + x];
            uchar a = 0;
            if (v >= kSolidAt)      a = 255;
            else if (v <= kClearAt) a = 0;
            else a = static_cast<uchar>(255 * (v - kClearAt) / (kSolidAt - kClearAt));
            line[x * 4 + 3] = a;
        }
    }

    creepLayer_ = QPixmap::fromImage(layer);
    return creepLayer_.isNull() ? nullptr : &creepLayer_;
}

void MapView::paintCreep(QPainter & painter, const QRect & dirty)
{
    const QPixmap * layer = creepLayer();
    if (layer == nullptr)
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const double originX = horizontalScrollBar()->value();
    const double originY = verticalScrollBar()->value();
    const double scaleToScreen = zoom_ * creepLayerScale_;

    // 보이는 부분만 잘라 그린다.
    const QRectF target(dirty);
    const QRectF source((target.left() + originX) / scaleToScreen,
                        (target.top() + originY) / scaleToScreen,
                        target.width() / scaleToScreen,
                        target.height() / scaleToScreen);

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(target, *layer, source);
    painter.restore();
}

const MapView::UnitSprite * MapView::unitSprite(std::uint16_t type, std::uint8_t owner,
                                               std::uint32_t resourceAmount)
{
    if (tileset_ == nullptr || !tileset_->hasUnitGraphics() || document_ == nullptr)
        return nullptr;

    // 자원 유닛은 남은 양에 따라 그래픽 단계가 달라지므로 캐시 키에 넣는다.
    // 단계는 몇 개뿐이라 양 자체를 그대로 쓰면 캐시가 흩어진다 — 구간으로 묶는다.
    const std::uint32_t resourceBucket = resourceAmount == 0 ? 0u
        : (resourceAmount < 250 ? 1u : (resourceAmount < 500 ? 2u : 3u));
    const std::uint32_t key =
        (static_cast<std::uint32_t>(type) << 10) |
        (static_cast<std::uint32_t>(owner) << 2) | resourceBucket;

    auto found = unitCache_.find(key);
    if (found != unitCache_.end())
        return found.value().pixmap.isNull() ? nullptr : &found.value();

    const io::UnitImage image =
        tileset_->renderUnit(type, owner, document_->info().tilesetId, resourceAmount);

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

const MapView::UnitSprite * MapView::mapSprite(std::uint16_t type, std::uint8_t owner,
                                               bool drawnAsSprite)
{
    if (tileset_ == nullptr || !tileset_->hasUnitGraphics() || document_ == nullptr)
        return nullptr;

    const std::uint32_t key = (static_cast<std::uint32_t>(type) << 9) |
                              (static_cast<std::uint32_t>(owner) << 1) |
                              (drawnAsSprite ? 1u : 0u);

    auto found = spriteCache_.find(key);
    if (found != spriteCache_.end())
        return found.value().pixmap.isNull() ? nullptr : &found.value();

    const io::UnitImage image =
        tileset_->renderSprite(type, owner, document_->info().tilesetId, drawnAsSprite);

    UnitSprite sprite;
    if (image.width > 0 && image.height > 0)
    {
        const QImage qimage(image.rgba.data(), image.width, image.height,
                            image.width * 4, QImage::Format_RGBA8888);
        sprite.pixmap = QPixmap::fromImage(qimage.copy());
        sprite.anchorX = image.anchorX;
        sprite.anchorY = image.anchorY;
    }

    auto inserted = spriteCache_.insert(key, sprite);
    return inserted.value().pixmap.isNull() ? nullptr : &inserted.value();
}

void MapView::paintUnits(QPainter & painter, const QRect & dirty)
{
    const auto & units = document_->units();
    const auto & sprites = document_->sprites();
    if (units.empty() && sprites.empty())
        return;

    painter.save();

    // 맵 스프라이트(THG2)를 먼저 — 대개 나무·바위 같은 배경 장식이다.
    for (const auto & sprite : sprites)
    {
        const UnitSprite * pixmap = mapSprite(sprite.type, sprite.owner, sprite.drawnAsSprite);
        if (pixmap == nullptr)
            continue;

        const QPointF topLeft =
            mapToScreen(sprite.x - pixmap->anchorX, sprite.y - pixmap->anchorY);
        const QRectF bounds(topLeft.x(), topLeft.y(),
                            pixmap->pixmap.width() * zoom_,
                            pixmap->pixmap.height() * zoom_);
        if (!dirty.intersects(bounds.toAlignedRect().adjusted(-1, -1, 1, 1)))
            continue;

        painter.drawPixmap(bounds, pixmap->pixmap,
                           QRectF(0, 0, pixmap->pixmap.width(), pixmap->pixmap.height()));
    }

    // 스프라이트가 없는 유닛(또는 그래픽 미로드)을 위한 대체 표시 크기.
    const double diameter = std::clamp(16.0 * zoom_, 3.0, 48.0);
    const double radius = diameter / 2.0;

    for (const auto & unit : units)
    {
        const UnitSprite * sprite = unitSprite(unit.type, unit.owner, unit.resourceAmount);

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
