#include "ui/trigger_argument_panel.h"

#include "ui/eud_calculator.h"

#include <QComboBox>
#include <QLineEdit>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace splash::ui {

TriggerArgumentPanel::TriggerArgumentPanel(Kind kind, QWidget * parent)
    : QWidget(parent), kind_(kind)
{
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    empty_ = new QLabel(tr("왼쪽에서 한 줄을 고르세요."), this);
    empty_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    layout->addWidget(empty_);

    body_ = new QWidget(this);
    form_ = new QFormLayout(body_);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(body_, 1);

    body_->hide();
}

int TriggerArgumentPanel::findArg(io::TriggerArgRole role) const
{
    for (std::size_t i = 0; i < element_.args.size(); ++i)
    {
        if (element_.args[i].role == role)
            return static_cast<int>(i);
    }
    return -1;
}

bool TriggerArgumentPanel::isDeaths() const
{
    // Chk 의 Deaths 조건과 Set Deaths 액션. 메모리를 건드릴 수 있는 것은
    // 이 둘뿐이라 다른 줄에는 단추를 달지 않는다.
    constexpr std::uint8_t kDeathsCondition = 15;
    constexpr std::uint8_t kSetDeathsAction = 45;

    return kind_ == Kind::Condition ? element_.type == kDeathsCondition
                                    : element_.type == kSetDeathsAction;
}

void TriggerArgumentPanel::clear()
{
    element_ = io::TriggerElement {};
    rebuild();
}

void TriggerArgumentPanel::setElement(std::size_t triggerIndex, std::size_t slot,
                                      const io::TriggerElement & element,
                                      const std::vector<io::TriggerChoice> & types)
{
    triggerIndex_ = triggerIndex;
    slot_ = slot;
    element_ = element;
    types_ = types;
    rebuild();
}

