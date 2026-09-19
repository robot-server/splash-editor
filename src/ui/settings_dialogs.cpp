#include "ui/settings_dialogs.h"

#include "chk/map_document.h"
#include "io/game_graphics.h"

#include <QCheckBox>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace splash::ui {
namespace {

constexpr int kPlayers = 12;

/// 게임 안에서 체력은 표시 값의 256배로 들어 있다.
constexpr std::uint32_t kHitpointScale = 256;

/// 표에 체크 상자 한 칸을 만든다.
QTableWidgetItem * checkItem(bool checked)
{
    auto * item = new QTableWidgetItem;
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return item;
}

bool isChecked(const QTableWidget * table, int row, int column)
{
    const QTableWidgetItem * item = table->item(row, column);
    return item != nullptr && item->checkState() == Qt::Checked;
}

/// 플레이어 이름이 붙은 표를 만든다.
QTableWidget * makePlayerTable(QWidget * parent, const QStringList & headers)
{
    auto * table = new QTableWidget(kPlayers, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);

    QStringList rows;
    for (int player = 1; player <= kPlayers; ++player)
        rows << QObject::tr("플레이어 %1").arg(player);
    table->setVerticalHeaderLabels(rows);

    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setDefaultSectionSize(22);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::AllEditTriggers);
    return table;
}

/// 단계 값을 담는 칸 (0~3).
QTableWidgetItem * levelItem(int value)
{
    auto * item = new QTableWidgetItem(QString::number(value));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsSelectable);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

int levelOf(const QTableWidget * table, int row, int column, int maximum)
{
    const QTableWidgetItem * item = table->item(row, column);
    if (item == nullptr)
        return 0;
    bool ok = false;
    const int value = item->text().toInt(&ok);
    if (!ok)
        return 0;
    return std::clamp(value, 0, maximum);
}

/// UnitImage 를 목록에 넣을 아이콘으로.
QIcon toIcon(const io::UnitImage & image, int box = 32)
{
    if (image.width <= 0 || image.height <= 0 || image.rgba.empty())
        return QIcon();

    QImage picture(image.rgba.data(), image.width, image.height,
                   image.width * 4, QImage::Format_RGBA8888);

    // 원본을 그대로 두고 복사본을 쓴다 — rgba 는 곧 사라진다.
    QPixmap pixmap = QPixmap::fromImage(picture.copy());
    if (pixmap.width() > box || pixmap.height() > box)
        pixmap = pixmap.scaled(box, box, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return QIcon(pixmap);
}

} // namespace

// ---------------------------------------------------------------- 유닛

UnitSettingsDialog::UnitSettingsDialog(chk::MapDocument & document, io::GameGraphics & graphics,
                                        QWidget * parent)
    : QDialog(parent), document_(document), graphics_(graphics)
{
    setWindowTitle(tr("유닛 설정"));
    resize(860, 620);

    auto * outer = new QVBoxLayout(this);
    auto * columns = new QHBoxLayout;
    outer->addLayout(columns, 1);

    // --- 왼쪽: 유닛 목록 ---
    auto * left = new QVBoxLayout;
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("유닛 이름으로 거르기"));
    list_ = new QListWidget(this);
    list_->setIconSize(QSize(32, 32));
    left->addWidget(filter_);
    left->addWidget(list_, 1);
    columns->addLayout(left, 1);

    // --- 오른쪽: 능력치 ---
    auto * right = new QVBoxLayout;

    useDefault_ = new QCheckBox(tr("게임 기본값 사용"), this);
    right->addWidget(useDefault_);

    auto * statsBox = new QGroupBox(tr("능력치"), this);
    auto * form = new QFormLayout(statsBox);

    hitpoints_ = new QSpinBox(statsBox);
    hitpoints_->setRange(0, 16777215);
    form->addRow(tr("체력"), hitpoints_);

    shields_ = new QSpinBox(statsBox);
    shields_->setRange(0, 65535);
    form->addRow(tr("실드"), shields_);

    armor_ = new QSpinBox(statsBox);
    armor_->setRange(0, 255);
    form->addRow(tr("장갑"), armor_);

    buildTime_ = new QSpinBox(statsBox);
    buildTime_->setRange(0, 65535);
    buildTime_->setSuffix(tr(" (1/15초)"));
    form->addRow(tr("생산 시간"), buildTime_);

    minerals_ = new QSpinBox(statsBox);
    minerals_->setRange(0, 65535);
    form->addRow(tr("미네랄"), minerals_);

    gas_ = new QSpinBox(statsBox);
    gas_->setRange(0, 65535);
    form->addRow(tr("가스"), gas_);

    right->addWidget(statsBox);

    auto * availBox = new QGroupBox(tr("생산 허용"), this);
    auto * availLayout = new QVBoxLayout(availBox);
    defaultBuildable_ = new QCheckBox(tr("기본값: 생산할 수 있음"), availBox);
    availLayout->addWidget(defaultBuildable_);
    players_ = makePlayerTable(availBox, {tr("기본값 사용"), tr("생산 가능")});
    availLayout->addWidget(players_, 1);
    right->addWidget(availBox, 1);

    columns->addLayout(right, 2);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { commitPending(); accept(); });

    // --- 신호 ---
    connect(filter_, &QLineEdit::textChanged, this, [this] { reloadList(); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
        commitPending();
        loadSelected();
    });
    connect(useDefault_, &QCheckBox::toggled, this, [this] {
        if (!loading_) { updateEnabled(); pendingType_ = list_->currentItem() != nullptr
            ? list_->currentItem()->data(Qt::UserRole).toInt() : -1; }
    });

    const auto touched = [this] {
        if (loading_ || list_->currentItem() == nullptr)
            return;
        pendingType_ = list_->currentItem()->data(Qt::UserRole).toInt();
    };
    for (QSpinBox * box : {hitpoints_, shields_, armor_, buildTime_, minerals_, gas_})
        connect(box, &QSpinBox::valueChanged, this, touched);
    connect(defaultBuildable_, &QCheckBox::toggled, this, touched);
    connect(players_, &QTableWidget::itemChanged, this, touched);

    reloadList();
}

