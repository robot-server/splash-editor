#include "ui/brush_palette.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace splash::ui {
namespace {

/// 파일 첫머리에 두는 표. 다른 파일을 잘못 읽지 않으려는 것이다.
constexpr quint32 kMagic = 0x53504C42; // "SPLB"
constexpr quint16 kVersion = 2; // 2: 칸 가리개(mask) 추가

} // namespace

BrushPalette::BrushPalette(QWidget * parent) : QWidget(parent)
{
    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);

    auto * useButton = new QPushButton(tr("쓰기"), this);
    auto * removeButton = new QPushButton(tr("지우기"), this);
    auto * saveButton = new QPushButton(tr("저장…"), this);
    auto * loadButton = new QPushButton(tr("불러오기…"), this);

    auto * buttons = new QHBoxLayout();
    buttons->addWidget(useButton);
    buttons->addWidget(removeButton);
    buttons->addWidget(saveButton);
    buttons->addWidget(loadButton);

    status_ = new QLabel(
        tr("지형을 고르고 복사한 뒤 '브러시로 담기'를 누르면 여기 쌓입니다."), this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);
    layout->addWidget(status_);

    const auto useCurrent = [this] {
        const int row = list_->currentRow();
        if (row < 0 || row >= static_cast<int>(brushes_.size()))
            return;
        emit brushChosen(brushes_[static_cast<std::size_t>(row)]);
        status_->setText(tr("'%1' 을 브러시로 씁니다 — 맵을 누르면 찍힙니다")
                             .arg(brushes_[static_cast<std::size_t>(row)].name));
    };

    connect(useButton, &QPushButton::clicked, this, useCurrent);
    connect(list_, &QListWidget::itemDoubleClicked, this, useCurrent);
    connect(removeButton, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { saveToFile(); });
    connect(loadButton, &QPushButton::clicked, this, [this] { loadFromFile(); });

    loadDefault();
}

void BrushPalette::addFromClipboard(const MapView::TerrainBrush & brush)
{
    if (brush.width <= 0 || brush.height <= 0 || brush.tiles.empty())
    {
        status_->setText(tr("담아 둔 지형이 없습니다. 먼저 지형을 골라 복사하세요."));
        return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("브러시로 담기"), tr("이름"), QLineEdit::Normal,
        tr("브러시 %1").arg(brushes_.size() + 1), &accepted);

    if (!accepted)
        return;

    MapView::TerrainBrush stored = brush;
    stored.name = name.isEmpty() ? tr("이름 없음") : name;
    brushes_.push_back(std::move(stored));

    reloadList();
    list_->setCurrentRow(static_cast<int>(brushes_.size()) - 1);
    saveDefault();

    status_->setText(tr("브러시를 담았습니다 — %1개").arg(brushes_.size()));
}

void BrushPalette::reloadList()
{
    list_->clear();
    for (const auto & brush : brushes_)
    {
        list_->addItem(tr("%1   %2 x %3   (타일셋 %4)")
                           .arg(brush.name)
                           .arg(brush.width)
                           .arg(brush.height)
                           .arg(brush.tilesetId));
    }
}

void BrushPalette::removeSelected()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(brushes_.size()))
        return;

    brushes_.erase(brushes_.begin() + row);
    reloadList();
    saveDefault();
    status_->setText(tr("브러시를 지웠습니다 — %1개 남음").arg(brushes_.size()));
}

QString BrushPalette::defaultPath() const
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        return QString();

    QDir().mkpath(dir);
    return dir + QStringLiteral("/brushes.splashbrush");
}

void BrushPalette::loadDefault()
{
    const QString path = defaultPath();
    if (!path.isEmpty() && QFile::exists(path))
    {
        readFile(path, /*append*/ false);
        reloadList();
    }
}

void BrushPalette::saveDefault() const
{
    const QString path = defaultPath();
    if (!path.isEmpty())
        writeFile(path);
}

