#include "ui/trigger_editor.h"

#include "chk/map_document.h"
#include "io/game_graphics.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace splash::ui {
namespace {

/// owners 배열에서 "모든 플레이어"가 놓인 자리.
constexpr std::size_t kAllPlayersSlot = 17;

} // namespace

TriggerEditor::TriggerEditor(chk::MapDocument & document, io::GameGraphics & graphics,
                             QWidget * parent)
    : QDialog(parent), document_(document), graphics_(graphics)
{
    setWindowTitle(tr("트리거 편집기"));
    resize(920, 620);

    // --- 왼쪽: 트리거 목록 ---
    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);

    auto * addButton = new QPushButton(tr("추가"), this);
    auto * removeButton = new QPushButton(tr("삭제"), this);

    auto * listButtons = new QHBoxLayout();
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    listButtons->addStretch();

    auto * listPanel = new QWidget(this);
    auto * listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 0, 0);
    summary_ = new QLabel(this);
    listLayout->addWidget(summary_);
    listLayout->addWidget(list_, 1);
    listLayout->addLayout(listButtons);

    // --- 오른쪽: 고른 트리거 ---
    auto * ownerBox = new QGroupBox(tr("실행 플레이어"), this);
    auto * ownerLayout = new QHBoxLayout(ownerBox);
    for (int i = 0; i < 8; ++i)
    {
        owners_[i] = new QCheckBox(QString::number(i + 1), ownerBox);
        ownerLayout->addWidget(owners_[i]);
        connect(owners_[i], &QCheckBox::toggled, this, [this] { applyOwners(); });
    }
    owners_[8] = new QCheckBox(tr("모두"), ownerBox);
    ownerLayout->addWidget(owners_[8]);
    ownerLayout->addStretch();
    connect(owners_[8], &QCheckBox::toggled, this, [this] { applyOwners(); });

    enabled_ = new QCheckBox(tr("트리거 사용"), this);
    connect(enabled_, &QCheckBox::toggled, this, [this](bool on) {
        if (loading_)
            return;
        const int index = currentIndex();
        if (index < 0)
            return;
        if (document_.setTriggerEnabled(static_cast<std::size_t>(index), on))
        {
            emit documentEdited();
            reloadList(index);
        }
    });

    conditions_ = new QListWidget(this);
    actions_ = new QListWidget(this);

    auto * conditionBox = new QGroupBox(tr("조건"), this);
    auto * conditionLayout = new QVBoxLayout(conditionBox);
    conditionLayout->addWidget(conditions_);

    auto * actionBox = new QGroupBox(tr("동작"), this);
    auto * actionLayout = new QVBoxLayout(actionBox);
    actionLayout->addWidget(actions_);

    text_ = new QPlainTextEdit(this);
    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    text_->setFont(mono);

    auto * applyText = new QPushButton(tr("이 트리거에 적용"), this);
    connect(applyText, &QPushButton::clicked, this, [this] {
        const int index = currentIndex();
        if (index < 0)
            return;

        if (!document_.applyTriggerText(static_cast<std::size_t>(index),
                                        text_->toPlainText().toStdString(), graphics_))
        {
            QMessageBox::warning(this, tr("적용 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(index);
    });

    auto * textBox = new QGroupBox(tr("이 트리거의 텍스트 — 조건·동작의 인자는 여기서 고친다"), this);
    auto * textLayout = new QVBoxLayout(textBox);
    textLayout->addWidget(text_);
    textLayout->addWidget(applyText);

    auto * detailPanel = new QWidget(this);
    auto * detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(ownerBox);
    detailLayout->addWidget(enabled_);

    auto * listsRow = new QHBoxLayout();
    listsRow->addWidget(conditionBox);
    listsRow->addWidget(actionBox);
    detailLayout->addLayout(listsRow);
    detailLayout->addWidget(textBox, 1);

    auto * splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(listPanel);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 620});

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto * layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { reloadDetail(); });

    connect(addButton, &QPushButton::clicked, this, [this] {
        if (!document_.addTrigger())
        {
            QMessageBox::warning(this, tr("추가 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(static_cast<int>(document_.info().triggerCount) - 1);
    });

    connect(removeButton, &QPushButton::clicked, this, [this] {
        const int index = currentIndex();
        if (index < 0)
            return;
        if (!document_.removeTrigger(static_cast<std::size_t>(index)))
        {
            QMessageBox::warning(this, tr("삭제 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(std::max(0, index - 1));
    });

    reloadList(0);
}

TriggerEditor::~TriggerEditor() = default;

int TriggerEditor::currentIndex() const
{
    return list_ == nullptr ? -1 : list_->currentRow();
}

void TriggerEditor::reloadList(int selectRow)
{
    loading_ = true;

    const int keep = (selectRow >= 0) ? selectRow : list_->currentRow();
    list_->clear();

    const auto summaries = document_.triggerSummaries(graphics_);
    for (const auto & summary : summaries)
    {
        QString label = tr("#%1  %2  조건 %3 · 동작 %4")
            .arg(summary.index)
            .arg(QString::fromStdString(summary.players))
            .arg(summary.conditions)
            .arg(summary.actions);

        if (!summary.firstAction.empty())
            label += QStringLiteral("  — ") + QString::fromStdString(summary.firstAction);
        if (summary.disabled)
            label += tr("  [꺼짐]");

        list_->addItem(label);
    }

    summary_->setText(tr("트리거 %1개").arg(summaries.size()));

    if (!summaries.empty())
        list_->setCurrentRow(std::min<int>(keep, static_cast<int>(summaries.size()) - 1));

    loading_ = false;
    reloadDetail();
}

void TriggerEditor::reloadDetail()
{
    loading_ = true;

    conditions_->clear();
    actions_->clear();
    text_->clear();
    for (auto * box : owners_)
    {
        if (box != nullptr)
            box->setChecked(false);
    }

    const int index = currentIndex();
    if (index >= 0)
    {
        if (const auto detail = document_.triggerDetail(static_cast<std::size_t>(index), graphics_))
        {
            for (int i = 0; i < 8; ++i)
                owners_[i]->setChecked(detail->owners[static_cast<std::size_t>(i)]);
            owners_[8]->setChecked(detail->owners[kAllPlayersSlot]);

            enabled_->setChecked((detail->flags & 0x08) == 0); // Disabled 비트가 꺼져 있으면 사용

            for (const auto & condition : detail->conditions)
                conditions_->addItem(QString::fromStdString(condition));
            for (const auto & action : detail->actions)
                actions_->addItem(QString::fromStdString(action));

            text_->setPlainText(QString::fromStdString(detail->text));
        }
    }

    loading_ = false;
}

void TriggerEditor::applyOwners()
{
    if (loading_)
        return;

    const int index = currentIndex();
    if (index < 0)
        return;

    std::array<bool, 27> owners {};
    for (int i = 0; i < 8; ++i)
        owners[static_cast<std::size_t>(i)] = owners_[i]->isChecked();
    owners[kAllPlayersSlot] = owners_[8]->isChecked();

    if (document_.setTriggerOwners(static_cast<std::size_t>(index), owners))
    {
        emit documentEdited();
        reloadList(index);
    }
}

} // namespace splash::ui
