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
#include <QKeyEvent>
#include <QMouseEvent>
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

    // 버튼을 누르지 않아도 커서를 따라 미리보기를 그려야 한다.
    viewport()->setMouseTracking(true);

    // 스크롤이 움직이면 미니맵이 따라와야 한다. 매 페인트마다 알리는 것보다
    // 스크롤 신호에 붙이는 편이 싸다.
    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
            this, &MapView::viewportMoved);
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MapView::viewportMoved);
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
    // 플레이어 색은 맵마다 다르다. 유닛 그림 캐시를 비우는 이 자리에서
    // 함께 읽어 둔다 — 색만 바뀌어도 그림이 전부 달라지기 때문이다.
    playerColors_ = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    if (document_ != nullptr && document_->isOpen())
    {
        const auto settings = document_->playerSettings();
        for (std::size_t i = 0; i < settings.size() && i < playerColors_.size(); ++i)
            playerColors_[i] = settings[i].color;
    }

    fogPreviewReady_ = false;
    pathAreasReady_ = false;
    aiTownsReady_ = false;
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

void MapView::refreshUnits()
{
    fogPreviewReady_ = false;

    // 크립은 저그 건물이 어디 있는지에 달렸으므로 다시 셈해야 한다.
    creepTiles_.clear();
    creepMask_.clear();
    creepLayer_ = QPixmap();
    creepLayerReady_ = false;
    creepReady_ = false;

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
    emit viewportMoved();

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

void MapView::setGridVisible(bool visible)
{
    if (showGrid_ == visible)
        return;
    showGrid_ = visible;
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
    emit viewportMoved();
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

    // 지형 브러시로 칠하는 중이면 그 자리를 미리 보여 준다.
    if (!strokeTiles_.empty())
    {
        const double tileSize = scaledTileSize();
        const int ox = horizontalScrollBar()->value();
        const int oy = verticalScrollBar()->value();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(120, 200, 255, 90));
        for (const auto & [tx, ty] : strokeTiles_)
        {
            painter.drawRect(QRectF(tx * tileSize - ox, ty * tileSize - oy,
                                    tileSize, tileSize));
        }
    }

    // 크립은 지형 위, 유닛 아래.
    if (showCreep_)
        paintCreep(painter, dirty);
    // 격자는 지형 위, 나머지 아래. 좌표를 가늠하는 용도라 옅게 긋는다.
    // 칸 크기는 타일과 별개로 고를 수 있다 (8 ~ 128 픽셀).
    const double gridStep = gridSize_ * zoom_;
    if (showGrid_ && gridStep >= 4.0)
    {
        painter.save();
        painter.setPen(QPen(gridColor_, 1.0));

        const int firstLineX = static_cast<int>((originX + dirty.left()) / gridStep);
        const int lastLineX = static_cast<int>((originX + dirty.right()) / gridStep);
        const int firstLineY = static_cast<int>((originY + dirty.top()) / gridStep);
        const int lastLineY = static_cast<int>((originY + dirty.bottom()) / gridStep);

        for (int line = firstLineX; line <= lastLineX + 1; ++line)
        {
            const double x = line * gridStep - originX;
            painter.drawLine(QPointF(x, dirty.top()), QPointF(x, dirty.bottom()));
        }
        for (int line = firstLineY; line <= lastLineY + 1; ++line)
        {
            const double y = line * gridStep - originY;
            painter.drawLine(QPointF(dirty.left(), y), QPointF(dirty.right(), y));
        }
        painter.restore();
    }

    paintTerrainOverlay(painter, dirty);
    paintPathAreas(painter, dirty);

    paintFogPreview(painter, dirty);

    if (showFog_)
        paintFog(painter, dirty);
    if (showLocations_)
        paintLocations(painter, dirty);
    if (showUnits_)
    {
        paintUnits(painter, dirty);
        paintUnitLinks(painter);
    }

    paintPlacementPreview(painter);
    paintTerrainCursor(painter);
    paintSelectionBox(painter);
    paintTerrainSelection(painter);
    paintTileValues(painter, dirty);
    paintAiTowns(painter);
    paintPylonRanges(painter);
    paintUnitRanges(painter);
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

    // 2) 크립이 실제로 덮는 모양대로 알파를 깎는다.
    //
    // 경계는 건물마다의 타원을 그대로 쓴다. 타일 격자에 맞추면 네모나게
    // 보이고, 흐리면 뿌옇게 보인다. 대신 크립이 넘어가면 안 되는 지형은
    // 이미 마스크가 막고 있다(1단계에서 그 타일은 비어 있다).
    std::vector<std::uint8_t> shape(
        static_cast<std::size_t>(layerW) * layerH, 0);

    for (const auto & unit : document_->units())
    {
        const auto range = tileset_->creepRange(unit.type);
        if (range.radiusX <= 0.0 || range.radiusY <= 0.0)
            continue;

        const double cx = static_cast<double>(unit.x) / scale;
        const double cy = static_cast<double>(unit.y) / scale;
        const double rx = range.radiusX / scale;
        const double ry = range.radiusY / scale;

        const int x0 = std::max(0, static_cast<int>(cx - rx));
        const int x1 = std::min(layerW - 1, static_cast<int>(cx + rx) + 1);
        const int y0 = std::max(0, static_cast<int>(cy - ry));
        const int y1 = std::min(layerH - 1, static_cast<int>(cy + ry) + 1);

        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const double dx = (x - cx) / rx;
                const double dy = (y - cy) / ry;
                const double d2 = dx * dx + dy * dy;
                if (d2 > 1.0)
                    continue;

                // 가장자리 한 겹만 부드럽게 — 타원 테두리에서 서서히 사라진다.
                const double edge = (d2 > 0.82) ? (1.0 - d2) / 0.18 : 1.0;
                const int alpha = static_cast<int>(255 * edge);
                std::uint8_t & slot = shape[static_cast<std::size_t>(y) * layerW + x];
                slot = static_cast<std::uint8_t>(std::max<int>(slot, alpha));
            }
        }
    }

    for (int y = 0; y < layerH; ++y)
    {
        uchar * line = layer.scanLine(y);
        for (int x = 0; x < layerW; ++x)
        {
            const int existing = line[x * 4 + 3]; // 크립 타일이 깔린 자리인지
            const int wanted = shape[static_cast<std::size_t>(y) * layerW + x];
            line[x * 4 + 3] = static_cast<uchar>(existing == 0 ? 0 : wanted);
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

    // 크립 아래 지형을 살필 때는 옅게 그린다.
    if (creepTranslucent_)
        painter.setOpacity(0.35);

    painter.drawPixmap(target, *layer, source);
    painter.restore();
}

const MapView::UnitSprite * MapView::unitSprite(std::uint16_t type, std::uint8_t owner,
                                               std::uint32_t resourceAmount,
                                               std::uint16_t stateFlags,
                                               std::uint16_t relationFlags)
{
    if (tileset_ == nullptr || !tileset_->hasUnitGraphics() || document_ == nullptr)
        return nullptr;

    // 자원 유닛은 남은 양에 따라 그래픽 단계가 달라지므로 캐시 키에 넣는다.
    // 단계는 몇 개뿐이라 양 자체를 그대로 쓰면 캐시가 흩어진다 — 구간으로 묶는다.
    const std::uint32_t resourceBucket = resourceAmount == 0 ? 0u
        : (resourceAmount < 250 ? 1u : (resourceAmount < 500 ? 2u : 3u));
    // 상태에 따라 그림이 달라지므로 캐시 키에 함께 넣는다. 그림을 바꾸는
    // 비트만 추린다 — 나머지까지 넣으면 캐시가 쓸데없이 흩어진다.
    constexpr std::uint16_t kDrawingStates = 0x01 | 0x02 | 0x04 | 0x08; // 은폐·버로우·떠 있음·환영
    constexpr std::uint16_t kAddonLink = 0x0400;

    const std::uint32_t stateKey = (stateFlags & kDrawingStates) |
                                   ((relationFlags & kAddonLink) != 0 ? 0x10u : 0u);

    const std::uint32_t key =
        (static_cast<std::uint32_t>(type) << 15) |
        (stateKey << 10) |
        (static_cast<std::uint32_t>(owner) << 2) | resourceBucket;

    auto found = unitCache_.find(key);
    if (found != unitCache_.end())
        return found.value().pixmap.isNull() ? nullptr : &found.value();

    const io::UnitImage image =
        tileset_->renderUnit(type, owner, document_->info().tilesetId, resourceAmount,
                             stateFlags, relationFlags, unitColorIndex(owner));

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

std::uint8_t MapView::unitColorIndex(std::uint8_t owner) const
{
    const std::uint8_t color = owner < playerColors_.size() ? playerColors_[owner] : owner;
    return document_ != nullptr && document_->isOpen()
        ? io::tilesetPlayerColor(document_->info().tilesetId, color)
        : color;
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
        tileset_->renderSprite(type, owner, document_->info().tilesetId, drawnAsSprite,
                               unitColorIndex(owner));

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

        // 고른 스프라이트는 테두리로 알린다.
        if (&sprite == &sprites[static_cast<std::size_t>(std::max(0, selectedSprite_))] &&
            selectedSprite_ >= 0)
        {
            painter.setPen(QPen(QColor(120, 200, 255), 1.5, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(bounds);
        }
    }

    // 스프라이트가 없는 유닛(또는 그래픽 미로드)을 위한 대체 표시 크기.
    const double diameter = std::clamp(16.0 * zoom_, 3.0, 48.0);
    const double radius = diameter / 2.0;

    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const auto & unit = units[index];

        // 시야를 열려고 잔뜩 놓은 리빌러는 감출 수 있다.
        constexpr std::uint16_t kMapRevealer = 101;
        if (hideRevealers_ && unit.type == kMapRevealer)
            continue;

        // 여럿 고른 것 가운데 하나여도 고른 것으로 그린다.
        const bool isSelected =
            std::find(selectedUnits_.begin(), selectedUnits_.end(),
                      static_cast<int>(index)) != selectedUnits_.end() ||
            static_cast<int>(index) == selectedUnit_;

        // 드래그 중인 유닛은 손을 따라 움직이는 위치에 그린다. 여럿을
        // 골랐으면 각자 자기 자리에서 같은 거리만큼 옮겨 보여 준다 —
        // 미리보기 좌표를 그대로 쓰면 모두 한 점에 겹친다.
        const int previewDx = hasPreview_ ? previewPos_.x() - dragStartUnitPos_.x() : 0;
        const int previewDy = hasPreview_ ? previewPos_.y() - dragStartUnitPos_.y() : 0;

        const int drawX = (isSelected && hasPreview_)
            ? std::max(0, static_cast<int>(unit.x) + previewDx) : unit.x;
        const int drawY = (isSelected && hasPreview_)
            ? std::max(0, static_cast<int>(unit.y) + previewDy) : unit.y;

        const UnitSprite * sprite = unitSprite(unit.type, unit.owner, unit.resourceAmount,
                                              unit.stateFlags, unit.relationFlags);

        if (sprite != nullptr)
        {
            // 스프라이트는 유닛 중심(anchor)을 기준으로 놓인다.
            const QPointF topLeft =
                mapToScreen(drawX - sprite->anchorX, drawY - sprite->anchorY);
            const QRectF bounds(topLeft.x(), topLeft.y(),
                                sprite->pixmap.width() * zoom_,
                                sprite->pixmap.height() * zoom_);

            // 화면 밖이면 건너뛴다 — 유닛이 수천 개인 맵이 흔하다.
            if (!dirty.intersects(bounds.toAlignedRect().adjusted(-2, -2, 2, 2)))
                continue;

            painter.drawPixmap(bounds, sprite->pixmap,
                               QRectF(0, 0, sprite->pixmap.width(), sprite->pixmap.height()));

            if (isSelected)
            {
                painter.setPen(QPen(QColor(90, 255, 120), 1.5, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(bounds);
            }
            continue;
        }

        const QPointF center = mapToScreen(drawX, drawY);
        const QRectF bounds(center.x() - radius, center.y() - radius, diameter, diameter);
        if (!dirty.intersects(bounds.toAlignedRect().adjusted(-2, -2, 2, 2)))
            continue;

        const chk::PlayerColor color = chk::playerColor(unitColorIndex(unit.owner));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(QColor(color.r, color.g, color.b));
        painter.setPen(isSelected ? QPen(QColor(90, 255, 120), 1.5)
                                  : QPen(QColor(0, 0, 0, 160), 1.0));
        painter.drawEllipse(bounds);
    }

    painter.restore();
}

void MapView::setTool(Tool tool)
{
    if (tool != tool_)
    {
        hasHover_ = false;
        placingDrag_ = false;
        lastPlaced_ = QPoint(-1, -1);
        lastNydusUnit_ = -1;

        // 지형 도구를 벗어나면 고른 네모와 붙여넣기 브러시를 거둔다.
        if (tool != Tool::SelectTerrain)
        {
            terrainSelecting_ = false;
            terrainSelection_ = QRect();
            terrainRegion_ = QRegion();
            pastingTerrain_ = false;
        }
    }

    if (tool_ == tool)
        return;
    tool_ = tool;
    clearSelection();
    viewport()->setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    viewport()->update();
}

void MapView::setTerrainMode(TerrainMode mode)
{
    terrainMode_ = mode;
}

void MapView::setIsomTerrainType(std::size_t brushIndex)
{
    isomTerrainType_ = brushIndex;
}

void MapView::setPlacementUnit(std::uint16_t unitType, std::uint8_t owner)
{
    placeUnitType_ = unitType;
    placeUnitOwner_ = owner;
}

void MapView::setPlacementSprite(std::uint16_t spriteType, std::uint8_t owner)
{
    placeSpriteType_ = spriteType;
    placeUnitOwner_ = owner;
}

void MapView::setBrushTile(std::uint16_t tileId)
{
    if (brushTile_ == tileId)
        return;
    brushTile_ = tileId;
    emit brushTileChanged(tileId);
}

void MapView::setUnitSnap(UnitSnap snap)
{
    unitSnap_ = snap;
}

void MapView::setUnitStackingAllowed(bool allowed)
{
    allowStack_ = allowed;
}

QPoint MapView::snapUnitPos(std::uint16_t unitType, int x, int y) const
{
    int step = 0;
    switch (unitSnap_)
    {
        case UnitSnap::Free:     return QPoint(std::max(0, x), std::max(0, y));
        case UnitSnap::Quarter:  step = io::kTilePixels / 4; break;
        case UnitSnap::HalfTile: step = io::kTilePixels / 2; break;
        case UnitSnap::Tile:     step = io::kTilePixels; break;
    }

    // 게임은 유닛 좌표를 중심으로 다루지만, 건물이 타일에 딱 맞아 보이는
    // 것은 배치 상자의 왼쪽·위 모서리가 타일 경계에 붙기 때문이다.
    //
    // 기준은 그래픽 경계(unitBounds)가 아니라 배치 상자(units.dat 의
    // StarEdit placement box)다. 그래픽은 건물마다 삐죽 튀어나온 부분이
    // 달라 그것으로 맞추면 게임과 어긋난다 — 커맨드 센터는 배치 상자가
    // 128x96 인데 그래픽 경계는 왼쪽 58 픽셀이라 14 픽셀이 밀린다.
    int halfWidth = 0;
    int halfHeight = 0;

    if (tileset_ != nullptr)
    {
        const auto box = tileset_->placementBox(unitType);
        if (box.width > 0 && box.height > 0)
        {
            halfWidth = box.width / 2;
            halfHeight = box.height / 2;
        }
        else
        {
            // 배치 상자가 없는 것(건물이 아닌 유닛)은 그래픽 경계로 맞춘다.
            const auto bounds = tileset_->unitBounds(unitType);
            halfWidth = bounds.left;
            halfHeight = bounds.up;
        }
    }

    const int left = x - halfWidth;
    const int top = y - halfHeight;

    const int snappedLeft = static_cast<int>(std::lround(double(left) / step)) * step;
    const int snappedTop = static_cast<int>(std::lround(double(top) / step)) * step;

    return QPoint(std::max(0, snappedLeft + halfWidth),
                  std::max(0, snappedTop + halfHeight));
}

bool MapView::unitWouldOverlap(std::uint16_t unitType, int x, int y, int skipIndex) const
{
    if (document_ == nullptr || tileset_ == nullptr)
        return false;

    const auto bounds = tileset_->unitBounds(unitType);
    const int left = x - bounds.left;
    const int right = x + bounds.right;
    const int top = y - bounds.up;
    const int bottom = y + bounds.down;

    const auto & units = document_->units();
    for (std::size_t i = 0; i < units.size(); ++i)
    {
        if (static_cast<int>(i) == skipIndex)
            continue;

        const auto & other = units[i];
        const auto otherBounds = tileset_->unitBounds(other.type);

        const int otherLeft = static_cast<int>(other.x) - otherBounds.left;
        const int otherRight = static_cast<int>(other.x) + otherBounds.right;
        const int otherTop = static_cast<int>(other.y) - otherBounds.up;
        const int otherBottom = static_cast<int>(other.y) + otherBounds.down;

        if (left <= otherRight && right >= otherLeft &&
            top <= otherBottom && bottom >= otherTop)
            return true;
    }
    return false;
}

void MapView::autoLinkPlaced(std::size_t placedIndex)
{
    if (document_ == nullptr || tileset_ == nullptr)
        return;

    const auto & units = document_->units();
    if (placedIndex >= units.size())
        return;

    const auto & placed = units[placedIndex];
    auto * doc = const_cast<chk::MapDocument *>(document_);

    // --- 나이더스 굴: 이어서 놓은 둘을 잇는다 ---
    constexpr std::uint16_t kNydusCanal = 134;
    if (placed.type == kNydusCanal)
    {
        if (lastNydusUnit_ >= 0 && lastNydusUnit_ < static_cast<int>(units.size()) &&
            static_cast<std::size_t>(lastNydusUnit_) != placedIndex &&
            units[static_cast<std::size_t>(lastNydusUnit_)].type == kNydusCanal &&
            units[static_cast<std::size_t>(lastNydusUnit_)].relationFlags == 0)
        {
            if (doc->linkUnits(static_cast<std::size_t>(lastNydusUnit_), placedIndex,
                               /*addon*/ false))
            {
                // 굴은 둘씩 짝지으므로 짝이 찼으면 다음 굴을 기다린다.
                lastNydusUnit_ = -1;
                emit documentEdited();
                return;
            }
        }

        lastNydusUnit_ = static_cast<int>(placedIndex);
        return;
    }

    // --- 애드온: 왼쪽에 닿는 본체 건물에 붙인다 ---
    const auto placedClass = tileset_->unitClass(placed.type);
    if (!placedClass.addon)
        return;

    // 애드온마다 붙을 수 있는 건물이 정해져 있다. 게임 규칙이라 자료에
    // 없어 표로 적어 둔다 — 아무 건물에나 붙이면 게임이 이상하게 읽는다.
    const auto hostFor = [](std::uint16_t addonType) -> std::uint16_t {
        switch (addonType)
        {
            case 107: return 106; // 콤샛 스테이션 <- 커맨드 센터
            case 108: return 106; // 핵 사일로     <- 커맨드 센터
            case 115: return 114; // 컨트롤 타워   <- 스타포트
            case 117: return 116; // 코버트 옵스   <- 사이언스 퍼실리티
            case 118: return 116; // 피직스 랩     <- 사이언스 퍼실리티
            case 120: return 113; // 머신 숍       <- 팩토리
            default:  return 0;
        }
    };

    const std::uint16_t hostType = hostFor(placed.type);
    if (hostType == 0)
        return;

    // 애드온이 붙는 자리는 정해져 있다 — 본체 배치 상자의 오른쪽에 붙고
    // 바닥이 맞는다. units.dat 의 애드온 오프셋은 비어 있어(게임이 자리를
    // 직접 안다) 배치 상자로 셈한다.
    const auto addonBox = tileset_->placementBox(placed.type);
    if (addonBox.width <= 0 || addonBox.height <= 0)
        return;

    int best = -1;
    QPoint bestSpot;

    for (std::size_t i = 0; i < units.size(); ++i)
    {
        if (i == placedIndex)
            continue;

        const auto & other = units[i];
        if (other.type != hostType || other.relationFlags != 0)
            continue;

        const auto hostBox = tileset_->placementBox(other.type);
        if (hostBox.width <= 0 || hostBox.height <= 0)
            continue;

        // 본체 배치 상자의 왼쪽 위.
        const int hostLeft = static_cast<int>(other.x) - hostBox.width / 2;
        const int hostTop = static_cast<int>(other.y) - hostBox.height / 2;

        // 애드온 상자의 왼쪽 위는 본체 오른쪽, 바닥 맞춤이다.
        const int addonLeft = hostLeft + hostBox.width;
        const int addonTop = hostTop + (hostBox.height - addonBox.height);

        const QPoint spot(addonLeft + addonBox.width / 2, addonTop + addonBox.height / 2);

        // 애드온이 붙는 자리에 정확히 놓았을 때만 잇는다. 자리를 몰래
        // 옮겨 주면 어디에 놓았는지와 저장되는 좌표가 달라진다.
        if (spot.x() != static_cast<int>(placed.x) || spot.y() != static_cast<int>(placed.y))
            continue;

        best = static_cast<int>(i);
        bestSpot = spot;
        break;
    }

    if (best < 0)
        return;

    if (doc->linkUnits(static_cast<std::size_t>(best), placedIndex, /*addon*/ true))
        emit documentEdited();
}

void MapView::focusLocation(std::size_t index)
{
    if (document_ == nullptr)
        return;

    const auto & locations = document_->locations();
    if (index >= locations.size())
        return;

    const auto & location = locations[index];
    centerOnMap(QPointF((location.left + location.right) / 2.0,
                        (location.top + location.bottom) / 2.0));

    selectedLocation_ = static_cast<int>(index);
    selectedUnit_ = -1;
    emit selectionChanged(-1);
    viewport()->update();
}

void MapView::setUnitLinksVisible(bool visible)
{
    showLinks_ = visible;
    viewport()->update();
}

void MapView::paintUnitLinks(QPainter & painter)
{
    if (!showLinks_ || document_ == nullptr)
        return;

    const auto & units = document_->units();
    if (units.empty())
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const double scale = tile / io::kTilePixels;
    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    // classId 로 상대를 찾는다. 맵마다 유닛이 수천이라 표를 한 번 만든다.
    QHash<std::uint32_t, int> byClassId;
    byClassId.reserve(static_cast<int>(units.size()));
    for (int i = 0; i < static_cast<int>(units.size()); ++i)
    {
        const auto & unit = units[static_cast<std::size_t>(i)];
        if (unit.classId != 0)
            byClassId.insert(unit.classId, i);
    }

    constexpr std::uint16_t kNydusLink = 0x0200;
    constexpr std::uint16_t kAddonLink = 0x0400;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < static_cast<int>(units.size()); ++i)
    {
        const auto & unit = units[static_cast<std::size_t>(i)];
        if (unit.relationFlags == 0 || unit.relationClassId == unit.classId)
            continue;

        const auto found = byClassId.find(unit.relationClassId);
        if (found == byClassId.end())
            continue;

        // 짝을 한 번만 그린다.
        if (found.value() < i)
            continue;

        const auto & other = units[static_cast<std::size_t>(found.value())];

        const QPointF from(unit.x * scale - originX, unit.y * scale - originY);
        const QPointF to(other.x * scale - originX, other.y * scale - originY);

        // 애드온은 노랑, 나이더스는 보라로 구분한다.
        const bool addon = (unit.relationFlags & kAddonLink) != 0;
        const bool nydus = (unit.relationFlags & kNydusLink) != 0;
        const QColor colour = addon ? QColor(255, 220, 90) : QColor(190, 130, 255);
        if (!addon && !nydus)
            continue;

        QPen pen(colour, 1.5);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(from, to);

        // 양 끝에 작은 표를 찍어 어느 유닛이 이어졌는지 분명히 한다.
        painter.setBrush(colour);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(from, 3.0, 3.0);
        painter.drawEllipse(to, 3.0, 3.0);
    }

    painter.restore();
}

void MapView::setFogVisible(bool visible)
{
    showFog_ = visible;
    viewport()->update();
}

void MapView::setFogPlayers(std::uint8_t players)
{
    fogPlayers_ = players;
}

void MapView::setFogErasing(bool erasing)
{
    fogErase_ = erasing;
}

void MapView::setTerrainOverlay(TerrainOverlay overlay)
{
    overlay_ = overlay;
    viewport()->update();
}

void MapView::setTileValuesVisible(bool visible)
{
    showTileValues_ = visible;
    viewport()->update();
}

void MapView::paintTerrainOverlay(QPainter & painter, const QRect & dirty)
{
    if (overlay_ == TerrainOverlay::None || document_ == nullptr || tileset_ == nullptr)
        return;

    const auto & info = document_->info();
    const auto & tiles = document_->tiles();
    if (tiles.empty())
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    const int firstX = std::max(0, static_cast<int>((originX + dirty.left()) / tile));
    const int firstY = std::max(0, static_cast<int>((originY + dirty.top()) / tile));
    const int lastX = std::min<int>(info.width - 1,
                                    static_cast<int>((originX + dirty.right()) / tile));
    const int lastY = std::min<int>(info.height - 1,
                                    static_cast<int>((originY + dirty.bottom()) / tile));

    painter.save();

    for (int ty = firstY; ty <= lastY; ++ty)
    {
        for (int tx = firstX; tx <= lastX; ++tx)
        {
            const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
            if (index >= tiles.size())
                continue;

            const auto terrain = tileset_->tileTerrain(info.tilesetId, tiles[index]);

            QColor shade;
            switch (overlay_)
            {
                case TerrainOverlay::Elevation:
                    // 낮을수록 어둡게, 높을수록 밝게.
                    shade = terrain.elevation == 2 ? QColor(255, 240, 150, 90)
                          : terrain.elevation == 1 ? QColor(150, 200, 255, 80)
                                                   : QColor(40, 60, 100, 70);
                    break;

                case TerrainOverlay::Walkable:
                    shade = terrain.fullyWalkable ? QColor(60, 220, 90, 70)
                          : terrain.walkable      ? QColor(230, 200, 60, 80)
                                                  : QColor(220, 50, 50, 90);
                    break;

                case TerrainOverlay::Buildable:
                    shade = terrain.buildable ? QColor(60, 220, 90, 70)
                                              : QColor(220, 50, 50, 80);
                    break;

                case TerrainOverlay::Creep:
                    if (!terrain.creep)
                        continue;
                    shade = QColor(180, 70, 200, 80);
                    break;

                case TerrainOverlay::None:
                    continue;
            }

            painter.fillRect(QRectF(tx * tile - originX, ty * tile - originY, tile, tile), shade);
        }
    }

    painter.restore();
}

void MapView::setUnitRangesVisible(bool visible)
{
    showRanges_ = visible;
    viewport()->update();
}

void MapView::setPylonRangeVisible(bool visible)
{
    showPylons_ = visible;
    viewport()->update();
}

std::size_t MapView::stackSelectedUnits(int copies)
{
    if (document_ == nullptr || !document_->isOpen() || copies <= 0)
        return 0;

    std::vector<int> targets = selectedUnits_;
    if (targets.empty() && selectedUnit_ >= 0)
        targets.push_back(selectedUnit_);
    if (targets.empty())
        return 0;

    auto * doc = const_cast<chk::MapDocument *>(document_);

    // 원본을 먼저 베껴 둔다 — 놓는 동안 목록이 자란다.
    std::vector<chk::MapUnit> originals;
    originals.reserve(targets.size());
    for (int index : targets)
    {
        if (index >= 0 && index < static_cast<int>(document_->units().size()))
            originals.push_back(document_->units()[static_cast<std::size_t>(index)]);
    }

    std::size_t placed = 0;
    for (const auto & unit : originals)
    {
        for (int copy = 0; copy < copies; ++copy)
        {
            if (doc->addUnit(unit.type, unit.owner, unit.x, unit.y))
                ++placed;
        }
    }

    if (placed > 0)
    {
        refreshUnits();
        emit documentEdited();
    }
    return placed;
}

void MapView::selectAllUnits()
{
    if (document_ == nullptr || !document_->isOpen())
        return;

    const auto & units = document_->units();
    selectedUnits_.clear();
    selectedUnits_.reserve(units.size());
    for (int i = 0; i < static_cast<int>(units.size()); ++i)
        selectedUnits_.push_back(i);

    selectedUnit_ = selectedUnits_.empty() ? -1 : selectedUnits_.back();
    selectedLocation_ = -1;
    emit selectionChanged(selectedUnit_);
    viewport()->update();
}

bool MapView::jumpToStartLocation(std::uint8_t player)
{
    if (document_ == nullptr || !document_->isOpen())
        return false;

    // 시작 위치는 유닛 214 로 놓인다.
    constexpr std::uint16_t kStartLocation = 214;

    for (const auto & unit : document_->units())
    {
        if (unit.type != kStartLocation || unit.owner != player)
            continue;

        centerOnMap(QPointF(unit.x, unit.y));
        return true;
    }
    return false;
}

QImage MapView::renderToImage() const
{
    if (document_ == nullptr || !document_->isOpen() || tileset_ == nullptr)
        return QImage();

    const auto & info = document_->info();
    const int width = info.width * io::kTilePixels;
    const int height = info.height * io::kTilePixels;
    if (width <= 0 || height <= 0)
        return QImage();

    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::black);

    const auto & tiles = document_->tiles();
    std::vector<std::uint8_t> rgba(io::kTileRgbaBytes);

    // 지형을 원래 크기로 그린다 — 화면 배율과 무관하게 같은 그림이 나온다.
    for (int ty = 0; ty < info.height; ++ty)
    {
        for (int tx = 0; tx < info.width; ++tx)
        {
            const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
            const std::uint16_t tileId = (index < tiles.size()) ? tiles[index] : 0;

            if (!tileset_->renderTile(info.tilesetId, tileId, rgba.data()))
                continue;

            const QImage tileImage(rgba.data(), io::kTilePixels, io::kTilePixels,
                                   io::kTilePixels * 4, QImage::Format_RGBA8888);

            QPainter painter(&image);
            painter.drawImage(QPoint(tx * io::kTilePixels, ty * io::kTilePixels), tileImage);
        }
    }

    // 유닛도 얹는다 — 그림만 보고 맵 구성을 알 수 있어야 쓸모가 있다.
    QPainter painter(&image);
    for (const auto & unit : document_->units())
    {
        const io::UnitImage drawn = tileset_->renderUnit(
            unit.type, unit.owner, info.tilesetId, unit.resourceAmount,
            unit.stateFlags, unit.relationFlags);

        if (drawn.width <= 0 || drawn.height <= 0)
            continue;

        const QImage sprite(drawn.rgba.data(), drawn.width, drawn.height,
                            drawn.width * 4, QImage::Format_RGBA8888);

        painter.drawImage(QPoint(unit.x - drawn.anchorX, unit.y - drawn.anchorY), sprite);
    }

    return image;
}

void MapView::setAiTownsVisible(bool visible)
{
    showAiTowns_ = visible;
    aiTownsReady_ = false;
    viewport()->update();
}

void MapView::paintAiTowns(QPainter & painter)
{
    if (!showAiTowns_ || document_ == nullptr || !document_->isOpen())
        return;

    if (!aiTownsReady_)
    {
        aiTowns_ = document_->aiTownLocations();
        aiTownsReady_ = true;
    }

    if (aiTowns_.empty())
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const double scale = tile / io::kTilePixels;
    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    const auto & locations = document_->locations();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (std::size_t townId : aiTowns_)
    {
        // 로케이션 번호(MRGN)로 목록에서 찾는다.
        const auto found = std::find_if(locations.begin(), locations.end(),
            [townId](const auto & entry) { return entry.index == townId; });
        if (found == locations.end())
            continue;

        const QRectF box(found->left * scale - originX, found->top * scale - originY,
                         (found->right - found->left) * scale,
                         (found->bottom - found->top) * scale);

        painter.fillRect(box, QColor(255, 140, 60, 40));
        painter.setPen(QPen(QColor(255, 160, 80, 230), 2, Qt::DashDotLine));
        painter.drawRect(box);

        painter.setPen(QColor(255, 200, 150));
        painter.drawText(box.adjusted(4, 4, -4, -4), Qt::AlignTop | Qt::AlignLeft,
                         tr("AI 타운"));
    }

    painter.restore();
}

void MapView::setCreepTranslucent(bool translucent)
{
    creepTranslucent_ = translucent;
    creepLayerReady_ = false; // 알파가 달라지므로 층을 다시 만든다
    creepLayer_ = QPixmap();
    viewport()->update();
}

void MapView::setRevealersHidden(bool hidden)
{
    hideRevealers_ = hidden;
    viewport()->update();
}

void MapView::setPathAreasVisible(bool visible)
{
    showPathAreas_ = visible;
    pathAreasReady_ = false;
    viewport()->update();
}

void MapView::buildPathAreas()
{
    pathAreas_.clear();
    pathAreaCount_ = 0;
    pathAreasReady_ = true;

    if (document_ == nullptr || !document_->isOpen() || tileset_ == nullptr)
        return;

    const auto & info = document_->info();
    const auto & tiles = document_->tiles();
    if (tiles.empty() || info.width <= 0 || info.height <= 0)
        return;

    const std::size_t cells = static_cast<std::size_t>(info.width) * info.height;
    pathAreas_.assign(cells, 0);

    // 걸을 수 있는 칸을 이어 붙여 영역으로 묶는다. 게임의 길찾기와 같지는
    // 않지만(대각선·유닛 크기를 따지지 않는다) 막힌 자리는 드러난다.
    std::vector<bool> walkable(cells, false);
    for (std::size_t i = 0; i < cells && i < tiles.size(); ++i)
        walkable[i] = tileset_->tileTerrain(info.tilesetId, tiles[i]).walkable;

    std::vector<std::size_t> stack;
    for (std::size_t start = 0; start < cells; ++start)
    {
        if (!walkable[start] || pathAreas_[start] != 0)
            continue;

        ++pathAreaCount_;
        stack.clear();
        stack.push_back(start);
        pathAreas_[start] = pathAreaCount_;

        while (!stack.empty())
        {
            const std::size_t at = stack.back();
            stack.pop_back();

            const int x = static_cast<int>(at % info.width);
            const int y = static_cast<int>(at / info.width);

            const std::pair<int, int> neighbours[4] {
                {x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}
            };

            for (const auto & [nx, ny] : neighbours)
            {
                if (nx < 0 || ny < 0 || nx >= info.width || ny >= info.height)
                    continue;

                const std::size_t next = static_cast<std::size_t>(ny) * info.width + nx;
                if (!walkable[next] || pathAreas_[next] != 0)
                    continue;

                pathAreas_[next] = pathAreaCount_;
                stack.push_back(next);
            }
        }
    }
}

void MapView::paintPathAreas(QPainter & painter, const QRect & dirty)
{
    if (!showPathAreas_ || document_ == nullptr)
        return;

    if (!pathAreasReady_)
        buildPathAreas();

    if (pathAreas_.empty())
        return;

    const auto & info = document_->info();
    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    const int firstX = std::max(0, static_cast<int>((originX + dirty.left()) / tile));
    const int firstY = std::max(0, static_cast<int>((originY + dirty.top()) / tile));
    const int lastX = std::min<int>(info.width - 1,
                                    static_cast<int>((originX + dirty.right()) / tile));
    const int lastY = std::min<int>(info.height - 1,
                                    static_cast<int>((originY + dirty.bottom()) / tile));

    painter.save();
    for (int ty = firstY; ty <= lastY; ++ty)
    {
        for (int tx = firstX; tx <= lastX; ++tx)
        {
            const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
            if (index >= pathAreas_.size())
                continue;

            const int area = pathAreas_[index];
            if (area == 0)
                continue;

            // 영역마다 다른 색을 준다. 이어진 곳은 같은 색이라 한눈에 들어온다.
            const int hue = (area * 67) % 360;
            QColor colour = QColor::fromHsv(hue, 200, 255, 70);

            painter.fillRect(QRectF(tx * tile - originX, ty * tile - originY, tile, tile),
                             colour);
        }
    }
    painter.restore();
}

void MapView::setFogPreviewVisible(bool visible)
{
    showFogPreview_ = visible;
    fogPreviewReady_ = false;
    viewport()->update();
}

void MapView::setFogPreviewPlayer(std::uint8_t player)
{
    fogPreviewPlayer_ = player;
    fogPreviewReady_ = false;
    viewport()->update();
}

void MapView::buildFogPreview()
{
    fogPreview_.clear();
    fogPreviewReady_ = true;

    if (document_ == nullptr || !document_->isOpen() || tileset_ == nullptr)
        return;

    const auto & info = document_->info();
    if (info.width <= 0 || info.height <= 0)
        return;

    // 처음에는 모두 가려져 있고, 그 플레이어의 유닛이 보는 칸을 걷어 낸다.
    fogPreview_.assign(static_cast<std::size_t>(info.width) * info.height, true);

    for (const auto & unit : document_->units())
    {
        if (unit.owner != fogPreviewPlayer_)
            continue;

        const auto ranges = tileset_->unitRanges(unit.type);
        if (ranges.sight <= 0)
            continue;

        const int centreX = static_cast<int>(unit.x) / io::kTilePixels;
        const int centreY = static_cast<int>(unit.y) / io::kTilePixels;
        const int radius = ranges.sight / io::kTilePixels;

        const int left = std::max(0, centreX - radius);
        const int right = std::min(info.width - 1, centreX + radius);
        const int top = std::max(0, centreY - radius);
        const int bottom = std::min(info.height - 1, centreY + radius);

        for (int y = top; y <= bottom; ++y)
        {
            for (int x = left; x <= right; ++x)
            {
                // 게임은 시야를 원으로 친다. 지형 높이에 따른 가림은 게임
                // 실행 파일 안에 있어 여기서는 셈하지 않는다 — 대략만 본다.
                const int dx = x - centreX;
                const int dy = y - centreY;
                if (dx * dx + dy * dy > radius * radius)
                    continue;

                fogPreview_[static_cast<std::size_t>(y) * info.width + x] = false;
            }
        }
    }
}

void MapView::paintFogPreview(QPainter & painter, const QRect & dirty)
{
    if (!showFogPreview_ || document_ == nullptr)
        return;

    if (!fogPreviewReady_)
        buildFogPreview();

    if (fogPreview_.empty())
        return;

    const auto & info = document_->info();
    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    const int firstX = std::max(0, static_cast<int>((originX + dirty.left()) / tile));
    const int firstY = std::max(0, static_cast<int>((originY + dirty.top()) / tile));
    const int lastX = std::min<int>(info.width - 1,
                                    static_cast<int>((originX + dirty.right()) / tile));
    const int lastY = std::min<int>(info.height - 1,
                                    static_cast<int>((originY + dirty.bottom()) / tile));

    painter.save();
    for (int ty = firstY; ty <= lastY; ++ty)
    {
        for (int tx = firstX; tx <= lastX; ++tx)
        {
            const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
            if (index >= fogPreview_.size() || !fogPreview_[index])
                continue;

            painter.fillRect(QRectF(tx * tile - originX, ty * tile - originY, tile, tile),
                             QColor(0, 0, 0, 150));
        }
    }
    painter.restore();
}

void MapView::paintPylonRanges(QPainter & painter)
{
    if (!showPylons_ || document_ == nullptr)
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const double scale = tile / io::kTilePixels;
    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    // 파일런 전력 범위는 게임 자료에 없다. 널리 알려진 크기를 쓴다 —
    // 파일런을 가운데 두고 가로 16, 세로 10 타일의 타원이다.
    constexpr std::uint16_t kPylon = 156;
    constexpr double kHalfWidth = 8.0 * io::kTilePixels;
    constexpr double kHalfHeight = 5.0 * io::kTilePixels;

    const auto & units = document_->units();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const auto & unit : units)
    {
        if (unit.type != kPylon)
            continue;

        const QPointF centre(unit.x * scale - originX, unit.y * scale - originY);

        painter.setBrush(QColor(120, 160, 255, 30));
        painter.setPen(QPen(QColor(150, 190, 255, 180), 1.5, Qt::DashLine));
        painter.drawEllipse(centre, kHalfWidth * scale, kHalfHeight * scale);
    }

    painter.restore();
}

void MapView::paintUnitRanges(QPainter & painter)
{
    if (!showRanges_ || document_ == nullptr || tileset_ == nullptr)
        return;

    // 고른 유닛만 보여 준다 — 맵에 있는 모든 유닛에 원을 그리면 아무것도
    // 알아볼 수 없다.
    std::vector<int> targets = selectedUnits_;
    if (targets.empty() && selectedUnit_ >= 0)
        targets.push_back(selectedUnit_);
    if (targets.empty())
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const double scale = tile / io::kTilePixels;
    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    const auto & units = document_->units();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);

    for (int index : targets)
    {
        if (index < 0 || index >= static_cast<int>(units.size()))
            continue;

        const auto & unit = units[static_cast<std::size_t>(index)];
        const auto ranges = tileset_->unitRanges(unit.type);

        const QPointF centre(unit.x * scale - originX, unit.y * scale - originY);

        const auto circle = [&](int radius, const QColor & colour, Qt::PenStyle style) {
            if (radius <= 0)
                return;
            painter.setPen(QPen(colour, 1.5, style));
            painter.drawEllipse(centre, radius * scale, radius * scale);
        };

        // 시야는 옅게, 사거리는 진하게. 탐지는 따로 색을 준다.
        circle(ranges.sight, QColor(120, 200, 255, 120), Qt::DotLine);
        circle(ranges.detection, QColor(255, 220, 90, 150), Qt::DashDotLine);
        circle(ranges.groundWeapon, QColor(255, 120, 120, 200), Qt::SolidLine);

        // 공중 사거리가 지상과 같으면 겹쳐 그릴 필요가 없다.
        if (ranges.airWeapon != ranges.groundWeapon)
            circle(ranges.airWeapon, QColor(150, 255, 150, 190), Qt::DashLine);
    }

    painter.restore();
}

