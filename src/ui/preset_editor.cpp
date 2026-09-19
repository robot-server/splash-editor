#include "ui/preset_editor.h"

#include "chk/map_document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace splash::ui {

PresetEditor::PresetEditor(chk::MapDocument & document, QWidget * parent)
    : QDialog(parent), document_(document)
{
    setWindowTitle(tr("유닛 속성 프리셋"));
    resize(760, 560);

    list_ = new QListWidget(this);

    auto * form = new QFormLayout();

    const auto addRow = [this, form](QCheckBox *& toggle, QWidget * editor,
                                     const QString & label) {
        toggle = new QCheckBox(tr("정함"), this);
        auto * row = new QHBoxLayout();
        row->addWidget(toggle);
        row->addWidget(editor, 1);
        form->addRow(label, row);
    };

    owner_ = new QComboBox(this);
    for (int player = 1; player <= 12; ++player)
        owner_->addItem(tr("플레이어 %1").arg(player), player - 1);
    addRow(setOwner_, owner_, tr("소유자"));

    hitpoints_ = new QSpinBox(this);
    hitpoints_->setRange(0, 100);
    hitpoints_->setSuffix(tr(" %"));
    addRow(setHitpoints_, hitpoints_, tr("체력"));

    shields_ = new QSpinBox(this);
    shields_->setRange(0, 100);
    shields_->setSuffix(tr(" %"));
    addRow(setShields_, shields_, tr("실드"));

    energy_ = new QSpinBox(this);
    energy_->setRange(0, 100);
    energy_->setSuffix(tr(" %"));
    addRow(setEnergy_, energy_, tr("에너지"));

    resources_ = new QSpinBox(this);
    resources_->setRange(0, 2147483647);
    addRow(setResources_, resources_, tr("자원량"));

    hangar_ = new QSpinBox(this);
    hangar_->setRange(0, 65535);
    addRow(setHangar_, hangar_, tr("격납고"));

    auto * stateBox = new QGroupBox(tr("상태"), this);
    auto * stateLayout = new QHBoxLayout(stateBox);
    cloaked_ = new QCheckBox(tr("은폐"), stateBox);
    burrowed_ = new QCheckBox(tr("버로우"), stateBox);
    inTransit_ = new QCheckBox(tr("수송 중"), stateBox);
    hallucinated_ = new QCheckBox(tr("환각"), stateBox);
    invincible_ = new QCheckBox(tr("무적"), stateBox);
    for (QCheckBox * box : {cloaked_, burrowed_, inTransit_, hallucinated_, invincible_})
        stateLayout->addWidget(box);
    stateLayout->addStretch();

    auto * applyButton = new QPushButton(tr("이 프리셋에 적용"), this);
    connect(applyButton, &QPushButton::clicked, this, [this] { applyCurrent(); });

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto * right = new QVBoxLayout();
    right->addLayout(form);
    right->addWidget(stateBox);
    right->addWidget(applyButton);
    right->addStretch();
    right->addWidget(status_);

    auto * columns = new QHBoxLayout();
    columns->addWidget(list_, 1);
    columns->addLayout(right, 2);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto * layout = new QVBoxLayout(this);
    layout->addLayout(columns, 1);
    layout->addWidget(buttons);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
        if (!loading_)
            loadSelected();
    });

    for (QCheckBox * toggle : {setOwner_, setHitpoints_, setShields_, setEnergy_,
                               setResources_, setHangar_})
    {
        connect(toggle, &QCheckBox::toggled, this, [this] { updateEnabled(); });
    }

    reloadList();
}

