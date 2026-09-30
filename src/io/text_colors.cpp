#include "io/text_colors.h"

namespace splash::io {
namespace {

/// 코드와 색. 0x09(탭)·0x0A·0x0D(줄바꿈)처럼 글자를 흘리는 제어 문자는
/// 여기에 넣지 않는다 — 편집기가 글자 그대로 다루기 때문이다.
///
/// 값은 2026-09-28에 StarCraft: Remastered 맵 찾기 화면의 시나리오
/// 이름란에서 실측했다(`docs/tools/editors.md`의 "실측한 색 코드" 참고).
/// 어둡거나 배경과 구분이 안 돼 실측하지 못한 코드는 아예 넣지 않았다 —
/// `findTextControlCode`가 그런 코드는 nullptr을 돌려준다.
const std::vector<TextControlCode> kCodes {
    { 0x01, "이전 색으로",   false,   0,   0,   0 },
    { 0x02, "기본색(흰색)",  true,  240, 240, 240 },
    { 0x03, "초록",          true,   66, 182,  53 },
    { 0x04, "연두",          true,  170, 249, 115 },
    { 0x05, "회색",          true,  130, 130, 130 },
    { 0x06, "흰색",          true,  240, 240, 240 },
    { 0x07, "빨강",          true,  238,   0,   8 },
    { 0x0E, "회색",          true,  130, 130, 130 },
    { 0x11, "노랑",          true,  225, 210,  67 },
    { 0x12, "오른쪽 정렬",   false,   0,   0,   0 },
    { 0x13, "가운데 정렬",   false,   0,   0,   0 },
    { 0x14, "숨김",          false,   0,   0,   0 },
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