void MapView::paintTileValues(QPainter & painter, const QRect & dirty)
{
    if (!showTileValues_ || document_ == nullptr)
        return;

    const double tile = scaledTileSize();

    // 글자가 들어갈 자리가 없으면 그리지 않는다.
    if (tile < 28)
        return;

    const auto & info = document_->info();
    const auto & tiles = document_->tiles();
    if (tiles.empty())
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    const int firstX = std::max(0, static_cast<int>((originX + dirty.left()) / tile));
    const int firstY = std::max(0, static_cast<int>((originY + dirty.top()) / tile));
    const int lastX = std::min<int>(info.width - 1,
                                    static_cast<int>((originX + dirty.right()) / tile));
    const int lastY = std::min<int>(info.height - 1,
                                    static_cast<int>((originY + dirty.bottom()) / tile));

    painter.save();

    QFont small = painter.font();
    small.setPointSizeF(std::max(6.0, tile / 4.0));
    painter.setFont(small);

    for (int ty = firstY; ty <= lastY; ++ty)
    {
        for (int tx = firstX; tx <= lastX; ++tx)
        {
            const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
            if (index >= tiles.size())
                continue;

            const QRectF cell(tx * tile - originX, ty * tile - originY, tile, tile);

            // 어떤 지형 위에서도 읽히도록 검은 테두리를 깔고 흰 글자를 얹는다.
            const QString text = QString::number(tiles[index]);
            painter.setPen(QColor(0, 0, 0, 200));
            painter.drawText(cell.adjusted(1, 1, 1, 1), Qt::AlignCenter, text);
            painter.setPen(QColor(240, 240, 245));
            painter.drawText(cell, Qt::AlignCenter, text);
        }
    }

    painter.restore();
}

