#include "ui/location_editor.h"

#include "chk/map_document.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace splash::ui {
namespace {

/// Chk::Location::Elevation 비트와 이름.
struct ElevationBit { std::uint16_t bit; const char * label; };

const ElevationBit kElevations[6] {
    { 0x01, QT_TRANSLATE_NOOP("LocationEditor", "저지대") },
    { 0x02, QT_TRANSLATE_NOOP("LocationEditor", "중지대") },
    { 0x04, QT_TRANSLATE_NOOP("LocationEditor", "고지대") },
    { 0x08, QT_TRANSLATE_NOOP("LocationEditor", "저공") },
    { 0x10, QT_TRANSLATE_NOOP("LocationEditor", "중공") },
    { 0x20, QT_TRANSLATE_NOOP("LocationEditor", "고공") },
};

} // namespace

LocationEditor::LocationEditor(chk::MapDocument & document, QWidget * parent)
    : QDialog(parent), document_(document)
{
    setWindowTitle(tr("로케이션"));
    resize(880, 560);

    list_ = new QTableWidget(0, 4, this);
    list_->setHorizontalHeaderLabels({tr("번호"), tr("이름"), tr("자리"), tr("크기")});
    list_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    list_->verticalHeader()->setVisible(false);
    list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // --- 오른쪽: 고른 로케이션 ---
    auto * form = new QFormLayout();

    name_ = new QLineEdit(this);
    form->addRow(tr("이름"), name_);

    const auto makeSpin = [this] {
        auto * box = new QSpinBox(this);
        box->setRange(0, 0xFFFF * 32);
        box->setSingleStep(32);
        box->setSuffix(tr(" px"));
        box->setKeyboardTracking(false);
        return box;
    };

    left_ = makeSpin();
    top_ = makeSpin();
    right_ = makeSpin();
    bottom_ = makeSpin();

    auto * leftTop = new QHBoxLayout();
    leftTop->addWidget(left_);
    leftTop->addWidget(top_);
    form->addRow(tr("왼쪽·위"), leftTop);

    auto * rightBottom = new QHBoxLayout();
    rightBottom->addWidget(right_);
    rightBottom->addWidget(bottom_);
    form->addRow(tr("오른쪽·아래"), rightBottom);

    auto * elevationBox = new QGroupBox(tr("잡는 높이"), this);
    auto * elevationLayout = new QHBoxLayout(elevationBox);
    for (int i = 0; i < 6; ++i)
    {
        elevation_[i] = new QCheckBox(tr(kElevations[i].label), elevationBox);
        elevationLayout->addWidget(elevation_[i]);
    }
    elevationLayout->addStretch();

    auto * applyButton = new QPushButton(tr("이 로케이션에 적용"), this);
    connect(applyButton, &QPushButton::clicked, this, [this] { applyCurrent(); });

    auto * addButton = new QPushButton(tr("새로 만들기"), this);
    auto * removeButton = new QPushButton(tr("지우기"), this);
    removeIfUsed_ = new QCheckBox(tr("쓰는 중이어도 지우기"), this);

    auto * listButtons = new QHBoxLayout();
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    listButtons->addWidget(removeIfUsed_);
    listButtons->addStretch();

    status_ = new QLabel(this);
    status_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    status_->setWordWrap(true);

    auto * right = new QVBoxLayout();
    right->addLayout(form);
    right->addWidget(elevationBox);
    right->addWidget(applyButton);
    right->addStretch();
    right->addWidget(status_);

    auto * columns = new QHBoxLayout();
    auto * leftPanel = new QVBoxLayout();
    leftPanel->addWidget(list_, 1);
    leftPanel->addLayout(listButtons);
    columns->addLayout(leftPanel, 3);
    columns->addLayout(right, 2);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto * layout = new QVBoxLayout(this);
    layout->addLayout(columns, 1);
    layout->addWidget(buttons);

    connect(list_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (loading_ || row < 0)
            return;
        loadSelected();
        emit locationFocused(static_cast<std::size_t>(row));
    });

    connect(addButton, &QPushButton::clicked, this, [this] { addLocation(); });
    connect(removeButton, &QPushButton::clicked, this, [this] { removeSelected(); });

    reloadList();
}

int LocationEditor::currentRow() const
{
    return list_ == nullptr ? -1 : list_->currentRow();
}

