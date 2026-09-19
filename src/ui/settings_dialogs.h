#pragma once

// 맵이 정하는 유닛·업그레이드·기술 설정 창.
//
// StarEdit 과 SCMDraft 의 "Unit Settings / Upgrade Settings / Tech
// Settings" 에 해당한다. 왼쪽에서 종류를 고르고 오른쪽에서 값을 고친다.
// 값은 종류를 바꾸거나 창을 닫을 때 한 번에 기록한다 — 스핀 상자를
// 누를 때마다 쓰면 편집 이력이 잘게 쪼개진다.

#include <QDialog>

#include <cstdint>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTableWidget;

namespace splash::chk { class MapDocument; }
namespace splash::io  { class GameGraphics; }

namespace splash::ui {

/// 유닛 능력치와 플레이어별 생산 가능 여부 (UNIS/UNIx, PUNI).
class UnitSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    UnitSettingsDialog(chk::MapDocument & document, io::GameGraphics & graphics,
                        QWidget * parent = nullptr);

signals:
    void documentEdited();

private:
    void reloadList();
    void loadSelected();
    void commitPending();
    void updateEnabled();

    chk::MapDocument & document_;
    io::GameGraphics & graphics_;

    QLineEdit * filter_ = nullptr;
    QListWidget * list_ = nullptr;
    QCheckBox * useDefault_ = nullptr;
    QSpinBox * hitpoints_ = nullptr;
    QSpinBox * shields_ = nullptr;
    QSpinBox * armor_ = nullptr;
    QSpinBox * buildTime_ = nullptr;
    QSpinBox * minerals_ = nullptr;
    QSpinBox * gas_ = nullptr;
    QCheckBox * defaultBuildable_ = nullptr;
    QTableWidget * players_ = nullptr;

    int pendingType_ = -1;  ///< 아직 기록하지 않은 종류 (-1 이면 없음)
    bool loading_ = false;
};

/// 업그레이드 비용과 플레이어별 시작·최대 단계 (UPGS/UPGx, UPGR/PUPx).
class UpgradeSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    UpgradeSettingsDialog(chk::MapDocument & document, io::GameGraphics & graphics,
                           QWidget * parent = nullptr);

signals:
    void documentEdited();

private:
    void loadSelected();
    void commitPending();
    void updateEnabled();

    chk::MapDocument & document_;
    io::GameGraphics & graphics_;

    QListWidget * list_ = nullptr;
    QCheckBox * useDefault_ = nullptr;
    QSpinBox * baseMinerals_ = nullptr;
    QSpinBox * mineralFactor_ = nullptr;
    QSpinBox * baseGas_ = nullptr;
    QSpinBox * gasFactor_ = nullptr;
    QSpinBox * baseTime_ = nullptr;
    QSpinBox * timeFactor_ = nullptr;
    QSpinBox * defaultStart_ = nullptr;
    QSpinBox * defaultMax_ = nullptr;
    QTableWidget * players_ = nullptr;

    int pendingType_ = -1;
    bool loading_ = false;
};

/// 기술 비용과 플레이어별 사용 가능·연구 여부 (TECS/TECx, PTEC/PTEx).
class TechSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    TechSettingsDialog(chk::MapDocument & document, io::GameGraphics & graphics,
                        QWidget * parent = nullptr);

signals:
    void documentEdited();

private:
    void loadSelected();
    void commitPending();
    void updateEnabled();

    chk::MapDocument & document_;
    io::GameGraphics & graphics_;

    QListWidget * list_ = nullptr;
    QCheckBox * useDefault_ = nullptr;
    QSpinBox * minerals_ = nullptr;
    QSpinBox * gas_ = nullptr;
    QSpinBox * researchTime_ = nullptr;
    QSpinBox * energy_ = nullptr;
    QCheckBox * defaultAvailable_ = nullptr;
    QCheckBox * defaultResearched_ = nullptr;
    QTableWidget * players_ = nullptr;

    int pendingType_ = -1;
    bool loading_ = false;
};

} // namespace splash::ui