void MapView::paintFog(QPainter & painter, const QRect & dirty)
{
    const auto & fog = document_->fogTiles();
    if (fog.empty())
        return;

    const auto & info = document_->info();
    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    const int firstX = std::max(0, static_cast<int>((originX + dirty.left()) / tile));
    const int firstY = std::max(0, static_cast<int>((originY + dirty.top()) / tile));
    const int lastX = std::min<int>(info.width - 1,
                                    static_cast<int>((originX + dirty.right()) / tile));
    const int lastY = std::min<int>(info.height - 1,
                                    static_cast<int>((originY + dirty.bottom()) / tile));

    painter.save();
    for (int ty = firstY; ty <= lastY; ++ty)
    {
        for (int tx = firstX; tx <= lastX; ++tx)
        {
            const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
            if (index >= fog.size())
                continue;

            const std::uint8_t players = fog[index];
            if (players == 0)
                continue;

            // 지금 고른 플레이어에게 가려진 칸은 진하게, 다른 플레이어만
            // 가려진 칸은 옅게 — 누구의 가리개인지 구분할 수 있어야 한다.
            const bool mine = (players & fogPlayers_) != 0;
            const QColor shade = mine ? QColor(0, 0, 0, 130) : QColor(40, 60, 120, 70);

            const QRectF cell(tx * tile - originX, ty * tile - originY, tile, tile);
            painter.fillRect(cell, shade);
        }
    }
    painter.restore();
}

