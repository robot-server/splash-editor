#include "ui/tile_palette.h"

#include "io/game_graphics.h"

#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QScrollBar>

#include <algorithm>

namespace splash::ui {
namespace {

/// 팔레트 칸 한 변의 화면 크기. 타일 원본(32)보다 조금 키워 고르기 쉽게 한다.
constexpr int kCell = 36;

/// 캐시 상한. 한 칸이 32x32 RGBA(4KB)이므로 2048개면 약 8MB 다.
constexpr int kMaxCached = 2048;

// 시스템 테마를 따르면 밝은 테마에서 배경이 하얘진다. 타일 사이 틈이
// 눈에 띄어 고르기 나쁘므로 늘 어둡게 둔다.
const QColor kBackground(38, 38, 42);

} // namespace

TilePalette::TilePalette(QWidget * parent) : QAbstractScrollArea(parent)
{
    viewport()->setAutoFillBackground(true);
    setMinimumWidth(kCell * 4 + 24);
}

TilePalette::~TilePalette() = default;

void TilePalette::setTileset(const io::GameGraphics * tileset)
{
    tileset_ = tileset;
    rebuild();
}

void TilePalette::setTilesetId(std::uint16_t tilesetId)
{
    if (tilesetId_ == tilesetId && !tiles_.empty())
        return;
    tilesetId_ = tilesetId;
    rebuild();
}

void TilePalette::setSelectedTile(std::uint16_t tileId)
{
    if (selectedTile_ == tileId)
        return;
    selectedTile_ = tileId;
    viewport()->update();
}

void TilePalette::setTerrainTypeMode(bool on)
{
    if (terrainTypeMode_ == on)
        return;
    terrainTypeMode_ = on;
    rebuild();
    verticalScrollBar()->setValue(0);
}

void TilePalette::rebuild()
{
    cache_.clear();
    tiles_.clear();
    terrainTypes_.clear();

    if (tileset_ != nullptr && tileset_->isLoaded())
    {
        if (terrainTypeMode_)
        {
            for (const auto & type : tileset_->terrainTypes(tilesetId_))
            {
                TerrainEntry entry;
                entry.brushIndex = type.brushIndex;
                entry.name = QString::fromStdString(type.name);
                entry.previewTileId = type.previewTileId;
                entry.hasPreview = type.hasPreview;
                terrainTypes_.push_back(std::move(entry));
            }
        }
        else
        {
            tiles_ = tileset_->paletteTileIds(tilesetId_);
        }
    }

    updateScrollRange();
    viewport()->update();
}

int TilePalette::columns() const
{
    return std::max(1, viewport()->width() / kCell);
}

void TilePalette::updateScrollRange()
{
    if (terrainTypeMode_)
    {
        // 지형 종류는 한 줄에 하나씩 이름으로 보여 준다.
        const int rowHeight = 40;
        const int contentHeight = static_cast<int>(terrainTypes_.size()) * rowHeight;
        verticalScrollBar()->setRange(0, std::max(0, contentHeight - viewport()->height()));
        verticalScrollBar()->setPageStep(viewport()->height());
        verticalScrollBar()->setSingleStep(rowHeight);
        return;
    }

    const int cols = columns();
    const int rows = (static_cast<int>(tiles_.size()) + cols - 1) / cols;
    const int contentHeight = rows * kCell;

    verticalScrollBar()->setRange(0, std::max(0, contentHeight - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setSingleStep(kCell);
}

void TilePalette::resizeEvent(QResizeEvent * event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollRange();
}

const QPixmap * TilePalette::tilePixmap(std::uint16_t tileId)
{
    if (tileset_ == nullptr || !tileset_->isLoaded())
        return nullptr;

    auto found = cache_.find(tileId);
    if (found != cache_.end())
        return &found.value();

    if (cache_.size() >= kMaxCached)
        cache_.clear();

    std::vector<std::uint8_t> rgba(io::kTileRgbaBytes);
    if (!tileset_->renderTile(tilesetId_, tileId, rgba.data()))
        return nullptr;

    const QImage image(rgba.data(), io::kTilePixels, io::kTilePixels,
                       io::kTilePixels * 4, QImage::Format_RGBA8888);
    auto inserted = cache_.insert(tileId, QPixmap::fromImage(image.copy()));
    return &inserted.value();
}

void TilePalette::paintEvent(QPaintEvent * event)
{
    QPainter painter(viewport());
    painter.fillRect(event->rect(), kBackground);

    if (terrainTypeMode_)
    {
        if (terrainTypes_.empty())
        {
            painter.setPen(QColor(200, 200, 205));
            painter.drawText(viewport()->rect(), Qt::AlignCenter,
                             tr("지형 종류를 보려면\nStarCraft 설치 폴더가 필요합니다."));
            return;
        }

        // 이름만 있으면 어떤 지형인지 감이 안 온다. 대표 타일을 함께 보여 준다.
        const int rowHeight = 40;
        const int origin = verticalScrollBar()->value();
        for (std::size_t i = 0; i < terrainTypes_.size(); ++i)
        {
            const QRect row(0, static_cast<int>(i) * rowHeight - origin,
                            viewport()->width(), rowHeight);
            if (!row.intersects(event->rect()))
                continue;

            const bool selected = (static_cast<int>(i) == selectedTerrainRow_);
            painter.fillRect(row, selected ? QColor(58, 84, 62) : QColor(46, 46, 52));
            painter.setPen(QColor(70, 70, 78));
            painter.drawLine(row.bottomLeft(), row.bottomRight());

            const auto & entry = terrainTypes_[i];
            if (entry.hasPreview)
            {
                if (const QPixmap * pixmap = tilePixmap(entry.previewTileId))
                    painter.drawPixmap(QRect(row.left() + 4, row.top() + 4, 32, 32), *pixmap);
            }

            painter.setPen(selected ? QColor(150, 255, 170) : QColor(215, 215, 220));
            painter.drawText(row.adjusted(44, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             entry.name);
        }
        return;
    }

    if (tiles_.empty())
    {
        painter.setPen(QColor(200, 200, 205));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         tr("타일을 보려면\nStarCraft 설치 폴더가 필요합니다."));
        return;
    }

    const int cols = columns();
    const int origin = verticalScrollBar()->value();

    // 보이는 줄만 그린다.
    const int firstRow = std::max(0, (origin + event->rect().top()) / kCell);
    const int lastRow = (origin + event->rect().bottom()) / kCell;

    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const std::size_t index = static_cast<std::size_t>(row) * cols + col;
            if (index >= tiles_.size())
                return;

            const std::uint16_t tileId = tiles_[index];
            const QPixmap * pixmap = tilePixmap(tileId);
            if (pixmap == nullptr)
                continue;

            const QRect cell(col * kCell, row * kCell - origin, kCell, kCell);
            const QRect target = cell.adjusted(2, 2, -2, -2);
            painter.drawPixmap(target, *pixmap);

            if (tileId == selectedTile_)
            {
                painter.setPen(QPen(QColor(90, 255, 120), 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(cell.adjusted(1, 1, -1, -1));
            }
        }
    }
}

void TilePalette::mousePressEvent(QMouseEvent * event)
{
    if (terrainTypeMode_)
    {
        if (event->button() != Qt::LeftButton || terrainTypes_.empty())
            return;

        const int rowHeight = 40;
        const int index =
            (static_cast<int>(event->position().y()) + verticalScrollBar()->value()) / rowHeight;
        if (index < 0 || index >= static_cast<int>(terrainTypes_.size()))
            return;

        selectedTerrainRow_ = index;
        emit terrainTypeSelected(terrainTypes_[static_cast<std::size_t>(index)].brushIndex);
        viewport()->update();
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton || tiles_.empty())
    {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const int cols = columns();
    const int origin = verticalScrollBar()->value();
    const int col = static_cast<int>(event->position().x()) / kCell;
    const int row = (static_cast<int>(event->position().y()) + origin) / kCell;

    if (col < 0 || col >= cols || row < 0)
        return;

    const std::size_t index = static_cast<std::size_t>(row) * cols + col;
    if (index >= tiles_.size())
        return;

    selectedTile_ = tiles_[index];
    emit tileSelected(selectedTile_);
    viewport()->update();
    event->accept();
}

} // namespace splash::ui