void UnitSettingsDialog::reloadList()
{
    const QString needle = filter_->text().trimmed();

    loading_ = true;
    list_->clear();
    for (std::uint16_t type = 0; type < 228; ++type)
    {
        const QString name = QString::fromStdString(io::unitTypeName(type));
        if (!needle.isEmpty() && !name.contains(needle, Qt::CaseInsensitive))
            continue;

        auto * item = new QListWidgetItem(QStringLiteral("%1  %2").arg(type, 3).arg(name));
        item->setData(Qt::UserRole, type);

        // 실제 유닛 그림을 보여 준다 — 아이콘보다 알아보기 쉽다.
        if (graphics_.hasUnitGraphics())
            item->setIcon(toIcon(graphics_.renderUnit(type, 0, document_.info().tilesetId, 1500)));

        list_->addItem(item);
    }
    loading_ = false;

    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

void UnitSettingsDialog::loadSelected()
{
    const QListWidgetItem * item = list_->currentItem();
    if (item == nullptr)
        return;

    const auto type = static_cast<std::uint16_t>(item->data(Qt::UserRole).toInt());
    const auto stats = document_.unitStats(type);
    if (!stats)
        return;

    loading_ = true;
    useDefault_->setChecked(stats->useDefault);
    hitpoints_->setValue(static_cast<int>(stats->hitpoints / kHitpointScale));
    shields_->setValue(stats->shields);
    armor_->setValue(stats->armor);
    buildTime_->setValue(stats->buildTime);
    minerals_->setValue(stats->mineralCost);
    gas_->setValue(stats->gasCost);
    defaultBuildable_->setChecked(stats->defaultBuildable);

    for (int player = 0; player < kPlayers; ++player)
    {
        players_->setItem(player, 0, checkItem(stats->playerUsesDefault[player]));
        players_->setItem(player, 1, checkItem(stats->buildable[player]));
    }
    loading_ = false;

    pendingType_ = -1;
    updateEnabled();
}

void UnitSettingsDialog::updateEnabled()
{
    const bool custom = !useDefault_->isChecked();
    for (QSpinBox * box : {hitpoints_, shields_, armor_, buildTime_, minerals_, gas_})
        box->setEnabled(custom);
}

void UnitSettingsDialog::commitPending()
{
    if (pendingType_ < 0)
        return;

    const auto type = static_cast<std::uint16_t>(pendingType_);
    pendingType_ = -1;

    io::UnitStats stats;
    stats.useDefault = useDefault_->isChecked();
    stats.hitpoints = static_cast<std::uint32_t>(hitpoints_->value()) * kHitpointScale;
    stats.shields = static_cast<std::uint16_t>(shields_->value());
    stats.armor = static_cast<std::uint8_t>(armor_->value());
    stats.buildTime = static_cast<std::uint16_t>(buildTime_->value());
    stats.mineralCost = static_cast<std::uint16_t>(minerals_->value());
    stats.gasCost = static_cast<std::uint16_t>(gas_->value());
    stats.defaultBuildable = defaultBuildable_->isChecked();

    for (int player = 0; player < kPlayers; ++player)
    {
        stats.playerUsesDefault[player] = isChecked(players_, player, 0);
        stats.buildable[player] = isChecked(players_, player, 1);
    }

    if (document_.setUnitStats(type, stats))
        emit documentEdited();
}

// ---------------------------------------------------------- 업그레이드

UpgradeSettingsDialog::UpgradeSettingsDialog(chk::MapDocument & document, io::GameGraphics & graphics,
                                              QWidget * parent)
    : QDialog(parent), document_(document), graphics_(graphics)
{
    setWindowTitle(tr("업그레이드 설정"));
    resize(860, 620);

    auto * outer = new QVBoxLayout(this);
    auto * columns = new QHBoxLayout;
    outer->addLayout(columns, 1);

    list_ = new QListWidget(this);
    list_->setIconSize(QSize(32, 32));
    for (std::size_t type = 0; type < io::upgradeTypeCount(); ++type)
    {
        const auto value = static_cast<std::uint16_t>(type);
        auto * item = new QListWidgetItem(QStringLiteral("%1  %2")
            .arg(type, 2).arg(QString::fromStdString(io::upgradeTypeName(value))));
        item->setData(Qt::UserRole, static_cast<int>(type));

        if (graphics_.isLoaded())
            item->setIcon(toIcon(graphics_.renderIcon(graphics_.upgradeIcon(value),
                                                     document_.info().tilesetId)));

        list_->addItem(item);
    }
    columns->addWidget(list_, 1);

    auto * right = new QVBoxLayout;

    useDefault_ = new QCheckBox(tr("게임 기본 비용 사용"), this);
    right->addWidget(useDefault_);

    auto * costBox = new QGroupBox(tr("비용"), this);
    auto * form = new QFormLayout(costBox);

    const auto makeSpin = [costBox](int max) {
        auto * box = new QSpinBox(costBox);
        box->setRange(0, max);
        return box;
    };

    baseMinerals_ = makeSpin(65535);
    form->addRow(tr("미네랄 (1단계)"), baseMinerals_);
    mineralFactor_ = makeSpin(65535);
    form->addRow(tr("미네랄 증가분"), mineralFactor_);
    baseGas_ = makeSpin(65535);
    form->addRow(tr("가스 (1단계)"), baseGas_);
    gasFactor_ = makeSpin(65535);
    form->addRow(tr("가스 증가분"), gasFactor_);
    baseTime_ = makeSpin(65535);
    baseTime_->setSuffix(tr(" (1/15초)"));
    form->addRow(tr("연구 시간 (1단계)"), baseTime_);
    timeFactor_ = makeSpin(65535);
    timeFactor_->setSuffix(tr(" (1/15초)"));
    form->addRow(tr("연구 시간 증가분"), timeFactor_);

    right->addWidget(costBox);

    auto * levelBox = new QGroupBox(tr("단계"), this);
    auto * levelLayout = new QVBoxLayout(levelBox);
    auto * defaults = new QFormLayout;
    defaultStart_ = new QSpinBox(levelBox);
    defaultStart_->setRange(0, 3);
    defaults->addRow(tr("기본 시작 단계"), defaultStart_);
    defaultMax_ = new QSpinBox(levelBox);
    defaultMax_->setRange(0, 3);
    defaults->addRow(tr("기본 최대 단계"), defaultMax_);
    levelLayout->addLayout(defaults);

    players_ = makePlayerTable(levelBox, {tr("기본값 사용"), tr("시작"), tr("최대")});
    levelLayout->addWidget(players_, 1);
    right->addWidget(levelBox, 1);

    columns->addLayout(right, 2);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { commitPending(); accept(); });

    connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
        commitPending();
        loadSelected();
    });

    const auto touched = [this] {
        if (loading_ || list_->currentItem() == nullptr)
            return;
        pendingType_ = list_->currentItem()->data(Qt::UserRole).toInt();
    };
    connect(useDefault_, &QCheckBox::toggled, this, [this, touched] {
        if (loading_) return;
        updateEnabled();
        touched();
    });
    for (QSpinBox * box : {baseMinerals_, mineralFactor_, baseGas_, gasFactor_,
                           baseTime_, timeFactor_, defaultStart_, defaultMax_})
        connect(box, &QSpinBox::valueChanged, this, touched);
    connect(players_, &QTableWidget::itemChanged, this, touched);

    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

