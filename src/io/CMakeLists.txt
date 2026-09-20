# MappingCore 를 감싸는 얇은 I/O 계층. Qt 를 링크하지 않는다.

add_library(splash_io STATIC
  map_archive.cpp
  map_archive.h
  game_assets.cpp
  game_assets.h
  chk_bytes.cpp
  chk_bytes.h
  eud.cpp
  eud.h
  game_graphics.cpp
  game_graphics.h
  text_colors.cpp
  text_colors.h
  text_encoding.cpp
  text_encoding.h
)

target_include_directories(splash_io PUBLIC "${CMAKE_SOURCE_DIR}/src")

# MappingCore 는 PRIVATE — 그 헤더가 splash_io 를 쓰는 쪽으로 새어 나가면
# 코어 경계가 무너진다. map_archive.h 는 표준 타입만 노출한다.
# ICU 는 맵 문자열의 코드 페이지(CP949 등)를 UTF-8 로 옮기는 데 쓴다.
# MappingCore 도 ICU 를 쓰므로 새 의존성이 아니다.
target_link_libraries(splash_io PRIVATE splash_mappingcore ICU::uc ICU::i18n ICU::data)

target_compile_features(splash_io PUBLIC cxx_std_20)
