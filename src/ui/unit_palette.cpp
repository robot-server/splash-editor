#include "ui/unit_palette.h"

#include "io/game_graphics.h"
#include "io/map_archive.h"

#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QScrollBar>
#include <QToolTip>

#include <algorithm>

namespace splash::ui {
namespace {

constexpr int kCell = 52;
constexpr int kMaxCached = 512;

// 팔레트는 늘 어둡게 둔다. 시스템 테마를 따르면 밝은 테마에서 배경이
// 하얘지는데, 유닛 스프라이트는 밝은 색이 많아 형체가 묻힌다.
const QColor kBackground(38, 38, 42);
const QColor kCellFill(52, 52, 58);
const QColor kCellLine(70, 70, 78);

/// StarCraft 의 실제 유닛 종류 수. 그 뒤는 트리거 전용 가상 항목이다.
constexpr std::uint16_t kRealUnitTypes = 228;

/// 맵에 놓을 수 있는 스프라이트 종류 수.
constexpr std::uint16_t kSpriteTypes = 517;

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
    rebuild(); // 분류는 게임 데이터가 있어야 알 수 있다
}

void UnitPalette::setCategory(Category category)
{
    if (category_ == category)
        return;
    category_ = category;
    rebuild();
    verticalScrollBar()->setValue(0);
}

QString UnitPalette::categoryName(Category category)
{
    switch (category)
    {
        case Category::All:              return tr("전체");
        case Category::TerranUnits:      return tr("테란 유닛");
        case Category::TerranBuildings:  return tr("테란 건물");
        case Category::ZergUnits:        return tr("저그 유닛");
        case Category::ZergBuildings:    return tr("저그 건물");
        case Category::ProtossUnits:     return tr("프로토스 유닛");
        case Category::ProtossBuildings: return tr("프로토스 건물");
        case Category::Neutral:          return tr("중립 · 자원");
        case Category::Sprites:          return tr("스프라이트 (장식)");
    }
    return {};
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

void UnitPalette::setPlayerColor(std::uint8_t colorIndex)
{
    if (colorIndex_ == colorIndex)
        return;
    colorIndex_ = colorIndex;
    cache_.clear(); // 색이 바뀌면 그림도 달라진다
    viewport()->update();
}

void UnitPalette::setFilter(const QString & text)
{
    if (filter_ == text.trimmed())
        return;

    filter_ = text.trimmed();
    rebuild();
}

namespace {

/// 그 항목이 찾는 글자와 맞는지. 이름과 번호 둘 다로 찾을 수 있다.
bool matchesFilter(const QString & filter, const QString & name, std::uint16_t type)
{
    if (filter.isEmpty())
        return true;
    if (name.contains(filter, Qt::CaseInsensitive))
        return true;
    return QString::number(type) == filter;
}

} // namespace

void UnitPalette::rebuild()
{
    units_.clear();

    if (category_ == Category::Sprites)
    {
        units_.reserve(kSpriteTypes);
        for (std::uint16_t type = 0; type < kSpriteTypes; ++type)
        {
            if (matchesFilter(filter_, tr("스프라이트 %1").arg(type), type))
                units_.push_back(type);
        }

        updateScrollRange();
        viewport()->update();
        return;
    }

    units_.reserve(kRealUnitTypes);

    using Race = io::GameGraphics::UnitClass::Race;

    for (std::uint16_t type = 0; type < kRealUnitTypes; ++type)
    {
        if (!matchesFilter(filter_,
                           QString::fromStdString(splash::io::unitTypeName(type)), type))
            continue;

        if (category_ == Category::All)
        {
            units_.push_back(type);
            continue;
        }

        // 분류는 게임 데이터(units.dat)가 알려 준다. 없으면 전부 보여 준다.
        if (tileset_ == nullptr || !tileset_->hasUnitGraphics())
        {
            units_.push_back(type);
            continue;
        }

        const auto info = tileset_->unitClass(type);
        bool keep = false;
        switch (category_)
        {
            case Category::TerranUnits:      keep = info.race == Race::Terran  && !info.building; break;
            case Category::TerranBuildings:  keep = info.race == Race::Terran  &&  info.building; break;
            case Category::ZergUnits:        keep = info.race == Race::Zerg    && !info.building; break;
            case Category::ZergBuildings:    keep = info.race == Race::Zerg    &&  info.building; break;
            case Category::ProtossUnits:     keep = info.race == Race::Protoss && !info.building; break;
            case Category::ProtossBuildings: keep = info.race == Race::Protoss &&  info.building; break;
            case Category::Neutral:          keep = info.race == Race::Neutral; break;
            case Category::All:              keep = true; break;
        }
        if (keep)
            units_.push_back(type);
    }

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

    // 스프라이트와 유닛은 그림이 다르므로 캐시 키도 나눈다.
    const std::uint32_t key = (static_cast<std::uint32_t>(unitType) << 9) |
                              (static_cast<std::uint32_t>(owner_) << 1) |
                              (category_ == Category::Sprites ? 1u : 0u);
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

    const io::UnitImage image = (category_ == Category::Sprites)
        ? tileset_->renderSprite(unitType, owner_, tilesetId_, /*drawnAsSprite*/ true,
                                 colorIndex_)
        : tileset_->renderUnit(unitType, owner_, tilesetId_, resource, 0, 0, colorIndex_);

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
    painter.fillRect(event->rect(), kBackground);

    if (tileset_ == nullptr || !tileset_->hasUnitGraphics())
    {
        painter.setPen(QColor(200, 200, 205));
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

            // 칸을 눈에 보이게 채운다. 스프라이트는 투명 배경이라 칸이
            // 없으면 어디서 어디까지가 한 칸인지 알 수 없다.
            painter.setPen(QPen(kCellLine, 1.0));
            painter.setBrush(unitType == selectedUnit_ ? QColor(58, 84, 62) : kCellFill);
            painter.drawRect(cell.adjusted(0, 0, -1, -1));

            const QPixmap * pixmap = unitPixmap(unitType);
            if (pixmap != nullptr)
            {
                const QPoint at(cell.center().x() - pixmap->width() / 2,
                                cell.center().y() - pixmap->height() / 2);
                painter.drawPixmap(at, *pixmap);
            }
            // 그림이 없는 항목은 빈 칸으로만 보여서는 찾을 수가 없다.
            // 찾는 중에는 어느 것이 걸렸는지 알아야 하므로 이름을 함께
            // 적는다.
            if (pixmap == nullptr || !filter_.isEmpty())
            {
                const QString name = category_ == Category::Sprites
                    ? tr("스프라이트 %1").arg(unitType)
                    : QString::fromStdString(splash::io::unitTypeName(unitType));

                painter.save();
                QFont small = painter.font();
                small.setPointSizeF(std::max(7.0, small.pointSizeF() - 2.5));
                painter.setFont(small);

                if (pixmap != nullptr)
                {
                    // 그림 위에 겹치므로 글자가 묻히지 않게 띠를 깐다.
                    const QRect strip(cell.left() + 1, cell.bottom() - 15,
                                      cell.width() - 2, 14);
                    painter.fillRect(strip, QColor(24, 24, 28, 210));
                    painter.setPen(QColor(220, 222, 228));
                    painter.drawText(strip.adjusted(2, 0, -2, 0),
                                     Qt::AlignCenter,
                                     painter.fontMetrics().elidedText(
                                         name, Qt::ElideRight, strip.width() - 4));
                }
                else
                {
                    painter.setPen(QColor(168, 172, 180));
                    painter.drawText(cell.adjusted(3, 3, -3, -3),
                                     Qt::AlignCenter | Qt::TextWordWrap, name);
                }
                painter.restore();
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
    if (category_ == Category::Sprites)
    {
        emit spriteSelected(selectedUnit_);
        QToolTip::showText(event->globalPosition().toPoint(),
                           tr("스프라이트 %1").arg(selectedUnit_), this);
    }
    else
    {
        emit unitSelected(selectedUnit_);
        QToolTip::showText(event->globalPosition().toPoint(),
                           QString::fromStdString(splash::io::unitTypeName(selectedUnit_)),
                           this);
    }

    viewport()->update();
    event->accept();
}

} // namespace splash::ui
