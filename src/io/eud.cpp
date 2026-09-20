#include "io/eud.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>

namespace splash::io::eud {

namespace {

std::string lowered(std::string text)
{
    for (char & c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// 16진수 글자만으로 이루어졌는지 (a-f 가 하나라도 있으면 10진수일 수 없다).
bool looksHexOnly(const std::string & text)
{
    bool anyLetter = false;
    for (const char c : text)
    {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0)
            continue;
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower >= 'a' && lower <= 'f')
        {
            anyLetter = true;
            continue;
        }
        return false;
    }
    return anyLetter;
}

// --- 아주 작은 JSON 읽개 ---
//
// EUD Book 의 api.json 은 "문자열만 담긴 객체의 배열" 이라는 아주 좁은
// 꼴이다. 그 꼴만 읽으면 되므로 의존성을 하나 더 들이지 않는다.
// 모르는 값(중첩 객체·배열)은 건너뛰고, 깨진 파일이면 거짓을 돌린다.
class JsonReader
{
public:
    explicit JsonReader(const std::string & text) : text_(text) {}

    void skipSpace()
    {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_])) != 0)
            ++pos_;
    }

    bool eat(char expected)
    {
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == expected)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    bool peek(char expected)
    {
        skipSpace();
        return pos_ < text_.size() && text_[pos_] == expected;
    }

    bool atEnd()
    {
        skipSpace();
        return pos_ >= text_.size();
    }

    /// 따옴표로 둘러싼 글자를 읽는다. 이스케이프는 흔한 것만 푼다.
    bool readString(std::string & out)
    {
        if (!eat('"'))
            return false;

        out.clear();
        while (pos_ < text_.size())
        {
            const char c = text_[pos_++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size())
                return false;

            const char escaped = text_[pos_++];
            switch (escaped)
            {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': break;                       // 줄 끝의 CR 은 버린다
                case 'u': readUnicodeEscape(out); break;
                default:  out.push_back(escaped); break;
            }
        }
        return false;
    }

    /// 값 하나를 글자로 읽는다. 수·참거짓·널도 글자로 돌려준다.
    /// 중첩 객체와 배열은 건너뛰고 빈 글자를 돌린다.
    bool readValue(std::string & out)
    {
        skipSpace();
        if (pos_ >= text_.size())
            return false;

        const char c = text_[pos_];
        if (c == '"')
            return readString(out);

        if (c == '{' || c == '[')
        {
            out.clear();
            return skipContainer();
        }

        const std::size_t start = pos_;
        while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' &&
               text_[pos_] != ']')
            ++pos_;
        out = text_.substr(start, pos_ - start);
        while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())) != 0)
            out.pop_back();
        return true;
    }

private:
    void readUnicodeEscape(std::string & out)
    {
        if (pos_ + 4 > text_.size())
            return;
        const std::string digits = text_.substr(pos_, 4);
        pos_ += 4;

        unsigned code = 0;
        try
        {
            code = static_cast<unsigned>(std::stoul(digits, nullptr, 16));
        }
        catch (const std::exception &)
        {
            return;
        }

        // UTF-8 로 옮긴다. 서로게이트 쌍은 쓰이지 않으므로 다루지 않는다.
        if (code < 0x80)
        {
            out.push_back(static_cast<char>(code));
        }
        else if (code < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool skipContainer()
    {
        const char open = text_[pos_];
        const char close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (pos_ < text_.size())
        {
            const char c = text_[pos_];
            if (c == '"')
            {
                std::string ignored;
                if (!readString(ignored))
                    return false;
                continue;
            }
            ++pos_;
            if (c == open)
                ++depth;
            else if (c == close && --depth == 0)
                return true;
        }
        return false;
    }

    const std::string & text_;
    std::size_t pos_ = 0;
};

/// 바깥에서 읽어 들인 표. 읽기는 여러 곳에서 하므로 잠금으로 감싼다.
struct Database
{
    std::mutex guard;
    std::vector<OffsetEntry> entries;   ///< 붙박이 + 바깥 표를 합쳐 정렬한 것
    std::vector<OffsetEntry> external;
    std::string path;
    bool merged = false;
};

Database & database()
{
    static Database db;
    return db;
}

/// 주소 순으로, 같으면 좁은 것 먼저.
void sortEntries(std::vector<OffsetEntry> & entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const OffsetEntry & a, const OffsetEntry & b) {
                  if (a.address != b.address)
                      return a.address < b.address;
                  return a.span() < b.span();
              });
}

