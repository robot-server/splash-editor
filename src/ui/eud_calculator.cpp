#include "ui/eud_calculator.h"

#include "io/map_archive.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QClipboard>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace splash::ui {

unsigned EudCalculator::addressFor(unsigned player, unsigned unit)
{
    // 게임이 하는 셈과 같게 32비트로 감아 돈다.
    return kDeathsBase + 4u * (player * kUnitTypes + unit);
}

int EudCalculator::epdFor(unsigned address)
{
    return static_cast<int>(address - kDeathsBase) / 4;
}

bool EudCalculator::slotFor(unsigned address, unsigned * player, unsigned * unit)
{
    if ((address - kDeathsBase) % 4 != 0)
        return false; // 네 바이트 경계가 아니면 Deaths 칸이 아니다

    const unsigned slot = (address - kDeathsBase) / 4;
    if (player != nullptr)
        *player = slot / kUnitTypes;
    if (unit != nullptr)
        *unit = slot % kUnitTypes;
    return true;
}

const std::vector<EudCalculator::KnownAddress> & EudCalculator::knownAddresses()
{
    // 널리 알려진 자리들. 1.16.1 기준이며 리마스터도 같은 자리를 흉내 낸다.
    // 출처: StarEdit Network 의 EUD 주소 모음.
    static const std::vector<KnownAddress> kKnown {
        { "플레이어 미네랄",        0x0057F0F0 },
        { "플레이어 가스",          0x0057F120 },
        { "캔 가스 총량",           0x0057F150 },
        { "캔 미네랄 총량",         0x0057F180 },
        { "저그 대군주 여유",       0x00582144 },
        { "저그 대군주 사용",       0x00582174 },
        { "테란 보급 여유",         0x005821D4 },
        { "테란 보급 사용",         0x00582204 },
        { "프로토스 파일런 여유",   0x00582264 },
        { "프로토스 파일런 사용",   0x00582294 },
        { "맵 크기",                0x0057F1D4 },
        { "타일셋",                 0x0057F1DC },
        { "화면 위치(타일)",        0x0057F1D0 },
        { "유닛 수 표",             0x00582324 },
        { "다 지은 유닛 수 표",     0x00584DE4 },
        { "잡은 유닛 수 표",        0x005878A4 },
        { "단축키 묶음",            0x0057FE60 },
        { "유닛 목록 첫 자리",      0x0059CCA8 },
        { "Deaths 표 첫 자리",      0x0058A364 },
    };
    return kKnown;
}

EudCalculator::EudCalculator(QWidget * parent)
    : QDialog(parent)
{
    setWindowTitle(tr("EUD 주소 계산기"));

    auto * form = new QFormLayout();

    known_ = new QComboBox(this);
    known_->addItem(tr("(직접 넣기)"), 0u);
    for (const auto & entry : knownAddresses())
    {
        known_->addItem(QString::fromUtf8(entry.name),
                        static_cast<unsigned>(entry.address));
    }
    connect(known_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index <= 0)
            return;
        address_->setText(
            QStringLiteral("0x%1")
                .arg(known_->currentData().toUInt(), 8, 16, QLatin1Char('0')).toUpper()
                .replace(QStringLiteral("0X"), QStringLiteral("0x")));
        refreshFromAddress();
    });
    form->addRow(tr("자주 쓰는 자리"), known_);

    player_ = new QSpinBox(this);
    // 자리는 32비트로 감아 도므로 아주 큰 값도 받아야 한다. QSpinBox 는
    // 부호 있는 32비트까지만 다루니 두 배 범위를 나눠 쓰지 않고, 주소 쪽을
    // 주된 입력으로 삼는다.
    player_->setRange(0, 2147483647);
    player_->setToolTip(
        tr("Deaths 조건의 플레이어 자리. EUD 는 12 를 넘는 값을 일부러 써서 "
           "표 밖의 메모리를 가리킨다."));
    form->addRow(tr("플레이어"), player_);

    unit_ = new QSpinBox(this);
    unit_->setRange(0, static_cast<int>(kUnitTypes) - 1);
    unit_->setToolTip(tr("Deaths 조건의 유닛 자리 (0-227)."));
    form->addRow(tr("유닛"), unit_);

    address_ = new QLineEdit(this);
    address_->setToolTip(tr("메모리 주소. 0x 를 붙여도 되고 안 붙여도 된다."));
    form->addRow(tr("주소"), address_);

    note_ = new QLabel(this);
    note_->setWordWrap(true);

    auto * copyAddress = new QPushButton(tr("주소 복사"), this);
    auto * copySlot = new QPushButton(tr("플레이어·유닛 복사"), this);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addButton(copyAddress, QDialogButtonBox::ActionRole);
    buttons->addButton(copySlot, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(copyAddress, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(address_->text());
    });
    connect(copySlot, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(
            tr("%1, %2").arg(player_->value()).arg(unit_->value()));
    });

    auto * layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note_);
    layout->addWidget(buttons);

    connect(player_, &QSpinBox::valueChanged, this, [this](int) { refreshFromSlot(); });
    connect(unit_, &QSpinBox::valueChanged, this, [this](int) { refreshFromSlot(); });
    connect(address_, &QLineEdit::textEdited, this, [this](const QString &) {
        refreshFromAddress();
    });

    refreshFromSlot();
}

void EudCalculator::refreshFromSlot()
{
    if (updating_)
        return;

    updating_ = true;
    const unsigned address = addressFor(static_cast<unsigned>(player_->value()),
                                        static_cast<unsigned>(unit_->value()));
    address_->setText(QStringLiteral("0x%1").arg(address, 8, 16, QLatin1Char('0')).toUpper()
                          .replace(QStringLiteral("0X"), QStringLiteral("0x")));

    note_->setText(
        tr("플레이어 %1 의 '%2' 를 세는 칸이 주소 %3 (EPD %4) 이다.")
            .arg(player_->value())
            .arg(QString::fromStdString(
                io::unitTypeName(static_cast<std::uint16_t>(unit_->value()))))
            .arg(address_->text())
            .arg(epdFor(address)));
    updating_ = false;
}

void EudCalculator::refreshFromAddress()
{
    if (updating_)
        return;

    QString text = address_->text().trimmed();
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        text = text.mid(2);

    bool ok = false;
    const unsigned address = text.toUInt(&ok, 16);
    if (!ok)
    {
        note_->setText(tr("열여섯 자리 수로 적어 주세요 (예: 0058A364)."));
        return;
    }

    unsigned player = 0;
    unsigned unit = 0;
    if (!slotFor(address, &player, &unit))
    {
        note_->setText(tr("네 바이트 경계가 아닙니다 — Deaths 칸으로는 읽을 수 없습니다."));
        return;
    }

    updating_ = true;
    player_->setValue(static_cast<int>(std::min(player, 2147483647u)));
    unit_->setValue(static_cast<int>(unit));
    note_->setText(
        tr("주소 %1 (EPD %2) 은 플레이어 %3 의 '%4' 칸이다.")
            .arg(address_->text())
            .arg(epdFor(address))
            .arg(player)
            .arg(QString::fromStdString(io::unitTypeName(static_cast<std::uint16_t>(unit)))));
    updating_ = false;
}

} // namespace splash::ui
