#include "ui/briefing_editor.h"

#include "chk/map_document.h"
#include "ui/code_editor_pane.h"
#include "ui/trigger_argument_panel.h"
#include "io/game_graphics.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFont>
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

/// 트리거 소유자 배열에서 "모든 플레이어" 가 앉는 자리.
constexpr std::size_t kAllPlayersSlot = 17;

/// 브리핑에서 쓸 수 있는 동작. 문법이 생각나지 않을 때 보라고 적어 둔다.
const char * const kBriefingActions =
    "Wait(밀리초);\n"
    "Play WAV(\"경로\", 밀리초);\n"
    "Display Speaking Portrait(슬롯, 밀리초);\n"
    "Show Portrait(\"유닛\", 슬롯);\n"
    "Hide Portrait(슬롯);\n"
    "Text Message(\"내용\");\n"
    "Mission Objectives(\"내용\");\n"
    "Transmission(\"유닛\", 슬롯, \"경로\", 방식, 값, \"내용\");\n"
    "Enable Skip Tutorial Button();";

} // namespace

BriefingEditor::BriefingEditor(chk::MapDocument & document, io::GameGraphics & graphics,
                               QWidget * parent)
    : QDialog(parent), document_(document), graphics_(graphics)
{
    setWindowTitle(tr("미션 브리핑"));
    resize(1360, 760);

    // --- 왼쪽: 브리핑 목록 ---
    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);

    auto * addButton = new QPushButton(tr("추가"), this);
    auto * removeButton = new QPushButton(tr("삭제"), this);
    auto * upButton = new QPushButton(tr("위로"), this);
    auto * downButton = new QPushButton(tr("아래로"), this);

    auto * listButtons = new QHBoxLayout();
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    listButtons->addWidget(upButton);
    listButtons->addWidget(downButton);
    listButtons->addStretch();

    auto * listPanel = new QWidget(this);
    auto * listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 0, 0);
    summary_ = new QLabel(this);
    listLayout->addWidget(summary_);
    listLayout->addWidget(list_, 1);
    listLayout->addLayout(listButtons);

    // --- 오른쪽: 고른 브리핑 ---
    auto * ownerBox = new QGroupBox(tr("이 브리핑을 보는 플레이어"), this);
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

    actions_ = new QListWidget(this);
    actionArgs_ = new TriggerArgumentPanel(TriggerArgumentPanel::Kind::Action, this);

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

    connect(actions_, &QListWidget::currentRowChanged, this,
            [this](int row) { showActionArgs(row); });

    const auto briefingIndex = [this] { return static_cast<std::size_t>(currentIndex()); };

    connect(actionArgs_, &TriggerArgumentPanel::typeChanged, this,
            [this, briefingIndex](std::size_t slot, std::uint8_t type) {
        if (currentIndex() < 0) return;
        const int row = actions_->currentRow();
        if (document_.setBriefingActionType(briefingIndex(), slot, type))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(row);
        }
    });
    connect(actionArgs_, &TriggerArgumentPanel::argChanged, this,
            [this, briefingIndex](std::size_t slot, std::size_t argIndex, std::uint32_t value) {
        if (currentIndex() < 0) return;
        const int row = actions_->currentRow();
        if (document_.setBriefingActionArg(briefingIndex(), slot, argIndex, value))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(row);
        }
    });
    connect(actionArgs_, &TriggerArgumentPanel::argTextChanged, this,
            [this, briefingIndex](std::size_t slot, std::size_t argIndex, const QString & text) {
        if (currentIndex() < 0) return;
        const int row = actions_->currentRow();
        if (document_.setBriefingActionArgText(briefingIndex(), slot, argIndex, text.toStdString()))
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

    connect(removeAction, &QPushButton::clicked, this, [this, briefingIndex] {
        const int row = actions_->currentRow();
        if (currentIndex() < 0 || row < 0) return;
        if (document_.removeBriefingAction(briefingIndex(), static_cast<std::size_t>(row)))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(row);
        }
    });

    const auto moveAction = [this, briefingIndex](int delta) {
        const int row = actions_->currentRow();
        const int target = row + delta;
        if (currentIndex() < 0 || row < 0 || target < 0 || target >= actions_->count())
            return;
        if (document_.moveBriefingAction(briefingIndex(), static_cast<std::size_t>(row),
                                         static_cast<std::size_t>(target)))
        {
            emit documentEdited();
            reloadElements();
            actions_->setCurrentRow(target);
        }
    };
    connect(actionUp, &QPushButton::clicked, this, [moveAction] { moveAction(-1); });
    connect(actionDown, &QPushButton::clicked, this, [moveAction] { moveAction(1); });

    text_ = new CodeEditorPane(this);
    text_->setVocabulary(document_.triggerVocabulary(graphics_));

    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);

    hint_ = new QLabel(this);
    hint_->setText(tr("쓸 수 있는 동작:\n%1").arg(QString::fromUtf8(kBriefingActions)));
    hint_->setWordWrap(true);
    hint_->setFont(mono);
    hint_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto * applyText = new QPushButton(tr("이 브리핑에 적용"), this);
    connect(applyText, &QPushButton::clicked, this, [this] {
        const int index = currentIndex();
        if (index < 0)
            return;

        if (!document_.setBriefingText(static_cast<std::size_t>(index),
                                       text_->text().toStdString(), graphics_))
        {
            QMessageBox::warning(this, tr("적용 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(index);
    });

    auto * textBox = new QGroupBox(tr("이 브리핑의 텍스트 — 동작의 인자는 여기서 고친다"), this);
    auto * textLayout = new QVBoxLayout(textBox);
    textLayout->addWidget(text_, 1);
    textLayout->addWidget(hint_);
    textLayout->addWidget(applyText);

    auto * detailPanel = new QWidget(this);
    auto * detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(ownerBox);
    detailLayout->addWidget(actionBox, 1);

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
        if (!document_.addBriefing())
        {
            QMessageBox::warning(this, tr("추가 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(list_->count());
    });

    connect(removeButton, &QPushButton::clicked, this, [this] {
        const int index = currentIndex();
        if (index < 0)
            return;
        if (!document_.removeBriefing(static_cast<std::size_t>(index)))
        {
            QMessageBox::warning(this, tr("삭제 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(std::max(0, index - 1));
    });

    const auto move = [this](int delta) {
        const int index = currentIndex();
        const int target = index + delta;
        if (index < 0 || target < 0 || target >= list_->count())
            return;
        if (!document_.moveBriefing(static_cast<std::size_t>(index),
                                    static_cast<std::size_t>(target)))
        {
            QMessageBox::warning(this, tr("순서 바꾸기 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }
        emit documentEdited();
        reloadList(target);
    };
    connect(upButton, &QPushButton::clicked, this, [move] { move(-1); });
    connect(downButton, &QPushButton::clicked, this, [move] { move(1); });

    reloadList(0);
}

BriefingEditor::~BriefingEditor() = default;

int BriefingEditor::currentIndex() const
{
    return list_ == nullptr ? -1 : list_->currentRow();
}

void BriefingEditor::reloadList(int selectRow)
{
    const auto summaries = document_.briefingSummaries(graphics_);

    loading_ = true;
    list_->clear();
    for (const auto & summary : summaries)
    {
        QString label = tr("#%1  [%2]  동작 %3")
            .arg(summary.index + 1)
            .arg(QString::fromStdString(summary.players))
            .arg(summary.actions);

        if (!summary.firstAction.empty())
            label += QStringLiteral("  —  ") + QString::fromStdString(summary.firstAction);

        list_->addItem(label);
    }
    loading_ = false;

    summary_->setText(tr("브리핑 %1개").arg(summaries.size()));

    if (!summaries.empty())
    {
        const int row = std::clamp(selectRow, 0, static_cast<int>(summaries.size()) - 1);
        list_->setCurrentRow(row);
    }
    else
    {
        reloadDetail();
    }
}

void BriefingEditor::reloadDetail()
{
    const int index = currentIndex();

    loading_ = true;
    actions_->clear();
    text_->setText(QString());
    for (auto * box : owners_)
    {
        if (box != nullptr)
            box->setChecked(false);
    }

    if (index >= 0)
    {
        if (auto detail = document_.briefingDetail(static_cast<std::size_t>(index), graphics_))
        {
            for (int i = 0; i < 8; ++i)
                owners_[i]->setChecked(detail->owners[static_cast<std::size_t>(i)]);
            owners_[8]->setChecked(detail->owners[kAllPlayersSlot]);

            text_->setText(QString::fromStdString(detail->text));
        }
    }
    loading_ = false;

    reloadElements();
}

/// 동작 목록을 다시 읽는다. 마지막에 빈 자리를 한 줄 남겨 두어 거기서
/// 종류를 고르면 새 동작이 된다.
void BriefingEditor::reloadElements()
{
    const int index = currentIndex();

    const bool wasLoading = loading_;
    loading_ = true;

    actions_->clear();
    actionElements_.clear();

    if (index >= 0)
    {
        if (actionTypes_.empty())
            actionTypes_ = document_.briefingActionTypes(graphics_);

        actionElements_ = document_.briefingActions(static_cast<std::size_t>(index), graphics_);

        std::size_t used = 0;
        for (std::size_t i = 0; i < actionElements_.size(); ++i)
        {
            if (actionElements_[i].type != 0)
                used = i + 1;
        }

        for (std::size_t i = 0; i < actionElements_.size() && i <= used; ++i)
        {
            const auto & element = actionElements_[i];
            QString label = element.type == 0
                ? tr("(빈 자리 — 여기에 새로 넣습니다)")
                : QString::fromStdString(element.text);
            if (element.disabled)
                label = tr("[꺼짐] ") + label;
            actions_->addItem(label);
        }
    }

    loading_ = wasLoading;
    actionArgs_->clear();
}

void BriefingEditor::showActionArgs(int row)
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

void BriefingEditor::applyOwners()
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

    if (document_.setBriefingOwners(static_cast<std::size_t>(index), owners))
    {
        emit documentEdited();
        reloadList(index);
    }
}

} // namespace splash::ui
