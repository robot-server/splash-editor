#include "ui/eud_calculator.h"

#include "io/eud.h"
#include "io/map_archive.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace splash::ui {

namespace {

namespace eud = io::eud;

QString hex(std::uint32_t value)
{
    return QStringLiteral("0x%1")
        .arg(value, 8, 16, QLatin1Char('0'))
        .toUpper()
        .replace(QStringLiteral("0X"), QStringLiteral("0x"));
}

} // namespace

EudCalculator::EudCalculator(QWidget * parent)
    : QDialog(parent)
{
    setWindowTitle(tr("EUD 주소 계산기"));
    resize(680, 560);

    auto * layout = new QVBoxLayout(this);

    // --- 이름으로 찾기 ---
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("이름이나 주소로 찾기 (예: 미네랄, 0x57F0F0)"));
    search_->setClearButtonEnabled(true);
    layout->addWidget(search_);

    offsets_ = new QTreeWidget(this);
    offsets_->setColumnCount(4);
    offsets_->setHeaderLabels({tr("이름"), tr("주소"), tr("EPD"), tr("리마스터")});
    offsets_->setRootIsDecorated(false);
    offsets_->setAlternatingRowColors(true);
    offsets_->header()->setStretchLastSection(false);
    offsets_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(offsets_, 1);

    dbNote_ = new QLabel(this);
    dbNote_->setWordWrap(true);
    dbNote_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    layout->addWidget(dbNote_);

    // --- 값 ---
    auto * form = new QFormLayout();

    address_ = new QLineEdit(this);
    address_->setToolTip(tr("메모리 주소. 0x 를 붙여도 되고 안 붙여도 된다."));
    form->addRow(tr("주소"), address_);

    epd_ = new QLineEdit(this);
    epd_->setToolTip(
        tr("Deaths 표 첫 자리로부터 네 바이트 단위로 센 거리. 표보다 앞이면 음수다.\n"
           "Memory 조건·동작은 이 값을 플레이어 자리에 통째로 넣는다."));
    form->addRow(tr("EPD"), epd_);

    player_ = new QSpinBox(this);
    // 자리는 32비트로 감아 도므로 아주 큰 값도 받아야 한다. QSpinBox 는
    // 부호 있는 32비트까지만 다루니 주소·EPD 쪽을 주된 입력으로 삼는다.
    player_->setRange(0, 2147483647);
    player_->setToolTip(
        tr("Deaths 조건의 플레이어 자리. EUD 는 12 를 넘는 값을 일부러 써서 "
           "표 밖의 메모리를 가리킨다."));
    form->addRow(tr("플레이어"), player_);

    unit_ = new QSpinBox(this);
    unit_->setRange(0, static_cast<int>(kUnitTypes) - 1);
    unit_->setToolTip(tr("Deaths 조건의 유닛 자리 (0-227)."));
    form->addRow(tr("유닛"), unit_);

    layout->addLayout(form);

    note_ = new QLabel(this);
    note_->setWordWrap(true);
    layout->addWidget(note_);

    auto * copyAddress = new QPushButton(tr("주소 복사"), this);
    auto * copyEpd = new QPushButton(tr("EPD 복사"), this);
    auto * clearDb = new QPushButton(tr("표 비우기"), this);
    auto * loadDb = new QPushButton(tr("오프셋 표 불러오기…"), this);
    loadDb->setToolTip(
        tr("EUD Book(armoha/eud-book) 의 api.json 을 읽어 900개가 넘는 자리를\n"
           "함께 보여 줍니다. 그 파일은 라이선스가 밝혀져 있지 않아 저장소에\n"
           "넣지 않았습니다 — 직접 받아 가리켜 주세요."));

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addButton(loadDb, QDialogButtonBox::ActionRole);
    buttons->addButton(clearDb, QDialogButtonBox::ActionRole);
    buttons->addButton(copyAddress, QDialogButtonBox::ActionRole);
    buttons->addButton(copyEpd, QDialogButtonBox::ActionRole);
    clearDb->setEnabled(eud::hasOffsetDatabase());
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(copyAddress, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(address_->text());
    });
    connect(copyEpd, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(epd_->text());
    });
    connect(loadDb, &QPushButton::clicked, this, [this, clearDb] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("오프셋 표 (api.json)"), QString(), tr("JSON (*.json);;모든 파일 (*)"));
        if (path.isEmpty())
            return;

        std::string error;
        if (!eud::loadOffsetDatabase(path.toStdString(), &error))
        {
            QMessageBox::warning(this, tr("오프셋 표"),
                                 tr("읽지 못했습니다.\n\n%1")
                                     .arg(QString::fromStdString(error)));
            return;
        }
        // 다음에 켤 때 다시 고르지 않아도 되게 어디 두었는지 기억한다.
        QSettings().setValue(QStringLiteral("eudOffsetDatabase"), path);
        clearDb->setEnabled(true);
        reloadOffsets();
    });
    connect(clearDb, &QPushButton::clicked, this, [this, clearDb] {
        eud::clearOffsetDatabase();
        QSettings().remove(QStringLiteral("eudOffsetDatabase"));
        clearDb->setEnabled(false);
        reloadOffsets();
    });

    connect(search_, &QLineEdit::textChanged, this, [this](const QString &) {
        reloadOffsets();
    });
    connect(offsets_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem * item, QTreeWidgetItem *) {
        if (item == nullptr || updating_)
            return;
        applyAddress(item->data(0, Qt::UserRole).toUInt());
    });
    connect(offsets_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem * item, int) {
        if (item == nullptr)
            return;
        applyAddress(item->data(0, Qt::UserRole).toUInt());
        if (auto * box = findChild<QDialogButtonBox *>();
            box != nullptr && box->button(QDialogButtonBox::Ok) != nullptr)
        {
            accept();
        }
    });

    connect(player_, &QSpinBox::valueChanged, this, [this](int) { refreshFromSlot(); });
    connect(unit_, &QSpinBox::valueChanged, this, [this](int) { refreshFromSlot(); });
    connect(address_, &QLineEdit::textEdited, this, [this](const QString &) {
        refreshFromAddress();
    });
    connect(epd_, &QLineEdit::textEdited, this, [this](const QString &) {
        refreshFromEpd();
    });

    reloadOffsets();
    showAddress(eud::kDeathsBase);
}

