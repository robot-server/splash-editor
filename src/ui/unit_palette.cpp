#include "ui/unit_palette.h"

#include "io/game_graphics.h"
#include "io/map_archive.h"

#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>

#include <algorithm>

namespace splash::ui {
namespace {

constexpr int kCell = 52;
constexpr int kMaxCached = 512;

/// StarCraft 의 실제 유닛 종류 수. 그 뒤는 트리거 전용 가상 항목이다.
constexpr std::uint16_t kRealUnitTypes = 228;

} // namespace

UnitPalette::UnitPalette(QWidget * parent) : QAbstractScrollArea(parent)
{
    viewport()->setAutoFillBackground(true);
    setMinimumWidth(kCell * 4 + 24);
    setMouseTracking(true);
    rebuild();
}

UnitPalette::~UnitPalette() = default;

void UnitPalette::setTileset(const io::GameGraphics * tileset)
{
    tileset_ = tileset;
    cache_.clear();
    viewport()->update();
}

void UnitPalette::setTilesetId(std::uint16_t tilesetId)
{
    if (tilesetId_ == tilesetId)
        return;
    tilesetId_ = tilesetId;
    cache_.clear();
    viewport()->update();
}

void UnitPalette::setOwner(std::uint8_t owner)
{
    if (owner_ == owner)
        return;
    owner_ = owner;
    cache_.clear(); // 플레이어 색이 바뀌면 그림도 달라진다
    viewport()->update();
}

void UnitPalette::rebuild()
{
    units_.clear();
    units_.reserve(kRealUnitTypes);
    for (std::uint16_t type = 0; type < kRealUnitTypes; ++type)
        units_.push_back(type);

    updateScrollRange();
    viewport()->update();
}

int UnitPalette::columns() const
{
    return std::max(1, viewport()->width() / kCell);
}

void UnitPalette::updateScrollRange()
{
    const int cols = columns();
    const int rows = (static_cast<int>(units_.size()) + cols - 1) / cols;
    verticalScrollBar()->setRange(0, std::max(0, rows * kCell - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setSingleStep(kCell);
}

void UnitPalette::resizeEvent(QResizeEvent * event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollRange();
}

const QPixmap * UnitPalette::unitPixmap(std::uint16_t unitType)
{
    if (tileset_ == nullptr || !tileset_->hasUnitGraphics())
        return nullptr;

    const std::uint32_t key = (static_cast<std::uint32_t>(unitType) << 8) | owner_;
    auto found = cache_.find(key);
    if (found != cache_.end())
        return found.value().isNull() ? nullptr : &found.value();

    if (cache_.size() >= kMaxCached)
        cache_.clear();

    // 자원 유닛은 가득 찬 모습으로 보여 준다 — 팔레트에서 고갈된 그림을
    // 보여 줄 이유가 없다.
    std::uint32_t resource = 0;
    if (unitType >= 176 && unitType <= 178)
        resource = 1500;
    else if (unitType == 188)
        resource = 5000;

    const io::UnitImage image = tileset_->renderUnit(unitType, owner_, tilesetId_, resource);

    QPixmap pixmap;
    if (image.width > 0 && image.height > 0)
    {
        const QImage qimage(image.rgba.data(), image.width, image.height,
                            image.width * 4, QImage::Format_RGBA8888);
        // 칸보다 크면 줄인다. 작으면 그대로 둔다 — 억지로 키우면 뭉개진다.
        const int limit = kCell - 8;
        pixmap = QPixmap::fromImage(
            (image.width > limit || image.height > limit)
                ? qimage.scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                : qimage.copy());
    }

    auto inserted = cache_.insert(key, pixmap);
    return inserted.value().isNull() ? nullptr : &inserted.value();
}

void UnitPalette::paintEvent(QPaintEvent * event)
{
    QPainter painter(viewport());
    painter.fillRect(event->rect(), palette().dark());

    if (tileset_ == nullptr || !tileset_->hasUnitGraphics())
    {
        painter.setPen(palette().color(QPalette::BrightText));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         tr("유닛을 보려면\nStarCraft 설치 폴더가 필요합니다."));
        return;
    }

    const int cols = columns();
    const int origin = verticalScrollBar()->value();
    const int firstRow = std::max(0, (origin + event->rect().top()) / kCell);
    const int lastRow = (origin + event->rect().bottom()) / kCell;

    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const std::size_t index = static_cast<std::size_t>(row) * cols + col;
            if (index >= units_.size())
                return;

            const std::uint16_t unitType = units_[index];
            const QRect cell(col * kCell, row * kCell - origin, kCell, kCell);

            if (unitType == selectedUnit_)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(90, 255, 120, 50));
                painter.drawRect(cell.adjusted(1, 1, -1, -1));
            }

            const QPixmap * pixmap = unitPixmap(unitType);
            if (pixmap != nullptr)
            {
                const QPoint at(cell.center().x() - pixmap->width() / 2,
                                cell.center().y() - pixmap->height() / 2);
                painter.drawPixmap(at, *pixmap);
            }

            if (unitType == selectedUnit_)
            {
                painter.setPen(QPen(QColor(90, 255, 120), 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(cell.adjusted(1, 1, -1, -1));
            }
        }
    }
}

void UnitPalette::mousePressEvent(QMouseEvent * event)
{
    if (event->button() != Qt::LeftButton)
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
    if (index >= units_.size())
        return;

    selectedUnit_ = units_[index];
    emit unitSelected(selectedUnit_);

    QToolTip::showText(event->globalPosition().toPoint(),
                       QString::fromStdString(splash::io::unitTypeName(selectedUnit_)),
                       this);

    viewport()->update();
    event->accept();
}

} // namespace splash::ui