void TriggerArgumentPanel::rebuild()
{
    loading_ = true;

    // 이전 위젯을 모두 치운다.
    while (form_->count() > 0)
    {
        QLayoutItem * item = form_->takeAt(0);
        if (item->widget() != nullptr)
            item->widget()->deleteLater();
        delete item;
    }

    if (types_.empty())
    {
        body_->hide();
        empty_->show();
        loading_ = false;
        return;
    }

    empty_->hide();
    body_->show();

    // --- 종류 고르기 ---
    auto * typeBox = new QComboBox(body_);
    typeBox->addItem(kind_ == Kind::Condition ? tr("(없음)") : tr("(없음)"), 0);
    for (const auto & type : types_)
    {
        if (type.value == 0)
            continue; // 0 은 "없음" 이라 위에서 이미 넣었다
        typeBox->addItem(QString::fromStdString(type.text), type.value);
    }

    const int typeIndex = typeBox->findData(static_cast<int>(element_.type));
    typeBox->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);

    connect(typeBox, &QComboBox::currentIndexChanged, this, [this, typeBox](int) {
        if (loading_)
            return;
        emit typeChanged(slot_, static_cast<std::uint8_t>(typeBox->currentData().toInt()));
    });
    form_->addRow(tr("종류"), typeBox);

    // --- EUD 자리 고르기 ---
    //
    // Deaths 는 "플레이어 N 의 유닛 M 을 죽인 수"를 세는 척하면서 실은
    // 게임 메모리의 한 자리를 읽는다. 자리를 손으로 셈해 넣으면 틀리기
    // 쉬우니, 주소로 고를 수 있게 단추를 달아 둔다.
    const int playerArg = findArg(io::TriggerArgRole::Player);
    const int unitArg = findArg(io::TriggerArgRole::UnitType);

    if (isDeaths() && playerArg >= 0 && unitArg >= 0)
    {
        auto * pick = new QPushButton(tr("EUD 주소로 고르기…"), body_);
        pick->setToolTip(
            tr("메모리 주소를 넣으면 그 자리에 맞는 플레이어·유닛으로 채웁니다."));

        connect(pick, &QPushButton::clicked, this, [this, playerArg, unitArg] {
            unsigned player = element_.args[std::size_t(playerArg)].value;
            unsigned unit = element_.args[std::size_t(unitArg)].value;

            if (!EudCalculator::pickSlot(this, &player, &unit, player, unit))
                return;

            // 값을 넣으면 이 판이 통째로 다시 그려져 지금 누른 단추가
            // 사라진다. 누름이 끝난 뒤로 미뤄 둔다.
            const std::size_t slot = slot_;
            QTimer::singleShot(0, this, [this, slot, playerArg, unitArg, player, unit] {
                emit eudSlotPicked(slot, std::size_t(playerArg), player,
                                   std::size_t(unitArg), unit);
            });
        });

        form_->addRow(QString(), pick);
    }

    // --- 인자마다 알맞은 상자 ---
    for (std::size_t i = 0; i < element_.args.size(); ++i)
    {
        const io::TriggerArg & arg = element_.args[i];
        const QString label = QString::fromStdString(arg.label);

        switch (arg.kind)
        {
            case io::TriggerArgKind::Choice:
            {
                auto * box = new QComboBox(body_);
                box->setMaxVisibleItems(20);
                int current = -1;
                for (std::size_t c = 0; c < arg.choices.size(); ++c)
                {
                    const auto & choice = arg.choices[c];
                    box->addItem(QString::fromStdString(choice.text),
                                 QVariant::fromValue(choice.value));
                    if (choice.value == arg.value)
                        current = static_cast<int>(c);
                }

                if (current < 0)
                {
                    // 이름표가 없는 값이면 숫자 그대로 넣어 둔다 — 고른 적
                    // 없는 값을 조용히 바꿔 버리면 안 된다.
                    box->addItem(QString::number(arg.value), QVariant::fromValue(arg.value));
                    current = box->count() - 1;
                }
                box->setCurrentIndex(current);

                connect(box, &QComboBox::currentIndexChanged, this, [this, box, i](int) {
                    if (loading_)
                        return;
                    emit argChanged(slot_, i, box->currentData().toUInt());
                });
                form_->addRow(label, box);
                break;
            }

            case io::TriggerArgKind::Number:
            {
                auto * box = new QSpinBox(body_);
                box->setRange(0, static_cast<int>(std::min<std::uint32_t>(arg.maximum, 2147483647u)));
                box->setValue(static_cast<int>(std::min<std::uint32_t>(arg.value, 2147483647u)));
                box->setKeyboardTracking(false);
                connect(box, &QSpinBox::valueChanged, this, [this, i](int value) {
                    if (loading_)
                        return;
                    emit argChanged(slot_, i, static_cast<std::uint32_t>(value));
                });
                form_->addRow(label, box);
                break;
            }

            case io::TriggerArgKind::Sound:
            {
                // 맵에 든 소리를 고르거나, 목록에 없는 경로를 직접 적는다.
                auto * box = new QComboBox(body_);
                box->setEditable(true);
                box->setInsertPolicy(QComboBox::NoInsert);

                QString current = QString::fromStdString(arg.text);
                if (current.startsWith('"') && current.endsWith('"') && current.size() >= 2)
                    current = current.mid(1, current.size() - 2);

                int found = -1;
                for (std::size_t c = 0; c < arg.choices.size(); ++c)
                {
                    const auto & choice = arg.choices[c];
                    box->addItem(QString::fromStdString(choice.text),
                                 QVariant::fromValue(choice.value));
                    if (choice.value == arg.value)
                        found = static_cast<int>(c);
                }

                if (found >= 0)
                    box->setCurrentIndex(found);
                else
                    box->setEditText(current);

                // 목록에서 고르면 그 문자열 번호를 그대로 쓴다 — 새 문자열을
                // 만들지 않아 STR 이 불어나지 않는다.
                connect(box, &QComboBox::activated, this, [this, box, i](int index) {
                    if (loading_ || index < 0)
                        return;
                    emit argChanged(slot_, i, box->itemData(index).toUInt());
                });

                // 직접 적은 경로는 글자로 넘긴다.
                connect(box->lineEdit(), &QLineEdit::editingFinished, this, [this, box, i] {
                    if (loading_)
                        return;
                    if (box->findText(box->currentText()) >= 0)
                        return; // 목록에 있는 것은 위에서 처리했다
                    emit argTextChanged(slot_, i, box->currentText());
                });

                form_->addRow(label, box);
                break;
            }

            case io::TriggerArgKind::Text:
            {
                auto * edit = new QPlainTextEdit(body_);
                // 인용 부호를 벗겨 내 실제 글자만 보여 준다.
                QString text = QString::fromStdString(arg.text);
                if (text.startsWith('"') && text.endsWith('"') && text.size() >= 2)
                    text = text.mid(1, text.size() - 2);
                edit->setPlainText(text);
                edit->setMaximumHeight(110);

                auto * apply = new QPushButton(tr("적용"), body_);
                connect(apply, &QPushButton::clicked, this, [this, edit, i] {
                    emit argTextChanged(slot_, i, edit->toPlainText());
                });

                auto * holder = new QWidget(body_);
                auto * holderLayout = new QVBoxLayout(holder);
                holderLayout->setContentsMargins(0, 0, 0, 0);
                holderLayout->addWidget(edit);
                holderLayout->addWidget(apply);

                form_->addRow(label, holder);
                break;
            }

            case io::TriggerArgKind::None:
                break;
        }
    }

    loading_ = false;
}

} // namespace splash::ui