void EudCalculator::reloadOffsets()
{
    const bool wasUpdating = updating_;
    updating_ = true;

    offsets_->clear();
    for (const auto * entry : eud::findOffsets(search_->text().trimmed().toStdString()))
    {
        auto * item = new QTreeWidgetItem(offsets_);
        QString name = QString::fromStdString(entry->name);
        if (entry->length > 1)
            name += QStringLiteral(" x%1").arg(entry->length);
        item->setText(0, name);
        item->setText(1, hex(entry->address));
        item->setText(2, QString::number(eud::signedEpdFor(entry->address)));
        item->setText(3, QString::fromStdString(eud::scrSupportName(entry->scr)));
        item->setData(0, Qt::UserRole, entry->address);
        if (!entry->description.empty())
            item->setToolTip(0, QString::fromStdString(entry->description));

        // 리마스터에서 안 되는 자리는 눈에 띄게 둔다.
        if (entry->scr == eud::ScrSupport::Unsupported)
            item->setForeground(3, QBrush(QColor(0xC0, 0x39, 0x2B)));
        else if (entry->scr == eud::ScrSupport::ReadOnly)
            item->setForeground(3, QBrush(QColor(0xB7, 0x79, 0x1F)));
    }
    offsets_->resizeColumnToContents(1);
    offsets_->resizeColumnToContents(2);
    offsets_->resizeColumnToContents(3);

    if (eud::hasOffsetDatabase())
    {
        dbNote_->setText(tr("자리 %1개 — 붙박이 표 + %2")
                             .arg(eud::offsets().size())
                             .arg(QString::fromStdString(eud::offsetDatabasePath())));
    }
    else
    {
        dbNote_->setText(tr("자리 %1개 (붙박이 표만). '오프셋 표 불러오기' 로 "
                            "EUD Book 의 api.json 을 얹으면 900개가 넘습니다.")
                             .arg(eud::offsets().size()));
    }

    updating_ = wasUpdating;
}

unsigned EudCalculator::currentPlayer() const
{
    return static_cast<unsigned>(player_->value());
}

unsigned EudCalculator::currentUnit() const
{
    return static_cast<unsigned>(unit_->value());
}

std::uint32_t EudCalculator::currentAddress() const
{
    const auto address = eud::parseAddress(address_->text().toStdString());
    return address ? *address : eud::kDeathsBase;
}

void EudCalculator::enablePicking()
{
    if (auto * buttons = findChild<QDialogButtonBox *>())
    {
        auto * pick = buttons->addButton(tr("이 자리 쓰기"), QDialogButtonBox::AcceptRole);
        pick->setDefault(true);
        connect(pick, &QPushButton::clicked, this, &QDialog::accept);
    }
}

