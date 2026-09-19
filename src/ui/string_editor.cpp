#include "ui/string_editor.h"

#include "chk/map_document.h"
#include "io/text_colors.h"
#include "ui/code_editor.h"
#include "ui/code_editor_pane.h"

#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace splash::ui {
namespace {

/// 편집기에서 글자를 고를 때 쓰는 기본 색. 색 코드가 나오기 전의 글자다.
const QColor kDefaultColor(220, 220, 220);

/// 게임 화면처럼 보이도록 문자열을 올려 두는 바탕.
const QColor kPreviewBackground(16, 16, 20);

QColor colorOf(const io::TextControlCode & code)
{
    return QColor(code.red, code.green, code.blue);
}

/// 색이 아닌 제어 문자를 글 안에 남겨 두는 표기. 위지윅에서도 이 모습
/// 그대로 보이고, 되돌릴 때 원래 바이트로 돌아간다.
QString markerFor(std::uint8_t code)
{
    return QStringLiteral("⟪%1⟫").arg(code, 2, 16, QChar('0')).toUpper();
}

bool isControl(QChar c)
{
    const ushort value = c.unicode();
    return value < 0x20 && value != '\r' && value != '\n' && value != '\t';
}

} // namespace

StringEditor::StringEditor(chk::MapDocument & document, QWidget * parent)
    : QDialog(parent), document_(document)
{
    setWindowTitle(tr("문자열 편집기"));
    resize(1120, 700);

    // --- 왼쪽: 문자열 목록 ---
    list_ = new QTableWidget(0, 3, this);
    list_->setHorizontalHeaderLabels({tr("번호"), tr("쓰임"), tr("내용")});
    list_->horizontalHeader()->setStretchLastSection(true);
    list_->verticalHeader()->setVisible(false);
    list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // --- 오른쪽: 위지윅과 원본 ---
    wysiwyg_ = new QTextEdit(this);
    wysiwyg_->setAcceptRichText(false);
    QPalette wysiwygColors = wysiwyg_->palette();
    wysiwygColors.setColor(QPalette::Base, kPreviewBackground);
    wysiwygColors.setColor(QPalette::Text, kDefaultColor);
    wysiwyg_->setPalette(wysiwygColors);

    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(13);
    wysiwyg_->setFont(mono);

    // 색 단추 줄 — 누르면 커서 자리부터 그 색이 된다.
    auto * colorBar = new QWidget(this);
    auto * colorLayout = new QHBoxLayout(colorBar);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->setSpacing(2);

    for (const auto & code : io::textControlCodes())
    {
        auto * button = new QToolButton(colorBar);
        button->setFixedSize(22, 22);
        button->setToolTip(QStringLiteral("%1  (0x%2)")
            .arg(QString::fromStdString(code.name))
            .arg(code.code, 2, 16, QChar('0')));

        if (code.isColor)
        {
            const QColor color = colorOf(code);
            button->setStyleSheet(QStringLiteral(
                "background: %1; border: 1px solid #555;").arg(color.name()));
        }
        else
        {
            button->setText(QStringLiteral("%1").arg(code.code, 2, 16, QChar('0')).toUpper());
            button->setStyleSheet(QStringLiteral("font-size: 9px;"));
        }

        connect(button, &QToolButton::clicked, this, [this, code] {
            QTextCursor cursor = wysiwyg_->textCursor();
            if (code.isColor)
            {
                // 고른 글자가 있으면 그 부분만, 없으면 여기서부터 색이 바뀐다.
                QTextCharFormat format;
                format.setForeground(colorOf(code));
                if (cursor.hasSelection())
                    cursor.mergeCharFormat(format);
                else
                    wysiwyg_->mergeCurrentCharFormat(format);
            }
            else
            {
                cursor.insertText(markerFor(code.code));
            }
            wysiwyg_->setFocus();
            syncFromWysiwyg();
        });

        colorLayout->addWidget(button);
    }
    colorLayout->addStretch();

    auto * wysiwygPanel = new QWidget(this);
    auto * wysiwygLayout = new QVBoxLayout(wysiwygPanel);
    wysiwygLayout->setContentsMargins(4, 4, 4, 4);
    wysiwygLayout->addWidget(colorBar);
    wysiwygLayout->addWidget(wysiwyg_, 1);

    auto * wysiwygHint = new QLabel(
        tr("색 단추로 글자 색을 바꿉니다. 색이 아닌 제어 문자는 ⟪12⟫ 처럼 "
           "남겨 두고 그대로 저장합니다."), wysiwygPanel);
    wysiwygHint->setWordWrap(true);
    wysiwygHint->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    wysiwygLayout->addWidget(wysiwygHint);

    raw_ = new CodeEditorPane(this);

    tabs_ = new QTabWidget(this);
    tabs_->addTab(wysiwygPanel, tr("보이는 대로"));
    tabs_->addTab(raw_, tr("원본"));

    preview_ = new QLabel(this);
    preview_->setTextFormat(Qt::RichText);
    preview_->setWordWrap(true);
    preview_->setMinimumHeight(70);
    preview_->setAutoFillBackground(true);
    QPalette previewColors = preview_->palette();
    previewColors.setColor(QPalette::Window, kPreviewBackground);
    preview_->setPalette(previewColors);
    preview_->setFont(mono);

    auto * previewBox = new QWidget(this);
    auto * previewLayout = new QVBoxLayout(previewBox);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    auto * previewLabel = new QLabel(tr("게임에서 보이는 모습"), previewBox);
    previewLabel->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    previewLayout->addWidget(previewLabel);
    previewLayout->addWidget(preview_);

    auto * applyButton = new QPushButton(tr("이 문자열에 적용"), this);
    connect(applyButton, &QPushButton::clicked, this, [this] { applyCurrent(); });

    status_ = new QLabel(this);
    status_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto * rightPanel = new QWidget(this);
    auto * rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(tabs_, 1);
    rightLayout->addWidget(previewBox);
    rightLayout->addWidget(applyButton);
    rightLayout->addWidget(status_);

    auto * splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(list_);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 700});

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto * layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);

    connect(list_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
        if (loading_)
            return;
        currentRow_ = row;
        loadSelected();
    });

    // 탭을 옮기면 방금 고치던 내용을 다른 쪽으로 넘긴다.
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (loading_)
            return;
        if (lastTab_ == 0 && index == 1)
            syncFromWysiwyg();
        else if (lastTab_ == 1 && index == 0)
            syncFromRaw();
        lastTab_ = index;
    });

    connect(wysiwyg_, &QTextEdit::textChanged, this, [this] {
        if (loading_ || tabs_->currentIndex() != 0)
            return;
        syncFromWysiwyg();
    });

    connect(raw_->editor(), &CodeEditor::textChanged, this, [this] {
        if (loading_ || tabs_->currentIndex() != 1)
            return;
        syncFromRaw();
    });

    reloadList();
}

