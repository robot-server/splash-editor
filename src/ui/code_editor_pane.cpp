#include "ui/code_editor_pane.h"

#include "ui/code_editor.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

namespace splash::ui {

CodeEditorPane::CodeEditorPane(QWidget * parent)
    : QWidget(parent)
{
    editor_ = new CodeEditor(this);

    // --- 찾기·바꾸기 줄 ---
    findBar_ = new QWidget(this);
    auto * findLayout = new QVBoxLayout(findBar_);
    findLayout->setContentsMargins(4, 4, 4, 4);
    findLayout->setSpacing(3);

    auto * findRow = new QHBoxLayout();
    findField_ = new QLineEdit(findBar_);
    findField_->setPlaceholderText(tr("찾기"));
    auto * findPrev = new QToolButton(findBar_);
    findPrev->setText(QStringLiteral("▲"));
    findPrev->setToolTip(tr("이전 (Shift+Enter)"));
    auto * findNext = new QToolButton(findBar_);
    findNext->setText(QStringLiteral("▼"));
    findNext->setToolTip(tr("다음 (Enter)"));
    caseSensitive_ = new QCheckBox(tr("Aa"), findBar_);
    caseSensitive_->setToolTip(tr("대소문자 구분"));
    wholeWords_ = new QCheckBox(tr("단어"), findBar_);
    wholeWords_->setToolTip(tr("낱말 전체만"));
    auto * closeFind = new QToolButton(findBar_);
    closeFind->setText(QStringLiteral("✕"));

    findRow->addWidget(findField_, 1);
    findRow->addWidget(findPrev);
    findRow->addWidget(findNext);
    findRow->addWidget(caseSensitive_);
    findRow->addWidget(wholeWords_);
    findRow->addWidget(closeFind);
    findLayout->addLayout(findRow);

    replaceRow_ = new QWidget(findBar_);
    auto * replaceLayout = new QHBoxLayout(replaceRow_);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceField_ = new QLineEdit(replaceRow_);
    replaceField_->setPlaceholderText(tr("바꾸기"));
    auto * replaceOne = new QPushButton(tr("바꾸기"), replaceRow_);
    auto * replaceAll = new QPushButton(tr("모두"), replaceRow_);
    replaceLayout->addWidget(replaceField_, 1);
    replaceLayout->addWidget(replaceOne);
    replaceLayout->addWidget(replaceAll);
    findLayout->addWidget(replaceRow_);

    findBar_->hide();

    // --- 문제 목록 ---
    problems_ = new QListWidget(this);
    problems_->setMaximumHeight(110);
    problems_->hide();

    status_ = new QLabel(this);
    status_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(findBar_);
    layout->addWidget(editor_, 1);
    layout->addWidget(problems_);
    layout->addWidget(status_);

    // --- 신호 ---
    const auto findAgain = [this](bool forward) {
        const QString needle = findField_->text();
        if (needle.isEmpty())
            return;
        const bool found = editor_->findText(needle, forward,
                                             caseSensitive_->isChecked(),
                                             wholeWords_->isChecked());
        findField_->setStyleSheet(found ? QString()
                                        : QStringLiteral("background: #5a1d1d;"));
    };

    connect(findNext, &QToolButton::clicked, this, [findAgain] { findAgain(true); });
    connect(findPrev, &QToolButton::clicked, this, [findAgain] { findAgain(false); });
    connect(findField_, &QLineEdit::returnPressed, this, [findAgain] { findAgain(true); });
    connect(findField_, &QLineEdit::textChanged, this,
            [this] { findField_->setStyleSheet(QString()); });

    connect(closeFind, &QToolButton::clicked, this, [this] {
        findBar_->hide();
        editor_->setFocus();
    });

    connect(replaceOne, &QPushButton::clicked, this, [this] {
        editor_->replaceCurrent(findField_->text(), replaceField_->text(),
                                caseSensitive_->isChecked(), wholeWords_->isChecked());
    });
    connect(replaceAll, &QPushButton::clicked, this, [this] {
        const int count = editor_->replaceAll(findField_->text(), replaceField_->text(),
                                              caseSensitive_->isChecked(),
                                              wholeWords_->isChecked());
        status_->setText(tr("%1군데를 바꿨습니다").arg(count));
    });

    connect(problems_, &QListWidget::itemActivated, this, [this](QListWidgetItem * item) {
        if (item != nullptr)
            editor_->gotoLine(item->data(Qt::UserRole).toInt());
    });
    connect(problems_, &QListWidget::itemClicked, this, [this](QListWidgetItem * item) {
        if (item != nullptr)
            editor_->gotoLine(item->data(Qt::UserRole).toInt());
    });

    connect(editor_, &CodeEditor::diagnosticsChanged, this, [this] {
        refreshProblems();
        refreshStatus();
    });
    connect(editor_, &CodeEditor::cursorPositionChanged, this, [this] { refreshStatus(); });

    auto * findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this] { showFind(false); });
    auto * replaceShortcut = new QShortcut(QKeySequence::Replace, this);
    connect(replaceShortcut, &QShortcut::activated, this, [this] { showFind(true); });

    refreshStatus();
}

QString CodeEditorPane::text() const
{
    return editor_->toPlainText();
}

void CodeEditorPane::setText(const QString & text)
{
    editor_->setPlainText(text);
}

void CodeEditorPane::setVocabulary(const io::TriggerVocabulary & vocabulary)
{
    editor_->setVocabulary(vocabulary);
}

bool CodeEditorPane::hasErrors() const
{
    for (const auto & diagnostic : editor_->diagnostics())
    {
        if (diagnostic.error)
            return true;
    }
    return false;
}

void CodeEditorPane::showFind(bool withReplace)
{
    findBar_->show();
    replaceRow_->setVisible(withReplace);

    // 고른 글자가 있으면 그것을 찾을 말로 삼는다.
    const QString selected = editor_->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar(0x2029)))
        findField_->setText(selected);

    findField_->setFocus();
    findField_->selectAll();
}

void CodeEditorPane::refreshProblems()
{
    problems_->clear();

    for (const auto & diagnostic : editor_->diagnostics())
    {
        auto * item = new QListWidgetItem(
            tr("%1줄  %2").arg(diagnostic.line + 1).arg(diagnostic.message));
        item->setData(Qt::UserRole, diagnostic.line);
        item->setForeground(diagnostic.error ? QColor(244, 135, 113)
                                             : QColor(220, 200, 120));
        problems_->addItem(item);
    }

    problems_->setVisible(problems_->count() > 0);
}

void CodeEditorPane::refreshStatus()
{
    const QTextCursor cursor = editor_->textCursor();
    const int line = cursor.blockNumber() + 1;
    const int column = cursor.positionInBlock() + 1;

    int errors = 0;
    int warnings = 0;
    for (const auto & diagnostic : editor_->diagnostics())
        (diagnostic.error ? errors : warnings)++;

    QString text = tr("%1줄 %2열").arg(line).arg(column);
    if (errors > 0 || warnings > 0)
        text += tr("   ·   오류 %1 · 경고 %2").arg(errors).arg(warnings);
    else
        text += tr("   ·   문제 없음");

    status_->setText(text);
}

} // namespace splash::ui
