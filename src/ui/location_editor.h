#pragma once

// 로케이션 편집 창.
//
// 트리거가 가리키는 자리다. 목록에서 고르고 이름·범위·높이를 고친다.
// 화면에서 끌어 옮기는 것과 함께 쓴다.

#include <QDialog>

#include <cstddef>

namespace splash::chk { class MapDocument; }

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace splash::ui {

class LocationEditor : public QDialog
{
    Q_OBJECT

public:
    explicit LocationEditor(chk::MapDocument & document, QWidget * parent = nullptr);

signals:
    void documentEdited();

    /// 고른 로케이션을 화면에서 보여 달라는 뜻.
    void locationFocused(std::size_t index);

private:
    void reloadList(int selectRow = 0);
    void loadSelected();
    void applyCurrent();
    void addLocation();
    void removeSelected();

    int currentRow() const;

    chk::MapDocument & document_;

    QTableWidget * list_ = nullptr;
    QLineEdit * name_ = nullptr;
    QSpinBox * left_ = nullptr;
    QSpinBox * top_ = nullptr;
    QSpinBox * right_ = nullptr;
    QSpinBox * bottom_ = nullptr;
    QCheckBox * elevation_[6] {};
    QCheckBox * inverted_ = nullptr;
    QCheckBox * removeIfUsed_ = nullptr;
    QLabel * status_ = nullptr;

    bool loading_ = false;
};

} // namespace splash::ui
