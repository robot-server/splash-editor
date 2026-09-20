#include "ui/sound_editor.h"

#include "chk/map_document.h"
#include "ui/sound_player.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <filesystem>
#include <fstream>

namespace splash::ui {
namespace {

QString prettySize(std::size_t bytes)
{
    if (bytes == 0)
        return QObject::tr("—");
    if (bytes < 1024)
        return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QObject::tr("%1 KB").arg(bytes / 1024);
    return QObject::tr("%1.%2 MB").arg(bytes / (1024 * 1024))
                                  .arg((bytes % (1024 * 1024)) * 10 / (1024 * 1024));
}

} // namespace

SoundEditor::SoundEditor(chk::MapDocument & document, SoundPlayer * player, QWidget * parent)
    : QDialog(parent), document_(document), player_(player)
{
    setWindowTitle(tr("소리 설정"));
    resize(820, 520);

    list_ = new QTableWidget(0, 4, this);
    list_->setHorizontalHeaderLabels({tr("등록"), tr("경로"), tr("맵 안"), tr("쓰임")});
    list_->horizontalHeader()->setStretchLastSection(false);
    list_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    list_->verticalHeader()->setVisible(false);
    list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto * addButton = new QPushButton(tr("WAV 넣기…"), this);
    auto * removeButton = new QPushButton(tr("빼기"), this);
    auto * extractButton = new QPushButton(tr("꺼내기…"), this);
    auto * playButton = new QPushButton(tr("들어 보기"), this);

    removeIfUsed_ = new QCheckBox(tr("쓰는 중이어도 빼기"), this);
    removeIfUsed_->setToolTip(
        tr("트리거가 가리키는 소리를 빼면 그 트리거는 소리 없이 남습니다."));

    auto * buttonRow = new QHBoxLayout();
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton);
    buttonRow->addWidget(extractButton);
    buttonRow->addWidget(playButton);
    buttonRow->addWidget(removeIfUsed_);
    buttonRow->addStretch();

    status_ = new QLabel(this);
    status_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    status_->setWordWrap(true);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto * layout = new QVBoxLayout(this);
    layout->addWidget(list_, 1);
    layout->addLayout(buttonRow);
    layout->addWidget(status_);
    layout->addWidget(buttons);

    connect(addButton, &QPushButton::clicked, this, [this] { addSounds(); });
    connect(removeButton, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(extractButton, &QPushButton::clicked, this, [this] { extractSelected(); });
    connect(playButton, &QPushButton::clicked, this, [this] { playSelected(); });
    connect(list_, &QTableWidget::itemDoubleClicked, this, [this] { playSelected(); });

    reloadList();
}

int SoundEditor::currentRow() const
{
    return list_ == nullptr ? -1 : list_->currentRow();
}

void SoundEditor::reloadList(int selectRow)
{
    sounds_ = document_.sounds();

    list_->setRowCount(static_cast<int>(sounds_.size()));
    for (std::size_t row = 0; row < sounds_.size(); ++row)
    {
        const auto & sound = sounds_[row];
        const int r = static_cast<int>(row);

        auto * registered = new QTableWidgetItem(
            sound.registered ? QString::number(sound.index) : tr("미등록"));
        if (!sound.registered)
            registered->setForeground(QColor(200, 160, 90));
        list_->setItem(r, 0, registered);
        list_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(sound.path)));

        auto * inArchive = new QTableWidgetItem(
            sound.inArchive ? prettySize(sound.bytes) : tr("없음"));
        if (!sound.inArchive)
            inArchive->setForeground(QColor(200, 160, 90));
        list_->setItem(r, 2, inArchive);

        list_->setItem(r, 3, new QTableWidgetItem(
            sound.usedByTrigger ? tr("트리거") : tr("—")));
    }

    setWindowTitle(tr("소리 설정 — %1개").arg(sounds_.size()));

    if (!sounds_.empty())
        list_->setCurrentCell(std::clamp(selectRow, 0, static_cast<int>(sounds_.size()) - 1), 1);

    status_->setText(sounds_.empty()
        ? tr("이 맵에는 소리가 없습니다. WAV 를 넣으면 트리거의 Play WAV 에서 고를 수 있습니다.")
        : tr("맵 안에 없는 소리는 게임 기본 소리를 가리키는 것입니다. "
             "'미등록'은 WAV 목록에 올라 있지 않지만 트리거가 쓰거나 맵 안에 든 파일입니다."));
}

