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

# 게임 자료 없이 도는 단위 테스트. 실제 맵에서 겪고 고친 일들을 다시 겪지
# 않기 위한 그물이다.

add_executable(splash_unit_test
  unit_test.cpp
  test_support.h
)

target_link_libraries(splash_unit_test PRIVATE splash_io)
target_include_directories(splash_unit_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT MSVC)
  target_compile_options(splash_unit_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME unit COMMAND splash_unit_test)

# 화면을 실제로 두드려 보는 스모크 테스트. Qt 가 있어야 하므로 GUI 를
# 빌드할 때만 넣는다. 화면 없는 곳에서도 돌도록 offscreen 으로 띄운다.

find_package(Qt6 QUIET COMPONENTS Widgets)

if(Qt6Widgets_FOUND)
  qt_add_executable(splash_gui_smoke_test
    gui_smoke_test.cpp
    test_support.h
  )

  target_link_libraries(splash_gui_smoke_test PRIVATE splash_ui_lib Qt6::Widgets)
  target_include_directories(splash_gui_smoke_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")

  add_test(NAME gui_smoke COMMAND splash_gui_smoke_test -platform offscreen)
endif()
