#pragma once

// 미션 브리핑 편집 창 (MBRF).
//
// 브리핑은 조건이 없고 동작만 줄지어 있는 트리거다. 왼쪽에 브리핑
// 목록, 오른쪽에 그 브리핑을 보는 플레이어와 동작 목록·텍스트를 둔다.

#include <QDialog>

#include <cstdint>
#include <vector>

#include "io/map_archive.h"

class QCheckBox;
class QListWidget;

class QLabel;

namespace splash::chk { class MapDocument; }
namespace splash::io  { class GameGraphics; }

namespace splash::ui {

class CodeEditorPane;
class TriggerArgumentPanel;

class BriefingEditor : public QDialog
{
    Q_OBJECT

public:
    BriefingEditor(chk::MapDocument & document, io::GameGraphics & graphics,
                   QWidget * parent = nullptr);
    ~BriefingEditor() override;

signals:
    void documentEdited();

private:
    void reloadList(int selectRow = -1);
    void reloadDetail();
    void reloadElements();
    void showActionArgs(int row);
    void applyOwners();

    int currentIndex() const;

    chk::MapDocument & document_;
    io::GameGraphics & graphics_;

    QListWidget * list_ = nullptr;
    QCheckBox * owners_[9] {};   ///< 플레이어 1~8 + 모든 플레이어
    QListWidget * actions_ = nullptr;
    TriggerArgumentPanel * actionArgs_ = nullptr;
    CodeEditorPane * text_ = nullptr;

    std::vector<io::TriggerElement> actionElements_;
    std::vector<io::TriggerChoice> actionTypes_;
    QLabel * summary_ = nullptr;
    QLabel * hint_ = nullptr;

    bool loading_ = false;
};

} // namespace splash::ui
