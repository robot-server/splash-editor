#pragma once

#include "io/eud.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QLineEdit;
class QLabel;
class QSpinBox;

namespace splash::ui {

/// EUD 주소와 Deaths 자리(플레이어·유닛)를 서로 바꿔 주는 창.
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

    /// 자리를 골라 달라고 띄운다. 골랐으면 참이고 player·unit 에 담긴다.
    ///
    /// Deaths 조건을 고치는 중에 부르면 고른 자리가 곧바로 그 조건에
    /// 들어간다 — 주소를 손으로 옮겨 적지 않아도 된다.
    static bool pickSlot(QWidget * parent, unsigned * player, unsigned * unit,
                         unsigned startPlayer = 0, unsigned startUnit = 0);

    /// 지금 고른 자리.
    unsigned currentPlayer() const;
    unsigned currentUnit() const;

    /// 고르기 단추를 달아 둔다 (pickSlot 이 쓴다).
    void enablePicking();

private:
    void refreshFromSlot();
    void refreshFromAddress();

    QComboBox * known_ = nullptr;
    QSpinBox * player_ = nullptr;
    QSpinBox * unit_ = nullptr;
    QLineEdit * address_ = nullptr;
    QLabel * note_ = nullptr;
    bool updating_ = false;
};

} // namespace splash::ui
