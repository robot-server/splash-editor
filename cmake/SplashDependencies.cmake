# 외부 의존성 확보.
#
# 정책: 복붙 벤더링 대신 FetchContent 로 커밋 고정(pinned) 취득한다.
# 고정 해시를 쓰는 이유는 CHK 파싱 동작이 업스트림 변경으로 조용히 바뀌면
# round-trip 회귀를 추적할 수 없기 때문이다.

include(FetchContent)

# ---------------------------------------------------------------------------
# RareCpp — MappingCore 의 chk.h 가 요구하는 헤더 온리 리플렉션 라이브러리.
# Chkdraft 상위 CMakeLists 가 지정한 것과 동일한 커밋(release-2.6.2)을 쓴다.
# ---------------------------------------------------------------------------
# 헤더 온리이므로 소스만 받고 업스트림 CMakeLists 는 실행하지 않는다.
# (그쪽은 INTERFACE 타겟을 'Root' 라는 충돌하기 쉬운 이름으로 만들고,
#  -pedantic-errors -Wconversion 을 디렉터리 스코프로 추가한다.)
FetchContent_Declare(rarecpp
  GIT_REPOSITORY https://github.com/TheNitesWhoSay/RareCpp.git
  GIT_TAG        67de40ea8bf05f2bb13ca01d97a7309b7f5ac3ac # release-2.6.2
  GIT_SHALLOW    FALSE
  SOURCE_SUBDIR  cmake-entry-point-that-does-not-exist
)

# ---------------------------------------------------------------------------
# StormLib — MPQ 아카이브 입출력. .scm/.scx 는 MPQ 컨테이너다.
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(STORM_SKIP_INSTALL ON  CACHE BOOL "" FORCE)
set(STORM_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
# 번들 libtommath/libtomcrypt 를 쓴다. 시스템 패키지를 요구하면 플랫폼마다
# 사전 설치 목록이 늘어나고, macOS 에는 기본 제공되지 않는다.
set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "" FORCE)
FetchContent_Declare(stormlib
  GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
  GIT_TAG        44ebfbfc109d76e2a85bbd5d8b0c949df7e65c6f # v9.30
  GIT_SHALLOW    FALSE
)

# ---------------------------------------------------------------------------
# CascLib — 리마스터 게임 설치 폴더의 에셋(CASC) 접근.
#
# M1 기능에는 쓰지 않는다. 그럼에도 링크하는 이유: MappingCore 의 sc.cpp 에
# scenario.cpp 가 필요로 하는 유닛 기본 이름 테이블과 CASC 접근 코드가 같은
# 번역 단위에 들어 있어, 정적 테이블만 떼어 올 수 없다.
# 어차피 M2 그래픽에서 필요한 의존성이므로 지금 배선해 둔다. (MIT)
#
# 업스트림이 cmake_minimum_required(3.2) 라 CMake 4.x 가 거부한다.
# 정책 최소 버전을 올려 주되, 우리 프로젝트 설정은 건드리지 않도록 복원한다.
set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_STATIC_LIB ON  CACHE BOOL "" FORCE)
set(CASC_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(SPLASH_POLICY_MIN_SAVED "${CMAKE_POLICY_VERSION_MINIMUM}")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(casclib
  GIT_REPOSITORY https://github.com/ladislav-zezula/CascLib.git
  GIT_TAG        2a280f5a231966dc5d1b534978dd9f9f04a374cd
  GIT_SHALLOW    FALSE
)

# ---------------------------------------------------------------------------
# Chkdraft — MappingCore 의 출처. 소스만 가져오고 Chkdraft 의 CMakeLists 는
# 실행하지 않는다(그쪽은 MSVC 전용 플래그와 Win32 UI 타겟을 끌고 온다).
# SOURCE_SUBDIR 를 CMakeLists.txt 가 없는 경로로 지정해 add_subdirectory 를 건너뛴다.
# 필요한 번역 단위만 골라 cmake/SplashMappingCore.cmake 에서 직접 타겟을 만든다.
# ---------------------------------------------------------------------------
FetchContent_Declare(chkdraft
  GIT_REPOSITORY https://github.com/TheNitesWhoSay/Chkdraft.git
  GIT_TAG        32d27861b16dda0b0f3d95e34bad894ea4efb2c3
  GIT_SHALLOW    FALSE
  SOURCE_SUBDIR  cmake-entry-point-that-does-not-exist
)

FetchContent_MakeAvailable(rarecpp stormlib casclib chkdraft)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
set(CMAKE_POLICY_VERSION_MINIMUM "${SPLASH_POLICY_MIN_SAVED}")

# ---------------------------------------------------------------------------
# ICU — cross_cut/simple_icu.cpp 가 UTF-8/UTF-16 변환에 사용한다.
# Homebrew 의 icu4c 는 keg-only 라 CMake 기본 탐색 경로에 없다. 힌트를 만들어 준다.
# ---------------------------------------------------------------------------
if(APPLE AND NOT DEFINED ICU_ROOT)
  find_program(SPLASH_BREW_EXECUTABLE brew)
  if(SPLASH_BREW_EXECUTABLE)
    foreach(icu_formula icu4c@78 icu4c@77 icu4c@76 icu4c)
      execute_process(
        COMMAND "${SPLASH_BREW_EXECUTABLE}" --prefix ${icu_formula}
        OUTPUT_VARIABLE icu_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(icu_prefix AND EXISTS "${icu_prefix}")
        set(ICU_ROOT "${icu_prefix}" CACHE PATH "Homebrew ICU prefix (자동 탐지)")
        message(STATUS "Splash: ICU 자동 탐지 -> ${ICU_ROOT}")
        break()
      endif()
    endforeach()
  endif()
endif()

find_package(ICU REQUIRED COMPONENTS uc i18n data)
