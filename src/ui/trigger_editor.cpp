#include "ui/trigger_editor.h"

#include "ui/code_editor_pane.h"
#include "ui/trigger_argument_panel.h"

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
    resize(1360, 760);

    // --- 왼쪽: 트리거 목록 ---
    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);

    auto * addButton = new QPushButton(tr("추가"), this);
    auto * duplicateButton = new QPushButton(tr("복제"), this);
    auto * removeButton = new QPushButton(tr("삭제"), this);

    auto * listButtons = new QHBoxLayout();
    listButtons->addWidget(addButton);
    listButtons->addWidget(duplicateButton);
    listButtons->addWidget(removeButton);
    listButtons->addStretch();

    connect(duplicateButton, &QPushButton::clicked, this, [this] {
        const int index = currentIndex();
        if (index < 0)
            return;
        if (!document_.duplicateTrigger(static_cast<std::size_t>(index)))
        {
            QMessageBox::warning(this, tr("복제 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(index + 1);
    });

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
    conditionArgs_ = new TriggerArgumentPanel(TriggerArgumentPanel::Kind::Condition, this);
    actionArgs_ = new TriggerArgumentPanel(TriggerArgumentPanel::Kind::Action, this);

    // 조건 — 목록에서 한 줄을 고르면 아래에 그 줄의 인자가 뜬다.
    auto * conditionBox = new QGroupBox(tr("조건"), this);
    auto * conditionLayout = new QVBoxLayout(conditionBox);
    conditionLayout->addWidget(conditions_, 1);

    auto * conditionButtons = new QHBoxLayout();
    auto * removeCondition = new QPushButton(tr("줄 지우기"), this);
    auto * conditionUp = new QPushButton(tr("위로"), this);
    auto * conditionDown = new QPushButton(tr("아래로"), this);
    conditionButtons->addWidget(removeCondition);
    conditionButtons->addWidget(conditionUp);
    conditionButtons->addWidget(conditionDown);
    conditionButtons->addStretch();
    conditionLayout->addLayout(conditionButtons);
    conditionLayout->addWidget(conditionArgs_, 1);

    // 동작 — 같은 구성.
    auto * actionBox = new QGroupBox(tr("동작"), this);
    auto * actionLayout = new QVBoxLayout(actionBox);
    actionLayout->addWidget(actions_, 1);

    auto * actionButtons = new QHBoxLayout();
    auto * removeAction = new QPushButton(tr("줄 지우기"), this);
    auto * actionUp = new QPushButton(tr("위로"), this);
    auto * actionDown = new QPushButton(tr("아래로"), this);
    actionButtons->addWidget(removeAction);
    actionButtons->addWidget(actionUp);
    actionButtons->addWidget(actionDown);
    actionButtons->addStretch();
    actionLayout->addLayout(actionButtons);
    actionLayout->addWidget(actionArgs_, 1);

    connect(conditions_, &QListWidget::currentRowChanged, this,
            [this](int row) { showConditionArgs(row); });
    connect(actions_, &QListWidget::currentRowChanged, this,
            [this](int row) { showActionArgs(row); });

    // --- 인자 판에서 올라오는 편집 ---
    const auto triggerIndex = [this] { return static_cast<std::size_t>(currentIndex()); };

    connect(conditionArgs_, &TriggerArgumentPanel::typeChanged, this,
            [this, triggerIndex](std::size_t slot, std::uint8_t type) {
        if (currentIndex() < 0) return;
        const int row = conditions_->currentRow();
        if (document_.setConditionType(triggerIndex(), slot, type))
        {
            emit documentEdited();
            reloadElements();
            conditions_->setCurrentRow(row);
        }
    });
    connect(conditionArgs_, &TriggerArgumentPanel::argChanged, this,
            [this, triggerIndex](std::size_t slot, std::size_t argIndex, std::uint32_t value) {
        if (currentIndex() < 0) return;
        const int row = conditions_->currentRow();
        if (document_.setConditionArg(triggerIndex(), slot, argIndex, value))
        {
            emit documentEdited();
            reloadElements();
            conditions_->setCurrentRow(row);
        }
    });

    // EUD 자리는 플레이어와 유닛을 함께 바꾼다.
    const auto applyEudSlot = [this, triggerIndex](std::size_t slot,
                                     std::size_t playerArg, std::uint32_t player,
                                     std::size_t unitArg, std::uint32_t unit) {
        if (currentIndex() < 0)
            return;

        const int row = conditions_->currentRow();
        const bool unitOk = document_.setConditionArg(triggerIndex(), slot, unitArg, unit);
        const bool playerOk = document_.setConditionArg(triggerIndex(), slot, playerArg, player);

        if (unitOk || playerOk)
        {
            emit documentEdited();
            reloadElements();
            conditions_->setCurrentRow(row);
        }
    };
    connect(conditionArgs_, &TriggerArgumentPanel::eudSlotPicked, this, applyEudSlot);

    connect(actionArgs_, &TriggerArgumentPanel::eudSlotPicked, this,
            [this, triggerIndex](std::size_t slot, std::size_t playerArg,
                                 std::uint32_t player, std::size_t unitArg,
                                 std::uint32_t unit) {
        if (currentIndex() < 0)
            return;

        const int row = actions_->currentRow();
        const bool unitOk = document_.setActionArg(triggerIndex(), slot, unitArg, unit);
        const bool playerOk = document_.setActionArg(triggerIndex(), slot, playerArg, player);

        if (unitOk || playerOk)
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(row);
        }
    });

    connect(actionArgs_, &TriggerArgumentPanel::typeChanged, this,
            [this, triggerIndex](std::size_t slot, std::uint8_t type) {
        if (currentIndex() < 0) return;
        const int row = actions_->currentRow();
        if (document_.setActionType(triggerIndex(), slot, type))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(row);
        }
    });
    connect(actionArgs_, &TriggerArgumentPanel::argChanged, this,
            [this, triggerIndex](std::size_t slot, std::size_t argIndex, std::uint32_t value) {
        if (currentIndex() < 0) return;
        const int row = actions_->currentRow();
        if (document_.setActionArg(triggerIndex(), slot, argIndex, value))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(row);
        }
    });
    connect(actionArgs_, &TriggerArgumentPanel::argTextChanged, this,
            [this, triggerIndex](std::size_t slot, std::size_t argIndex, const QString & text) {
        if (currentIndex() < 0) return;
        const int row = actions_->currentRow();
        if (document_.setActionArgText(triggerIndex(), slot, argIndex, text.toStdString()))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(row);
        }
        else
        {
            QMessageBox::warning(this, tr("적용 실패"),
                                 QString::fromStdString(document_.lastError()));
        }
    });

    // --- 줄 지우기·순서 바꾸기 ---
    connect(removeCondition, &QPushButton::clicked, this, [this, triggerIndex] {
        const int row = conditions_->currentRow();
        if (currentIndex() < 0 || row < 0) return;
        if (document_.removeCondition(triggerIndex(), static_cast<std::size_t>(row)))
        {
            emit documentEdited();
            reloadElements();
            conditions_->setCurrentRow(row);
        }
    });
    connect(removeAction, &QPushButton::clicked, this, [this, triggerIndex] {
        const int row = actions_->currentRow();
        if (currentIndex() < 0 || row < 0) return;
        if (document_.removeAction(triggerIndex(), static_cast<std::size_t>(row)))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(row);
        }
    });

    const auto moveCondition = [this, triggerIndex](int delta) {
        const int row = conditions_->currentRow();
        const int target = row + delta;
        if (currentIndex() < 0 || row < 0 || target < 0 || target >= conditions_->count())
            return;
        if (document_.moveCondition(triggerIndex(), static_cast<std::size_t>(row),
                                    static_cast<std::size_t>(target)))
        {
            emit documentEdited();
            reloadElements();
            conditions_->setCurrentRow(target);
        }
    };
    connect(conditionUp, &QPushButton::clicked, this, [moveCondition] { moveCondition(-1); });
    connect(conditionDown, &QPushButton::clicked, this, [moveCondition] { moveCondition(1); });

    const auto moveAction = [this, triggerIndex](int delta) {
        const int row = actions_->currentRow();
        const int target = row + delta;
        if (currentIndex() < 0 || row < 0 || target < 0 || target >= actions_->count())
            return;
        if (document_.moveAction(triggerIndex(), static_cast<std::size_t>(row),
                                 static_cast<std::size_t>(target)))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(target);
        }
    };
    connect(actionUp, &QPushButton::clicked, this, [moveAction] { moveAction(-1); });
    connect(actionDown, &QPushButton::clicked, this, [moveAction] { moveAction(1); });

    // 텍스트 쪽은 코드 편집기다 — 줄 번호·구문 강조·자동 완성·문법 검사에
    // 찾기·바꾸기와 문제 목록까지 붙은 판이다.
    text_ = new CodeEditorPane(this);
    text_->setVocabulary(document_.triggerVocabulary(graphics_));

    auto * applyText = new QPushButton(tr("이 트리거에 적용"), this);
    connect(applyText, &QPushButton::clicked, this, [this] {
        const int index = currentIndex();
        if (index < 0)
            return;

        if (!document_.applyTriggerText(static_cast<std::size_t>(index),
                                        text_->text().toStdString(), graphics_))
        {
            QMessageBox::warning(this, tr("적용 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(index);
    });

    auto * textBox = new QGroupBox(tr("이 트리거의 텍스트"), this);
    auto * textLayout = new QVBoxLayout(textBox);
    textLayout->addWidget(text_, 1);
    textLayout->addWidget(applyText);

    // 가운데 — 실행 플레이어와 조건·동작. 조건과 동작은 위아래로 나눠
    // 각자 인자 판을 넉넉히 갖는다.
    auto * detailPanel = new QWidget(this);
    auto * detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(ownerBox);
    detailLayout->addWidget(enabled_);

    auto * elementSplitter = new QSplitter(Qt::Vertical, this);
    elementSplitter->addWidget(conditionBox);
    elementSplitter->addWidget(actionBox);
    elementSplitter->setSizes({320, 320});
    detailLayout->addWidget(elementSplitter, 1);

    // 오른쪽 — 코드 편집기를 창 높이만큼 크게 둔다.
    auto * splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(listPanel);
    splitter->addWidget(detailPanel);
    splitter->addWidget(textBox);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 2);
    splitter->setSizes({260, 460, 560});

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
    text_->setText(QString());
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

            text_->setText(QString::fromStdString(detail->text));
        }
    }

    loading_ = false;

    reloadElements();
}