void PresetEditor::reloadList(int selectRow)
{
    presets_ = document_.unitPresets();

    loading_ = true;
    list_->clear();
    for (const auto & preset : presets_)
    {
        QStringList parts;
        if (preset.setOwner)     parts << tr("소유자 %1").arg(preset.owner + 1);
        if (preset.setHitpoints) parts << tr("체력 %1%").arg(preset.hitpointPercent);
        if (preset.setShields)   parts << tr("실드 %1%").arg(preset.shieldPercent);
        if (preset.setEnergy)    parts << tr("에너지 %1%").arg(preset.energyPercent);
        if (preset.setResources) parts << tr("자원 %1").arg(preset.resourceAmount);
        if (preset.setHangar)    parts << tr("격납고 %1").arg(preset.hangarAmount);

        QString label = tr("#%1").arg(preset.index + 1);
        if (preset.used)
            label += tr("  [쓰는 중]");
        if (!parts.isEmpty())
            label += QStringLiteral("  ") + parts.join(QStringLiteral(", "));

        list_->addItem(label);
    }
    loading_ = false;

    if (!presets_.empty())
    {
        list_->setCurrentRow(std::clamp(selectRow, 0, static_cast<int>(presets_.size()) - 1));
        loadSelected();
    }
}

void PresetEditor::loadSelected()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(presets_.size()))
        return;

    const auto & preset = presets_[static_cast<std::size_t>(row)];

    loading_ = true;
    setOwner_->setChecked(preset.setOwner);
    const int ownerIndex = owner_->findData(preset.owner);
    owner_->setCurrentIndex(ownerIndex >= 0 ? ownerIndex : 0);

    setHitpoints_->setChecked(preset.setHitpoints);
    hitpoints_->setValue(preset.hitpointPercent);
    setShields_->setChecked(preset.setShields);
    shields_->setValue(preset.shieldPercent);
    setEnergy_->setChecked(preset.setEnergy);
    energy_->setValue(preset.energyPercent);
    setResources_->setChecked(preset.setResources);
    resources_->setValue(static_cast<int>(std::min<std::uint32_t>(preset.resourceAmount, 2147483647u)));
    setHangar_->setChecked(preset.setHangar);
    hangar_->setValue(preset.hangarAmount);

    cloaked_->setChecked(preset.cloaked);
    burrowed_->setChecked(preset.burrowed);
    inTransit_->setChecked(preset.inTransit);
    hallucinated_->setChecked(preset.hallucinated);
    invincible_->setChecked(preset.invincible);
    loading_ = false;

    updateEnabled();
    status_->setText(preset.used
        ? tr("트리거가 쓰고 있는 프리셋입니다.")
        : tr("아직 쓰이지 않는 자리입니다."));
}

void PresetEditor::updateEnabled()
{
    owner_->setEnabled(setOwner_->isChecked());
    hitpoints_->setEnabled(setHitpoints_->isChecked());
    shields_->setEnabled(setShields_->isChecked());
    energy_->setEnabled(setEnergy_->isChecked());
    resources_->setEnabled(setResources_->isChecked());
    hangar_->setEnabled(setHangar_->isChecked());
}

void PresetEditor::applyCurrent()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(presets_.size()))
        return;

    io::MapArchive::UnitPreset preset = presets_[static_cast<std::size_t>(row)];

    preset.setOwner = setOwner_->isChecked();
    preset.owner = static_cast<std::uint8_t>(owner_->currentData().toInt());
    preset.setHitpoints = setHitpoints_->isChecked();
    preset.hitpointPercent = static_cast<std::uint8_t>(hitpoints_->value());
    preset.setShields = setShields_->isChecked();
    preset.shieldPercent = static_cast<std::uint8_t>(shields_->value());
    preset.setEnergy = setEnergy_->isChecked();
    preset.energyPercent = static_cast<std::uint8_t>(energy_->value());
    preset.setResources = setResources_->isChecked();
    preset.resourceAmount = static_cast<std::uint32_t>(resources_->value());
    preset.setHangar = setHangar_->isChecked();
    preset.hangarAmount = static_cast<std::uint16_t>(hangar_->value());

    preset.cloaked = cloaked_->isChecked();
    preset.burrowed = burrowed_->isChecked();
    preset.inTransit = inTransit_->isChecked();
    preset.hallucinated = hallucinated_->isChecked();
    preset.invincible = invincible_->isChecked();

    if (!document_.setUnitPreset(preset.index, preset))
    {
        status_->setText(QString::fromStdString(document_.lastError()));
        return;
    }

    emit documentEdited();
    reloadList(row);
    status_->setText(tr("프리셋 %1번을 고쳤습니다.").arg(preset.index + 1));
}

} // namespace splash::ui
