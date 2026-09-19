#include "ui/sound_player.h"

#include <QAudioOutput>
#include <QMediaPlayer>

namespace splash::ui {

SoundPlayer::SoundPlayer(QObject * parent) : QObject(parent)
{
    player_ = std::make_unique<QMediaPlayer>(this);
    output_ = std::make_unique<QAudioOutput>(this);
    player_->setAudioOutput(output_.get());
}

SoundPlayer::~SoundPlayer() = default;

void SoundPlayer::play(const std::vector<std::uint8_t> & wav)
{
    if (!enabled_ || wav.empty() || player_ == nullptr)
        return;

    // 앞 소리가 아직 나고 있으면 끊고 새로 낸다 — 유닛을 여러 개 놓을 때
    // 소리가 겹쳐 쌓이면 시끄럽기만 하다.
    player_->stop();

    data_ = QByteArray(reinterpret_cast<const char *>(wav.data()),
                       static_cast<qsizetype>(wav.size()));

    buffer_ = std::make_unique<QBuffer>(&data_);
    buffer_->open(QIODevice::ReadOnly);

    player_->setSourceDevice(buffer_.get());
    player_->play();
}

} // namespace splash::ui
