#include "ui/switch_editor.h"

#include "chk/map_document.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace splash::ui {

SwitchEditor::SwitchEditor(chk::MapDocument & document, QWidget * parent)
    : QDialog(parent), document_(document)
{
    setWindowTitle(tr("스위치 이름"));
    resize(520, 620);

    list_ = new QTableWidget(0, 2, this);
    list_->setHorizontalHeaderLabels({tr("번호"), tr("이름")});
    list_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    list_->verticalHeader()->setVisible(false);

    status_ = new QLabel(
        tr("이름을 붙이면 트리거 편집기의 스위치 목록에 그 이름이 나옵니다."), this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto * apply = new QPushButton(tr("적용"), this);
    connect(apply, &QPushButton::clicked, this, [this] { applyChanges(); });

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto * layout = new QVBoxLayout(this);
    layout->addWidget(list_, 1);
    layout->addWidget(status_);
    layout->addWidget(apply);
    layout->addWidget(buttons);

    reloadList();
}

void SwitchEditor::reloadList()
{
    names_ = document_.switchNames();

    list_->setRowCount(static_cast<int>(names_.size()));
    for (std::size_t i = 0; i < names_.size(); ++i)
    {
        const int row = static_cast<int>(i);

        auto * number = new QTableWidgetItem(QString::number(i + 1));
        number->setFlags(number->flags() & ~Qt::ItemIsEditable);
        list_->setItem(row, 0, number);

        list_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(names_[i])));
    }
}

void SwitchEditor::applyChanges()
{
    int changed = 0;
    for (std::size_t i = 0; i < names_.size(); ++i)
    {
        const QTableWidgetItem * item = list_->item(static_cast<int>(i), 1);
        if (item == nullptr)
            continue;

        const std::string edited = item->text().toStdString();
        if (edited == names_[i])
            continue;

        if (document_.setSwitchName(i, edited))
            ++changed;
    }

    if (changed == 0)
    {
        status_->setText(tr("바뀐 이름이 없습니다."));
        return;
    }

    emit documentEdited();
    reloadList();
    status_->setText(tr("스위치 이름 %1개를 바꿨습니다.").arg(changed));
}

} // namespace splash::ui