void UpgradeSettingsDialog::loadSelected()
{
    const QListWidgetItem * item = list_->currentItem();
    if (item == nullptr)
        return;

    const auto type = static_cast<std::uint16_t>(item->data(Qt::UserRole).toInt());
    const auto settings = document_.upgradeSettings(type);
    if (!settings)
        return;

    loading_ = true;
    useDefault_->setChecked(settings->useDefaultCosts);
    baseMinerals_->setValue(settings->baseMineralCost);
    mineralFactor_->setValue(settings->mineralCostFactor);
    baseGas_->setValue(settings->baseGasCost);
    gasFactor_->setValue(settings->gasCostFactor);
    baseTime_->setValue(settings->baseResearchTime);
    timeFactor_->setValue(settings->researchTimeFactor);
    defaultStart_->setValue(settings->defaultStartLevel);
    defaultMax_->setValue(settings->defaultMaxLevel);

    for (int player = 0; player < kPlayers; ++player)
    {
        players_->setItem(player, 0, checkItem(settings->playerUsesDefault[player]));
        players_->setItem(player, 1, levelItem(settings->startLevel[player]));
        players_->setItem(player, 2, levelItem(settings->maxLevel[player]));
    }
    loading_ = false;

    pendingType_ = -1;
    updateEnabled();
}

