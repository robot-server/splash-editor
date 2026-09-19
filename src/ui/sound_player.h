#pragma once

// 유닛을 놓을 때 그 유닛의 소리를 낸다. SCMDraft 처럼 배치가 손에 잡히는
// 느낌을 준다.
//
// 소리는 게임 아카이브에서 읽은 WAV 바이트다. 파일로 떨구지 않고 메모리에서
// 바로 재생한다.

#include <QBuffer>
#include <QByteArray>
#include <QObject>

#include <cstdint>
#include <memory>
#include <vector>

class QAudioOutput;
class QMediaPlayer;

namespace splash::ui {

class SoundPlayer : public QObject
{
    Q_OBJECT

public:
    explicit SoundPlayer(QObject * parent = nullptr);
    ~SoundPlayer() override;

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    /// WAV 바이트를 재생한다. 비어 있으면 아무것도 하지 않는다.
    void play(const std::vector<std::uint8_t> & wav);

private:
    bool enabled_ = true;

    // 재생이 끝날 때까지 버퍼가 살아 있어야 한다.
    QByteArray data_;
    std::unique_ptr<QBuffer> buffer_;
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> output_;
};

} // namespace splash::ui