bool BrushPalette::writeFile(const QString & path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out.setByteOrder(QDataStream::LittleEndian);

    out << kMagic << kVersion << static_cast<quint32>(brushes_.size());

    for (const auto & brush : brushes_)
    {
        out << brush.name
            << static_cast<qint32>(brush.width)
            << static_cast<qint32>(brush.height)
            << static_cast<quint16>(brush.tilesetId)
            << static_cast<quint32>(brush.tiles.size());

        for (std::uint16_t tile : brush.tiles)
            out << static_cast<quint16>(tile);

        // 떨어진 덩어리를 여러 개 고른 브러시는 빈 칸을 가린다. 비어 있으면
        // 모든 칸이 든다는 뜻이라 길이 0 으로 적는다.
        out << static_cast<quint32>(brush.mask.size());
        for (bool used : brush.mask)
            out << static_cast<quint8>(used ? 1 : 0);
    }

    return out.status() == QDataStream::Ok;
}

bool BrushPalette::readFile(const QString & path, bool append)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    in.setByteOrder(QDataStream::LittleEndian);

    quint32 magic = 0;
    quint16 version = 0;
    quint32 count = 0;
    in >> magic >> version >> count;

    if (magic != kMagic || version > kVersion)
        return false;

    std::vector<MapView::TerrainBrush> loaded;
    loaded.reserve(count);

    for (quint32 i = 0; i < count; ++i)
    {
        MapView::TerrainBrush brush;
        qint32 width = 0;
        qint32 height = 0;
        quint16 tilesetId = 0;
        quint32 tileCount = 0;

        in >> brush.name >> width >> height >> tilesetId >> tileCount;
        if (in.status() != QDataStream::Ok)
            return false;

        // 크기와 타일 수가 맞지 않으면 잘못된 파일이다.
        if (width <= 0 || height <= 0 ||
            tileCount != static_cast<quint32>(width) * static_cast<quint32>(height))
        {
            return false;
        }

        brush.width = width;
        brush.height = height;
        brush.tilesetId = tilesetId;
        brush.tiles.resize(tileCount);

        for (quint32 t = 0; t < tileCount; ++t)
        {
            quint16 tile = 0;
            in >> tile;
            brush.tiles[t] = tile;
        }

        if (version >= 2)
        {
            quint32 maskCount = 0;
            in >> maskCount;

            if (maskCount != 0 && maskCount != tileCount)
                return false;

            brush.mask.resize(maskCount);
            for (quint32 m = 0; m < maskCount; ++m)
            {
                quint8 used = 0;
                in >> used;
                brush.mask[m] = (used != 0);
            }
        }

        loaded.push_back(std::move(brush));
    }

    if (append)
        brushes_.insert(brushes_.end(), loaded.begin(), loaded.end());
    else
        brushes_ = std::move(loaded);

    return true;
}

void BrushPalette::saveToFile()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("브러시 저장"), QStringLiteral("brushes.splashbrush"),
        tr("Splash 브러시 (*.splashbrush)"));

    if (path.isEmpty())
        return;

    if (!writeFile(path))
    {
        QMessageBox::warning(this, tr("저장 실패"), tr("파일을 쓰지 못했습니다."));
        return;
    }

    status_->setText(tr("브러시 %1개를 썼습니다").arg(brushes_.size()));
}

void BrushPalette::loadFromFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("브러시 불러오기"), QString(),
        tr("Splash 브러시 (*.splashbrush);;모든 파일 (*)"));

    if (path.isEmpty())
        return;

    if (!readFile(path, /*append*/ true))
    {
        QMessageBox::warning(this, tr("불러오기 실패"),
                             tr("Splash 브러시 파일이 아니거나 내용이 망가졌습니다."));
        return;
    }

    reloadList();
    saveDefault();
    status_->setText(tr("브러시를 읽었습니다 — %1개").arg(brushes_.size()));
}

} // namespace splash::ui
