#include "io/eud.h"

namespace splash::io::eud {

const std::vector<KnownAddress> & knownAddresses()
{
    static const std::vector<KnownAddress> kKnown {
        { "플레이어 미네랄",        0x0057F0F0 },
        { "플레이어 가스",          0x0057F120 },
        { "캔 가스 총량",           0x0057F150 },
        { "캔 미네랄 총량",         0x0057F180 },
        { "저그 대군주 여유",       0x00582144 },
        { "저그 대군주 사용",       0x00582174 },
        { "테란 보급 여유",         0x005821D4 },
        { "테란 보급 사용",         0x00582204 },
        { "프로토스 파일런 여유",   0x00582264 },
        { "프로토스 파일런 사용",   0x00582294 },
        { "맵 크기",                0x0057F1D4 },
        { "타일셋",                 0x0057F1DC },
        { "화면 위치(타일)",        0x0057F1D0 },
        { "유닛 수 표",             0x00582324 },
        { "다 지은 유닛 수 표",     0x00584DE4 },
        { "잡은 유닛 수 표",        0x005878A4 },
        { "단축키 묶음",            0x0057FE60 },
        { "유닛 목록 첫 자리",      0x0059CCA8 },
        { "Deaths 표 첫 자리",      0x0058A364 },
    };
    return kKnown;
}

} // namespace splash::io::eud
