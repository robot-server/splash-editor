# CLI 프런트엔드. GUI 없이 코어를 검증하는 경로이자 테스트 하네스.

add_executable(splash-cli main.cpp)

target_link_libraries(splash-cli PRIVATE splash_core)

if(NOT MSVC)
  target_compile_options(splash-cli PRIVATE -Wall -Wextra)
endif()
