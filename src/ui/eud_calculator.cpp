#include "ui/eud_calculator.h"

#include "io/map_archive.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QClipboard>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace splash::ui {

unsigned EudCalculator::addressFor(unsigned player, unsigned unit)
{
    return kDeathsBase + 4 * (player * kUnitTypes + unit);
}

bool EudCalculator::slotFor(unsigned address, unsigned * player, unsigned * unit)
{
    if (address < kDeathsBase)
        return false;

    const unsigned delta = address - kDeathsBase;
    if (delta % 4 != 0)
        return false; // 네 바이트 경계가 아니면 Deaths 칸이 아니다

    const unsigned slot = delta / 4;
    if (player != nullptr)
        *player = slot / kUnitTypes;
    if (unit != nullptr)
        *unit = slot % kUnitTypes;
    return true;
}

EudCalculator::EudCalculator(QWidget * parent)
    : QDialog(parent)
{
    setWindowTitle(tr("EUD 주소 계산기"));

    auto * form = new QFormLayout();

    player_ = new QSpinBox(this);
    player_->setRange(0, 65535);
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
        tr("플레이어 %1 의 '%2' 를 세는 칸이 주소 %3 이다.")
            .arg(player_->value())
            .arg(QString::fromStdString(
                io::unitTypeName(static_cast<std::uint16_t>(unit_->value()))))
            .arg(address_->text()));
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
        note_->setText(
            tr("Deaths 표(%1 부터)보다 앞이거나 네 바이트 경계가 아닙니다.")
                .arg(QStringLiteral("0x0058A364")));
        return;
    }

    updating_ = true;
    player_->setValue(static_cast<int>(std::min(player, 65535u)));
    unit_->setValue(static_cast<int>(unit));
    note_->setText(
        tr("주소 %1 은 플레이어 %2 의 '%3' 칸이다.")
            .arg(address_->text())
            .arg(player)
            .arg(QString::fromStdString(io::unitTypeName(static_cast<std::uint16_t>(unit)))));
    updating_ = false;
}

} // namespace splash::ui
