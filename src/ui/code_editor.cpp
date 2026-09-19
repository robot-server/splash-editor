#include "ui/code_editor.h"

#include "io/map_archive.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextDocumentLayout>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QTimer>
#include <QWheelEvent>

namespace splash::ui {
namespace {

// VS Code 의 Dark+ 에서 가져온 색이다. 어느 테마에서도 같은 색으로
// 보이도록 편집기 배경까지 직접 칠한다.
const QColor kBackground   (30, 30, 30);
const QColor kForeground   (212, 212, 212);
const QColor kLineNumberBg (30, 30, 30);
const QColor kLineNumber   (133, 133, 133);
const QColor kLineNumberNow(198, 198, 198);
const QColor kCurrentLine  (42, 45, 46);
const QColor kSelection    (38, 79, 120);
const QColor kMatchBracket (58, 61, 65);

const QColor kKeyword      (86, 156, 214);
const QColor kCall         (220, 220, 170);
const QColor kUnknownCall  (244, 135, 113);
const QColor kString       (206, 145, 120);
const QColor kEscape       (215, 186, 125);
const QColor kNumber       (181, 206, 168);
const QColor kComment      (106, 153, 85);
const QColor kConstant     (78, 201, 176);
const QColor kPunctuation  (212, 212, 212);

/// 줄 번호를 그리는 여백. 그리기는 편집기가 맡는다.
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor * editor)
        : QWidget(editor), editor_(editor) {}

    QSize sizeHint() const override
    {
        return QSize(editor_->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent * event) override
    {
        editor_->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor * editor_;
};

/// 트리거 문법에서 뼈대가 되는 낱말.
const QStringList kKeywords {
    QStringLiteral("Trigger"), QStringLiteral("Briefing"),
    QStringLiteral("Conditions"), QStringLiteral("Actions"),
    QStringLiteral("Players"), QStringLiteral("Flags"),
};

} // namespace

// ------------------------------------------------------------- 구문 강조

TriggerHighlighter::TriggerHighlighter(QTextDocument * document)
    : QSyntaxHighlighter(document)
{
    keyword_.setForeground(kKeyword);
    keyword_.setFontWeight(QFont::DemiBold);
    call_.setForeground(kCall);
    unknownCall_.setForeground(kUnknownCall);
    unknownCall_.setFontUnderline(true);
    unknownCall_.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    unknownCall_.setUnderlineColor(kUnknownCall);
    string_.setForeground(kString);
    escape_.setForeground(kEscape);
    number_.setForeground(kNumber);
    comment_.setForeground(kComment);
    comment_.setFontItalic(true);
    constant_.setForeground(kConstant);
    punctuation_.setForeground(kPunctuation);
}

void TriggerHighlighter::setVocabulary(const io::TriggerVocabulary & vocabulary)
{
    knownCalls_.clear();
    constants_.clear();

    for (const auto & name : vocabulary.conditions)
        knownCalls_.insert(QString::fromStdString(name));
    for (const auto & name : vocabulary.actions)
        knownCalls_.insert(QString::fromStdString(name));
    for (const auto & name : vocabulary.constants)
        constants_.insert(QString::fromStdString(name).trimmed());

    rehighlight();
}

void TriggerHighlighter::highlightBlock(const QString & text)
{
    // 주석이 먼저다 — 주석 안에서는 다른 규칙을 적용하지 않는다.
    const int commentAt = text.indexOf(QStringLiteral("//"));

    const int limit = (commentAt >= 0) ? commentAt : text.size();

    int i = 0;
    while (i < limit)
    {
        const QChar c = text[i];

        // --- 문자열 ---
        if (c == '"')
        {
            int end = i + 1;
            while (end < text.size())
            {
                if (text[end] == '\\' && end + 1 < text.size())
                {
                    end += 2;
                    continue;
                }
                if (text[end] == '"')
                    break;
                ++end;
            }
            const int stop = std::min<int>(end + 1, static_cast<int>(text.size()));
            setFormat(i, stop - i, string_);

            // 이스케이프는 문자열 안에서 따로 칠한다.
            for (int e = i; e + 1 < stop; ++e)
            {
                if (text[e] == '\\')
                {
                    setFormat(e, 2, escape_);
                    ++e;
                }
            }
            i = stop;
            continue;
        }

        // --- 낱말 ---
        if (c.isLetter() || c == '_')
        {
            int end = i;
            while (end < limit && (text[end].isLetterOrNumber() || text[end] == '_' ||
                                   text[end] == ' '))
            {
                // 이름 사이의 공백은 "Set Resources" 처럼 이름의 일부다.
                // 다만 공백 뒤에 글자가 오지 않으면 거기서 끊는다.
                if (text[end] == ' ')
                {
                    if (end + 1 >= limit || !(text[end + 1].isLetter()))
                        break;
                }
                ++end;
            }

            const QString word = text.mid(i, end - i);
            const QString trimmed = word.trimmed();

            // 뒤에 여는 괄호가 오면 조건·액션 호출이다.
            int after = end;
            while (after < limit && text[after] == ' ')
                ++after;

            if (kKeywords.contains(trimmed))
                setFormat(i, end - i, keyword_);
            else if (after < limit && text[after] == '(')
                setFormat(i, end - i, knownCalls_.isEmpty() || knownCalls_.contains(trimmed)
                                          ? call_ : unknownCall_);
            else if (constants_.contains(trimmed))
                setFormat(i, end - i, constant_);

            i = end;
            continue;
        }

        // --- 숫자 ---
        if (c.isDigit())
        {
            int end = i;
            while (end < limit && (text[end].isLetterOrNumber()))
                ++end;
            setFormat(i, end - i, number_);
            i = end;
            continue;
        }

        if (c == '(' || c == ')' || c == '{' || c == '}' || c == ',' || c == ';' || c == ':')
            setFormat(i, 1, punctuation_);

        ++i;
    }

    if (commentAt >= 0)
        setFormat(commentAt, text.size() - commentAt, comment_);
}

// ------------------------------------------------------------- 편집기

CodeEditor::CodeEditor(QWidget * parent)
    : QPlainTextEdit(parent)
{
    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(12);
    setFont(mono);
    setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(' '));
    setLineWrapMode(QPlainTextEdit::NoWrap);

