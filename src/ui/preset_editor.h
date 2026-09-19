#pragma once

// 유닛 속성 프리셋 편집 창 (CUWP).
//
// 트리거의 "유닛 속성으로 유닛 만들기" 가 이 64자리 가운데 하나를
// 가리킨다. 여기서 각 자리의 값을 정한다.

#include <QDialog>

#include <cstddef>
#include <vector>

#include "io/map_archive.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QComboBox;

namespace splash::chk { class MapDocument; }

namespace splash::ui {

class PresetEditor : public QDialog
{
    Q_OBJECT

public:
    explicit PresetEditor(chk::MapDocument & document, QWidget * parent = nullptr);

signals:
    void documentEdited();

private:
    void reloadList(int selectRow = 0);
    void loadSelected();
    void applyCurrent();
    void updateEnabled();

    chk::MapDocument & document_;
    std::vector<io::MapArchive::UnitPreset> presets_;

    QListWidget * list_ = nullptr;
    QCheckBox * setOwner_ = nullptr;
    QComboBox * owner_ = nullptr;
    QCheckBox * setHitpoints_ = nullptr;
    QSpinBox * hitpoints_ = nullptr;
    QCheckBox * setShields_ = nullptr;
    QSpinBox * shields_ = nullptr;
    QCheckBox * setEnergy_ = nullptr;
    QSpinBox * energy_ = nullptr;
    QCheckBox * setResources_ = nullptr;
    QSpinBox * resources_ = nullptr;
    QCheckBox * setHangar_ = nullptr;
    QSpinBox * hangar_ = nullptr;

    QCheckBox * cloaked_ = nullptr;
    QCheckBox * burrowed_ = nullptr;
    QCheckBox * inTransit_ = nullptr;
    QCheckBox * hallucinated_ = nullptr;
    QCheckBox * invincible_ = nullptr;

    QLabel * status_ = nullptr;
    bool loading_ = false;
};

} // namespace splash::ui
