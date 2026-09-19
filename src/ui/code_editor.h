#pragma once

// 트리거 텍스트를 고치는 코드 편집기.
//
// 줄 번호, 구문 강조, 괄호 짝 맞춤, 자동 완성, 실시간 문법 검사까지
// 요즘 코드 편집기가 하는 일을 갖췄다. 트리거 문법은 MappingCore 의
// 텍스트 트리거를 따르고, 낱말 목록은 열린 맵에서 받아 온다.

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

class QCompleter;
class QStringListModel;

namespace splash::io { struct TriggerVocabulary; }

namespace splash::ui {

class CodeEditor;

/// 트리거 텍스트의 구문 강조.
class TriggerHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit TriggerHighlighter(QTextDocument * document);

    /// 맵에서 받은 이름들을 강조 대상에 넣는다.
    void setVocabulary(const io::TriggerVocabulary & vocabulary);

protected:
    void highlightBlock(const QString & text) override;

private:
    QTextCharFormat keyword_;     ///< Trigger / Conditions / Actions
    QTextCharFormat call_;        ///< 조건·액션 이름
    QTextCharFormat unknownCall_; ///< 이름표에 없는 호출
    QTextCharFormat string_;
    QTextCharFormat escape_;
    QTextCharFormat number_;
    QTextCharFormat comment_;
    QTextCharFormat constant_;
    QTextCharFormat punctuation_;

    QSet<QString> knownCalls_;
    QSet<QString> constants_;
};

/// 한 줄에 매달린 문제. 편집기가 물결 밑줄과 여백 표시로 보여 준다.
struct Diagnostic
{
    int line = 0;       ///< 0 부터
    int column = 0;     ///< 0 부터
    int length = 0;
    bool error = true;  ///< 거짓이면 경고
    QString message;
};

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget * parent = nullptr);
    ~CodeEditor() override;

    /// 맵에서 받은 낱말로 강조·자동 완성·검사를 채운다.
    void setVocabulary(const io::TriggerVocabulary & vocabulary);

    /// 바깥에서 받은 문제(컴파일 실패 등)를 표시한다.
    void setExternalDiagnostics(const std::vector<Diagnostic> & diagnostics);

    /// 지금 편집기가 찾아낸 문제들.
    const std::vector<Diagnostic> & diagnostics() const { return diagnostics_; }

    /// 글자를 찾는다. 찾으면 그 자리를 고르고 참을 준다.
    bool findText(const QString & needle, bool forward, bool caseSensitive, bool wholeWords);

    /// 찾은 것을 모두 바꾼다. 바꾼 수를 준다.
    int replaceAll(const QString & needle, const QString & replacement,
                   bool caseSensitive, bool wholeWords);

    /// 고른 자리가 찾는 글자와 같으면 바꾸고 다음으로 넘어간다.
    bool replaceCurrent(const QString & needle, const QString & replacement,
                        bool caseSensitive, bool wholeWords);

    /// 그 줄로 커서를 옮긴다 (0 부터).
    void gotoLine(int line);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent * event);

signals:
    /// 문법 검사 결과가 바뀌었다.
    void diagnosticsChanged();

protected:
    void resizeEvent(QResizeEvent * event) override;
    void keyPressEvent(QKeyEvent * event) override;
    void paintEvent(QPaintEvent * event) override;
    void focusInEvent(QFocusEvent * event) override;
    void wheelEvent(QWheelEvent * event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect & rect, int dy);
    void refreshExtraSelections();
    void runLinter();
    void maybeComplete();
    void insertCompletion(const QString & completion);
    QString completionPrefix() const;

    QWidget * lineNumbers_ = nullptr;
    TriggerHighlighter * highlighter_ = nullptr;
    QCompleter * completer_ = nullptr;
    QStringListModel * completionModel_ = nullptr;

    QSet<QString> knownCalls_;
    std::vector<Diagnostic> diagnostics_;
    std::vector<Diagnostic> external_;
};

} // namespace splash::ui
