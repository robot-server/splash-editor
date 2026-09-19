#pragma once

// 아주 작은 테스트 러너.
//
// GoogleTest 를 끌어오지 않는 이유: 지금 필요한 것은 "확인하고 세는" 것뿐이고,
// 의존성과 빌드 시간을 늘릴 만큼의 이득이 없다. 어서션 종류가 늘어나면
// 그때 교체한다.

#include <iostream>
#include <string>

namespace splash::test {

struct Registry
{
    int checks = 0;
    int failures = 0;

    void pass() { ++checks; }

    void fail(const std::string & what, const char * file, int line)
    {
        ++checks;
        ++failures;
        std::cerr << "  [실패] " << file << ":" << line << "  " << what << "\n";
    }

    int report(const std::string & suiteName) const
    {
        std::cout << "\n" << suiteName << ": " << (checks - failures) << "/" << checks
                  << " 통과\n";
        if (failures > 0)
            std::cout << suiteName << ": " << failures << "건 실패\n";
        return failures == 0 ? 0 : 1;
    }
};

inline Registry & registry()
{
    static Registry instance;
    return instance;
}

} // namespace splash::test

#define SPLASH_CHECK(cond)                                                        \
    do {                                                                          \
        if (cond) { ::splash::test::registry().pass(); }                          \
        else { ::splash::test::registry().fail(#cond, __FILE__, __LINE__); }      \
    } while (false)

#define SPLASH_CHECK_EQ(a, b)                                                     \
    do {                                                                          \
        const auto _lhs = (a);                                                    \
        const auto _rhs = (b);                                                    \
        if (_lhs == _rhs) { ::splash::test::registry().pass(); }                  \
        else {                                                                    \
            std::ostringstream _oss;                                              \
            _oss << #a " == " #b "  (" << _lhs << " vs " << _rhs << ")";          \
            ::splash::test::registry().fail(_oss.str(), __FILE__, __LINE__);      \
        }                                                                         \
    } while (false)
