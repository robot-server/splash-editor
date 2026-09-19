#pragma once

// StarCraft 문자열의 색·정렬 제어 문자.
//
// 맵 문자열은 0x01~0x1F 의 제어 문자로 뒤따르는 글자의 색을 바꾼다.
// 게임은 이 값을 폰트 팔레트(tfontgam.pcx)의 램프로 옮기는데, 램프
// 번호는 실행 파일에 박혀 있어 자료에서 읽을 수 없다. 그래서 여기서는
// 널리 쓰이는 값으로 적어 둔다 — 편집기에서 보이는 색은 게임 화면과
// 완전히 같지는 않을 수 있고, 저장되는 바이트는 언제나 원래 코드다.

#include <cstdint>
#include <string>
#include <vector>

namespace splash::io {

/// 제어 문자 하나가 하는 일.
struct TextControlCode
{
    std::uint8_t code = 0;
    std::string name;      ///< 사람이 읽는 이름
    bool isColor = false;  ///< 색을 바꾸는 코드인지
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

/// 맵 문자열에서 쓸 수 있는 제어 문자 목록.
const std::vector<TextControlCode> & textControlCodes();

/// 그 코드의 뜻. 모르는 코드면 nullptr.
const TextControlCode * findTextControlCode(std::uint8_t code);

} // namespace splash::io