void rebuildMerged(Database & db)
{
    db.entries = builtinOffsets();
    db.entries.insert(db.entries.end(), db.external.begin(), db.external.end());
    sortEntries(db.entries);
    db.merged = true;
}

} // namespace

std::string scrSupportName(ScrSupport support)
{
    switch (support)
    {
        case ScrSupport::SimpleData:     return "그냥 값";
        case ScrSupport::Supported:      return "됨";
        case ScrSupport::BackedByCode:   return "코드가 받침";
        case ScrSupport::ReadOnly:       return "읽기만";
        case ScrSupport::Unsupported:    return "안 됨";
        case ScrSupport::SeeDescription: return "설명 참고";
        case ScrSupport::Unknown:        break;
    }
    return "모름";
}

ScrSupport parseScrSupport(const std::string & text)
{
    const std::string key = lowered(text);
    if (key == "simple data")     return ScrSupport::SimpleData;
    if (key == "supported")       return ScrSupport::Supported;
    if (key == "backed by code")  return ScrSupport::BackedByCode;
    if (key == "read only")       return ScrSupport::ReadOnly;
    if (key == "unsupported")     return ScrSupport::Unsupported;
    if (key == "see description") return ScrSupport::SeeDescription;
    return ScrSupport::Unknown;
}

const std::vector<OffsetEntry> & builtinOffsets()
{
    // 제원(주소·크기·개수·SCR 상태)은 공개된 EUD 자료에서 확인한 사실이고,
    // 이름과 설명은 우리가 우리말로 적은 것이다.
    static const std::vector<OffsetEntry> kBuiltin = [] {
        std::vector<OffsetEntry> out {
            { "플레이어 미네랄",        0x0057F0F0,  4,  12, ScrSupport::SimpleData,
              "플레이어별 미네랄. 플레이어 1 이 +0, 2 가 +4 다." },
            { "플레이어 가스",          0x0057F120,  4,  12, ScrSupport::SimpleData, {} },
            { "캔 가스 총량",           0x0057F150,  4,  12, ScrSupport::SimpleData, {} },
            { "캔 미네랄 총량",         0x0057F180,  4,  12, ScrSupport::SimpleData, {} },
            { "저그 대군주 여유",       0x00582144,  4,  12, ScrSupport::SimpleData,
              "4바이트지만 게임은 16비트로만 보여 준다." },
            { "저그 대군주 사용",       0x00582174,  4,  12, ScrSupport::SimpleData, {} },
            { "테란 보급 여유",         0x005821D4,  4,  12, ScrSupport::SimpleData, {} },
            { "테란 보급 사용",         0x00582204,  4,  12, ScrSupport::SimpleData, {} },
            { "프로토스 파일런 여유",   0x00582264,  4,  12, ScrSupport::SimpleData, {} },
            { "프로토스 파일런 사용",   0x00582294,  4,  12, ScrSupport::SimpleData, {} },
            { "화면 위치 (타일)",       0x0057F1D0,  2,   2, ScrSupport::SimpleData, {} },
            { "맵 크기 (타일)",         0x0057F1D4,  2,   2, ScrSupport::SimpleData, {} },
            { "타일셋",                 0x0057F1DC,  2,   1, ScrSupport::SimpleData, {} },
            { "유닛 수 표",             0x00582324, 48, 228, ScrSupport::SimpleData,
              "Deaths 표와 같은 배치다 — 유닛 하나마다 플레이어 12칸." },
            { "다 지은 유닛 수 표",     0x00584DE4, 48, 228, ScrSupport::SimpleData, {} },
            { "잡은 유닛 수 표",        0x005878A4, 48, 228, ScrSupport::SimpleData, {} },
            { "Deaths 표 첫 자리",      0x0058A364, 48, 228, ScrSupport::SimpleData,
              "EPD 0. 모든 EUD 셈의 기준점이다." },
            { "로케이션 표",            0x0058DC60, 20, 255, ScrSupport::SimpleData,
              "한 칸이 20바이트 — 왼쪽·위·오른쪽·아래·플래그." },
            { "단축키 묶음",            0x0057FE60, 864,  8, ScrSupport::SimpleData, {} },
            { "유닛 표 첫 자리 (CUnit)", 0x0059CCA8, 4,   1, ScrSupport::Supported,
              "첫 CUnit 의 앞 유닛 자리. 유닛 배열이 여기서 시작한다." },
            { "트리거 실행 타이머",     0x006509A0,  4,   1, ScrSupport::SimpleData,
              "0 이 되면 트리거가 돈다. 하이퍼 트리거가 이 자리를 붙든다." },
            { "내 플레이어 번호",       0x00512684,  4,   1, ScrSupport::BackedByCode,
              "이 컴퓨터가 몇 번 플레이어인지. 사람마다 다르다." },
            { "마우스 좌표 X",          0x006CDDC4,  4,   1, ScrSupport::SimpleData, {} },
            { "플레이어 이름",          0x0057EE9C, 25,   1, ScrSupport::Unsupported,
              "리마스터에서는 안 된다." },
        };
        sortEntries(out);
        return out;
    }();
    return kBuiltin;
}