void LocationEditor::reloadList(int selectRow)
{
    const auto & locations = document_.locations();

    loading_ = true;
    list_->setRowCount(static_cast<int>(locations.size()));

    for (std::size_t row = 0; row < locations.size(); ++row)
    {
        const auto & location = locations[row];
        const int r = static_cast<int>(row);

        list_->setItem(r, 0, new QTableWidgetItem(QString::number(location.index)));
        list_->setItem(r, 1, new QTableWidgetItem(
            location.name.empty() ? tr("(이름 없음)") : QString::fromStdString(location.name)));
        list_->setItem(r, 2, new QTableWidgetItem(
            QStringLiteral("%1, %2").arg(location.left).arg(location.top)));
        list_->setItem(r, 3, new QTableWidgetItem(
            QStringLiteral("%1 x %2")
                .arg(static_cast<int>(location.right) - static_cast<int>(location.left))
                .arg(static_cast<int>(location.bottom) - static_cast<int>(location.top))));
    }
    loading_ = false;

    setWindowTitle(tr("로케이션 — %1개").arg(locations.size()));

    if (!locations.empty())
    {
        const int row = std::clamp(selectRow, 0, static_cast<int>(locations.size()) - 1);
        list_->setCurrentCell(row, 1);
        loadSelected();
    }
}

void LocationEditor::loadSelected()
{
    const int row = currentRow();
    const auto & locations = document_.locations();
    if (row < 0 || row >= static_cast<int>(locations.size()))
        return;

    const auto & location = locations[static_cast<std::size_t>(row)];

    loading_ = true;
    name_->setText(QString::fromStdString(location.name));
    left_->setValue(static_cast<int>(location.left));
    top_->setValue(static_cast<int>(location.top));
    right_->setValue(static_cast<int>(location.right));
    bottom_->setValue(static_cast<int>(location.bottom));

    for (int i = 0; i < 6; ++i)
        elevation_[i]->setChecked((location.elevationFlags & kElevations[i].bit) != 0);
    loading_ = false;

    status_->setText(tr("로케이션 %1번").arg(location.index));
}

void LocationEditor::applyCurrent()
{
    const int row = currentRow();
    const auto & locations = document_.locations();
    if (row < 0 || row >= static_cast<int>(locations.size()))
        return;

    const auto index = static_cast<std::size_t>(row);
    const auto before = locations[index];

    bool changed = false;

    if (name_->text().toStdString() != before.name)
        changed |= document_.setLocationName(index, name_->text().toStdString());

    if (static_cast<std::uint32_t>(left_->value()) != before.left ||
        static_cast<std::uint32_t>(top_->value()) != before.top ||
        static_cast<std::uint32_t>(right_->value()) != before.right ||
        static_cast<std::uint32_t>(bottom_->value()) != before.bottom)
    {
        changed |= document_.setLocationBounds(index,
            static_cast<std::uint32_t>(left_->value()),
            static_cast<std::uint32_t>(top_->value()),
            static_cast<std::uint32_t>(right_->value()),
            static_cast<std::uint32_t>(bottom_->value()));
    }

    std::uint16_t flags = 0;
    for (int i = 0; i < 6; ++i)
    {
        if (elevation_[i]->isChecked())
            flags = static_cast<std::uint16_t>(flags | kElevations[i].bit);
    }
    if (flags != before.elevationFlags)
        changed |= document_.setLocationElevationFlags(index, flags);

    if (!changed)
    {
        status_->setText(QString::fromStdString(document_.lastError()).isEmpty()
            ? tr("바뀐 내용이 없습니다")
            : QString::fromStdString(document_.lastError()));
        return;
    }

    emit documentEdited();
    reloadList(row);
    status_->setText(tr("로케이션을 고쳤습니다"));
}

void LocationEditor::addLocation()
{
    // 맵 가운데에 두 타일짜리로 만든다. 자리는 곧바로 고칠 수 있다.
    const auto & info = document_.info();
    const std::uint32_t centreX = static_cast<std::uint32_t>(info.width) * 32 / 2;
    const std::uint32_t centreY = static_cast<std::uint32_t>(info.height) * 32 / 2;

    if (!document_.addLocation(centreX - 32, centreY - 32, centreX + 32, centreY + 32,
                               tr("새 로케이션").toStdString()))
    {
        QMessageBox::warning(this, tr("만들지 못했습니다"),
                             QString::fromStdString(document_.lastError()));
        return;
    }

    emit documentEdited();
    reloadList(static_cast<int>(document_.locations().size()) - 1);
    status_->setText(tr("로케이션을 만들었습니다"));
}

void LocationEditor::removeSelected()
{
    const int row = currentRow();
    if (row < 0 || row >= static_cast<int>(document_.locations().size()))
        return;

    if (!document_.removeLocation(static_cast<std::size_t>(row), removeIfUsed_->isChecked()))
    {
        QMessageBox::warning(this, tr("지우지 못했습니다"),
                             QString::fromStdString(document_.lastError()));
        return;
    }

    emit documentEdited();
    reloadList(std::max(0, row - 1));
    status_->setText(tr("로케이션을 지웠습니다"));
}

} // namespace splash::ui
