# MappingCore(Chkdraft) 를 Splash Editor 가 쓰는 형태로 빌드하는 타겟.
#
# 왜 업스트림 CMakeLists 를 그대로 안 쓰나:
#  - Chkdraft 의 루트 CMakeLists 는 MSVC 전용 플래그(/permissive-)를 무조건 추가하고,
#    vcpkg 기반 find_package(CascLib/Freetype/harfbuzz/glm) 와 Win32 UI 타겟을 끌고 온다.
#  - 우리는 M1 범위(열기/메타/재저장)에 필요한 번역 단위만 필요하다.
#
# 소스 자체는 한 줄도 고치지 않는다 — CHK 파서는 재발명하지 않는다.
#
# lite_scenario.cpp / lite_map_file.cpp 를 쓰지 않는 이유:
#   이름만 보면 3rd-party 용으로 적합해 보이지만, 업스트림 빌드 목록에서 빠져 있어
#   현재 컴파일되지 않는다(LiteScenario 에 REFLECT 선언 누락, 삭제된 ModifiedAsset 타입 참조).
#   따라서 Chkdraft 본체가 실제로 빌드·검증하는 scenario.cpp / map_file.cpp 경로를 쓴다.

set(MC_SRC "${chkdraft_SOURCE_DIR}/src")
set(MC_DIR "${MC_SRC}/mapping_core")

if(NOT EXISTS "${MC_DIR}/map_file.cpp")
  message(FATAL_ERROR
    "MappingCore 소스를 찾을 수 없습니다: ${MC_DIR}\n"
    "Chkdraft 업스트림 레이아웃이 바뀌었을 수 있습니다.")
endif()

add_library(splash_mappingcore STATIC
  # --- cross_cut: MappingCore 가 의존하는 로깅/인코딩 헬퍼 ---
  "${MC_SRC}/cross_cut/logger.cpp"
  "${MC_SRC}/cross_cut/simple_icu.cpp"

  # --- 기반 ---
  "${MC_DIR}/basics.cpp"
  "${MC_DIR}/sha256.cpp"
  "${MC_DIR}/escape_strings.cpp"
  "${MC_DIR}/system_io.cpp"

  # --- 아카이브 계층 (MPQ) ---
  "${MC_DIR}/archive_file.cpp"
  "${MC_DIR}/archive_cluster.cpp"
  "${MC_DIR}/folder_archive.cpp"
  "${MC_DIR}/mpq_file.cpp"
  "${MC_DIR}/casc_archive.cpp"
  "${MC_DIR}/mpq_asset_manager.cpp"
  "${MC_DIR}/file_browser.cpp"

  # --- StarCraft 정적 데이터 ---
  # 유닛 기본 이름 테이블 등 scenario.cpp 가 참조하는 정적 테이블이 여기 있다.
  # 게임 에셋(MPQ/CASC) 로딩 코드도 같은 파일에 들어 있지만, 우리가 호출하지
  # 않는 한 링크되지 않는다 — M1 에 CascLib 은 필요 없다.
  "${MC_DIR}/sc.cpp"

  # --- 유닛 애니메이션 (iscript) ---
  # 유닛 하나는 여러 이미지 오버레이로 구성된다(본체 + 그림자 + 부가).
  # 그 조립은 iscript 가 정한다. 이 계층은 OpenGL 에 의존하지 않는다.
  "${MC_DIR}/render/map_image.cpp"
  "${MC_DIR}/render/map_actor.cpp"
  "${MC_DIR}/render/map_animations.cpp"

  # --- CHK / 시나리오 계층 ---
  "${MC_DIR}/chk.cpp"
  "${MC_DIR}/scenario.cpp"
  "${MC_DIR}/scenario_print.cpp"
  "${MC_DIR}/scenario_undo.cpp"
  "${MC_DIR}/scenario_redo.cpp"
  "${MC_DIR}/map_file.cpp"

  # --- 트리거를 사람이 읽는 텍스트로 (M5) ---
  "${MC_DIR}/text_trig_generator.cpp"
  "${MC_DIR}/text_trig_compiler.cpp"
)

target_include_directories(splash_mappingcore SYSTEM PUBLIC
  "${MC_SRC}"
  "${MC_DIR}"
  "${rarecpp_SOURCE_DIR}/include"
)

target_compile_features(splash_mappingcore PUBLIC cxx_std_20)

# MappingCore 는 내부적으로 UNICODE 빌드를 전제한다. 비Windows 에서는
# simple_icu.h 가 UTF-8 경로를 택하므로 무해하지만 정의는 맞춰 둔다.
target_compile_definitions(splash_mappingcore PUBLIC
  UNICODE
  _UNICODE
  __STORMLIB_NO_STATIC_LINK__
)

target_include_directories(splash_mappingcore SYSTEM PRIVATE
  "${casclib_SOURCE_DIR}/src"
)

target_link_libraries(splash_mappingcore
  PRIVATE StormLib::storm casc_static ICU::uc ICU::i18n ICU::data
)

# 업스트림 서드파티 소스의 경고는 우리 코드 품질 신호를 가린다.
if(NOT MSVC)
  target_compile_options(splash_mappingcore PRIVATE -w)
endif()