bool loadOffsetDatabase(const std::string & path, std::string * error)
{
    const auto fail = [error](std::string why) {
        if (error != nullptr)
            *error = std::move(why);
        return false;
    };

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return fail("파일을 열 수 없습니다: " + path);

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    if (text.empty())
        return fail("파일이 비어 있습니다: " + path);

    JsonReader reader(text);
    if (!reader.eat('['))
        return fail("JSON 배열이 아닙니다 (EUD Book 의 api.json 을 주세요).");

    std::vector<OffsetEntry> parsed;
    if (!reader.peek(']'))
    {
        while (true)
        {
            if (!reader.eat('{'))
                return fail("객체가 아닌 항목이 들어 있습니다.");

            OffsetEntry entry;
            bool haveAddress = false;

            if (!reader.peek('}'))
            {
                while (true)
                {
                    std::string key;
                    if (!reader.readString(key))
                        return fail("키를 읽지 못했습니다.");
                    if (!reader.eat(':'))
                        return fail("':' 가 없습니다.");

                    std::string value;
                    if (!reader.readValue(value))
                        return fail("값을 읽지 못했습니다.");

                    const std::string lowerKey = lowered(key);
                    if (lowerKey == "address")
                    {
                        // EUD Book 은 주소를 10진수 글자로 적는다.
                        try
                        {
                            entry.address =
                                static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
                            haveAddress = true;
                        }
                        catch (const std::exception &)
                        {
                        }
                    }
                    else if (lowerKey == "name")
                    {
                        entry.name = value;
                    }
                    else if (lowerKey == "size" || lowerKey == "length")
                    {
                        std::uint32_t number = 0;
                        try
                        {
                            number = static_cast<std::uint32_t>(std::stoul(value));
                        }
                        catch (const std::exception &)
                        {
                            number = 0;
                        }
                        if (number != 0)
                        {
                            if (lowerKey == "size")
                                entry.size = number;
                            else
                                entry.length = number;
                        }
                    }
                    else if (lowerKey == "scr")
                    {
                        entry.scr = parseScrSupport(value);
                    }
                    else if (lowerKey == "description")
                    {
                        entry.description = value;
                    }

                    if (reader.eat(','))
                        continue;
                    break;
                }
            }

            if (!reader.eat('}'))
                return fail("'}' 가 없습니다.");

            if (haveAddress && !entry.name.empty())
                parsed.push_back(std::move(entry));

            if (reader.eat(','))
                continue;
            break;
        }
    }

    if (!reader.eat(']'))
        return fail("']' 가 없습니다.");

    if (parsed.empty())
        return fail("쓸 만한 항목이 없습니다.");

    Database & db = database();
    const std::lock_guard<std::mutex> lock(db.guard);
    db.external = std::move(parsed);
    db.path = path;
    rebuildMerged(db);
    return true;
}

void clearOffsetDatabase()
{
    Database & db = database();
    const std::lock_guard<std::mutex> lock(db.guard);
    db.external.clear();
    db.path.clear();
    rebuildMerged(db);
}

