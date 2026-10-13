cmake_minimum_required(VERSION 3.21)
project(tanara VERSION 0.5.11 LANGUAGES CXX)

# The displayed version comes from ONE place: the numeric part from project(VERSION), the
# suffix (e.g. "a" = alpha; empty = release) from here. core/src/Version.cpp receives it as
# TANARA_VERSION.
set(TANARA_VERSION_SUFFIX "")
set(TANARA_VERSION "${PROJECT_VERSION}${TANARA_VERSION_SUFFIX}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Sql Test LinguistTools)
find_package(Threads REQUIRED)

# third-party (header-only)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_SOURCE_DIR}/third_party/miniaudio)

enable_testing()

option(TANARA_BUILD_GUI "Build the Qt Widgets GUI" ON)
option(TANARA_BUILD_WATCHER "Build the tray meeting-watcher" ON)
# Voice-ID needs ONNX Runtime + KISS FFT. When OFF, core/cli/gui build without them (fast
# Windows bring-up); VoiceEmbedder is then a stub (isValid() == false, so the app skips
# speaker labeling automatically).
option(TANARA_BUILD_VOICEID "Build with ONNX-based voice fingerprinting" ON)
# Tanara Cloud (closed, paid service): can be switched off cleanly in the open client:
#  TANARA_BUILD_CLOUD  - the sign-in cloud client (tanara-cloud STT/LLM provider, device
#                        flow, estimate, balance). OFF: the code registers nothing.
#                        At runtime: settings.cloudEnabled / TANARA_CLOUD.
#  TANARA_CLOUD_TEASER - the "Coming soon" (waitlist) panel + the readiness row before startup.
option(TANARA_BUILD_CLOUD "Build the Tanara Cloud client (login, cloud providers)" ON)
option(TANARA_CLOUD_TEASER "Build the Tanara Cloud 'coming soon' waitlist panel" ON)

# Each target asks for its own Qt components: gui/CMakeLists.txt (Widgets, Multimedia, Qml, Quick,
# QuickControls2, Svg) and watcher/CMakeLists.txt (Widgets, optional DBus). The core + CLI need
# neither Widgets nor Multimedia.
if(TANARA_BUILD_VOICEID)
    # kaldi-native-fbank (vendored, Apache-2.0): Kaldi-accurate fbank for the speaker embedding.
    # The FFT comes from kissfft-float (knf's rfft.cc calls kiss_fftr with float).
    # kissfft-float is built from the vendored source (third_party/kissfft) on every platform.
    # The project is CXX-only by default, so C must be enabled for the .c files (otherwise
    # CMake silently skips them).
    enable_language(C)
    set(KISSFFT_DIR ${CMAKE_SOURCE_DIR}/third_party/kissfft)
    add_library(kissfft_float STATIC
        ${KISSFFT_DIR}/kiss_fft.c
        ${KISSFFT_DIR}/kiss_fftr.c)
    target_include_directories(kissfft_float PUBLIC ${KISSFFT_DIR})
    target_compile_definitions(kissfft_float PUBLIC kiss_fft_scalar=float)
    set_target_properties(kissfft_float PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)   # plain C lib, no MOC
    add_library(tanara::kissfft ALIAS kissfft_float)

    file(GLOB KNF_SOURCES ${CMAKE_SOURCE_DIR}/third_party/kaldi-native-fbank/csrc/*.cc)
    add_library(kaldi_native_fbank STATIC ${KNF_SOURCES})
    target_include_directories(kaldi_native_fbank PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
    target_compile_features(kaldi_native_fbank PUBLIC cxx_std_17)
    target_link_libraries(kaldi_native_fbank PRIVATE tanara::kissfft)
    set_target_properties(kaldi_native_fbank PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)   # not a Qt target, no MOC

    # onnxruntime: for the speaker embedding model.
    #  - Linux (Fedora): onnxruntime-devel cmake config, via find_package.
    #  - Windows: pass the official win-x64 release with -DONNXRUNTIME_ROOT_DIR=...; an imported
    #    target is built from it (cmake/OnnxRuntimeWin.cmake).
    if(WIN32 AND ONNXRUNTIME_ROOT_DIR)
        include(${CMAKE_SOURCE_DIR}/cmake/OnnxRuntimeWin.cmake)
    else()
        find_package(onnxruntime REQUIRED)
    endif()
endif()

# Windows + voice-ID: copy the right onnxruntime.dll next to the exe (POST_BUILD).
# Windows ships its OWN, older onnxruntime.dll in System32 (Windows ML, ~1.17); if there is
# no DLL next to the exe, that one is loaded ("API version [20] is not available") and
# voice-ID dies silently. add_custom_command(TARGET ...) works only in the target's own
# directory, hence a helper function called from the cli/gui/watcher/tests CMakeLists.
function(tanara_copy_onnxruntime_dll target)
    if(WIN32 AND TANARA_BUILD_VOICEID AND ONNXRUNTIME_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ONNXRUNTIME_DLL}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

add_subdirectory(core)
add_subdirectory(cli)
if(TANARA_BUILD_GUI OR TANARA_BUILD_WATCHER)
    add_subdirectory(gui/common)    # header-only helpers shared by the GUI and the watcher
endif()
if(TANARA_BUILD_GUI)
    add_subdirectory(gui)
endif()
if(TANARA_BUILD_WATCHER)
    add_subdirectory(watcher)
endif()
add_subdirectory(tests)

# i18n: the SOURCE LANGUAGE IS HUNGARIAN; English is built from the i18n/tanara_en.ts translation.
#  - update strings in the .ts:  cmake --build build --target update_translations
#  - the .qm is built at build time (lrelease) and embedded in every executable target
#    (:/i18n/tanara_en.qm); loaded with tanara::installAppTranslator().
set(TANARA_I18N_TARGETS tanara-cli)
set(TANARA_I18N_SOURCE_TARGETS tanara_core)
if(TANARA_BUILD_GUI)
    list(APPEND TANARA_I18N_TARGETS tanara)
    # The qsTr() strings of the QML module (gui/qml) go into the shared .ts too; the .qm goes into the tanara exe.
    list(APPEND TANARA_I18N_SOURCE_TARGETS tanara_qml)
endif()
if(TANARA_BUILD_WATCHER)
    list(APPEND TANARA_I18N_TARGETS tanara-watcher)
endif()
qt_add_translations(TARGETS ${TANARA_I18N_TARGETS}
    SOURCE_TARGETS ${TANARA_I18N_SOURCE_TARGETS} ${TANARA_I18N_TARGETS}
    TS_FILES ${CMAKE_SOURCE_DIR}/i18n/tanara_en.ts
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations none)

# Windows installer (Inno Setup, packaging/windows/tanara.iss): the version comes from here.
if(WIN32)
    configure_file(${CMAKE_SOURCE_DIR}/packaging/windows/tanara-version.iss.in
                   ${CMAKE_BINARY_DIR}/tanara-version.iss @ONLY)
endif()