bool EudCalculator::pickSlot(QWidget * parent, unsigned * player, unsigned * unit,
                             unsigned startPlayer, unsigned startUnit)
{
    EudCalculator dialog(parent);
    dialog.setWindowTitle(tr("EUD 자리 고르기"));
    dialog.player_->setValue(static_cast<int>(std::min(startPlayer, 2147483647u)));
    dialog.unit_->setValue(static_cast<int>(std::min<unsigned>(startUnit, kUnitTypes - 1)));
    dialog.refreshFromSlot();
    dialog.enablePicking();

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (player != nullptr)
        *player = dialog.currentPlayer();
    if (unit != nullptr)
        *unit = dialog.currentUnit();
    return true;
}

bool EudCalculator::pickAddress(QWidget * parent, std::uint32_t * address,
                                std::uint32_t startAddress)
{
    EudCalculator dialog(parent);
    dialog.setWindowTitle(tr("EUD 주소 고르기"));
    dialog.showAddress(startAddress);
    dialog.enablePicking();

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (address != nullptr)
        *address = dialog.currentAddress();
    return true;
}

void EudCalculator::applyAddress(std::uint32_t address)
{
    showAddress(address);
}

void EudCalculator::showAddress(std::uint32_t address)
{
    const bool wasUpdating = updating_;
    updating_ = true;

    address_->setText(hex(address));
    epd_->setText(QString::number(eud::signedEpdFor(address)));

    std::uint32_t player = 0;
    std::uint32_t unit = 0;
    if (eud::slotFor(address, &player, &unit))
    {
        player_->setValue(static_cast<int>(std::min(player, 2147483647u)));
        unit_->setValue(static_cast<int>(std::min<std::uint32_t>(unit, kUnitTypes - 1)));
    }

    note_->setText(QString::fromStdString(eud::describeAddress(address)));
    updating_ = wasUpdating;
}

void EudCalculator::refreshFromSlot()
{
    if (updating_)
        return;
    showAddress(eud::addressFor(static_cast<std::uint32_t>(player_->value()),
                                static_cast<std::uint32_t>(unit_->value())));
}

void EudCalculator::refreshFromAddress()
{
    if (updating_)
        return;

    const auto address = eud::parseAddress(address_->text().toStdString());
    if (!address)
    {
        note_->setText(tr("주소로 읽을 수 없습니다 (예: 0x0058A364)."));
        return;
    }
    // 주소 칸은 사람이 치는 중이므로 다시 쓰지 않는다.
    const bool wasUpdating = updating_;
    updating_ = true;
    epd_->setText(QString::number(eud::signedEpdFor(*address)));

    // 네 바이트 경계가 아니어도 막지 않는다 — 담긴 칸을 마스크와 함께
    // 읽으면 되고, describeAddress 가 그 길을 알려 준다. Deaths 자리는
    // 담긴 칸 기준으로 보인다.
    const std::uint32_t aligned = eud::containingDword(*address);
    std::uint32_t player = 0;
    std::uint32_t unit = 0;
    eud::slotFor(aligned, &player, &unit);
    player_->setValue(static_cast<int>(std::min(player, 2147483647u)));
    unit_->setValue(static_cast<int>(std::min<std::uint32_t>(unit, kUnitTypes - 1)));
    note_->setText(QString::fromStdString(eud::describeAddress(*address)));
    updating_ = wasUpdating;
}

void EudCalculator::refreshFromEpd()
{
    if (updating_)
        return;

    bool ok = false;
    const long long value = epd_->text().trimmed().toLongLong(&ok);
    if (!ok)
    {
        note_->setText(tr("EPD 를 수로 읽을 수 없습니다."));
        return;
    }

    const auto epd = static_cast<std::uint32_t>(value);
    const std::uint32_t address = eud::addressForEpd(epd);

    const bool wasUpdating = updating_;
    updating_ = true;
    address_->setText(hex(address));
    std::uint32_t player = 0;
    std::uint32_t unit = 0;
    eud::slotFor(address, &player, &unit);
    player_->setValue(static_cast<int>(std::min(player, 2147483647u)));
    unit_->setValue(static_cast<int>(std::min<std::uint32_t>(unit, kUnitTypes - 1)));
    note_->setText(QString::fromStdString(eud::describeAddress(address)));
    updating_ = wasUpdating;
}

} // namespace splash::ui
