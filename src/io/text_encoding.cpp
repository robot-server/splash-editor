#include "text_encoding.h"

#include <unicode/ucnv.h>
#include <unicode/ustring.h>
#include <unicode/utf8.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>

namespace splash::io {
namespace {

/// ICU 가 아는 변환기 이름.
const char * converterName(TextEncoding encoding)
{
    switch (encoding)
    {
        case TextEncoding::Cp949:  return "windows-949";
        case TextEncoding::Cp1252: return "windows-1252";
        case TextEncoding::Cp936:  return "windows-936";
        case TextEncoding::Cp932:  return "windows-932";
        case TextEncoding::Utf8:   return "UTF-8";
        case TextEncoding::Ascii:  break;
    }
    return "US-ASCII";
}

/// 변환기를 열고 닫는 일을 맡는다.
struct Converter
{
    UConverter * handle = nullptr;

    explicit Converter(TextEncoding encoding)
    {
        UErrorCode status = U_ZERO_ERROR;
        handle = ucnv_open(converterName(encoding), &status);
        if (U_FAILURE(status))
            handle = nullptr;
    }

    ~Converter()
    {
        if (handle != nullptr)
            ucnv_close(handle);
    }

    Converter(const Converter &) = delete;
    Converter & operator=(const Converter &) = delete;