void StringEditor::reloadList(int selectRow)
{
    strings_ = document_.strings();

    loading_ = true;
    list_->setRowCount(static_cast<int>(strings_.size()));

    for (std::size_t row = 0; row < strings_.size(); ++row)
    {
        const auto & entry = strings_[row];
        const int r = static_cast<int>(row);

        list_->setItem(r, 0, new QTableWidgetItem(QString::number(entry.id)));
        list_->setItem(r, 1, new QTableWidgetItem(entry.used ? tr("예") : tr("아니오")));

        // 목록에서는 제어 문자를 지우고 한 줄로 줄여 보여 준다.
        QString shown = QString::fromStdString(entry.text);
        QString plain;
        plain.reserve(shown.size());
        for (QChar c : shown)
        {
            if (isControl(c))
                continue;
            plain += (c == '\r' || c == '\n') ? QChar(' ') : c;
        }
        list_->setItem(r, 2, new QTableWidgetItem(plain.trimmed()));
    }
    loading_ = false;

    setWindowTitle(tr("문자열 편집기 — %1개").arg(strings_.size()));

    if (!strings_.empty())
    {
        const int row = std::clamp(selectRow, 0, static_cast<int>(strings_.size()) - 1);
        list_->setCurrentCell(row, 2);
        currentRow_ = row;
        loadSelected();
    }
}

