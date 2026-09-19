#pragma once

// 트리거 편집 창. StarEdit 의 트리거 편집기와 같은 구성이다.
//
// 왼쪽에 트리거 목록, 오른쪽에 고른 트리거의 실행 플레이어·조건·액션.
// 조건과 액션의 세부 인자는 텍스트로 고친다 — 그쪽이 MappingCore 의
// 트리거 문법을 그대로 쓰므로 정확하다.

#include <QDialog>

#include <cstdint>
#include <vector>

#include "io/map_archive.h"

class QCheckBox;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QToolButton;

namespace splash::chk { class MapDocument; }
namespace splash::io  { class GameGraphics; }

namespace splash::ui {

class TriggerArgumentPanel;

class TriggerEditor : public QDialog
{
    Q_OBJECT

public:
    TriggerEditor(chk::MapDocument & document, io::GameGraphics & graphics,
                  QWidget * parent = nullptr);
    ~TriggerEditor() override;

signals:
    /// 트리거를 고쳤다. 창이 제목·상태를 갱신하도록 알린다.
    void documentEdited();

private:
    void reloadList(int selectRow = -1);
    void reloadDetail();
    void reloadElements();
    void showConditionArgs(int row);
    void showActionArgs(int row);
    void applyOwners();

    int currentIndex() const;

    chk::MapDocument & document_;
    io::GameGraphics & graphics_;

    QListWidget * list_ = nullptr;
    QCheckBox * owners_[9] {};      ///< 플레이어 1~8 + 모든 플레이어
    QCheckBox * enabled_ = nullptr;
    QListWidget * conditions_ = nullptr;
    QListWidget * actions_ = nullptr;

    // 고른 조건·액션의 인자를 위젯으로 고치는 판.
    TriggerArgumentPanel * conditionArgs_ = nullptr;
    TriggerArgumentPanel * actionArgs_ = nullptr;

    // 지금 트리거의 조건·액션과, 고를 수 있는 종류 목록.
    std::vector<io::TriggerElement> conditionElements_;
    std::vector<io::TriggerElement> actionElements_;
    std::vector<io::TriggerChoice> conditionTypes_;
    std::vector<io::TriggerChoice> actionTypes_;
    QPlainTextEdit * text_ = nullptr;
    QLabel * summary_ = nullptr;

    bool loading_ = false; ///< 체크박스를 코드로 바꾸는 동안 신호를 무시한다
};

} // namespace splash::ui