bool hasOffsetDatabase()
{
    Database & db = database();
    const std::lock_guard<std::mutex> lock(db.guard);
    return !db.external.empty();
}

const std::string & offsetDatabasePath()
{
    Database & db = database();
    const std::lock_guard<std::mutex> lock(db.guard);
    return db.path;
}

const std::vector<OffsetEntry> & offsets()
{
    Database & db = database();
    const std::lock_guard<std::mutex> lock(db.guard);
    if (!db.merged)
        rebuildMerged(db);
    return db.entries;
}

std::optional<std::uint32_t> parseAddress(const std::string & text)
{
    std::string token;
    for (const char c : text)
    {
        if (std::isspace(static_cast<unsigned char>(c)) == 0 && c != '_')
            token.push_back(c);
    }
    if (token.empty())
        return std::nullopt;

    int base = 10;
    if (token.size() > 2 && token[0] == '0' &&
        (token[1] == 'x' || token[1] == 'X'))
    {
        token = token.substr(2);
        base = 16;
    }
    else if (looksHexOnly(token))
    {
        base = 16;
    }

    try
    {
        std::size_t consumed = 0;
        const unsigned long value = std::stoul(token, &consumed, base);
        if (consumed != token.size())
            return std::nullopt;
        return static_cast<std::uint32_t>(value);
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

const OffsetEntry * offsetAt(std::uint32_t address)
{
    const OffsetEntry * best = nullptr;
    for (const OffsetEntry & entry : offsets())
    {
        if (!entry.covers(address))
            continue;
        if (best == nullptr || entry.span() < best->span())
            best = &entry;
    }
    return best;
}

std::vector<const OffsetEntry *> findOffsets(const std::string & query)
{
    const std::vector<OffsetEntry> & all = offsets();

    std::vector<const OffsetEntry *> out;
    if (query.empty())
    {
        out.reserve(all.size());
        for (const OffsetEntry & entry : all)
            out.push_back(&entry);
        return out;
    }

    // 주소처럼 보이면 그 자리를 덮는 항목을 먼저 본다.
    if (const auto address = parseAddress(query))
    {
        for (const OffsetEntry & entry : all)
        {
            if (entry.covers(*address))
                out.push_back(&entry);
        }
        if (!out.empty())
            return out;
    }

    const std::string needle = lowered(query);

    // 이름이 똑 맞으면 그것만 준다. "미네랄" 처럼 다른 이름에 들어 있는
    // 낱말로도 찾을 수 있게 하되, 정확한 이름이 우선이다.
    for (const OffsetEntry & entry : all)
    {
        if (lowered(entry.name) == needle)
            out.push_back(&entry);
    }
    if (!out.empty())
        return out;

    for (const OffsetEntry & entry : all)
    {
        if (lowered(entry.name).find(needle) != std::string::npos)
            out.push_back(&entry);
    }
    return out;
}

std::string describeAddress(std::uint32_t address)
{
    char head[64] = {};
    std::snprintf(head, sizeof(head), "0x%08X  EPD %d", address, signedEpdFor(address));

    std::string out = head;
    if (!isAligned(address))
    {
        out += "  (네 바이트 경계가 아님 — Deaths 로는 못 읽는다)";
        return out;
    }

    if (const OffsetEntry * entry = offsetAt(address))
    {
        out += "  " + entry->name;
        const std::uint32_t delta = address - entry->address;
        if (delta != 0 || entry->length > 1)
        {
            char suffix[48] = {};
            if (entry->size != 0 && delta % entry->size == 0 && entry->length > 1)
                std::snprintf(suffix, sizeof(suffix), " [%u번째]", delta / entry->size);
            else
                std::snprintf(suffix, sizeof(suffix), " [+0x%X]", delta);
            out += suffix;
        }
        out += "  (SCR: " + scrSupportName(entry->scr) + ")";
    }

    std::uint32_t player = 0;
    std::uint32_t unit = 0;
    slotFor(address, &player, &unit);
    if (isInsideDeathTable(player, unit) && epdForSlot(player, unit) == epdFor(address))
    {
        char slot[64] = {};
        std::snprintf(slot, sizeof(slot), "  Deaths(P%u, 유닛 %u)", player + 1, unit);
        out += slot;
    }

    return out;
}

} // namespace splash::io::eud