    QPalette colors = palette();
    colors.setColor(QPalette::Base, kBackground);
    colors.setColor(QPalette::Text, kForeground);
    colors.setColor(QPalette::Highlight, kSelection);
    colors.setColor(QPalette::HighlightedText, Qt::white);
    setPalette(colors);

    lineNumbers_ = new LineNumberArea(this);
    highlighter_ = new TriggerHighlighter(document());

    completionModel_ = new QStringListModel(this);
    completer_ = new QCompleter(completionModel_, this);
    completer_->setWidget(this);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    connect(completer_, QOverload<const QString &>::of(&QCompleter::activated),
            this, [this](const QString & completion) { insertCompletion(completion); });

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, [this](int) { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest,
            this, [this](const QRect & rect, int dy) { updateLineNumberArea(rect, dy); });
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, [this] { refreshExtraSelections(); });
    connect(this, &QPlainTextEdit::textChanged, this, [this] { runLinter(); });

    updateLineNumberAreaWidth();
    refreshExtraSelections();
}

CodeEditor::~CodeEditor() = default;

void CodeEditor::setVocabulary(const io::TriggerVocabulary & vocabulary)
{
    highlighter_->setVocabulary(vocabulary);

    knownCalls_.clear();
    QStringList words;
    for (const auto & name : vocabulary.conditions)
    {
        const QString text = QString::fromStdString(name);
        knownCalls_.insert(text);
        words << text + QStringLiteral("(");
    }
    for (const auto & name : vocabulary.actions)
    {
        const QString text = QString::fromStdString(name);
        knownCalls_.insert(text);
        words << text + QStringLiteral("(");
    }
    for (const auto & list : {vocabulary.constants, vocabulary.players, vocabulary.locations,
                              vocabulary.switches, vocabulary.units, vocabulary.scripts})
    {
        for (const auto & name : list)
            words << QString::fromStdString(name);
    }
    words << kKeywords;

    words.removeDuplicates();
    words.sort(Qt::CaseInsensitive);
    completionModel_->setStringList(words);

    runLinter();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = std::max(1, blockCount());
    while (max >= 10)
    {
        max /= 10;
        ++digits;
    }
    return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect & rect, int dy)
{
    if (dy != 0)
        lineNumbers_->scroll(0, dy);
    else
        lineNumbers_->update(0, rect.y(), lineNumbers_->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent * event)
{
    QPlainTextEdit::resizeEvent(event);

    const QRect box = contentsRect();
    lineNumbers_->setGeometry(QRect(box.left(), box.top(),
                                    lineNumberAreaWidth(), box.height()));
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent * event)
{
    QPainter painter(lineNumbers_);
    painter.fillRect(event->rect(), kLineNumberBg);

    // 문제가 있는 줄은 여백에 점을 찍는다.
    QSet<int> errorLines;
    QSet<int> warningLines;
    for (const auto & diagnostic : diagnostics_)
        (diagnostic.error ? errorLines : warningLines).insert(diagnostic.line);
    for (const auto & diagnostic : external_)
        (diagnostic.error ? errorLines : warningLines).insert(diagnostic.line);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingRect(block).height());

    const int currentLine = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            const bool isError = errorLines.contains(blockNumber);
            const bool isWarning = !isError && warningLines.contains(blockNumber);

            if (isError || isWarning)
            {
                painter.setBrush(isError ? QColor(244, 135, 113) : QColor(220, 200, 120));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(QPoint(6, (top + bottom) / 2), 3, 3);
            }

            painter.setPen(blockNumber == currentLine ? kLineNumberNow : kLineNumber);
            painter.drawText(0, top, lineNumbers_->width() - 8,
                             fontMetrics().height(), Qt::AlignRight,
                             QString::number(blockNumber + 1));
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::refreshExtraSelections()
{
    QList<QTextEdit::ExtraSelection> selections;

    // --- 지금 줄 ---
    if (!isReadOnly())
    {
        QTextEdit::ExtraSelection line;
        line.format.setBackground(kCurrentLine);
        line.format.setProperty(QTextFormat::FullWidthSelection, true);
        line.cursor = textCursor();
        line.cursor.clearSelection();
        selections.append(line);
    }

    // --- 짝이 되는 괄호 ---
    const QString text = toPlainText();
    const int position = textCursor().position();
    const auto matchAt = [&](int at, QChar open, QChar close, int direction) -> int {
        int depth = 0;
        for (int i = at; i >= 0 && i < text.size(); i += direction)
        {
            if (text[i] == open)
                ++depth;
            else if (text[i] == close)
            {
                --depth;
                if (depth == 0)
                    return i;
            }
        }
        return -1;
    };

    const auto addBracket = [&selections](int at) {
        if (at < 0)
            return;
        QTextEdit::ExtraSelection bracket;
        bracket.format.setBackground(kMatchBracket);
        bracket.cursor = QTextCursor();
        selections.append(bracket);
    };
    (void)addBracket;

    const auto markPair = [&](int a, int b) {
        for (int at : {a, b})
        {
            QTextEdit::ExtraSelection mark;
            mark.format.setBackground(kMatchBracket);
            mark.cursor = textCursor();
            mark.cursor.setPosition(at);
            mark.cursor.setPosition(at + 1, QTextCursor::KeepAnchor);
            selections.append(mark);
        }
    };

    if (position < text.size() && (text[position] == '(' || text[position] == '{'))
    {
        const QChar open = text[position];
        const QChar close = (open == '(') ? QChar(')') : QChar('}');
        const int other = matchAt(position, open, close, 1);
        if (other >= 0)
            markPair(position, other);
    }
    else if (position > 0 && (text[position - 1] == ')' || text[position - 1] == '}'))
    {
        const QChar close = text[position - 1];
        const QChar open = (close == ')') ? QChar('(') : QChar('{');
        const int other = matchAt(position - 1, close, open, -1);
        if (other >= 0)
            markPair(position - 1, other);
    }

    // --- 문제가 있는 자리 ---
    const auto markDiagnostics = [this, &selections](const std::vector<Diagnostic> & list) {
        for (const auto & diagnostic : list)
        {
            const QTextBlock block = document()->findBlockByNumber(diagnostic.line);
            if (!block.isValid())
                continue;

            QTextEdit::ExtraSelection mark;
            mark.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            mark.format.setUnderlineColor(diagnostic.error ? QColor(244, 71, 71)
                                                           : QColor(220, 200, 120));
            mark.format.setToolTip(diagnostic.message);
            mark.cursor = QTextCursor(block);
            mark.cursor.setPosition(block.position() + std::min(diagnostic.column,
                                                                block.length() - 1));
            mark.cursor.setPosition(block.position() +
                                    std::min(diagnostic.column + std::max(1, diagnostic.length),
                                             block.length() - 1),
                                    QTextCursor::KeepAnchor);
            selections.append(mark);
        }
    };
    markDiagnostics(diagnostics_);
    markDiagnostics(external_);

    setExtraSelections(selections);
}

void CodeEditor::setExternalDiagnostics(const std::vector<Diagnostic> & diagnostics)
{
    external_ = diagnostics;
    refreshExtraSelections();
    lineNumbers_->update();
    emit diagnosticsChanged();
}

void CodeEditor::runLinter()
{
    // 컴파일러가 실패 이유를 문자열로 돌려주지 않으므로, 편집기가 스스로
    // 흔한 실수를 잡는다 — 괄호·따옴표 짝, 세미콜론, 모르는 이름.
    diagnostics_.clear();

    const QStringList lines = toPlainText().split('\n');

    int parens = 0;
    int braces = 0;
    int parenLine = 0;
    int braceLine = 0;

    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber)
    {
        QString line = lines[lineNumber];

        const int commentAt = line.indexOf(QStringLiteral("//"));
        if (commentAt >= 0)
            line = line.left(commentAt);

        bool inString = false;
        for (int i = 0; i < line.size(); ++i)
        {
            const QChar c = line[i];
            if (inString)
            {
                if (c == '\\')
                    ++i;
                else if (c == '"')
                    inString = false;
                continue;
            }

            if (c == '"')
                inString = true;
            else if (c == '(')
            {
                ++parens;
                parenLine = lineNumber;
            }
            else if (c == ')')
            {
                --parens;
                if (parens < 0)
                {
                    diagnostics_.push_back(Diagnostic{lineNumber, i, 1, true,
                        tr("여는 괄호가 없습니다.")});
                    parens = 0;
                }
            }
            else if (c == '{')
            {
                ++braces;
                braceLine = lineNumber;
            }
            else if (c == '}')
            {
                --braces;
                if (braces < 0)
                {
                    diagnostics_.push_back(Diagnostic{lineNumber, i, 1, true,
                        tr("여는 중괄호가 없습니다.")});
                    braces = 0;
                }
            }
        }

        if (inString)
        {
            diagnostics_.push_back(Diagnostic{lineNumber, 0, static_cast<int>(line.size()), true,
                tr("따옴표가 닫히지 않았습니다.")});
        }

        // 조건·액션 한 줄은 세미콜론으로 끝난다.
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && trimmed.endsWith(')') && !trimmed.endsWith(");"))
        {
            const int at = line.lastIndexOf(')');
            diagnostics_.push_back(Diagnostic{lineNumber, at, 1, false,
                tr("줄 끝에 ; 가 빠진 것 같습니다.")});
        }

        // 이름표에 없는 호출.
        if (!knownCalls_.isEmpty())
        {
            const int open = trimmed.indexOf('(');
            if (open > 0 && !trimmed.startsWith(QStringLiteral("//")))
            {
                const QString name = trimmed.left(open).trimmed();
                if (!name.isEmpty() && !kKeywords.contains(name) && !knownCalls_.contains(name) &&
                    name.at(0).isLetter())
                {
                    const int at = line.indexOf(name);
                    diagnostics_.push_back(Diagnostic{lineNumber, std::max(0, at),
                        static_cast<int>(name.size()), true, tr("모르는 조건·동작 이름입니다: %1").arg(name)});
                }
            }
        }
    }

    if (parens > 0)
        diagnostics_.push_back(Diagnostic{parenLine, 0, 1, true, tr("괄호가 닫히지 않았습니다.")});
    if (braces > 0)
        diagnostics_.push_back(Diagnostic{braceLine, 0, 1, true, tr("중괄호가 닫히지 않았습니다.")});

    refreshExtraSelections();
    lineNumbers_->update();
    emit diagnosticsChanged();
}

