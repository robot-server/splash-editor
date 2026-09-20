#pragma once

#include "io/eud.h"

#include <QDialog>

#include <cstdint>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeWidget;

namespace splash::ui {

/// EUD 주소·EPD·Deaths 자리를 서로 바꾸고, 이름 붙은 메모리 자리를 찾는 창.
///
/// EUD 트리거는 "플레이어 N 의 유닛 M 을 죽인 수"를 세는 조건이 실은
/// 게임 메모리의 한 자리를 읽는다는 점을 이용한다. 자리와 주소를 손으로
/// 계산하면 틀리기 쉬워 여기서 바꿔 준다.
class EudCalculator : public QDialog
{
    Q_OBJECT

public:
    explicit EudCalculator(QWidget * parent = nullptr);

    /// 셈은 io/eud.h 에 있다. 여기서는 그것을 화면에 이어 붙이기만 한다.
    static constexpr unsigned kUnitTypes = io::eud::kUnitTypes;

    /// Deaths 자리를 골라 달라고 띄운다 (옛 방식 — 플레이어·유닛 두 칸).
    static bool pickSlot(QWidget * parent, unsigned * player, unsigned * unit,
                         unsigned startPlayer = 0, unsigned startUnit = 0);

    /// 주소를 골라 달라고 띄운다. Memory 조건·동작이 쓰는 길이다.
    static bool pickAddress(QWidget * parent, std::uint32_t * address,
                            std::uint32_t startAddress = io::eud::kDeathsBase);

    /// 지금 고른 값.
    unsigned currentPlayer() const;
    unsigned currentUnit() const;
    std::uint32_t currentAddress() const;

    /// 고르기 단추를 달아 둔다 (pickSlot·pickAddress 가 쓴다).
    void enablePicking();

private:
    void refreshFromSlot();
    void refreshFromAddress();
    void refreshFromEpd();
    void reloadOffsets();
    void applyAddress(std::uint32_t address);
    void showAddress(std::uint32_t address);

    QLineEdit * search_ = nullptr;
    QTreeWidget * offsets_ = nullptr;
    QSpinBox * player_ = nullptr;
    QSpinBox * unit_ = nullptr;
    QLineEdit * address_ = nullptr;
    QLineEdit * epd_ = nullptr;
    QLabel * note_ = nullptr;
    QLabel * dbNote_ = nullptr;
    bool updating_ = false;
};

} // namespace splash::ui
