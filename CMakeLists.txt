cmake_minimum_required(VERSION 3.21)
project(tanara VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Widgets Multimedia MultimediaWidgets Test)
find_package(Threads REQUIRED)

# third-party (header-only)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_SOURCE_DIR}/third_party/miniaudio)

enable_testing()

option(TANARA_BUILD_GUI "Build the Qt Widgets GUI" ON)
# A beszélő-ujjlenyomat (voice-ID) ONNX Runtime-ot + KISS FFT-t igényel. Kikapcsolva
# (OFF) a core/cli/gui ezek nélkül fordul (gyors Windows bring-up); a VoiceEmbedder
# ilyenkor stub (isValid()==false → az app automatikusan kihagyja a beszélő-címkézést).
option(TANARA_BUILD_VOICEID "Build with ONNX-based voice fingerprinting" ON)

if(TANARA_BUILD_VOICEID)
    # kaldi-native-fbank (vendorelt, Apache-2.0) — kaldi-pontos fbank a beszélő-embeddinghez.
    # Az FFT-t a kissfft-float adja (a knf rfft.cc a kiss_fftr-t float-tal hívja).
    if(WIN32)
        # Windowson nincs pkg-config; a kissfft-float-ot forrásból fordítjuk
        # (vendorelt, third_party/kissfft). A projekt alapból csak CXX — a .c
        # fájlokhoz engedélyezni kell a C nyelvet (különben CMake némán kihagyja).
        enable_language(C)
        set(KISSFFT_DIR ${CMAKE_SOURCE_DIR}/third_party/kissfft)
        add_library(kissfft_float STATIC
            ${KISSFFT_DIR}/kiss_fft.c
            ${KISSFFT_DIR}/kiss_fftr.c)
        target_include_directories(kissfft_float PUBLIC ${KISSFFT_DIR})
        target_compile_definitions(kissfft_float PUBLIC kiss_fft_scalar=float)
        set_target_properties(kissfft_float PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)   # tiszta C-lib, nincs MOC
        add_library(tanara::kissfft ALIAS kissfft_float)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(KISSFFT REQUIRED IMPORTED_TARGET kissfft-float)
        add_library(tanara::kissfft ALIAS PkgConfig::KISSFFT)
    endif()

    file(GLOB KNF_SOURCES ${CMAKE_SOURCE_DIR}/third_party/kaldi-native-fbank/csrc/*.cc)
    add_library(kaldi_native_fbank STATIC ${KNF_SOURCES})
    target_include_directories(kaldi_native_fbank PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
    target_compile_features(kaldi_native_fbank PUBLIC cxx_std_17)
    target_link_libraries(kaldi_native_fbank PRIVATE tanara::kissfft)
    set_target_properties(kaldi_native_fbank PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)   # nem Qt-cél, ne fusson rajta MOC

    # onnxruntime — beszélő-embedding modellhez.
    #  - Linux (Fedora): onnxruntime-devel cmake-config → find_package.
    #  - Windows: a hivatalos win-x64 release-t a -DONNXRUNTIME_ROOT_DIR=... -rel
    #    adjuk meg; abból import-target épül (cmake/OnnxRuntimeWin.cmake).
    if(WIN32 AND ONNXRUNTIME_ROOT_DIR)
        include(${CMAKE_SOURCE_DIR}/cmake/OnnxRuntimeWin.cmake)
    else()
        find_package(onnxruntime REQUIRED)
    endif()
endif()

add_subdirectory(core)
add_subdirectory(cli)
if(TANARA_BUILD_GUI)
    add_subdirectory(gui)
endif()
add_subdirectory(tests)
