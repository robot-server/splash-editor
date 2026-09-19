#include "io/text_colors.h"

namespace splash::io {
namespace {

/// 코드와 색. 0x09(탭)·0x0A·0x0D(줄바꿈)처럼 글자를 흘리는 제어 문자는
/// 여기에 넣지 않는다 — 편집기가 글자 그대로 다루기 때문이다.
const std::vector<TextControlCode> kCodes {
    { 0x01, "이전 색으로",   false,   0,   0,   0 },
    { 0x02, "연갈색",        true,  184, 156, 124 },
    { 0x03, "흰색",          true,  255, 255, 255 },
    { 0x04, "회색",          true,  132, 132, 132 },
    { 0x05, "빨강",          true,  200,  24,  24 },
    { 0x06, "초록",          true,   16, 252,  24 },
    { 0x07, "밝은 빨강",     true,  244,  68,  68 },
    { 0x08, "숨김",          false,   0,   0,   0 },
    { 0x0B, "숨김",          false,   0,   0,   0 },
    { 0x0E, "파랑",          true,   48,  64, 216 },
    { 0x0F, "청록",          true,   12, 144, 168 },
    { 0x10, "보라",          true,  136,  64, 156 },
    { 0x11, "주황",          true,  248, 140,  20 },
    { 0x12, "오른쪽 정렬",   false,   0,   0,   0 },
    { 0x13, "가운데 정렬",   false,   0,   0,   0 },
    { 0x14, "숨김",          false,   0,   0,   0 },
    { 0x15, "갈색",          true,  128,  84,  36 },
    { 0x16, "연노랑",        true,  228, 224, 176 },
    { 0x17, "연두",          true,  144, 200,  64 },
    { 0x18, "노랑",          true,  252, 252,  56 },
    { 0x19, "연회색",        true,  200, 200, 200 },
    { 0x1A, "하늘색",        true,   84, 168, 252 },
    { 0x1B, "분홍",          true,  252, 148, 200 },
    { 0x1C, "짙은 청록",     true,   16, 128, 128 },
    { 0x1D, "연한 회색",     true,  164, 164, 164 },
    { 0x1E, "짙은 파랑",     true,   32,  48, 136 },
    { 0x1F, "연파랑",        true,  148, 176, 252 },
};

} // namespace

const std::vector<TextControlCode> & textControlCodes()
{
    return kCodes;
}

const TextControlCode * findTextControlCode(std::uint8_t code)
{
    for (const auto & entry : kCodes)
    {
        if (entry.code == code)
            return &entry;
    }
    return nullptr;
}

} // namespace splash::io