void UpgradeSettingsDialog::updateEnabled()
{
    const bool custom = !useDefault_->isChecked();
    for (QSpinBox * box : {baseMinerals_, mineralFactor_, baseGas_, gasFactor_,
                           baseTime_, timeFactor_})
        box->setEnabled(custom);
}

void UpgradeSettingsDialog::commitPending()
{
    if (pendingType_ < 0)
        return;

    const auto type = static_cast<std::uint16_t>(pendingType_);
    pendingType_ = -1;

    io::UpgradeSettings settings;
    settings.useDefaultCosts = useDefault_->isChecked();
    settings.baseMineralCost = static_cast<std::uint16_t>(baseMinerals_->value());
    settings.mineralCostFactor = static_cast<std::uint16_t>(mineralFactor_->value());
    settings.baseGasCost = static_cast<std::uint16_t>(baseGas_->value());
    settings.gasCostFactor = static_cast<std::uint16_t>(gasFactor_->value());
    settings.baseResearchTime = static_cast<std::uint16_t>(baseTime_->value());
    settings.researchTimeFactor = static_cast<std::uint16_t>(timeFactor_->value());
    settings.defaultStartLevel = static_cast<std::uint8_t>(defaultStart_->value());
    settings.defaultMaxLevel = static_cast<std::uint8_t>(defaultMax_->value());

    for (int player = 0; player < kPlayers; ++player)
    {
        settings.playerUsesDefault[player] = isChecked(players_, player, 0);
        settings.startLevel[player] = static_cast<std::uint8_t>(levelOf(players_, player, 1, 3));
        settings.maxLevel[player] = static_cast<std::uint8_t>(levelOf(players_, player, 2, 3));
    }

    if (document_.setUpgradeSettings(type, settings))
        emit documentEdited();
}

// ---------------------------------------------------------------- 기술

