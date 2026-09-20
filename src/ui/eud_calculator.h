#pragma once

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

    /// Deaths 표가 시작하는 자리. 1.16.1 기준이며 리마스터도 같은 배치를
    /// 쓴다 — EUD 맵이 이 값을 전제로 만들어져 있다.
    static constexpr unsigned kDeathsBase = 0x0058A364;

    /// 유닛 종류 수. 한 플레이어분의 칸 수이기도 하다.
    static constexpr unsigned kUnitTypes = 228;

    /// 자리 -> 주소. 자리는 32비트로 감아 돈다.
    static unsigned addressFor(unsigned player, unsigned unit);

    /// 주소 -> 자리. 네 바이트에 맞지 않으면 거짓.
    ///
    /// EUD 가 노리는 곳은 대개 Deaths 표보다 앞이다. 그런 주소는 자리가
    /// 음수가 되는데, 게임은 32비트로 감아 세므로 아주 큰 플레이어 번호로
    /// 나타난다.
    static bool slotFor(unsigned address, unsigned * player, unsigned * unit);

    /// 주소 -> EPD. 네 바이트 단위로 센 Deaths 표로부터의 거리다.
    static int epdFor(unsigned address);

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

    /// 널리 쓰이는 자리. 이름과 주소.
    struct KnownAddress
    {
        const char * name;
        unsigned address;
    };
    static const std::vector<KnownAddress> & knownAddresses();

    QComboBox * known_ = nullptr;
    QSpinBox * player_ = nullptr;
    QSpinBox * unit_ = nullptr;
    QLineEdit * address_ = nullptr;
    QLabel * note_ = nullptr;
    bool updating_ = false;
};

} // namespace splash::ui
