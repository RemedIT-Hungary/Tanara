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

# kaldi-native-fbank (vendorelt, Apache-2.0) — kaldi-pontos fbank a beszélő-embeddinghez.
# Az FFT-t a rendszer kissfft-float adja (a knf rfft.cc a kiss_fftr-t float-tal hívja).
find_package(PkgConfig REQUIRED)
pkg_check_modules(KISSFFT REQUIRED IMPORTED_TARGET kissfft-float)
file(GLOB KNF_SOURCES ${CMAKE_SOURCE_DIR}/third_party/kaldi-native-fbank/csrc/*.cc)
add_library(kaldi_native_fbank STATIC ${KNF_SOURCES})
target_include_directories(kaldi_native_fbank PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
target_compile_features(kaldi_native_fbank PUBLIC cxx_std_17)
target_link_libraries(kaldi_native_fbank PRIVATE PkgConfig::KISSFFT)
set_target_properties(kaldi_native_fbank PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)   # nem Qt-cél, ne fusson rajta MOC

# onnxruntime (Fedora onnxruntime-devel; cmake-config) — beszélő-embedding modellhez.
find_package(onnxruntime REQUIRED)

enable_testing()

option(TANARA_BUILD_GUI "Build the Qt Widgets GUI" ON)

add_subdirectory(core)
add_subdirectory(cli)
if(TANARA_BUILD_GUI)
    add_subdirectory(gui)
endif()
add_subdirectory(tests)
