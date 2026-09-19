# Qt Widgets GUI. 단일 QMainWindow — 도킹/멀티맵은 M2 이후.
#
# Qt 는 LGPLv3 로 사용하므로 반드시 동적 링크한다(정적 링크 금지).
# 자세한 내용은 README 의 라이선스 절을 참고.

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

qt_standard_project_setup()

qt_add_executable(splash-editor
  main.cpp
  main_window.cpp
  main_window.h
  map_view.cpp
  map_view.h
  tile_palette.cpp
  tile_palette.h
  unit_palette.cpp
  unit_palette.h
  mini_map.cpp
  mini_map.h
  code_editor.cpp
  code_editor_pane.cpp
  code_editor_pane.h
  code_editor.h
  briefing_editor.cpp
  briefing_editor.h
  location_editor.cpp
  location_editor.h
  settings_dialogs.cpp
  sound_editor.cpp
  sound_editor.h
  string_editor.cpp
  switch_editor.cpp
  switch_editor.h
  string_editor.h
  settings_dialogs.h
  trigger_argument_panel.cpp
  trigger_argument_panel.h
  trigger_editor.cpp
  trigger_editor.h
  sound_player.cpp
  sound_player.h
)

target_link_libraries(splash-editor PRIVATE splash_core Qt6::Widgets Qt6::Multimedia)

target_compile_definitions(splash-editor PRIVATE
  SPLASH_VERSION="${PROJECT_VERSION}"
)

set_target_properties(splash-editor PROPERTIES
  MACOSX_BUNDLE FALSE
  WIN32_EXECUTABLE TRUE
)

if(NOT MSVC)
  target_compile_options(splash-editor PRIVATE -Wall -Wextra)
endif()

# Qt 를 동적 링크로 쓰고 있는지 확인한다. LGPLv3 준수의 핵심 조건이므로
# 실수로 정적 Qt 에 링크되면 빌드 시점에 알아야 한다.
get_target_property(_qt_core_type Qt6::Core TYPE)
if(_qt_core_type STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR
    "정적 Qt 가 감지되었습니다. Splash Editor 는 Qt 를 LGPLv3 하에 동적 링크로만 사용합니다.\n"
    "공유 라이브러리 Qt 빌드를 사용하세요.")
endif()
