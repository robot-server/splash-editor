#pragma once

// 맵 소리(WAV) 관리 창.
//
// 맵은 MPQ 안에 WAV 파일을 품고, CHK 의 WAV 구역이 그 경로를 가리킨다.
// 여기서 소리를 넣고 빼고 들어 본다. 트리거의 "Play WAV" 가 쓰는 것이
// 바로 이 목록이다.

#include <QDialog>

#include <cstddef>
#include <vector>

#include "io/map_archive.h"

class QCheckBox;
class QLabel;
class QTableWidget;

namespace splash::chk { class MapDocument; }

namespace splash::ui {

class SoundPlayer;

class SoundEditor : public QDialog
{
    Q_OBJECT

public:
    SoundEditor(chk::MapDocument & document, SoundPlayer * player,
                QWidget * parent = nullptr);

signals:
    void documentEdited();

private:
    void reloadList(int selectRow = 0);
    void addSounds();
    void removeSelected();
    void extractSelected();
    void playSelected();

    int currentRow() const;

    chk::MapDocument & document_;
    SoundPlayer * player_ = nullptr;

    QTableWidget * list_ = nullptr;
    QCheckBox * removeIfUsed_ = nullptr;
    QLabel * status_ = nullptr;

    std::vector<io::MapArchive::MapSound> sounds_;
};

} // namespace splash::ui
