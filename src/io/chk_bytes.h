#pragma once

// CHK 바이트를 손질하는 순수 함수들. MappingCore 에 기대지 않으므로
// 테스트로 감쌀 수 있다.

#include <cstdint>
#include <string>
#include <vector>

namespace splash::io {

/// CHK 끝을 넘어가는 구역을 잘라 낸다.
///
/// 구역 머리말의 길이 칸을 파일 크기보다 훨씬 크게 적어 두는 맵이 있다 —
/// 남은 바이트가 아홉인데 1.8GB 라고 적어 두는 식이다. 그 값을 믿고 자리를
/// 잡으면 작은 맵 하나를 여는 데 기가바이트를 쓴다. 게임은 읽을 수 없는
/// 그 뒤를 그냥 무시하므로 우리도 거기서 끊는다.
///
/// 길이가 음수인 구역은 MappingCore 가 "되감기"로 쓰므로 건드리지 않는다.
void truncateOverlongSections(std::vector<std::uint8_t> & chk);

/// 문자열 구역(STR)을 꼬리를 겹쳐 담아 만든다.
///
/// 한 구역은 65535 바이트까지고 자리표가 u16 이라, 글자가 많은 맵은 그대로
/// 담으면 들어가지 않는다. "abc" 를 담아 두면 "bc" 는 한 칸 뒤를, "c" 는 두
/// 칸 뒤를 가리키면 되므로 긴 것부터 담고 짧은 꼬리는 그 안을 가리키게 한다.
/// 게임이 읽는 방식(자리표가 가리키는 곳부터 NUL 까지)은 그대로다.
///
/// strings 는 1 번 자리부터 차례로 담는다. 담지 못하면 빈 벡터를 돌려준다.
std::vector<std::uint8_t> packStringsSharingTails(const std::vector<std::string> & strings);

} // namespace splash::io
