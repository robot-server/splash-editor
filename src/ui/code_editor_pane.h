#pragma once

// 코드 편집기 한 벌 — 편집기, 찾기·바꾸기 줄, 문제 목록, 상태 줄.
//
// 편집기 하나만 쓰면 IDE 에서 늘 쓰던 것들이 없어 답답하다. 이 판이
// 그 껍데기를 맡아 트리거 창과 브리핑 창이 같은 모양을 갖게 한다.

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QWidget;

namespace splash::io { struct TriggerVocabulary; }

namespace splash::ui {

class CodeEditor;

class CodeEditorPane : public QWidget
{
    Q_OBJECT

public:
    explicit CodeEditorPane(QWidget * parent = nullptr);

    CodeEditor * editor() const { return editor_; }

    QString text() const;
    void setText(const QString & text);

    void setVocabulary(const io::TriggerVocabulary & vocabulary);

    /// 문제가 있는지 — 적용 전에 물어보는 데 쓴다.
    bool hasErrors() const;

public slots:
    /// 찾기 줄을 열고 커서를 놓는다 (Ctrl+F).
    void showFind(bool withReplace = false);

private:
    void refreshProblems();
    void refreshStatus();

    CodeEditor * editor_ = nullptr;

    QWidget * findBar_ = nullptr;
    QLineEdit * findField_ = nullptr;
    QLineEdit * replaceField_ = nullptr;
    QWidget * replaceRow_ = nullptr;
    QCheckBox * caseSensitive_ = nullptr;
    QCheckBox * wholeWords_ = nullptr;

    QListWidget * problems_ = nullptr;
    QLabel * status_ = nullptr;
};

} // namespace splash::ui
