#pragma once

// 맵 문자열의 글자 인코딩을 다룬다.
//
// CHK 의 STR 구역은 바이트 열만 담고 어떤 인코딩인지 적어 두지 않는다.
// 실제 맵은 만든 나라의 기본 코드 페이지를 쓴다 — 한국어 맵은 CP949,
// 서양 맵은 CP1252, 최근 리마스터 맵은 UTF-8 이다. 그대로 UTF-8 로
// 읽으면 한글이 깨지므로, 맵을 열 때 인코딩을 가려내고 오갈 때마다
// 변환한다.
//
// 저장할 때는 읽어 들인 인코딩으로 되돌린다. 그러지 않으면 게임이
// 다른 글자로 읽는다 — "게임이 실제로 여는 저장이 이긴다".

#include <string>
#include <vector>

namespace splash::io {

/// 맵 문자열이 쓰는 인코딩.
enum class TextEncoding
{
    Ascii,   ///< 7비트만 쓴다. 어느 쪽으로 읽어도 같다
    Utf8,    ///< 이미 UTF-8
    Cp949,   ///< 한국어(통합 완성형). EUC-KR 을 포함한다
    Cp1252,  ///< 서유럽
    Cp936,   ///< 중국어 간체(GBK)
    Cp932,   ///< 일본어(Shift-JIS)
};

/// 인코딩의 사람이 읽는 이름.
std::string encodingName(TextEncoding encoding);

/// 맵에서 꺼낸 바이트를 UTF-8 로 옮긴다.
std::string decodeText(const std::string & raw, TextEncoding encoding);

/// UTF-8 문자열을 맵에 넣을 바이트로 옮긴다.
///
/// 그 인코딩으로 나타낼 수 없는 글자는 '?' 가 된다.
std::string encodeText(const std::string & utf8, TextEncoding encoding);

/// 주어진 바이트들이 어떤 인코딩인지 가려낸다.
///
/// ASCII 만 있으면 Ascii, UTF-8 로 읽히면 Utf8, 그 밖에는 각 코드
/// 페이지로 읽어 보고 한글·한자처럼 뜻이 통하는 글자가 가장 많이
/// 나오는 쪽을 고른다.
TextEncoding detectEncoding(const std::vector<std::string> & samples);

} // namespace splash::io
