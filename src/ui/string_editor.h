#pragma once

// 맵 문자열 편집 창.
//
// 맵 문자열은 0x01~0x1F 제어 문자로 글자 색을 바꾼다. 그래서 편집기를
// 둘로 나눈다 — 색이 그대로 보이는 쪽(위지윅)과 제어 문자를 <03> 처럼
// 드러내는 쪽(원본)이다. 둘은 같은 문자열을 보며, 탭을 옮길 때마다
// 서로의 내용을 넘겨받는다.

#include <QDialog>

#include <cstddef>
#include <vector>

#include "io/map_archive.h"

class QLabel;
class QTabWidget;
class QTableWidget;
class QTextEdit;

namespace splash::chk { class MapDocument; }

namespace splash::ui {

class CodeEditorPane;

class StringEditor : public QDialog
{
    Q_OBJECT

public:
    explicit StringEditor(chk::MapDocument & document, QWidget * parent = nullptr);

signals:
    void documentEdited();

private:
    void reloadList(int selectRow = 0);
    void loadSelected();
    void applyCurrent();
    void syncFromWysiwyg();
    void syncFromRaw();
    void refreshPreview(const QString & raw);

    /// 제어 문자가 든 문자열을 색 서식이 붙은 문서로 옮긴다.
    void fillDocument(QTextEdit * edit, const QString & raw) const;

    /// 색 서식이 붙은 문서를 제어 문자가 든 문자열로 되돌린다.
    QString documentToRaw(const QTextEdit * edit) const;

    /// 제어 문자를 <03> 처럼 드러낸 표기로.
    static QString toVisible(const QString & raw);

    /// <03> 표기를 제어 문자로.
    static QString fromVisible(const QString & visible);

    chk::MapDocument & document_;
    std::vector<io::MapString> strings_;

    QTableWidget * list_ = nullptr;
    QTabWidget * tabs_ = nullptr;
    QTextEdit * wysiwyg_ = nullptr;
    CodeEditorPane * raw_ = nullptr;
    QLabel * preview_ = nullptr;
    QLabel * status_ = nullptr;

    int currentRow_ = -1;
    bool loading_ = false;
    int lastTab_ = 0;
};

} // namespace splash::ui
