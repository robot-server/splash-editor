#include "io/chk_bytes.h"

#include <algorithm>
#include <cstring>

namespace splash::io {

void truncateOverlongSections(std::vector<std::uint8_t> & chk)
{
    constexpr std::size_t kHeaderSize = 8;

    std::size_t position = 0;
    while (position + kHeaderSize <= chk.size())
    {
        std::int32_t size = 0;
        std::memcpy(&size, chk.data() + position + 4, sizeof(size));

        if (size < 0)
        {
            position += kHeaderSize;
            continue;
        }

        const std::size_t remaining = chk.size() - position - kHeaderSize;
        if (static_cast<std::size_t>(size) > remaining)
        {
            chk.resize(position); // 이 구역 머리말부터 버린다
            return;
        }

        position += kHeaderSize + static_cast<std::size_t>(size);
    }
}

std::vector<std::uint8_t> packStringsSharingTails(const std::vector<std::string> & strings)
{
    const std::size_t count = strings.size();
    const std::size_t headerSize = 2 + 2 * count;
    if (headerSize > 0xFFFF)
        return {};

    // 긴 것부터 담아야 짧은 꼬리가 그 안에 들어간다.
    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return strings[a].size() > strings[b].size();
    });

    std::string data(1, '\0'); // 첫 NUL — 빈 자리는 모두 여기를 가리킨다
    std::vector<std::size_t> offsets(count, 0);

    for (const std::size_t index : order)
    {
        const std::string & text = strings[index];
        if (text.empty())
        {
            offsets[index] = 0; // 첫 NUL
            continue;
        }

        const std::string needle = text + '\0';
        const std::size_t found = data.find(needle);
        if (found != std::string::npos)
        {
            offsets[index] = found;
            continue;
        }

        offsets[index] = data.size();
        data += needle;
    }

    if (headerSize + data.size() > 0xFFFF)
        return {};

    std::vector<std::uint8_t> out(headerSize + data.size(), 0);
    const auto put16 = [&out](std::size_t at, std::uint16_t value) {
        out[at] = static_cast<std::uint8_t>(value & 0xFF);
        out[at + 1] = static_cast<std::uint8_t>(value >> 8);
    };

    put16(0, static_cast<std::uint16_t>(count));
    for (std::size_t i = 0; i < count; ++i)
        put16(2 + 2 * i, static_cast<std::uint16_t>(headerSize + offsets[i]));

    std::memcpy(out.data() + headerSize, data.data(), data.size());
    return out;
}

} // namespace splash::io