/// 조건·액션 목록을 다시 읽어 채운다. 마지막 빈 줄 하나를 남겨 두어
/// 거기서 종류를 고르면 새 줄이 된다 — StarEdit 과 같은 방식이다.
void TriggerEditor::reloadElements()
{
    const int index = currentIndex();

    const bool wasLoading = loading_;
    loading_ = true;

    conditions_->clear();
    actions_->clear();
    conditionElements_.clear();
    actionElements_.clear();

    if (index >= 0)
    {
        if (conditionTypes_.empty())
            conditionTypes_ = document_.conditionTypes(graphics_);
        if (actionTypes_.empty())
            actionTypes_ = document_.actionTypes(graphics_);

        conditionElements_ = document_.triggerConditions(static_cast<std::size_t>(index), graphics_);
        actionElements_ = document_.triggerActions(static_cast<std::size_t>(index), graphics_);

        const auto fill = [](QListWidget * list, const std::vector<io::TriggerElement> & elements) {
            // 쓰인 줄까지만 보여 주고, 그 다음 한 줄을 빈 자리로 남긴다.
            std::size_t used = 0;
            for (std::size_t i = 0; i < elements.size(); ++i)
            {
                if (elements[i].type != 0)
                    used = i + 1;
            }

            for (std::size_t i = 0; i < elements.size() && i <= used; ++i)
            {
                const auto & element = elements[i];
                QString label = element.type == 0
                    ? tr("(빈 자리 — 여기에 새로 넣습니다)")
                    : QString::fromStdString(element.text);
                if (element.disabled)
                    label = tr("[꺼짐] ") + label;
                list->addItem(label);
            }
        };

        fill(conditions_, conditionElements_);
        fill(actions_, actionElements_);
    }

    loading_ = wasLoading;

    conditionArgs_->clear();
    actionArgs_->clear();
}

void TriggerEditor::showConditionArgs(int row)
{
    const int index = currentIndex();
    if (index < 0 || row < 0 || row >= static_cast<int>(conditionElements_.size()))
    {
        conditionArgs_->clear();
        return;
    }

    conditionArgs_->setElement(static_cast<std::size_t>(index), static_cast<std::size_t>(row),
                               conditionElements_[static_cast<std::size_t>(row)], conditionTypes_);
}

void TriggerEditor::showActionArgs(int row)
{
    const int index = currentIndex();
    if (index < 0 || row < 0 || row >= static_cast<int>(actionElements_.size()))
    {
        actionArgs_->clear();
        return;
    }

    actionArgs_->setElement(static_cast<std::size_t>(index), static_cast<std::size_t>(row),
                            actionElements_[static_cast<std::size_t>(row)], actionTypes_);
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