    explicit operator bool() const { return handle != nullptr; }
};

/// 바이트에 7비트 밖 글자가 있는지.
bool hasHighBytes(const std::string & text)
{
    return std::any_of(text.begin(), text.end(), [](char c) {
        return static_cast<unsigned char>(c) >= 0x80;
    });
}

/// UTF-8 규칙에 맞는 바이트 열인지.
bool isValidUtf8(const std::string & text)
{
    const auto * bytes = reinterpret_cast<const std::uint8_t *>(text.data());
    const std::int32_t length = static_cast<std::int32_t>(text.size());
    std::int32_t offset = 0;
    while (offset < length)
    {
        UChar32 codepoint = 0;
        const std::int32_t previous = offset;
        U8_NEXT(bytes, offset, length, codepoint);
        if (codepoint < 0)
            return false;
        // 한 바이트짜리 제어 문자는 트리거 문자열에도 흔하므로 허용한다.
        if (offset <= previous)
            return false;
    }
    return true;
}

/// 그 인코딩으로 읽었을 때 "뜻이 통하는" 글자가 몇이나 되는지 센다.
///
/// 한글·한자·가나처럼 실제로 쓰이는 글자에 점을 주고, 사용처가 없는
/// 영역(사유 영역, 결합되지 않은 기호)에는 벌점을 준다. 잘못된 코드
/// 페이지로 읽으면 뜻 없는 글자가 쏟아지므로 점수가 낮아진다.
long long scoreDecoded(const std::u16string & utf16)
{
    long long score = 0;
    for (char16_t unit : utf16)
    {
        const unsigned int c = unit;
        if (c < 0x80)
            continue; // ASCII 는 어느 쪽이든 같으므로 점수에 넣지 않는다

        if ((c >= 0xAC00 && c <= 0xD7A3) ||   // 한글 음절
            (c >= 0x1100 && c <= 0x11FF) ||   // 한글 자모
            (c >= 0x3130 && c <= 0x318F))     // 호환 자모
            score += 12;
        else if (c >= 0x4E00 && c <= 0x9FFF)  // 한중일 한자
            score += 8;
        else if ((c >= 0x3040 && c <= 0x30FF)) // 히라가나·가타카나
            score += 8;
        else if (c >= 0xFF01 && c <= 0xFF60)  // 전각 기호
            score += 3;
        else if (c >= 0x00A0 && c <= 0x024F)  // 라틴 확장
            score += 2;
        else if (c >= 0xE000 && c <= 0xF8FF)  // 사유 영역 — 잘못 읽은 표시
            score -= 20;
        else
            score -= 2;
    }
    return score;
}

/// 한 인코딩으로 읽어 본다. 못 읽는 바이트가 있으면 비어 있는 값.
std::optional<std::u16string> tryDecode(const std::string & raw, TextEncoding encoding)
{
    Converter converter(encoding);
    if (!converter)
        return std::nullopt;

    // 못 읽는 바이트를 만나면 조용히 넘기지 말고 멈춘다 — 그래야 이
    // 인코딩이 아니라는 것을 알 수 있다.
    UErrorCode status = U_ZERO_ERROR;
    ucnv_setToUCallBack(converter.handle, UCNV_TO_U_CALLBACK_STOP,
                        nullptr, nullptr, nullptr, &status);
    if (U_FAILURE(status))
        return std::nullopt;

    std::u16string out(raw.size() + 1, u'\0');
    status = U_ZERO_ERROR;
    const std::int32_t length = ucnv_toUChars(
        converter.handle,
        reinterpret_cast<UChar *>(out.data()), static_cast<std::int32_t>(out.size()),
        raw.data(), static_cast<std::int32_t>(raw.size()), &status);

    if (U_FAILURE(status))
        return std::nullopt;

    out.resize(static_cast<std::size_t>(length));
    return out;
}

} // namespace

std::string encodingName(TextEncoding encoding)
{
    switch (encoding)
    {
        case TextEncoding::Ascii:  return "ASCII";
        case TextEncoding::Utf8:   return "UTF-8";
        case TextEncoding::Cp949:  return "한국어 (CP949)";
        case TextEncoding::Cp1252: return "서유럽 (CP1252)";
        case TextEncoding::Cp936:  return "중국어 간체 (CP936)";
        case TextEncoding::Cp932:  return "일본어 (CP932)";
    }
    return "ASCII";
}

std::string decodeText(const std::string & raw, TextEncoding encoding)
{
    if (raw.empty())
        return raw;

    // ASCII 와 UTF-8 은 이미 UTF-8 이므로 그대로 둔다. 다만 UTF-8 이라
    // 판정된 맵에도 깨진 바이트가 섞일 수 있어, 그때는 손대지 않는다.
    if (encoding == TextEncoding::Ascii || encoding == TextEncoding::Utf8)
        return raw;

    if (!hasHighBytes(raw))
        return raw;

    const auto utf16 = tryDecode(raw, encoding);
    if (!utf16)
    {
        // 못 읽는 바이트가 섞였다. 읽히는 데까지만이라도 옮긴다.
        Converter converter(encoding);
        if (!converter)
            return raw;

        std::u16string buffer(raw.size() + 1, u'\0');
        UErrorCode status = U_ZERO_ERROR;
        const std::int32_t length = ucnv_toUChars(
            converter.handle,
            reinterpret_cast<UChar *>(buffer.data()), static_cast<std::int32_t>(buffer.size()),
            raw.data(), static_cast<std::int32_t>(raw.size()), &status);
        if (U_FAILURE(status))
            return raw;
        buffer.resize(static_cast<std::size_t>(length));

        std::string out(buffer.size() * 4 + 1, '\0');
        status = U_ZERO_ERROR;
        std::int32_t outLength = 0;
        u_strToUTF8(out.data(), static_cast<std::int32_t>(out.size()), &outLength,
                    reinterpret_cast<const UChar *>(buffer.data()),
                    static_cast<std::int32_t>(buffer.size()), &status);
        if (U_FAILURE(status))
            return raw;
        out.resize(static_cast<std::size_t>(outLength));
        return out;
    }

    std::string out(utf16->size() * 4 + 1, '\0');
    UErrorCode status = U_ZERO_ERROR;
    std::int32_t outLength = 0;
    u_strToUTF8(out.data(), static_cast<std::int32_t>(out.size()), &outLength,
                reinterpret_cast<const UChar *>(utf16->data()),
                static_cast<std::int32_t>(utf16->size()), &status);
    if (U_FAILURE(status))
        return raw;

    out.resize(static_cast<std::size_t>(outLength));
    return out;
}

std::string encodeText(const std::string & utf8, TextEncoding encoding)
{
    if (utf8.empty())
        return utf8;

    if (encoding == TextEncoding::Ascii || encoding == TextEncoding::Utf8)
        return utf8;

    if (!hasHighBytes(utf8))
        return utf8;

    // UTF-8 -> UTF-16
    std::u16string utf16(utf8.size() + 1, u'\0');
    UErrorCode status = U_ZERO_ERROR;
    std::int32_t length = 0;
    u_strFromUTF8(reinterpret_cast<UChar *>(utf16.data()),
                  static_cast<std::int32_t>(utf16.size()), &length,
                  utf8.data(), static_cast<std::int32_t>(utf8.size()), &status);
    if (U_FAILURE(status))
        return utf8;
    utf16.resize(static_cast<std::size_t>(length));

    Converter converter(encoding);
    if (!converter)
        return utf8;

    // 나타낼 수 없는 글자는 '?' 로 바꾼다 — 멈추면 저장이 통째로 막힌다.
    status = U_ZERO_ERROR;
    ucnv_setFromUCallBack(converter.handle, UCNV_FROM_U_CALLBACK_SUBSTITUTE,
                          nullptr, nullptr, nullptr, &status);
    if (U_FAILURE(status))
        return utf8;

    std::string out(utf16.size() * 4 + 1, '\0');
    status = U_ZERO_ERROR;
    const std::int32_t outLength = ucnv_fromUChars(
        converter.handle, out.data(), static_cast<std::int32_t>(out.size()),
        reinterpret_cast<const UChar *>(utf16.data()),
        static_cast<std::int32_t>(utf16.size()), &status);
    if (U_FAILURE(status))
        return utf8;

    out.resize(static_cast<std::size_t>(outLength));
    return out;
}

TextEncoding detectEncoding(const std::vector<std::string> & samples)
{
    // 7비트만 쓰였으면 고민할 것이 없다.
    std::string joined;
    for (const auto & sample : samples)
    {
        if (hasHighBytes(sample))
        {
            joined += sample;
            joined.push_back('\n');
        }
    }
    if (joined.empty())
        return TextEncoding::Ascii;

    // UTF-8 로 읽히면서 뜻이 통하면 UTF-8 이다. 리마스터가 만든 맵이
    // 여기에 든다. CP949 바이트는 UTF-8 규칙을 거의 지키지 못한다.
    if (isValidUtf8(joined))
        return TextEncoding::Utf8;

    // 나머지는 코드 페이지별로 읽어 보고 가장 그럴듯한 쪽을 고른다.
    constexpr std::array<TextEncoding, 4> candidates {
        TextEncoding::Cp949, TextEncoding::Cp932, TextEncoding::Cp936, TextEncoding::Cp1252
    };

    TextEncoding best = TextEncoding::Cp1252;
    long long bestScore = std::numeric_limits<long long>::min();
    for (TextEncoding candidate : candidates)
    {
        const auto decoded = tryDecode(joined, candidate);
        if (!decoded)
            continue;

        long long score = scoreDecoded(*decoded);

        // 같은 점수면 한국어를 앞세운다. 스타크래프트 맵 가운데 압도적
        // 다수가 한국어 맵이고, CP1252 는 어떤 바이트든 읽어 내므로
        // 점수만으로는 언제나 후보로 남는다.
        if (candidate == TextEncoding::Cp949)
            score += 1;

        if (score > bestScore)
        {
            bestScore = score;
            best = candidate;
        }
    }

    return best;
}

} // namespace splash::io