void MapView::paintFogAt(const QPointF & screenPos)
{
    if (document_ == nullptr || !document_->isOpen())
        return;

    const QPointF mapPos = screenToMap(screenPos);
    const int centreX = static_cast<int>(mapPos.x()) / io::kTilePixels;
    const int centreY = static_cast<int>(mapPos.y()) / io::kTilePixels;

    const auto & info = document_->info();
    const auto & fog = document_->fogTiles();

    // 지형 브러시와 같은 크기로 칠한다.
    const int half = brushSize_ / 2;
    std::vector<std::pair<int, int>> cells;
    cells.reserve(static_cast<std::size_t>(brushSize_) * brushSize_);

    std::uint8_t sample = 0;
    bool haveSample = false;

    for (int dy = 0; dy < brushSize_; ++dy)
    {
        for (int dx = 0; dx < brushSize_; ++dx)
        {
            const int tx = centreX - half + dx;
            const int ty = centreY - half + dy;
            if (tx < 0 || ty < 0 || tx >= info.width || ty >= info.height)
                continue;

            if (!haveSample)
            {
                const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
                sample = (index < fog.size()) ? fog[index] : std::uint8_t(0);
                haveSample = true;
            }
            cells.emplace_back(tx, ty);
        }
    }

    if (cells.empty())
        return;

    // 칠하기는 고른 플레이어의 비트만 건드린다. 다른 플레이어의 가리개는
    // 그대로 둔다.
    const std::uint8_t next = fogErase_ ? std::uint8_t(sample & ~fogPlayers_)
                                        : std::uint8_t(sample | fogPlayers_);
    if (next == sample && brushSize_ == 1)
        return;

    auto * doc = const_cast<chk::MapDocument *>(document_);

    // 브러시가 여러 칸이면 칸마다 값이 다를 수 있으므로 한 칸씩 계산한다.
    if (brushSize_ == 1)
    {
        if (!doc->setFogTiles(cells, next))
            return;
    }
    else
    {
        for (const auto & cell : cells)
        {
            const std::size_t index =
                static_cast<std::size_t>(cell.second) * info.width + cell.first;
            const std::uint8_t before = (index < fog.size()) ? fog[index] : std::uint8_t(0);
            const std::uint8_t value = fogErase_ ? std::uint8_t(before & ~fogPlayers_)
                                                 : std::uint8_t(before | fogPlayers_);
            if (value != before)
                doc->setFogTiles({cell}, value);
        }
    }

    viewport()->update();
    emit documentEdited();
}

void MapView::setTerrainCheckEnabled(bool enabled)
{
    checkTerrain_ = enabled;
    viewport()->update();
}

void MapView::setGroundUnitCheckEnabled(bool enabled)
{
    checkGroundUnits_ = enabled;
    viewport()->update();
}

bool MapView::terrainAccepts(std::uint16_t unitType, int x, int y) const
{
    if (document_ == nullptr || tileset_ == nullptr)
        return true;

    const auto unitInfo = tileset_->unitClass(unitType);
    const auto & info = document_->info();
    const auto & tiles = document_->tiles();
    if (tiles.empty())
        return true;

    // --- 지상 유닛: 걸을 수 있는 땅인지 ---
    if (!unitInfo.building)
    {
        if (!checkGroundUnits_ || unitInfo.flyer)
            return true;

        const int tileX = x / io::kTilePixels;
        const int tileY = y / io::kTilePixels;
        if (tileX < 0 || tileY < 0 || tileX >= info.width || tileY >= info.height)
            return false;

        const std::size_t index = static_cast<std::size_t>(tileY) * info.width + tileX;
        if (index >= tiles.size())
            return false;

        // 한 타일은 4x4 칸으로 나뉘고 칸마다 걷기 여부가 다르다. 가장자리
        // 한 칸만 걸을 수 있는 자리에 놓으면 게임에서 갇히므로, 절반
        // 이상 걸을 수 있는 타일만 받는다.
        const auto terrain = tileset_->tileTerrain(info.tilesetId, tiles[index]);
        return terrain.fullyWalkable || terrain.walkable;
    }

    // --- 건물: 지을 수 있는 땅인지 ---
    if (!checkTerrain_)
        return true;

    const auto box = tileset_->placementBox(unitType);
    if (box.width <= 0 || box.height <= 0)
        return true;

    // 배치 상자는 유닛 좌표를 가운데로 둔다.
    const int left = (x - box.width / 2) / io::kTilePixels;
    const int top = (y - box.height / 2) / io::kTilePixels;
    const int right = (x + box.width / 2 - 1) / io::kTilePixels;
    const int bottom = (y + box.height / 2 - 1) / io::kTilePixels;

    if (left < 0 || top < 0 || right >= info.width || bottom >= info.height)
        return false;

    // 모든 칸이 지을 수 있는 땅이고 높이가 같아야 한다 — 게임과 같은 규칙이다.
    int elevation = -1;
    for (int ty = top; ty <= bottom; ++ty)
    {
        for (int tx = left; tx <= right; ++tx)
        {
            const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
            if (index >= tiles.size())
                return false;

            const auto terrain = tileset_->tileTerrain(info.tilesetId, tiles[index]);
            if (!terrain.buildable)
                return false;

            if (elevation < 0)
                elevation = terrain.elevation;
            else if (terrain.elevation != elevation)
                return false;
        }
    }
    return true;
}

bool MapView::canPlaceAt(int x, int y) const
{
    const std::uint16_t type =
        (tool_ == Tool::PlaceSprite) ? placeSpriteType_ : placeUnitType_;

    // 스프라이트는 장식이라 겹침·지형을 따지지 않는다.
    if (tool_ == Tool::PlaceSprite)
        return true;

    if (!allowStack_ && unitWouldOverlap(type, x, y))
        return false;
    if (!terrainAccepts(type, x, y))
        return false;
    return true;
}

void MapView::paintPlacementPreview(QPainter & painter)
{
    if (!hasHover_ || document_ == nullptr || tileset_ == nullptr)
        return;
    if (tool_ != Tool::PlaceUnit && tool_ != Tool::PlaceSprite)
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();
    const double scale = tile / io::kTilePixels;

    const std::uint16_t type =
        (tool_ == Tool::PlaceSprite) ? placeSpriteType_ : placeUnitType_;

    painter.save();

    // --- 건물이면 게임의 건설 화면처럼 놓일 칸을 격자로 보여 준다 ---
    const auto unitInfo = tileset_->unitClass(type);
    const auto box = tileset_->placementBox(type);
    if (tool_ == Tool::PlaceUnit && unitInfo.building && box.width > 0 && box.height > 0)
    {
        const int left = hoverPos_.x() - box.width / 2;
        const int top = hoverPos_.y() - box.height / 2;
        const int cols = std::max(1, box.width / io::kTilePixels);
        const int rows = std::max(1, box.height / io::kTilePixels);

        const QColor fill = hoverValid_ ? QColor(0, 220, 0, 60) : QColor(220, 0, 0, 70);
        const QColor line = hoverValid_ ? QColor(0, 255, 0, 190) : QColor(255, 60, 60, 190);

        for (int row = 0; row < rows; ++row)
        {
            for (int col = 0; col < cols; ++col)
            {
                const double x = (left + col * io::kTilePixels) * scale - originX;
                const double y = (top + row * io::kTilePixels) * scale - originY;
                const QRectF cell(x, y, tile, tile);

                painter.fillRect(cell, fill);
                painter.setPen(QPen(line, 1));
                painter.drawRect(cell);
            }
        }
    }

    // --- 유닛·스프라이트 그림을 반투명하게 겹쳐 그린다 ---
    const UnitSprite * sprite = (tool_ == Tool::PlaceSprite)
        ? mapSprite(type, placeUnitOwner_, /*drawnAsSprite*/ true)
        : unitSprite(type, placeUnitOwner_, /*resourceAmount*/ 0);

    if (sprite != nullptr && !sprite->pixmap.isNull())
    {
        const QPixmap & pixmap = sprite->pixmap;
        const double left = (hoverPos_.x() - sprite->anchorX) * scale - originX;
        const double top = (hoverPos_.y() - sprite->anchorY) * scale - originY;

        painter.setOpacity(hoverValid_ ? 0.65 : 0.35);
        painter.drawPixmap(QRectF(left, top, pixmap.width() * scale, pixmap.height() * scale),
                           pixmap, QRectF(pixmap.rect()));
        painter.setOpacity(1.0);
    }
    else
    {
        // 그림이 없으면 자리만 표시한다.
        const double x = hoverPos_.x() * scale - originX;
        const double y = hoverPos_.y() * scale - originY;
        painter.setPen(QPen(hoverValid_ ? QColor(0, 255, 0) : QColor(255, 60, 60), 2));
        painter.drawEllipse(QPointF(x, y), tile / 2, tile / 2);
    }

    painter.restore();
}

void MapView::setGridSize(int pixels)
{
    gridSize_ = std::clamp(pixels, 8, 128);
    viewport()->update();
}

void MapView::setGridColor(const QColor & colour)
{
    gridColor_ = colour;
    viewport()->update();
}

void MapView::setTerrainSymmetry(Symmetry symmetry)
{
    symmetry_ = symmetry;
    viewport()->update();
}

std::vector<QPoint> MapView::mirrorTiles(int tileX, int tileY) const
{
    std::vector<QPoint> out;
    out.emplace_back(tileX, tileY);

    if (symmetry_ == Symmetry::None || document_ == nullptr)
        return out;

    const auto & info = document_->info();
    const int lastX = info.width - 1;
    const int lastY = info.height - 1;

    const auto add = [&out](int x, int y) {
        for (const QPoint & existing : out)
        {
            if (existing.x() == x && existing.y() == y)
                return;
        }
        out.emplace_back(x, y);
    };

    const bool horizontal = symmetry_ == Symmetry::Horizontal || symmetry_ == Symmetry::Both;
    const bool vertical = symmetry_ == Symmetry::Vertical || symmetry_ == Symmetry::Both;

    if (horizontal)
        add(lastX - tileX, tileY);
    if (vertical)
        add(tileX, lastY - tileY);
    if (horizontal && vertical)
        add(lastX - tileX, lastY - tileY);

    // 돌리기 대칭. 180도는 어떤 맵에서도 되고, 90도는 가로세로가 같아야 한다.
    if (symmetry_ == Symmetry::Rotate180 || symmetry_ == Symmetry::Rotate90)
        add(lastX - tileX, lastY - tileY);

    if (symmetry_ == Symmetry::Rotate90 && info.width == info.height)
    {
        add(lastY - tileY, tileX);
        add(tileY, lastX - tileX);
    }

    return out;
}

std::vector<QPoint> MapView::mirrorPixels(int pixelX, int pixelY) const
{
    std::vector<QPoint> out;
    out.emplace_back(pixelX, pixelY);

    if (symmetry_ == Symmetry::None || document_ == nullptr)
        return out;

    const auto & info = document_->info();
    const int width = info.width * io::kTilePixels;
    const int height = info.height * io::kTilePixels;

    const auto add = [&out](int x, int y) {
        for (const QPoint & existing : out)
        {
            if (existing.x() == x && existing.y() == y)
                return;
        }
        out.emplace_back(x, y);
    };

    const bool horizontal = symmetry_ == Symmetry::Horizontal || symmetry_ == Symmetry::Both;
    const bool vertical = symmetry_ == Symmetry::Vertical || symmetry_ == Symmetry::Both;

    if (horizontal)
        add(width - pixelX, pixelY);
    if (vertical)
        add(pixelX, height - pixelY);
    if (horizontal && vertical)
        add(width - pixelX, height - pixelY);

    if (symmetry_ == Symmetry::Rotate180 || symmetry_ == Symmetry::Rotate90)
        add(width - pixelX, height - pixelY);

    if (symmetry_ == Symmetry::Rotate90 && width == height)
    {
        add(height - pixelY, pixelX);
        add(pixelY, width - pixelX);
    }

    return out;
}