void StringEditor::loadSelected()
{
    if (currentRow_ < 0 || currentRow_ >= static_cast<int>(strings_.size()))
        return;

    const QString raw = QString::fromStdString(strings_[static_cast<std::size_t>(currentRow_)].text);

    loading_ = true;
    fillDocument(wysiwyg_, raw);
    raw_->setText(toVisible(raw));
    loading_ = false;

    refreshPreview(raw);
    status_->setText(tr("문자열 %1번").arg(strings_[static_cast<std::size_t>(currentRow_)].id));
}

void StringEditor::fillDocument(QTextEdit * edit, const QString & raw) const
{
    edit->clear();

    QTextCursor cursor(edit->document());
    QTextCharFormat format;
    format.setForeground(kDefaultColor);

    QString run;
    const auto flush = [&] {
        if (run.isEmpty())
            return;
        cursor.insertText(run, format);
        run.clear();
    };

    for (QChar c : raw)
    {
        if (!isControl(c))
        {
            run += c;
            continue;
        }

        flush();

        const auto code = static_cast<std::uint8_t>(c.unicode());
        const io::TextControlCode * info = io::findTextControlCode(code);
        if (info != nullptr && info->isColor)
        {
            format.setForeground(colorOf(*info));
        }
        else if (code == 0x01)
        {
            format.setForeground(kDefaultColor);
        }
        else
        {
            // 색이 아닌 제어 문자는 눈에 보이게 남긴다.
            QTextCharFormat markerFormat = format;
            markerFormat.setForeground(QColor(150, 150, 160));
            markerFormat.setFontItalic(true);
            cursor.insertText(markerFor(code), markerFormat);
        }
    }
    flush();
}

QString StringEditor::documentToRaw(const QTextEdit * edit) const
{
    QString out;
    QColor current = kDefaultColor;

    for (QTextBlock block = edit->document()->begin(); block.isValid(); block = block.next())
    {
        if (block.blockNumber() > 0)
            out += QStringLiteral("\r\n");

        for (auto it = block.begin(); !it.atEnd(); ++it)
        {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;

            const QColor color = fragment.charFormat().foreground().color();
            if (color != current)
            {
                // 이 색을 내는 코드를 찾는다. 표에 없는 색이면 코드를 넣지
                // 않는다 — 없는 코드를 지어내면 게임에서 다른 색이 된다.
                for (const auto & code : io::textControlCodes())
                {
                    if (code.isColor && colorOf(code) == color)
                    {
                        out += QChar(static_cast<ushort>(code.code));
                        break;
                    }
                }
                if (color == kDefaultColor)
                    out += QChar(ushort(0x01));
                current = color;
            }

            out += fragment.text();
        }
    }

    // ⟪hh⟫ 표기를 원래 제어 문자로 되돌린다.
    static const QRegularExpression marker(QStringLiteral("⟪([0-9A-Fa-f]{2})⟫"));
    QString restored;
    restored.reserve(out.size());

    int at = 0;
    auto matches = marker.globalMatch(out);
    while (matches.hasNext())
    {
        const auto match = matches.next();
        restored += out.mid(at, match.capturedStart() - at);
        restored += QChar(static_cast<ushort>(match.captured(1).toUShort(nullptr, 16)));
        at = match.capturedEnd();
    }
    restored += out.mid(at);

    return restored;
}