QString CodeEditor::completionPrefix() const
{
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText();
}

void CodeEditor::insertCompletion(const QString & completion)
{
    QTextCursor cursor = textCursor();
    const int extra = completion.size() - completer_->completionPrefix().size();
    cursor.movePosition(QTextCursor::Left);
    cursor.movePosition(QTextCursor::EndOfWord);
    cursor.insertText(completion.right(extra));
    setTextCursor(cursor);
}

void CodeEditor::maybeComplete()
{
    const QString prefix = completionPrefix();
    if (prefix.size() < 2)
    {
        completer_->popup()->hide();
        return;
    }

    if (prefix != completer_->completionPrefix())
    {
        completer_->setCompletionPrefix(prefix);
        completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0));
    }

    if (completer_->completionCount() == 0)
    {
        completer_->popup()->hide();
        return;
    }

    QRect box = cursorRect();
    box.setWidth(completer_->popup()->sizeHintForColumn(0) +
                 completer_->popup()->verticalScrollBar()->sizeHint().width());
    completer_->complete(box);
}

namespace {

QTextDocument::FindFlags findFlags(bool forward, bool caseSensitive, bool wholeWords)
{
    QTextDocument::FindFlags flags;
    if (!forward)
        flags |= QTextDocument::FindBackward;
    if (caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;
    if (wholeWords)
        flags |= QTextDocument::FindWholeWords;
    return flags;
}

} // namespace

bool CodeEditor::findText(const QString & needle, bool forward,
                          bool caseSensitive, bool wholeWords)
{
    if (needle.isEmpty())
        return false;

    const auto flags = findFlags(forward, caseSensitive, wholeWords);
    if (find(needle, flags))
        return true;

    // 끝까지 갔으면 반대편에서 다시 찾는다.
    QTextCursor cursor = textCursor();
    QTextCursor wrapped = cursor;
    wrapped.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
    setTextCursor(wrapped);

    if (find(needle, flags))
        return true;

    setTextCursor(cursor);
    return false;
}

bool CodeEditor::replaceCurrent(const QString & needle, const QString & replacement,
                                bool caseSensitive, bool wholeWords)
{
    QTextCursor cursor = textCursor();
    const Qt::CaseSensitivity sensitivity =
        caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (cursor.hasSelection() &&
        cursor.selectedText().compare(needle, sensitivity) == 0)
    {
        cursor.insertText(replacement);
    }

    return findText(needle, true, caseSensitive, wholeWords);
}

int CodeEditor::replaceAll(const QString & needle, const QString & replacement,
                           bool caseSensitive, bool wholeWords)
{
    if (needle.isEmpty())
        return 0;

    const auto flags = findFlags(true, caseSensitive, wholeWords);

    // 한 번의 되돌리기로 묶는다 — 반쯤 바뀐 상태로 남으면 곤란하다.
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();

    QTextCursor scan(document());
    scan.movePosition(QTextCursor::Start);

    int count = 0;
    while (true)
    {
        scan = document()->find(needle, scan, flags);
        if (scan.isNull())
            break;

        scan.insertText(replacement);
        ++count;
    }

    cursor.endEditBlock();
    return count;
}

void CodeEditor::gotoLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(std::max(0, line));
    if (!block.isValid())
        return;

    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

void CodeEditor::keyPressEvent(QKeyEvent * event)
{
    // 자동 완성 목록이 떠 있으면 그쪽이 먼저 키를 가져간다.
    if (completer_->popup()->isVisible())
    {
        switch (event->key())
        {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                event->ignore();
                return;
            default:
                break;
        }
    }

    // Ctrl+Space 로 불러낸다.
    const bool askedForCompletion =
        (event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space;
    if (askedForCompletion)
    {
        maybeComplete();
        return;
    }

    // 줄을 바꾸면 앞 줄의 들여쓰기를 이어 준다. 여는 중괄호 뒤에서는 한
    // 단계 더 들어간다.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        const QString line = textCursor().block().text();
        QString indent;
        for (QChar c : line)
        {
            if (c == ' ' || c == '\t')
                indent += c;
            else
                break;
        }
        if (line.trimmed().endsWith('{'))
            indent += '\t';

        QPlainTextEdit::keyPressEvent(event);
        textCursor().insertText(indent);
        return;
    }

    // 괄호·따옴표를 짝지어 넣는다.
    const QString text = event->text();
    if (text == QStringLiteral("(") || text == QStringLiteral("\"") || text == QStringLiteral("{"))
    {
        const QString closing = text == QStringLiteral("(") ? QStringLiteral(")")
                              : text == QStringLiteral("{") ? QStringLiteral("}")
                                                            : QStringLiteral("\"");
        QTextCursor cursor = textCursor();
        if (!cursor.hasSelection())
        {
            cursor.insertText(text + closing);
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);
            return;
        }
    }

    QPlainTextEdit::keyPressEvent(event);

    // 글자를 치는 동안 조용히 후보를 보여 준다.
    if (!event->text().isEmpty() && event->text().at(0).isLetter())
        maybeComplete();
    else if (completer_->popup()->isVisible())
        completer_->popup()->hide();
}

void CodeEditor::paintEvent(QPaintEvent * event)
{
    QPlainTextEdit::paintEvent(event);
}

void CodeEditor::focusInEvent(QFocusEvent * event)
{
    completer_->setWidget(this);
    QPlainTextEdit::focusInEvent(event);
}

void CodeEditor::wheelEvent(QWheelEvent * event)
{
    // Ctrl+휠 로 글자 크기를 바꾼다.
    if (event->modifiers() & Qt::ControlModifier)
    {
        const int steps = event->angleDelta().y() / 120;
        if (steps > 0)
            zoomIn(steps);
        else if (steps < 0)
            zoomOut(-steps);
        updateLineNumberAreaWidth();
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

} // namespace splash::ui
