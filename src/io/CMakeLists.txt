# MappingCore 를 감싸는 얇은 I/O 계층. Qt 를 링크하지 않는다.

add_library(splash_io STATIC
  map_archive.cpp
  map_archive.h
)

target_include_directories(splash_io PUBLIC "${CMAKE_SOURCE_DIR}/src")

# MappingCore 는 PRIVATE — 그 헤더가 splash_io 를 쓰는 쪽으로 새어 나가면
# 코어 경계가 무너진다. map_archive.h 는 표준 타입만 노출한다.
target_link_libraries(splash_io PRIVATE splash_mappingcore)

target_compile_features(splash_io PUBLIC cxx_std_20)