QString StringEditor::toVisible(const QString & raw)
{
    QString out;
    out.reserve(raw.size());

    for (QChar c : raw)
    {
        if (c == '\r')
            continue; // \r\n 은 줄바꿈 하나로 보여 준다
        if (c == '\n')
        {
            out += '\n';
            continue;
        }
        if (isControl(c))
        {
            out += QStringLiteral("<%1>")
                .arg(static_cast<ushort>(c.unicode()), 2, 16, QChar('0')).toUpper();
            continue;
        }
        out += c;
    }
    return out;
}

QString StringEditor::fromVisible(const QString & visible)
{
    static const QRegularExpression code(QStringLiteral("<([0-9A-Fa-f]{2})>"));

    QString out;
    out.reserve(visible.size());

    int at = 0;
    auto matches = code.globalMatch(visible);
    while (matches.hasNext())
    {
        const auto match = matches.next();
        out += visible.mid(at, match.capturedStart() - at);
        out += QChar(static_cast<ushort>(match.captured(1).toUShort(nullptr, 16)));
        at = match.capturedEnd();
    }
    out += visible.mid(at);

    // 게임 문자열의 줄바꿈은 \r\n 이다.
    out.replace(QChar('\n'), QStringLiteral("\r\n"));
    return out;
}

void StringEditor::syncFromWysiwyg()
{
    const QString raw = documentToRaw(wysiwyg_);

    loading_ = true;
    raw_->setText(toVisible(raw));
    loading_ = false;

    refreshPreview(raw);
}

void StringEditor::syncFromRaw()
{
    const QString raw = fromVisible(raw_->text());

    loading_ = true;
    fillDocument(wysiwyg_, raw);
    loading_ = false;

    refreshPreview(raw);
}

void StringEditor::refreshPreview(const QString & raw)
{
    // 게임 화면처럼 색을 입힌 한 덩어리로 보여 준다.
    QString html = QStringLiteral("<div style='color:#dcdcdc; white-space:pre-wrap;'>");
    QString open;

    const auto escape = [](const QString & text) {
        QString out = text;
        out.replace('&', QStringLiteral("&amp;"));
        out.replace('<', QStringLiteral("&lt;"));
        out.replace('>', QStringLiteral("&gt;"));
        return out;
    };

    QString run;
    for (QChar c : raw)
    {
        if (!isControl(c))
        {
            run += c;
            continue;
        }

        html += escape(run);
        run.clear();

        const auto code = static_cast<std::uint8_t>(c.unicode());
        const io::TextControlCode * info = io::findTextControlCode(code);

        if (!open.isEmpty())
        {
            html += QStringLiteral("</span>");
            open.clear();
        }

        if (info != nullptr && info->isColor)
        {
            open = colorOf(*info).name();
            html += QStringLiteral("<span style='color:%1'>").arg(open);
        }
    }
    html += escape(run);
    if (!open.isEmpty())
        html += QStringLiteral("</span>");
    html += QStringLiteral("</div>");

    preview_->setText(html);
}

void StringEditor::applyCurrent()
{
    if (currentRow_ < 0 || currentRow_ >= static_cast<int>(strings_.size()))
        return;

    const QString raw = (tabs_->currentIndex() == 0)
        ? documentToRaw(wysiwyg_)
        : fromVisible(raw_->text());

    const auto & entry = strings_[static_cast<std::size_t>(currentRow_)];
    if (raw.toStdString() == entry.text)
    {
        status_->setText(tr("바뀐 내용이 없습니다"));
        return;
    }

    if (!document_.setString(entry.id, raw.toStdString()))
    {
        status_->setText(QString::fromStdString(document_.lastError()));
        return;
    }

    emit documentEdited();

    const int row = currentRow_;
    reloadList(row);
    status_->setText(tr("문자열 %1번을 바꿨습니다").arg(entry.id));
}

} // namespace splash::ui
