#pragma once

// 조건·액션 한 줄의 인자를 위젯으로 늘어놓는 판.
//
// StarEdit 의 트리거 편집기처럼, 고른 줄의 인자마다 알맞은 상자가
// 뜬다 — 플레이어·유닛·위치·스위치는 고르는 상자, 수량은 숫자 상자,
// 문자열은 여러 줄 입력이다. 값을 바꾸면 곧바로 문서에 기록한다.

#include <QWidget>

#include <cstdint>
#include <vector>

#include "io/map_archive.h"

class QFormLayout;
class QLabel;

namespace splash::ui {

class TriggerArgumentPanel : public QWidget
{
    Q_OBJECT

public:
    /// 이 판이 조건을 다루는지 액션을 다루는지.
    enum class Kind { Condition, Action };

    explicit TriggerArgumentPanel(Kind kind, QWidget * parent = nullptr);

    /// 어떤 줄의 인자를 보여 줄지 정한다. 종류 목록은 콤보에 채운다.
    void setElement(std::size_t triggerIndex, std::size_t slot,
                    const io::TriggerElement & element,
                    const std::vector<io::TriggerChoice> & types);

    /// 보여 줄 줄이 없을 때.
    void clear();

signals:
    /// 종류를 바꿨다.
    void typeChanged(std::size_t slot, std::uint8_t type);

    /// 인자 값을 바꿨다.
    void argChanged(std::size_t slot, std::size_t argIndex, std::uint32_t value);

    /// 문자열 인자를 바꿨다.
    void argTextChanged(std::size_t slot, std::size_t argIndex, const QString & text);

private:
    void rebuild();

    Kind kind_;
    std::size_t triggerIndex_ = 0;
    std::size_t slot_ = 0;
    io::TriggerElement element_;
    std::vector<io::TriggerChoice> types_;

    QFormLayout * form_ = nullptr;
    QWidget * body_ = nullptr;
    QLabel * empty_ = nullptr;
    bool loading_ = false;
};

} // namespace splash::ui
