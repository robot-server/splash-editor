#pragma once

#include <QDialog>

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

    /// Deaths 표가 시작하는 자리. 1.16.1 기준이며 리마스터도 같은 배치를
    /// 쓴다 — EUD 맵이 이 값을 전제로 만들어져 있다.
    static constexpr unsigned kDeathsBase = 0x0058A364;

    /// 유닛 종류 수. 한 플레이어분의 칸 수이기도 하다.
    static constexpr unsigned kUnitTypes = 228;

    /// 자리 -> 주소.
    static unsigned addressFor(unsigned player, unsigned unit);

    /// 주소 -> 자리. 표 밖이거나 네 바이트에 맞지 않으면 거짓.
    static bool slotFor(unsigned address, unsigned * player, unsigned * unit);

private:
    void refreshFromSlot();
    void refreshFromAddress();

    QSpinBox * player_ = nullptr;
    QSpinBox * unit_ = nullptr;
    QLineEdit * address_ = nullptr;
    QLabel * note_ = nullptr;
    bool updating_ = false;
};

} // namespace splash::ui
