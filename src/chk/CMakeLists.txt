# 코어 파사드. Qt 를 링크하지 않는다 — UI 는 이 계층 위에만 얹힌다.

add_library(splash_core STATIC
  map_document.cpp
  map_document.h
)

target_include_directories(splash_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

# map_document.h 가 io/map_archive.h 를 포함하므로 PUBLIC.
target_link_libraries(splash_core PUBLIC splash_io)

target_compile_features(splash_core PUBLIC cxx_std_20)

if(NOT MSVC)
  target_compile_options(splash_core PRIVATE -Wall -Wextra)
endif()
