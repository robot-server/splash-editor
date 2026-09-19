#pragma once

// 스위치 이름 편집 창 (SWNM).
//
// 트리거가 스위치를 번호로 다루지만, 이름을 붙여 두면 트리거 편집기의
// 목록에 그 이름이 나온다.

#include <QDialog>

#include <cstddef>
#include <vector>
#include <string>

class QTableWidget;
class QLabel;

namespace splash::chk { class MapDocument; }

namespace splash::ui {

class SwitchEditor : public QDialog
{
    Q_OBJECT

public:
    explicit SwitchEditor(chk::MapDocument & document, QWidget * parent = nullptr);

signals:
    void documentEdited();

private:
    void reloadList();
    void applyChanges();

    chk::MapDocument & document_;
    std::vector<std::string> names_;

    QTableWidget * list_ = nullptr;
    QLabel * status_ = nullptr;
};

} // namespace splash::ui