void SoundEditor::addSounds()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("맵에 넣을 WAV 고르기"), QString(), tr("소리 (*.wav);;모든 파일 (*)"));

    if (files.isEmpty())
        return;

    int added = 0;
    QStringList failed;
    for (const QString & file : files)
    {
        if (document_.addSound(file.toStdString()))
            ++added;
        else
            failed << QFileInfo(file).fileName();
    }

    if (added > 0)
        emit documentEdited();

    reloadList(list_->rowCount());

    if (!failed.isEmpty())
    {
        QMessageBox::warning(this, tr("넣지 못한 파일"),
            tr("%1개를 넣지 못했습니다:\n%2\n\n%3")
                .arg(failed.size())
                .arg(failed.join(QStringLiteral(", ")))
                .arg(QString::fromStdString(document_.lastError())));
    }
    else
    {
        status_->setText(tr("%1개를 넣었습니다").arg(added));
    }
}

void SoundEditor::removeSelected()
{
    const int row = currentRow();
    if (row < 0 || row >= static_cast<int>(sounds_.size()))
        return;

    const auto & sound = sounds_[static_cast<std::size_t>(row)];
    if (!sound.registered)
    {
        QMessageBox::information(this, tr("뺄 수 없습니다"),
            tr("WAV 목록에 올라 있지 않은 소리입니다. 트리거에서 그 소리를 "
               "가리키지 않게 고치면 사라집니다."));
        return;
    }

    if (!document_.removeSound(sound.index, removeIfUsed_->isChecked()))
    {
        QMessageBox::warning(this, tr("빼지 못했습니다"),
                             QString::fromStdString(document_.lastError()));
        return;
    }

    emit documentEdited();
    reloadList(std::max(0, row - 1));
    status_->setText(tr("소리를 뺐습니다"));
}

void SoundEditor::extractSelected()
{
    const int row = currentRow();
    if (row < 0 || row >= static_cast<int>(sounds_.size()))
        return;

    const auto & sound = sounds_[static_cast<std::size_t>(row)];

    // 맵 안 경로의 파일 이름을 기본값으로 준다.
    const QString suggested = QFileInfo(QString::fromStdString(sound.path)).fileName();
    const QString target = QFileDialog::getSaveFileName(
        this, tr("소리 꺼내기"), suggested, tr("소리 (*.wav)"));

    if (target.isEmpty())
        return;

    if (!document_.extractSoundByStringId(sound.stringId, target.toStdString()))
    {
        QMessageBox::warning(this, tr("꺼내지 못했습니다"),
                             QString::fromStdString(document_.lastError()));
        return;
    }

    status_->setText(tr("%1 로 꺼냈습니다").arg(target));
}

void SoundEditor::playSelected()
{
    const int row = currentRow();
    if (row < 0 || row >= static_cast<int>(sounds_.size()) || player_ == nullptr)
        return;

    const auto & sound = sounds_[static_cast<std::size_t>(row)];
    if (!sound.inArchive)
    {
        status_->setText(tr("맵 안에 없는 소리는 들어 볼 수 없습니다"));
        return;
    }

    // 임시 파일로 꺼내 읽는다 — 재생기는 바이트를 받는다.
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() / "splash-sound-preview.wav";

    if (!document_.extractSoundByStringId(sound.stringId, temporary.string()))
    {
        status_->setText(QString::fromStdString(document_.lastError()));
        return;
    }

    std::ifstream in(temporary, std::ios::binary);
    const std::vector<std::uint8_t> wav((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    std::error_code ec;
    std::filesystem::remove(temporary, ec);

    if (wav.empty())
    {
        status_->setText(tr("소리를 읽지 못했습니다"));
        return;
    }

    player_->play(wav);
    status_->setText(tr("재생: %1").arg(QString::fromStdString(sound.path)));
}

} // namespace splash::ui