void MapView::paintTerrainCursor(QPainter & painter)
{
    if (!hasHover_ || document_ == nullptr)
        return;
    if (tool_ != Tool::Terrain && tool_ != Tool::PlaceDoodad)
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();
    const double scale = tile / io::kTilePixels;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor line(255, 236, 120, 220);
    const QColor fill(255, 236, 120, 40);

    if (tool_ == Tool::PlaceDoodad)
    {
        // 두들이 덮을 타일 범위를 보여 준다.
        if (tileset_ == nullptr)
        {
            painter.restore();
            return;
        }

        const auto list = tileset_->doodads(document_->info().tilesetId);
        const auto found = std::find_if(list.begin(), list.end(),
            [this](const auto & entry) { return entry.id == placeDoodadId_; });

        if (found == list.end())
        {
            painter.restore();
            return;
        }

        const int centreX = static_cast<int>(hoverPos_.x()) / io::kTilePixels;
        const int centreY = static_cast<int>(hoverPos_.y()) / io::kTilePixels;
        const int left = centreX - found->tileWidth / 2;
        const int top = centreY - found->tileHeight / 2;

        const QRectF box(left * tile - originX, top * tile - originY,
                         tile * found->tileWidth, tile * found->tileHeight);

        painter.fillRect(box, fill);
        painter.setPen(QPen(line, 2));
        painter.drawRect(box);

        painter.restore();
        return;
    }

    if (terrainMode_ == TerrainMode::Isometric)
    {
        // 마름모의 자리와 크기는 io 가 MappingCore 와 같은 식으로 계산해
        // 준다 — 미리보기와 실제로 바뀌는 곳이 어긋나지 않는다.
        const auto centre = io::isomDiamondCentre(hoverPos_.x(), hoverPos_.y(), brushSize_);

        const double centreX = centre.x * scale - originX;
        const double centreY = centre.y * scale - originY;
        const double halfWidth = centre.halfWidth * scale;
        const double halfHeight = centre.halfHeight * scale;

        QPolygonF diamond;
        diamond << QPointF(centreX, centreY - halfHeight)
                << QPointF(centreX + halfWidth, centreY)
                << QPointF(centreX, centreY + halfHeight)
                << QPointF(centreX - halfWidth, centreY);

        painter.setBrush(fill);
        painter.setPen(QPen(line, 2));
        painter.drawPolygon(diamond);
    }
    else
    {
        // 사각형 — 칠할 타일 범위를 그대로 보여 준다.
        const int extent = (terrainMode_ == TerrainMode::Subtile) ? 1 : brushSize_;
        const int half = extent / 2;

        const int centreX = static_cast<int>(hoverPos_.x()) / io::kTilePixels;
        const int centreY = static_cast<int>(hoverPos_.y()) / io::kTilePixels;

        // 대칭이 켜져 있으면 맞은편 자리도 함께 보여 준다.
        for (const QPoint & spot : mirrorTiles(centreX, centreY))
        {
            if (spot.x() == centreX && spot.y() == centreY)
                continue;

            const QRectF mirrored((spot.x() - half) * tile - originX,
                                  (spot.y() - half) * tile - originY,
                                  tile * extent, tile * extent);
            painter.fillRect(mirrored, QColor(255, 236, 120, 24));
            painter.setPen(QPen(QColor(255, 236, 120, 120), 1, Qt::DashLine));
            painter.drawRect(mirrored);
        }

        const QRectF box((centreX - half) * tile - originX,
                         (centreY - half) * tile - originY,
                         tile * extent, tile * extent);

        painter.setBrush(fill);
        painter.setPen(QPen(line, 2));
        painter.drawRect(box);

        // 한 칸짜리는 격자를 함께 그려 어디에 떨어질지 분명히 한다.
        if (extent > 1)
        {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 236, 120, 90), 1));
            for (int i = 1; i < extent; ++i)
            {
                const double x = box.left() + i * tile;
                const double y = box.top() + i * tile;
                painter.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
                painter.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
            }
        }
    }

    painter.restore();
}

bool MapView::placeAt(const QPointF & screenPos)
{
    if (document_ == nullptr || !document_->isOpen())
        return false;

    const QPointF mapPos = screenToMap(screenPos);
    auto * doc = const_cast<chk::MapDocument *>(document_);

    if (tool_ == Tool::PlaceSprite)
    {
        const QPoint pos = snapUnitPos(placeSpriteType_,
                                       static_cast<int>(std::max(0.0, mapPos.x())),
                                       static_cast<int>(std::max(0.0, mapPos.y())));
        if (pos == lastPlaced_)
            return false;

        if (doc->addSprite(placeSpriteType_, placeUnitOwner_,
                           static_cast<std::uint16_t>(pos.x()),
                           static_cast<std::uint16_t>(pos.y()),
                           /*drawnAsSprite*/ true))
        {
            lastPlaced_ = pos;
            refreshUnits();
            emit documentEdited();
            return true;
        }
        return false;
    }

    const QPoint pos = snapUnitPos(placeUnitType_,
                                   static_cast<int>(std::max(0.0, mapPos.x())),
                                   static_cast<int>(std::max(0.0, mapPos.y())));
    if (pos == lastPlaced_)
        return false;

    if (!canPlaceAt(pos.x(), pos.y()))
    {
        // 끌며 놓는 동안에는 잔소리를 하지 않는다.
        if (!placingDrag_)
        {
            QString reason;
            if (!allowStack_ && unitWouldOverlap(placeUnitType_, pos.x(), pos.y()))
            {
                reason = tr("이미 다른 유닛이 있는 자리입니다 — 겹치기를 허용하면 놓을 수 있습니다.");
            }
            else if (tileset_ != nullptr && tileset_->unitClass(placeUnitType_).building)
            {
                reason = tr("이 땅에는 건물을 지을 수 없습니다 — 지형 검사를 끄면 놓을 수 있습니다.");
            }
            else
            {
                reason = tr("지상 유닛이 갈 수 없는 땅입니다 — 지상 유닛 지형 검사를 끄면 놓을 수 있습니다.");
            }
            emit placementRejected(reason);
        }
        return false;
    }

    if (doc->addUnit(placeUnitType_, placeUnitOwner_,
                     static_cast<std::uint16_t>(pos.x()),
                     static_cast<std::uint16_t>(pos.y())))
    {
        lastPlaced_ = pos;

        // 방금 놓은 유닛은 목록 끝에 붙는다.
        if (!document_->units().empty())
            autoLinkPlaced(document_->units().size() - 1);

        refreshUnits();
        emit documentEdited();

        // 끌며 여럿을 놓을 때 소리가 겹치면 시끄럽다. 한 번 끄는 동안
        // 처음 놓을 때만 낸다.
        if (!dragSoundPlayed_)
        {
            dragSoundPlayed_ = true;
            emit unitPlaced(placeUnitType_);
        }
        return true;
    }
    return false;
}

void MapView::setPlacementDoodad(std::uint16_t doodadId)
{
    placeDoodadId_ = doodadId;
}

void MapView::setBrushSize(int size)
{
    brushSize_ = std::clamp(size, 1, 16);
}

void MapView::paintTerrainAt(const QPointF & screenPos)
{
    if (document_ == nullptr || !document_->isOpen())
        return;

    const QPointF mapPos = screenToMap(screenPos);
    const int centerX = static_cast<int>(mapPos.x()) / io::kTilePixels;
    const int centerY = static_cast<int>(mapPos.y()) / io::kTilePixels;

    const auto & info = document_->info();

    // Subtile 은 한 칸씩 정밀하게, Rectangular 는 브러시 크기만큼.
    const int extent = (terrainMode_ == TerrainMode::Subtile) ? 1 : brushSize_;
    const int half = extent / 2;

    for (int dy = 0; dy < extent; ++dy)
    {
        for (int dx = 0; dx < extent; ++dx)
        {
            const int tx = centerX - half + dx;
            const int ty = centerY - half + dy;

            // 대칭이 켜져 있으면 맞은편에도 같이 칠한다.
            for (const QPoint & spot : mirrorTiles(tx, ty))
            {
                if (spot.x() < 0 || spot.y() < 0 ||
                    spot.x() >= info.width || spot.y() >= info.height)
                    continue;

                // 같은 획에서 같은 자리를 여러 번 칠하지 않는다.
                const std::pair<std::size_t, std::size_t> at{
                    static_cast<std::size_t>(spot.x()), static_cast<std::size_t>(spot.y())};
                if (std::find(strokeTiles_.begin(), strokeTiles_.end(), at) == strokeTiles_.end())
                    strokeTiles_.push_back(at);
            }
        }
    }

    viewport()->update();
}

QRectF MapView::visibleMapRect() const
{
    if (zoom_ <= 0)
        return QRectF();

    return QRectF(horizontalScrollBar()->value() / zoom_,
                  verticalScrollBar()->value() / zoom_,
                  viewport()->width() / zoom_,
                  viewport()->height() / zoom_);
}

