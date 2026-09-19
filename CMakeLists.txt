cmake_minimum_required(VERSION 3.25)

project(SplashEditor
  VERSION     0.1.0
  DESCRIPTION "StarCraft: Brood War / Remastered map editor"
  LANGUAGES   C CXX
)

# MappingCore 의 chk.h 는 RareCpp 리플렉션을 쓰므로 C++20 이 필요하다.
# (닫힌 제약은 C++17 이상 — 20 은 그 안에 든다.)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "빌드 타입" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SPLASH_BUILD_GUI   "Qt Widgets GUI 를 빌드한다"        ON)
option(SPLASH_BUILD_TESTS "테스트를 빌드한다"                  ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/SplashDependencies.cmake)
include(cmake/SplashMappingCore.cmake)

# 코어(Qt 비의존) -> UI 순서. 코어 타겟은 Qt 를 링크하지 않는다.
add_subdirectory(src/io)
add_subdirectory(src/chk)
add_subdirectory(src/cli)

if(SPLASH_BUILD_GUI)
  add_subdirectory(src/ui)
endif()

if(SPLASH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "Splash Editor ${PROJECT_VERSION}")
message(STATUS "  빌드 타입 : ${CMAKE_BUILD_TYPE}")
message(STATUS "  GUI       : ${SPLASH_BUILD_GUI}")
message(STATUS "  테스트    : ${SPLASH_BUILD_TESTS}")
message(STATUS "")
