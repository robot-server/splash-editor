# round-trip 테스트. M1 의 합격 조건을 지키는 안전망.

add_executable(splash_roundtrip_test
  roundtrip_test.cpp
  test_support.h
)

target_link_libraries(splash_roundtrip_test PRIVATE splash_core)
target_include_directories(splash_roundtrip_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT MSVC)
  target_compile_options(splash_roundtrip_test PRIVATE -Wall -Wextra)
endif()

# tests/maps/ 에 실제 맵을 넣어 두면 테스트가 자동으로 발견해 검증한다.
# 저작권 자료이므로 저장소에는 커밋하지 않는다 (tests/maps/README.md 참고).
add_test(NAME roundtrip
  COMMAND splash_roundtrip_test "${CMAKE_CURRENT_SOURCE_DIR}/maps"
)