void MapView::centerOnMap(const QPointF & mapPos)
{
    horizontalScrollBar()->setValue(
        static_cast<int>(mapPos.x() * zoom_ - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(
        static_cast<int>(mapPos.y() * zoom_ - viewport()->height() / 2.0));
    viewport()->update();
    emit viewportMoved();
}

void MapView::clearSelection()
{
    if (selectedUnit_ == -1 && selectedLocation_ == -1 && selectedUnits_.empty())
        return;
    selectedUnits_.clear();
    selectedUnit_ = -1;
    selectedLocation_ = -1;
    emit selectionChanged(-1);
    viewport()->update();
}

void MapView::selectUnitsInBox(const QPointF & fromMap, const QPointF & toMap, bool add)
{
    if (document_ == nullptr)
        return;

    const QRectF box = QRectF(fromMap, toMap).normalized();

    if (!add)
        selectedUnits_.clear();

    const auto & units = document_->units();
    for (std::size_t i = 0; i < units.size(); ++i)
    {
        const auto & unit = units[i];

        // 유닛이 차지하는 자리가 사각형에 걸치면 고른다 — 가운데만 보면
        // 큰 건물을 걸쳐 끌었을 때 빠진다.
        io::GameGraphics::UnitBounds bounds;
        if (tileset_ != nullptr)
            bounds = tileset_->unitBounds(unit.type);

        const QRectF area(unit.x - bounds.left, unit.y - bounds.up,
                          bounds.width(), bounds.height());
        if (!box.intersects(area))
            continue;

        const int index = static_cast<int>(i);
        if (std::find(selectedUnits_.begin(), selectedUnits_.end(), index) == selectedUnits_.end())
            selectedUnits_.push_back(index);
    }

    selectedUnit_ = selectedUnits_.empty() ? -1 : selectedUnits_.back();
    selectedLocation_ = -1;
    emit selectionChanged(selectedUnit_);
}

bool MapView::copyTerrainSelection()
{
    if (document_ == nullptr || !document_->isOpen())
        return false;

    const auto & info = document_->info();
    const auto & tiles = document_->tiles();
    if (tiles.empty())
        return false;

    const QRegion region = terrainRegion_.intersected(QRegion(0, 0, info.width, info.height));
    if (region.isEmpty())
        return false;

    // 여러 덩어리를 골랐으면 그것을 다 감싸는 네모를 크기로 삼고, 덩어리에
    // 들지 않는 칸은 "비었음"으로 남긴다 — 찍을 때 건너뛴다.
    const QRect box = region.boundingRect();

    clipboardWidth_ = box.width();
    clipboardHeight_ = box.height();

    const std::size_t cells = static_cast<std::size_t>(clipboardWidth_) * clipboardHeight_;
    terrainClipboard_.assign(cells, 0);
    clipboardMask_.assign(cells, false);

    bool anyEmpty = false;
    for (int y = 0; y < clipboardHeight_; ++y)
    {
        for (int x = 0; x < clipboardWidth_; ++x)
        {
            const int mapX = box.left() + x;
            const int mapY = box.top() + y;
            const std::size_t at = static_cast<std::size_t>(y) * clipboardWidth_ + x;

            if (!region.contains(QPoint(mapX, mapY)))
            {
                anyEmpty = true;
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(mapY) * info.width + mapX;
            terrainClipboard_[at] = (index < tiles.size()) ? tiles[index] : std::uint16_t(0);
            clipboardMask_[at] = true;
        }
    }

    // 네모 하나만 골랐으면 가리개가 필요 없다.
    if (!anyEmpty)
        clipboardMask_.clear();

    // 담은 지형이 곧 브러시가 된다 — SCMDraft 와 같은 흐름이다.
    pastingTerrain_ = true;
    viewport()->update();
    return true;
}

MapView::TerrainBrush MapView::terrainClipboardBrush() const
{
    TerrainBrush brush;
    if (terrainClipboard_.empty())
        return brush;

    brush.width = clipboardWidth_;
    brush.height = clipboardHeight_;
    brush.tiles = terrainClipboard_;
    brush.mask = clipboardMask_;
    brush.tilesetId = (document_ != nullptr) ? document_->info().tilesetId : 0;
    return brush;
}

void MapView::useTerrainBrush(const TerrainBrush & brush)
{
    if (brush.width <= 0 || brush.height <= 0 || brush.tiles.empty())
        return;

    clipboardWidth_ = brush.width;
    clipboardHeight_ = brush.height;
    terrainClipboard_ = brush.tiles;
    clipboardMask_ = brush.mask;

    // 곧바로 찍을 수 있게 브러시를 켜고 지형 도구로 옮긴다.
    pastingTerrain_ = true;
    if (tool_ != Tool::SelectTerrain)
    {
        tool_ = Tool::SelectTerrain;
        emit toolChanged(tool_);
    }

    viewport()->update();
}

bool MapView::copySelectedLocation()
{
    if (document_ == nullptr || selectedLocation_ < 0)
        return false;

    const auto & locations = document_->locations();
    if (selectedLocation_ >= static_cast<int>(locations.size()))
        return false;

    const auto & location = locations[static_cast<std::size_t>(selectedLocation_)];

    locationClipboard_.valid = true;
    locationClipboard_.width = static_cast<int>(location.right) - static_cast<int>(location.left);
    locationClipboard_.height = static_cast<int>(location.bottom) - static_cast<int>(location.top);
    locationClipboard_.name = location.name;
    locationClipboard_.elevationFlags = location.elevationFlags;
    locationClipboard_.inverted =
        document_->locationInverted(static_cast<std::size_t>(selectedLocation_));

    return true;
}

bool MapView::pasteLocationAt(const QPointF & screenPos)
{
    if (!locationClipboard_.valid || document_ == nullptr || !document_->isOpen())
        return false;

    const QPointF mapPos = screenToMap(screenPos);

    // 담아 둔 크기의 가운데를 커서에 맞춘다.
    const int left = static_cast<int>(mapPos.x()) - locationClipboard_.width / 2;
    const int top = static_cast<int>(mapPos.y()) - locationClipboard_.height / 2;

    auto * doc = const_cast<chk::MapDocument *>(document_);

    std::size_t created = 0;
    if (!doc->addLocation(static_cast<std::uint32_t>(std::max(0, left)),
                          static_cast<std::uint32_t>(std::max(0, top)),
                          static_cast<std::uint32_t>(std::max(0, left + locationClipboard_.width)),
                          static_cast<std::uint32_t>(std::max(0, top + locationClipboard_.height)),
                          locationClipboard_.name, &created))
    {
        emit placementRejected(QString::fromStdString(document_->lastError()));
        return false;
    }

    // 높이와 안팎도 함께 베낀다.
    if (locationClipboard_.elevationFlags != 0)
        doc->setLocationElevationFlags(created, locationClipboard_.elevationFlags);
    if (locationClipboard_.inverted)
        doc->setLocationInverted(created, true);

    selectedLocation_ = static_cast<int>(created);
    selectedUnit_ = -1;
    selectedUnits_.clear();
    emit selectionChanged(-1);
    emit documentEdited();

    viewport()->update();
    return true;
}

bool MapView::pasteLocationAtCentre()
{
    return pasteLocationAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
}

bool MapView::copySelectedSprite()
{
    if (document_ == nullptr || selectedSprite_ < 0)
        return false;

    const auto & sprites = document_->sprites();
    if (selectedSprite_ >= static_cast<int>(sprites.size()))
        return false;

    const auto & sprite = sprites[static_cast<std::size_t>(selectedSprite_)];

    spriteClipboard_.valid = true;
    spriteClipboard_.type = sprite.type;
    spriteClipboard_.owner = sprite.owner;
    spriteClipboard_.drawnAsSprite = sprite.drawnAsSprite;
    return true;
}

bool MapView::pasteSpriteAt(const QPointF & screenPos)
{
    if (!spriteClipboard_.valid || document_ == nullptr || !document_->isOpen())
        return false;

    const QPointF mapPos = screenToMap(screenPos);
    auto * doc = const_cast<chk::MapDocument *>(document_);

    if (!doc->addSprite(spriteClipboard_.type, spriteClipboard_.owner,
                        static_cast<std::uint16_t>(std::max(0.0, mapPos.x())),
                        static_cast<std::uint16_t>(std::max(0.0, mapPos.y())),
                        spriteClipboard_.drawnAsSprite))
    {
        return false;
    }

    refreshUnits();
    emit documentEdited();
    return true;
}

bool MapView::pasteSpriteAtCentre()
{
    return pasteSpriteAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
}

bool MapView::copyFogSelection()
{
    if (document_ == nullptr || !document_->isOpen())
        return false;

    const auto & fog = document_->fogTiles();
    if (fog.empty())
        return false;

    const auto & info = document_->info();
    const QRegion region = terrainRegion_.intersected(QRegion(0, 0, info.width, info.height));
    if (region.isEmpty())
        return false;

    // 가리개도 지형과 같은 네모로 고른다 — 도구를 따로 두지 않는다.
    const QRect box = region.boundingRect();
    fogClipboardWidth_ = box.width();
    fogClipboardHeight_ = box.height();
    fogClipboard_.assign(static_cast<std::size_t>(fogClipboardWidth_) * fogClipboardHeight_, 0);

    for (int y = 0; y < fogClipboardHeight_; ++y)
    {
        for (int x = 0; x < fogClipboardWidth_; ++x)
        {
            const int mapX = box.left() + x;
            const int mapY = box.top() + y;
            if (!region.contains(QPoint(mapX, mapY)))
                continue;

            const std::size_t index = static_cast<std::size_t>(mapY) * info.width + mapX;
            if (index < fog.size())
                fogClipboard_[static_cast<std::size_t>(y) * fogClipboardWidth_ + x] = fog[index];
        }
    }

    return true;
}

bool MapView::pasteFogAt(const QPointF & screenPos)
{
    if (fogClipboard_.empty() || document_ == nullptr || !document_->isOpen())
        return false;

    const QPointF mapPos = screenToMap(screenPos);
    const int centreX = static_cast<int>(std::max(0.0, mapPos.x())) / io::kTilePixels;
    const int centreY = static_cast<int>(std::max(0.0, mapPos.y())) / io::kTilePixels;

    const int left = centreX - fogClipboardWidth_ / 2;
    const int top = centreY - fogClipboardHeight_ / 2;

    const auto & info = document_->info();
    auto * doc = const_cast<chk::MapDocument *>(document_);

    // 값이 같은 칸끼리 묶어 한 번에 쓴다.
    std::map<std::uint8_t, std::vector<std::pair<int, int>>> byValue;
    for (int y = 0; y < fogClipboardHeight_; ++y)
    {
        for (int x = 0; x < fogClipboardWidth_; ++x)
        {
            const int targetX = left + x;
            const int targetY = top + y;
            if (targetX < 0 || targetY < 0 || targetX >= info.width || targetY >= info.height)
                continue;

            const std::uint8_t value =
                fogClipboard_[static_cast<std::size_t>(y) * fogClipboardWidth_ + x];
            byValue[value].emplace_back(targetX, targetY);
        }
    }

    bool wrote = false;
    for (const auto & [value, cells] : byValue)
    {
        if (doc->setFogTiles(cells, value))
            wrote = true;
    }

    if (!wrote)
        return false;

    refresh();
    emit documentEdited();
    return true;
}

bool MapView::pasteFogAtCentre()
{
    return pasteFogAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
}

bool MapView::cutTerrainSelection()
{
    if (!copyTerrainSelection())
        return false;

    const auto & info = document_->info();
    const QRegion region = terrainRegion_.intersected(QRegion(0, 0, info.width, info.height));
    if (region.isEmpty())
        return false;

    std::vector<io::MapArchive::TileWrite> writes;

    // 고른 덩어리에 든 칸만 비운다.
    for (const QRect & rect : region)
    {
        for (int y = rect.top(); y <= rect.bottom(); ++y)
        {
            for (int x = rect.left(); x <= rect.right(); ++x)
            {
                writes.push_back(io::MapArchive::TileWrite{
                    static_cast<std::size_t>(x), static_cast<std::size_t>(y), std::uint16_t(0)});
            }
        }
    }

    auto * doc = const_cast<chk::MapDocument *>(document_);
    if (!doc->writeTiles(writes))
        return false;

    refresh();
    emit documentEdited();
    return true;
}

bool MapView::pasteTerrainAt(const QPointF & screenPos)
{
    if (terrainClipboard_.empty() || document_ == nullptr || !document_->isOpen())
        return false;

    const QPointF mapPos = screenToMap(screenPos);
    const int centreX = static_cast<int>(std::max(0.0, mapPos.x())) / io::kTilePixels;
    const int centreY = static_cast<int>(std::max(0.0, mapPos.y())) / io::kTilePixels;

    // 담은 덩어리의 가운데를 커서에 맞춘다.
    const int left = centreX - clipboardWidth_ / 2;
    const int top = centreY - clipboardHeight_ / 2;

    const auto & info = document_->info();

    std::vector<io::MapArchive::TileWrite> writes;
    writes.reserve(terrainClipboard_.size());

    for (int y = 0; y < clipboardHeight_; ++y)
    {
        for (int x = 0; x < clipboardWidth_; ++x)
        {
            const int targetX = left + x;
            const int targetY = top + y;
            if (targetX < 0 || targetY < 0 || targetX >= info.width || targetY >= info.height)
                continue;

            const std::size_t at = static_cast<std::size_t>(y) * clipboardWidth_ + x;
            if (!clipboardMask_.empty() && !clipboardMask_[at])
                continue; // 고른 덩어리에 들지 않는 칸

            writes.push_back(io::MapArchive::TileWrite{
                static_cast<std::size_t>(targetX), static_cast<std::size_t>(targetY),
                terrainClipboard_[at]});
        }
    }

    if (writes.empty())
        return false;

    auto * doc = const_cast<chk::MapDocument *>(document_);
    if (!doc->writeTiles(writes))
        return false;

    refresh();
    emit documentEdited();
    return true;
}

void MapView::paintTerrainSelection(QPainter & painter)
{
    if (document_ == nullptr)
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    painter.save();

    // 고른 영역 — 떨어진 덩어리가 여러 개일 수 있다.
    const auto drawBox = [&](const QRect & rect, int alphaFill, int alphaLine) {
        if (rect.isEmpty())
            return;
        const QRectF box(rect.left() * tile - originX, rect.top() * tile - originY,
                         rect.width() * tile, rect.height() * tile);
        painter.fillRect(box, QColor(120, 255, 180, alphaFill));
        painter.setPen(QPen(QColor(120, 255, 180, alphaLine), 2));
        painter.drawRect(box);
    };

    for (const QRect & rect : terrainRegion_)
        drawBox(rect, 40, 220);

    // 지금 끌고 있는 네모는 조금 옅게.
    drawBox(terrainSelection_, 30, 160);

    // 붙여넣기 미리보기 — 담아 둔 지형이 커서를 따라온다.
    if (pastingTerrain_ && hasHover_ && !terrainClipboard_.empty() && tileset_ != nullptr)
    {
        const int centreX = static_cast<int>(hoverPos_.x()) / io::kTilePixels;
        const int centreY = static_cast<int>(hoverPos_.y()) / io::kTilePixels;
        const int left = centreX - clipboardWidth_ / 2;
        const int top = centreY - clipboardHeight_ / 2;

        painter.setOpacity(0.7);
        for (int y = 0; y < clipboardHeight_; ++y)
        {
            for (int x = 0; x < clipboardWidth_; ++x)
            {
                const std::size_t at = static_cast<std::size_t>(y) * clipboardWidth_ + x;
                if (!clipboardMask_.empty() && !clipboardMask_[at])
                    continue;

                const QPixmap * pixmap = tilePixmap(terrainClipboard_[at]);
                if (pixmap == nullptr)
                    continue;

                const QRectF cell((left + x) * tile - originX, (top + y) * tile - originY,
                                  tile, tile);
                painter.drawPixmap(cell, *pixmap, QRectF(pixmap->rect()));
            }
        }
        painter.setOpacity(1.0);

        const QRectF box(left * tile - originX, top * tile - originY,
                         clipboardWidth_ * tile, clipboardHeight_ * tile);
        painter.setPen(QPen(QColor(120, 255, 180, 200), 2, Qt::DashLine));
        painter.drawRect(box);
    }

    painter.restore();
}

void MapView::paintSelectionBox(QPainter & painter)
{
    if (!boxSelecting_ && !drawingLocation_)
        return;

    const double tile = scaledTileSize();
    if (tile <= 0)
        return;

    const double scale = tile / io::kTilePixels;
    const int originX = horizontalScrollBar()->value();
    const int originY = verticalScrollBar()->value();

    const QRectF box = QRectF(boxStart_, boxEnd_).normalized();
    const QRectF onScreen(box.left() * scale - originX, box.top() * scale - originY,
                          box.width() * scale, box.height() * scale);

    painter.save();
    painter.fillRect(onScreen, QColor(120, 200, 255, 40));
    painter.setPen(QPen(QColor(120, 200, 255, 220), 1, Qt::DashLine));
    painter.drawRect(onScreen);
    painter.restore();
}

int MapView::locationEdgeAt(const QPointF & screenPos, int locationIndex) const
{
    if (document_ == nullptr || locationIndex < 0)
        return 0;

    const auto & locations = document_->locations();
    if (locationIndex >= static_cast<int>(locations.size()))
        return 0;

    const auto & location = locations[static_cast<std::size_t>(locationIndex)];
    const QPointF mapPos = screenToMap(screenPos);

    // 잡기 쉬우라고 화면에서 일정한 굵기가 되도록 배율을 되돌린다.
    const double grab = std::max(3.0, 6.0 / std::max(0.1, zoom_));

    int edges = 0;
    if (std::abs(mapPos.x() - static_cast<double>(location.left)) <= grab)
        edges |= EdgeLeft;
    if (std::abs(mapPos.x() - static_cast<double>(location.right)) <= grab)
        edges |= EdgeRight;
    if (std::abs(mapPos.y() - static_cast<double>(location.top)) <= grab)
        edges |= EdgeTop;
    if (std::abs(mapPos.y() - static_cast<double>(location.bottom)) <= grab)
        edges |= EdgeBottom;

    // 사각형 밖이면 모서리를 잡은 것이 아니다.
    if (edges != 0)
    {
        const bool insideX = mapPos.x() >= location.left - grab &&
                             mapPos.x() <= location.right + grab;
        const bool insideY = mapPos.y() >= location.top - grab &&
                             mapPos.y() <= location.bottom + grab;
        if (!insideX || !insideY)
            edges = 0;
    }

    return edges;
}

int MapView::locationAt(const QPointF & screenPos) const
{
    if (document_ == nullptr || !document_->isOpen() || !showLocations_)
        return -1;

    const auto & locations = document_->locations();
    int best = -1;
    double bestArea = 0;

    for (std::size_t i = 0; i < locations.size(); ++i)
    {
        const auto & location = locations[i];
        const QPointF topLeft = mapToScreen(location.left, location.top);
        const QPointF bottomRight = mapToScreen(location.right, location.bottom);
        const QRectF bounds(topLeft, bottomRight);
        if (!bounds.contains(screenPos))
            continue;

        const double area = bounds.width() * bounds.height();
        if (best < 0 || area < bestArea)
        {
            best = static_cast<int>(i);
            bestArea = area;
        }
    }
    return best;
}

QPointF MapView::screenToMap(const QPointF & screen) const
{
    if (zoom_ <= 0)
        return QPointF();
    return QPointF((screen.x() + horizontalScrollBar()->value()) / zoom_,
                   (screen.y() + verticalScrollBar()->value()) / zoom_);
}

int MapView::unitAt(const QPointF & screenPos)
{
    if (document_ == nullptr || !document_->isOpen())
        return -1;

    const auto & units = document_->units();

    // 뒤에서부터 본다 — 나중에 그려진(위에 있는) 유닛이 먼저 잡혀야 한다.
    for (int i = static_cast<int>(units.size()) - 1; i >= 0; --i)
    {
        const auto & unit = units[static_cast<std::size_t>(i)];
        const UnitSprite * sprite = unitSprite(unit.type, unit.owner, unit.resourceAmount,
                                              unit.stateFlags, unit.relationFlags);

        QRectF bounds;
        if (sprite != nullptr)
        {
            const QPointF topLeft =
                mapToScreen(unit.x - sprite->anchorX, unit.y - sprite->anchorY);
            bounds = QRectF(topLeft.x(), topLeft.y(),
                            sprite->pixmap.width() * zoom_,
                            sprite->pixmap.height() * zoom_);
        }
        else
        {
            const double d = std::clamp(16.0 * zoom_, 3.0, 48.0);
            const QPointF center = mapToScreen(unit.x, unit.y);
            bounds = QRectF(center.x() - d / 2, center.y() - d / 2, d, d);
        }

        if (bounds.contains(screenPos))
            return i;
    }
    return -1;
}

int MapView::spriteAt(const QPointF & screenPos)
{
    if (document_ == nullptr || !document_->isOpen())
        return -1;

    const auto & sprites = document_->sprites();

    // 뒤에서부터 본다 — 나중에 그려진 것이 먼저 잡혀야 한다.
    for (int i = static_cast<int>(sprites.size()) - 1; i >= 0; --i)
    {
        const auto & sprite = sprites[static_cast<std::size_t>(i)];
        const UnitSprite * pixmap = mapSprite(sprite.type, sprite.owner, sprite.drawnAsSprite);
        if (pixmap == nullptr)
            continue;

        const QPointF topLeft =
            mapToScreen(sprite.x - pixmap->anchorX, sprite.y - pixmap->anchorY);
        const QRectF bounds(topLeft.x(), topLeft.y(),
                            pixmap->pixmap.width() * zoom_,
                            pixmap->pixmap.height() * zoom_);

        if (bounds.contains(screenPos))
            return i;
    }
    return -1;
}

void MapView::mousePressEvent(QMouseEvent * event)
{
    // 오른쪽 단추는 "그만두기"다. 놓거나 칠하던 것을 멈추고 선택 도구로
    // 돌아간다 — 팔레트에서 고른 유닛을 취소하려고 메뉴까지 갈 일이 없다.
    if (event->button() == Qt::RightButton)
    {
        const bool wasPlacing = (tool_ != Tool::Select);

        placingDrag_ = false;
        fogPainting_ = false;
        painting_ = false;
        isomPainting_ = false;
        hasHover_ = false;
        lastPlaced_ = QPoint(-1, -1);
        lastNydusUnit_ = -1;

        if (tool_ == Tool::SelectTerrain &&
            (pastingTerrain_ || !terrainRegion_.isEmpty() || !terrainSelection_.isEmpty()))
        {
            // 지형 도구에서는 붙여넣기 브러시를 먼저 거둔다.
            pastingTerrain_ = false;
            terrainSelection_ = QRect();
            terrainRegion_ = QRegion();
            viewport()->update();
            event->accept();
            return;
        }

        if (wasPlacing)
        {
            // 놓거나 칠하던 것을 그만두고 선택 도구로 돌아간다.
            tool_ = Tool::Select;
            emit toolChanged(tool_);
            viewport()->update();
            event->accept();
            return;
        }

        // 선택 도구에서는 누른 것을 집고 맥락 메뉴를 띄운다.
        if (document_ != nullptr && document_->isOpen())
        {
            const int unitHit = unitAt(event->position());
            const int locationHit = (unitHit >= 0) ? -1 : locationAt(event->position());

            if (unitHit != selectedUnit_ || locationHit != selectedLocation_)
            {
                selectedUnit_ = unitHit;
                selectedLocation_ = locationHit;
                emit selectionChanged(unitHit);
            }

            viewport()->update();
            emit contextMenuRequested(event->globalPosition().toPoint(), unitHit, locationHit);
            event->accept();
            return;
        }

        clearSelection();
        viewport()->update();
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton || document_ == nullptr || !document_->isOpen())
    {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    if (tool_ == Tool::Fog)
    {
        fogPainting_ = true;
        paintFogAt(event->position());
        event->accept();
        return;
    }

    if (tool_ == Tool::Location)
    {
        const int hit = locationAt(event->position());

        // 고른 로케이션의 모서리를 잡으면 늘인다.
        const int edges = locationEdgeAt(event->position(), hit);
        if (hit >= 0 && edges != 0)
        {
            const auto & location = document_->locations()[static_cast<std::size_t>(hit)];
            resizingLocation_ = hit;
            resizeEdges_ = edges;
            locationStart_ = QRect(QPoint(static_cast<int>(location.left),
                                          static_cast<int>(location.top)),
                                   QPoint(static_cast<int>(location.right),
                                          static_cast<int>(location.bottom)));

            selectedLocation_ = hit;
            selectedUnit_ = -1;
            selectedUnits_.clear();
            emit selectionChanged(-1);
            event->accept();
            return;
        }

        if (hit >= 0)
        {
            // 안쪽을 잡으면 옮긴다.
            selectedLocation_ = hit;
            selectedUnit_ = -1;
            selectedUnits_.clear();
            emit selectionChanged(-1);

            dragging_ = true;
            dragStartMap_ = screenToMap(event->position());
            const auto & location = document_->locations()[static_cast<std::size_t>(hit)];
            dragStartUnitPos_ = QPoint(static_cast<int>(location.left),
                                       static_cast<int>(location.top));
            viewport()->update();
            event->accept();
            return;
        }

        // 빈 곳을 끌면 새로 그린다.
        drawingLocation_ = true;
        boxStart_ = screenToMap(event->position());
        boxEnd_ = boxStart_;
        viewport()->update();
        event->accept();
        return;
    }

    if (tool_ == Tool::SelectTerrain)
    {
        // 붙여넣기 브러시가 켜져 있으면 찍고, 아니면 새로 고른다.
        if (pastingTerrain_ && !terrainClipboard_.empty())
        {
            pasteTerrainAt(event->position());
            event->accept();
            return;
        }

        const QPointF mapPos = screenToMap(event->position());
        const int tileX = static_cast<int>(std::max(0.0, mapPos.x())) / io::kTilePixels;
        const int tileY = static_cast<int>(std::max(0.0, mapPos.y())) / io::kTilePixels;

        // Shift 를 누르고 끌면 고른 영역에 덩어리를 더한다.
        if (!(event->modifiers() & Qt::ShiftModifier))
            terrainRegion_ = QRegion();

        terrainSelecting_ = true;
        terrainSelection_ = QRect(tileX, tileY, 1, 1);
        boxStart_ = QPointF(tileX, tileY);

        viewport()->update();
        event->accept();
        return;
    }

    if (tool_ == Tool::PlaceDoodad)
    {
        const QPointF mapPos = screenToMap(event->position());
        const int tileX = static_cast<int>(std::max(0.0, mapPos.x())) / io::kTilePixels;
        const int tileY = static_cast<int>(std::max(0.0, mapPos.y())) / io::kTilePixels;

        auto * doc = const_cast<chk::MapDocument *>(document_);
        if (tileset_ != nullptr && doc->placeDoodad(*tileset_, placeDoodadId_, tileX, tileY))
        {
            refresh(); // 지형 타일이 바뀌므로 통째로 다시 그린다
            emit documentEdited();
        }
        else
        {
            emit placementRejected(QString::fromStdString(document_->lastError()));
        }
        event->accept();
        return;
    }

    if (tool_ == Tool::PlaceSprite || tool_ == Tool::PlaceUnit)
    {
        // 누른 채 끌면 이어서 놓는다. 같은 자리에 겹쳐 놓지 않도록
        // 마지막으로 놓은 자리를 기억한다.
        lastPlaced_ = QPoint(-1, -1);
        placingDrag_ = true;
        dragSoundPlayed_ = false;
        placeAt(event->position());
        event->accept();
        return;
    }

    if (tool_ == Tool::Terrain)
    {
        // Alt 를 누른 채 찍으면 그 자리의 타일을 브러시로 집는다(스포이드).
        if (event->modifiers() & Qt::AltModifier)
        {
            const QPointF mapPos = screenToMap(event->position());
            const int tx = static_cast<int>(mapPos.x()) / io::kTilePixels;
            const int ty = static_cast<int>(mapPos.y()) / io::kTilePixels;
            const auto & info = document_->info();
            const auto & tiles = document_->tiles();
            if (tx >= 0 && ty >= 0 && tx < info.width && ty < info.height)
            {
                const std::size_t index = static_cast<std::size_t>(ty) * info.width + tx;
                if (index < tiles.size())
                    setBrushTile(tiles[index]);
            }
            event->accept();
            return;
        }

        // ISOM 은 한 번의 클릭이 여러 타일을 한꺼번에 바꾼다. 획을 모으지
        // 않고 바로 적용하되, 끌고 다니는 동안 계속 찍히게 한다.
        if (terrainMode_ == TerrainMode::Isometric)
        {
            isomPainting_ = true;
            lastIsomTile_ = QPoint(-1, -1);
            applyIsomAt(event->position());
            event->accept();
            return;
        }

        painting_ = true;
        strokeTiles_.clear();
        paintTerrainAt(event->position());
        event->accept();
        return;
    }

    // 유닛이 로케이션보다 위에 있다 — 겹치면 유닛을 먼저 집는다.
    const int unitHit = unitAt(event->position());
    const int locationHit = (unitHit >= 0) ? -1 : locationAt(event->position());

    const bool adding = (event->modifiers() & Qt::ShiftModifier) != 0;

    if (unitHit >= 0)
    {
        const auto found = std::find(selectedUnits_.begin(), selectedUnits_.end(), unitHit);
        const bool already = found != selectedUnits_.end();

        if (adding)
        {
            // Shift 로 누르면 고른 것에 넣거나 뺀다.
            if (already)
                selectedUnits_.erase(found);
            else
                selectedUnits_.push_back(unitHit);
        }
        else if (!already)
        {
            // 고르지 않은 것을 누르면 그것만 고른다. 이미 고른 것을
            // 누르면 여럿 고른 채로 두어 함께 끌 수 있게 한다.
            selectedUnits_.assign(1, unitHit);
        }

        selectedUnit_ = selectedUnits_.empty() ? -1 : selectedUnits_.back();
        selectedLocation_ = -1;
        selectedSprite_ = -1;
        emit selectionChanged(selectedUnit_);
    }
    else if (locationHit >= 0)
    {
        selectedUnits_.clear();
        selectedUnit_ = -1;
        selectedSprite_ = -1;
        selectedLocation_ = locationHit;
        emit selectionChanged(-1);
    }
    else
    {
        // 유닛도 로케이션도 없으면 스프라이트를 본다 — 나무·바위 장식이다.
        const int spriteHit = spriteAt(event->position());
        if (spriteHit >= 0)
        {
            selectedUnits_.clear();
            selectedUnit_ = -1;
            selectedLocation_ = -1;
            selectedSprite_ = spriteHit;
            emit selectionChanged(-1);
            emit spriteSelected(spriteHit);
            viewport()->update();
            event->accept();
            return;
        }

        // 빈 곳에서 누르면 끌어서 여럿 고르기를 시작한다.
        if (!adding)
        {
            selectedUnits_.clear();
            selectedUnit_ = -1;
            selectedLocation_ = -1;
            selectedSprite_ = -1;
            emit selectionChanged(-1);
        }

        boxSelecting_ = true;
        boxStart_ = screenToMap(event->position());
        boxEnd_ = boxStart_;
    }

    if (unitHit >= 0 && selectedUnit_ >= 0)
    {
        dragging_ = true;
        dragStartMap_ = screenToMap(event->position());
        const auto & unit = document_->units()[static_cast<std::size_t>(selectedUnit_)];
        dragStartUnitPos_ = QPoint(unit.x, unit.y);
    }
    else if (locationHit >= 0)
    {
        dragging_ = true;
        dragStartMap_ = screenToMap(event->position());
        const auto & location = document_->locations()[static_cast<std::size_t>(locationHit)];
        dragStartUnitPos_ = QPoint(static_cast<int>(location.left),
                                   static_cast<int>(location.top));
    }

    viewport()->update();
    event->accept();
}

void MapView::mouseDoubleClickEvent(QMouseEvent * event)
{
    if (tool_ != Tool::Select || document_ == nullptr || !document_->isOpen())
    {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }

    const int hit = unitAt(event->position());
    if (hit >= 0)
    {
        selectedUnits_.assign(1, hit);
        selectedUnit_ = hit;
        selectedLocation_ = -1;
        emit selectionChanged(hit);
        emit unitActivated(hit);
        event->accept();
        return;
    }

    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void MapView::applyIsomAt(const QPointF & screenPos)
{
    if (document_ == nullptr || !document_->isOpen() ||
        tileset_ == nullptr || !tileset_->isLoaded())
    {
        return;
    }

    const QPointF mapPos = screenToMap(screenPos);
    if (mapPos.x() < 0 || mapPos.y() < 0)
        return;

    // 끌고 다닐 때 같은 자리에 거듭 찍으면 느리기만 하고 결과는 같다.
    const QPoint tile(static_cast<int>(mapPos.x()) / io::kTilePixels,
                      static_cast<int>(mapPos.y()) / io::kTilePixels);
    if (tile == lastIsomTile_)
        return;
    lastIsomTile_ = tile;

    auto * doc = const_cast<chk::MapDocument *>(document_);
    auto * graphics = const_cast<io::GameGraphics *>(tileset_);

    bool placed = false;
    for (const QPoint & spot : mirrorPixels(static_cast<int>(mapPos.x()),
                                            static_cast<int>(mapPos.y())))
    {
        if (spot.x() < 0 || spot.y() < 0)
            continue;

        if (doc->placeIsomTerrain(*graphics,
                                  static_cast<std::size_t>(spot.x()),
                                  static_cast<std::size_t>(spot.y()),
                                  isomTerrainType_,
                                  static_cast<std::size_t>(brushSize_)))
        {
            placed = true;
        }
    }

    if (placed)
    {
        refresh();
        emit documentEdited();
    }
}

void MapView::mouseMoveEvent(QMouseEvent * event)
{
    if (isomPainting_ || painting_)
    {
        // 칠하는 동안에도 브러시 자리를 보여 준다.
        const QPointF mapPos = screenToMap(event->position());
        hoverPos_ = QPoint(static_cast<int>(std::max(0.0, mapPos.x())),
                           static_cast<int>(std::max(0.0, mapPos.y())));
        hasHover_ = true;

        if (isomPainting_)
            applyIsomAt(event->position());
        else
            paintTerrainAt(event->position());

        viewport()->update();
        event->accept();
        return;
    }

    if (resizingLocation_ >= 0 && (event->buttons() & Qt::LeftButton))
    {
        const QPointF mapPos = screenToMap(event->position());

        int left = locationStart_.left();
        int top = locationStart_.top();
        int right = locationStart_.right();
        int bottom = locationStart_.bottom();

        if (resizeEdges_ & EdgeLeft)   left = static_cast<int>(std::max(0.0, mapPos.x()));
        if (resizeEdges_ & EdgeRight)  right = static_cast<int>(std::max(0.0, mapPos.x()));
        if (resizeEdges_ & EdgeTop)    top = static_cast<int>(std::max(0.0, mapPos.y()));
        if (resizeEdges_ & EdgeBottom) bottom = static_cast<int>(std::max(0.0, mapPos.y()));

        auto * doc = const_cast<chk::MapDocument *>(document_);
        doc->setLocationBounds(static_cast<std::size_t>(resizingLocation_),
                               static_cast<std::uint32_t>(std::min(left, right)),
                               static_cast<std::uint32_t>(std::min(top, bottom)),
                               static_cast<std::uint32_t>(std::max(left, right)),
                               static_cast<std::uint32_t>(std::max(top, bottom)));

        viewport()->update();
        event->accept();
        return;
    }

    if (drawingLocation_ && (event->buttons() & Qt::LeftButton))
    {
        boxEnd_ = screenToMap(event->position());
        viewport()->update();
        event->accept();
        return;
    }

    if (terrainSelecting_ && (event->buttons() & Qt::LeftButton))
    {
        const QPointF mapPos = screenToMap(event->position());
        const int tileX = static_cast<int>(std::max(0.0, mapPos.x())) / io::kTilePixels;
        const int tileY = static_cast<int>(std::max(0.0, mapPos.y())) / io::kTilePixels;

        const int fromX = static_cast<int>(boxStart_.x());
        const int fromY = static_cast<int>(boxStart_.y());

        terrainSelection_ = QRect(QPoint(std::min(fromX, tileX), std::min(fromY, tileY)),
                                  QPoint(std::max(fromX, tileX), std::max(fromY, tileY)));

        viewport()->update();
        event->accept();
        return;
    }

    if (boxSelecting_ && (event->buttons() & Qt::LeftButton))
    {
        boxEnd_ = screenToMap(event->position());
        viewport()->update();
        event->accept();
        return;
    }

    if (fogPainting_ && (event->buttons() & Qt::LeftButton))
    {
        paintFogAt(event->position());
        event->accept();
        return;
    }

    // 지형·두들 도구는 덮을 자리를 커서 둘레에 보여 준다.
    if (tool_ == Tool::Terrain || tool_ == Tool::PlaceDoodad || tool_ == Tool::SelectTerrain)
    {
        if (document_ != nullptr && document_->isOpen())
        {
            const QPointF mapPos = screenToMap(event->position());
            const QPoint at(static_cast<int>(std::max(0.0, mapPos.x())),
                            static_cast<int>(std::max(0.0, mapPos.y())));

            const int tileX = at.x() / io::kTilePixels;
            const int tileY = at.y() / io::kTilePixels;
            const int wasX = hoverPos_.x() / io::kTilePixels;
            const int wasY = hoverPos_.y() / io::kTilePixels;

            hoverPos_ = at;
            if (!hasHover_ || tileX != wasX || tileY != wasY)
            {
                hasHover_ = true;
                viewport()->update();
            }
        }

        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    // 놓기 도구는 커서를 따라 미리보기를 보여 준다.
    if (tool_ == Tool::PlaceUnit || tool_ == Tool::PlaceSprite)
    {
        if (document_ != nullptr && document_->isOpen())
        {
            const QPointF mapPos = screenToMap(event->position());
            const std::uint16_t type =
                (tool_ == Tool::PlaceSprite) ? placeSpriteType_ : placeUnitType_;

            const QPoint snapped = snapUnitPos(type,
                                               static_cast<int>(std::max(0.0, mapPos.x())),
                                               static_cast<int>(std::max(0.0, mapPos.y())));

            if (!hasHover_ || snapped != hoverPos_)
            {
                hoverPos_ = snapped;
                hoverValid_ = canPlaceAt(snapped.x(), snapped.y());
                hasHover_ = true;
                viewport()->update();
            }

            if (placingDrag_ && (event->buttons() & Qt::LeftButton))
                placeAt(event->position());
        }

        event->accept();
        return;
    }

    // 왼쪽 단추를 누른 채일 때만 끈다. 단추를 뗀 뒤에도 끌리면 고른 것이
    // 커서를 따라다닌다.
    if (!dragging_ || !(event->buttons() & Qt::LeftButton) || document_ == nullptr ||
        (selectedUnit_ < 0 && selectedLocation_ < 0))
    {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    // 드래그 중에는 화면만 미리 옮겨 보여 주고, 문서에는 놓을 때 한 번만 쓴다.
    // 매 픽셀마다 편집하면 실행 취소 이력이 폭발한다.
    const QPointF now = screenToMap(event->position());
    const QPointF delta = now - dragStartMap_;

    const int newX = dragStartUnitPos_.x() + static_cast<int>(delta.x());
    const int newY = dragStartUnitPos_.y() + static_cast<int>(delta.y());

    // 유닛을 끌 때도 놓을 때와 같은 격자를 쓴다. 로케이션은 자기만의
    // 좌표계를 쓰므로 건드리지 않는다.
    if (selectedUnit_ >= 0 && selectedUnit_ < static_cast<int>(document_->units().size()))
    {
        const auto & unit = document_->units()[static_cast<std::size_t>(selectedUnit_)];
        previewPos_ = snapUnitPos(unit.type, newX, newY);
    }
    else
    {
        previewPos_ = QPoint(std::max(0, newX), std::max(0, newY));
    }
    hasPreview_ = true;

    viewport()->update();
    event->accept();
}

void MapView::mouseReleaseEvent(QMouseEvent * event)
{
    if (isomPainting_)
    {
        isomPainting_ = false;
        lastIsomTile_ = QPoint(-1, -1);
        event->accept();
        return;
    }

    if (painting_)
    {
        painting_ = false;
        if (!strokeTiles_.empty() && document_ != nullptr)
        {
            auto * doc = const_cast<chk::MapDocument *>(document_);
            if (doc->setTiles(strokeTiles_, brushTile_))
            {
                refresh();
                emit documentEdited();
            }
        }
        strokeTiles_.clear();
        event->accept();
        return;
    }

    if (resizingLocation_ >= 0)
    {
        resizingLocation_ = -1;
        resizeEdges_ = 0;
        emit documentEdited();
        viewport()->update();
        event->accept();
        return;
    }

    if (drawingLocation_)
    {
        drawingLocation_ = false;

        const QRectF box = QRectF(boxStart_, boxEnd_).normalized();

        // 너무 작으면 잘못 누른 것으로 본다.
        if (box.width() >= 8 && box.height() >= 8 && document_ != nullptr)
        {
            auto * doc = const_cast<chk::MapDocument *>(document_);
            std::size_t created = 0;
            if (doc->addLocation(static_cast<std::uint32_t>(std::max(0.0, box.left())),
                                 static_cast<std::uint32_t>(std::max(0.0, box.top())),
                                 static_cast<std::uint32_t>(std::max(0.0, box.right())),
                                 static_cast<std::uint32_t>(std::max(0.0, box.bottom())),
                                 tr("새 로케이션").toStdString(), &created))
            {
                // 그리자마자 고른 상태로 둔다 — 바로 모서리를 잡을 수 있다.
                selectedLocation_ = static_cast<int>(created);
                selectedUnit_ = -1;
                selectedUnits_.clear();
                emit selectionChanged(-1);
                emit documentEdited();
            }
            else
            {
                emit placementRejected(QString::fromStdString(document_->lastError()));
            }
        }

        viewport()->update();
        event->accept();
        return;
    }

    if (terrainSelecting_)
    {
        terrainSelecting_ = false;

        if (!terrainSelection_.isEmpty())
            terrainRegion_ = terrainRegion_.united(QRegion(terrainSelection_));

        terrainSelection_ = QRect();
        viewport()->update();
        event->accept();
        return;
    }

    if (boxSelecting_)
    {
        boxSelecting_ = false;

        // 거의 제자리에서 뗐으면 고르기가 아니라 빈 곳을 누른 것이다.
        const QPointF delta = boxEnd_ - boxStart_;
        if (std::abs(delta.x()) >= 4 || std::abs(delta.y()) >= 4)
        {
            selectUnitsInBox(boxStart_, boxEnd_,
                             (event->modifiers() & Qt::ShiftModifier) != 0);
        }

        viewport()->update();
        event->accept();
        return;
    }

    if (fogPainting_)
    {
        fogPainting_ = false;
        event->accept();
        return;
    }

    if (placingDrag_)
    {
        placingDrag_ = false;
        dragSoundPlayed_ = false;
        lastPlaced_ = QPoint(-1, -1);
        event->accept();
        return;
    }

    if (!dragging_)
    {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }

    dragging_ = false;

    if (hasPreview_ && document_ != nullptr)
    {
        auto * doc = const_cast<chk::MapDocument *>(document_);

        if (selectedUnit_ >= 0)
        {
            const auto & unit = document_->units()[static_cast<std::size_t>(selectedUnit_)];
            const int dx = previewPos_.x() - static_cast<int>(unit.x);
            const int dy = previewPos_.y() - static_cast<int>(unit.y);

            if (dx != 0 || dy != 0)
            {
                if (!allowStack_ && unitWouldOverlap(unit.type, previewPos_.x(), previewPos_.y(),
                                                     selectedUnit_))
                {
                    emit placementRejected(tr("그 자리에는 다른 유닛이 있습니다."));
                }
                else
                {
                    // 여럿을 골랐으면 같은 거리만큼 함께 옮긴다.
                    std::vector<int> targets = selectedUnits_;
                    if (targets.empty())
                        targets.push_back(selectedUnit_);

                    bool moved = false;
                    const auto & units = document_->units();
                    for (int index : targets)
                    {
                        if (index < 0 || index >= static_cast<int>(units.size()))
                            continue;

                        const auto & target = units[static_cast<std::size_t>(index)];
                        const int nextX = std::max(0, static_cast<int>(target.x) + dx);
                        const int nextY = std::max(0, static_cast<int>(target.y) + dy);

                        if (doc->moveUnit(static_cast<std::size_t>(index),
                                          static_cast<std::uint16_t>(nextX),
                                          static_cast<std::uint16_t>(nextY)))
                        {
                            moved = true;
                        }
                    }

                    if (moved)
                        emit documentEdited();
                }
            }
        }
        else if (selectedLocation_ >= 0)
        {
            const auto & location =
                document_->locations()[static_cast<std::size_t>(selectedLocation_)];
            const std::int64_t dx = previewPos_.x() - static_cast<std::int64_t>(location.left);
            const std::int64_t dy = previewPos_.y() - static_cast<std::int64_t>(location.top);
            if (dx != 0 || dy != 0)
            {
                if (doc->moveLocation(static_cast<std::size_t>(selectedLocation_), dx, dy))
                    emit documentEdited();
            }
        }
    }

    hasPreview_ = false;
    viewport()->update();
    event->accept();
}

bool MapView::copySelection()
{
    if (document_ == nullptr || !document_->isOpen())
        return false;

    std::vector<int> targets = selectedUnits_;
    if (targets.empty() && selectedUnit_ >= 0)
        targets.push_back(selectedUnit_);
    if (targets.empty())
        return false;

    const auto & units = document_->units();

    // 첫 유닛을 기준으로 삼아 나머지의 자리 관계를 담는다.
    const auto first = units[static_cast<std::size_t>(targets.front())];

    clipboard_.clear();
    for (int index : targets)
    {
        if (index < 0 || index >= static_cast<int>(units.size()))
            continue;

        const auto & unit = units[static_cast<std::size_t>(index)];
        clipboard_.push_back(ClipboardUnit{
            unit.type, unit.owner, unit.resourceAmount,
            static_cast<int>(unit.x) - static_cast<int>(first.x),
            static_cast<int>(unit.y) - static_cast<int>(first.y)});
    }

    if (clipboard_.empty())
        return false;

    // 붙여넣기 하나짜리 경로가 쓰는 값도 맞춰 둔다.
    clipboardType_ = first.type;
    clipboardOwner_ = first.owner;
    clipboardResource_ = first.resourceAmount;
    clipboardValid_ = true;
    return true;
}

bool MapView::pasteAt(const QPointF & screenPos)
{
    if (!clipboardValid_ || clipboard_.empty() ||
        document_ == nullptr || !document_->isOpen())
    {
        return false;
    }

    const QPointF mapPos = screenToMap(screenPos);
    auto * doc = const_cast<chk::MapDocument *>(document_);

    // 담아 둔 자리 관계를 그대로 두고 통째로 옮겨 놓는다.
    int placed = 0;
    for (const ClipboardUnit & entry : clipboard_)
    {
        const int x = static_cast<int>(mapPos.x()) + entry.dx;
        const int y = static_cast<int>(mapPos.y()) + entry.dy;

        if (doc->addUnit(entry.type, entry.owner,
                         static_cast<std::uint16_t>(std::max(0, x)),
                         static_cast<std::uint16_t>(std::max(0, y))))
        {
            ++placed;
        }
    }

    if (placed == 0)
        return false;

    refreshUnits();
    emit documentEdited();
    emit unitPlaced(clipboardType_);
    return true;
}

bool MapView::pasteAtCentre()
{
    return pasteAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
}

bool MapView::deleteSelectedUnit()
{
    if (document_ == nullptr || !document_->isOpen())
        return false;

    // 고른 것을 모두 지운다. 뒤에서부터 지워야 앞 번호가 밀리지 않는다.
    std::vector<int> targets = selectedUnits_;
    if (targets.empty() && selectedUnit_ >= 0)
        targets.push_back(selectedUnit_);
    if (targets.empty())
        return false;

    std::sort(targets.begin(), targets.end(), std::greater<int>());

    auto * doc = const_cast<chk::MapDocument *>(document_);
    int removed = 0;
    for (int index : targets)
    {
        if (index >= 0 && doc->removeUnit(static_cast<std::size_t>(index)))
            ++removed;
    }

    if (removed == 0)
        return false;

    selectedUnits_.clear();
    selectedUnit_ = -1;
    emit selectionChanged(-1);
    emit documentEdited();
    refreshUnits(); // 저그 건물을 지웠다면 크립도 달라진다
    return true;
}

void MapView::leaveEvent(QEvent * event)
{
    // 커서가 창을 벗어나면 미리보기를 지운다. 끌던 것은 단추를 뗄 때
    // 정리되므로 건드리지 않는다.
    if (hasHover_)
    {
        hasHover_ = false;
        viewport()->update();
    }

    QAbstractScrollArea::leaveEvent(event);
}

void MapView::focusOutEvent(QFocusEvent * event)
{
    // 다른 창으로 옮겨 가면 끌던 것을 그만둔다 — 단추를 뗀 사건을
    // 놓쳐 상태가 남으면 고른 것이 커서를 따라다닌다.
    boxSelecting_ = false;
    dragging_ = false;
    painting_ = false;
    isomPainting_ = false;
    fogPainting_ = false;
    placingDrag_ = false;
    hasPreview_ = false;
    hasHover_ = false;

    viewport()->update();
    QAbstractScrollArea::focusOutEvent(event);
}

void MapView::keyPressEvent(QKeyEvent * event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (deleteSelectedUnit())
        {
            event->accept();
            return;
        }
    }
    else if (event->key() == Qt::Key_Escape)
    {
        clearSelection();
        event->accept();
        return;
    }
    else if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9 &&
             (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
    {
        // 1~9 는 플레이어 1~9, 0 은 플레이어 10 이다. SCMDraft 와 같은
        // 손놀림으로 소유자를 바꿔 가며 놓을 수 있다.
        emit ownerRequested(static_cast<std::uint8_t>(event->key() - Qt::Key_1));
        event->accept();
        return;
    }
    else if (event->key() == Qt::Key_0 &&
             (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
    {
        emit ownerRequested(9);
        event->accept();
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
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

    for (std::size_t index = 0; index < locations.size(); ++index)
    {
        const auto & location = locations[index];
        const bool isSelected = (static_cast<int>(index) == selectedLocation_);

        // 드래그 중인 로케이션은 손을 따라 움직이는 자리에 그린다.
        const double offsetX = (isSelected && hasPreview_)
            ? previewPos_.x() - static_cast<double>(location.left) : 0.0;
        const double offsetY = (isSelected && hasPreview_)
            ? previewPos_.y() - static_cast<double>(location.top) : 0.0;

        const QPointF topLeft = mapToScreen(location.left + offsetX, location.top + offsetY);
        const QPointF bottomRight =
            mapToScreen(location.right + offsetX, location.bottom + offsetY);
        const QRectF bounds(topLeft, bottomRight);

        if (!dirty.intersects(bounds.toAlignedRect().adjusted(-1, -1, 1, 1)))
            continue;

        painter.setPen(isSelected ? QPen(QColor(90, 255, 120), 2.0)
                                  : QPen(edge, 1.0, Qt::DashLine));
        painter.setBrush(isSelected ? QColor(90, 255, 120, 40) : QColor(255, 220, 90, 28));
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
