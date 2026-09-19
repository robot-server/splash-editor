#include "ui/briefing_editor.h"

#include "chk/map_document.h"
#include "ui/code_editor.h"
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
    resize(920, 620);

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
    auto * actionBox = new QGroupBox(tr("동작"), this);
    auto * actionLayout = new QVBoxLayout(actionBox);
    actionLayout->addWidget(actions_);

    text_ = new CodeEditor(this);
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
                                       text_->toPlainText().toStdString(), graphics_))
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
    detailLayout->addWidget(actionBox);
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
    text_->clear();
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

            for (const auto & action : detail->actions)
                actions_->addItem(QString::fromStdString(action));

            text_->setPlainText(QString::fromStdString(detail->text));
        }
    }
    loading_ = false;
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