TechSettingsDialog::TechSettingsDialog(chk::MapDocument & document, io::GameGraphics & graphics,
                                        QWidget * parent)
    : QDialog(parent), document_(document), graphics_(graphics)
{
    setWindowTitle(tr("기술 설정"));
    resize(860, 560);

    auto * outer = new QVBoxLayout(this);
    auto * columns = new QHBoxLayout;
    outer->addLayout(columns, 1);

    list_ = new QListWidget(this);
    list_->setIconSize(QSize(32, 32));
    for (std::size_t type = 0; type < io::techTypeCount(); ++type)
    {
        const auto value = static_cast<std::uint16_t>(type);
        auto * item = new QListWidgetItem(QStringLiteral("%1  %2")
            .arg(type, 2).arg(QString::fromStdString(io::techTypeName(value))));
        item->setData(Qt::UserRole, static_cast<int>(type));

        if (graphics_.isLoaded())
            item->setIcon(toIcon(graphics_.renderIcon(graphics_.techIcon(value),
                                                     document_.info().tilesetId)));

        list_->addItem(item);
    }
    columns->addWidget(list_, 1);

    auto * right = new QVBoxLayout;

    useDefault_ = new QCheckBox(tr("게임 기본 비용 사용"), this);
    right->addWidget(useDefault_);

    auto * costBox = new QGroupBox(tr("비용"), this);
    auto * form = new QFormLayout(costBox);

    minerals_ = new QSpinBox(costBox);
    minerals_->setRange(0, 65535);
    form->addRow(tr("미네랄"), minerals_);
    gas_ = new QSpinBox(costBox);
    gas_->setRange(0, 65535);
    form->addRow(tr("가스"), gas_);
    researchTime_ = new QSpinBox(costBox);
    researchTime_->setRange(0, 65535);
    researchTime_->setSuffix(tr(" (1/15초)"));
    form->addRow(tr("연구 시간"), researchTime_);
    energy_ = new QSpinBox(costBox);
    energy_->setRange(0, 65535);
    form->addRow(tr("에너지"), energy_);

    right->addWidget(costBox);

    auto * stateBox = new QGroupBox(tr("사용 여부"), this);
    auto * stateLayout = new QVBoxLayout(stateBox);
    defaultAvailable_ = new QCheckBox(tr("기본값: 연구할 수 있음"), stateBox);
    defaultResearched_ = new QCheckBox(tr("기본값: 이미 연구됨"), stateBox);
    stateLayout->addWidget(defaultAvailable_);
    stateLayout->addWidget(defaultResearched_);

    players_ = makePlayerTable(stateBox, {tr("기본값 사용"), tr("연구 가능"), tr("연구됨")});
    stateLayout->addWidget(players_, 1);
    right->addWidget(stateBox, 1);

    columns->addLayout(right, 2);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { commitPending(); accept(); });

    connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
        commitPending();
        loadSelected();
    });

    const auto touched = [this] {
        if (loading_ || list_->currentItem() == nullptr)
            return;
        pendingType_ = list_->currentItem()->data(Qt::UserRole).toInt();
    };
    connect(useDefault_, &QCheckBox::toggled, this, [this, touched] {
        if (loading_) return;
        updateEnabled();
        touched();
    });
    for (QSpinBox * box : {minerals_, gas_, researchTime_, energy_})
        connect(box, &QSpinBox::valueChanged, this, touched);
    connect(defaultAvailable_, &QCheckBox::toggled, this, touched);
    connect(defaultResearched_, &QCheckBox::toggled, this, touched);
    connect(players_, &QTableWidget::itemChanged, this, touched);

    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

void TechSettingsDialog::loadSelected()
{
    const QListWidgetItem * item = list_->currentItem();
    if (item == nullptr)
        return;

    const auto type = static_cast<std::uint16_t>(item->data(Qt::UserRole).toInt());
    const auto settings = document_.techSettings(type);
    if (!settings)
        return;

    loading_ = true;
    useDefault_->setChecked(settings->useDefaultCosts);
    minerals_->setValue(settings->mineralCost);
    gas_->setValue(settings->gasCost);
    researchTime_->setValue(settings->researchTime);
    energy_->setValue(settings->energyCost);
    defaultAvailable_->setChecked(settings->defaultAvailable);
    defaultResearched_->setChecked(settings->defaultResearched);

    for (int player = 0; player < kPlayers; ++player)
    {
        players_->setItem(player, 0, checkItem(settings->playerUsesDefault[player]));
        players_->setItem(player, 1, checkItem(settings->available[player]));
        players_->setItem(player, 2, checkItem(settings->researched[player]));
    }
    loading_ = false;

    pendingType_ = -1;
    updateEnabled();
}

void TechSettingsDialog::updateEnabled()
{
    const bool custom = !useDefault_->isChecked();
    for (QSpinBox * box : {minerals_, gas_, researchTime_, energy_})
        box->setEnabled(custom);
}

void TechSettingsDialog::commitPending()
{
    if (pendingType_ < 0)
        return;

    const auto type = static_cast<std::uint16_t>(pendingType_);
    pendingType_ = -1;

    io::TechSettings settings;
    settings.useDefaultCosts = useDefault_->isChecked();
    settings.mineralCost = static_cast<std::uint16_t>(minerals_->value());
    settings.gasCost = static_cast<std::uint16_t>(gas_->value());
    settings.researchTime = static_cast<std::uint16_t>(researchTime_->value());
    settings.energyCost = static_cast<std::uint16_t>(energy_->value());
    settings.defaultAvailable = defaultAvailable_->isChecked();
    settings.defaultResearched = defaultResearched_->isChecked();

    for (int player = 0; player < kPlayers; ++player)
    {
        settings.playerUsesDefault[player] = isChecked(players_, player, 0);
        settings.available[player] = isChecked(players_, player, 1);
        settings.researched[player] = isChecked(players_, player, 2);
    }

    if (document_.setTechSettings(type, settings))
        emit documentEdited();
}

} // namespace splash::ui
