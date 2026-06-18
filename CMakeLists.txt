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

add_subdirectory(core)
add_subdirectory(cli)
if(TANARA_BUILD_GUI)
    add_subdirectory(gui)
endif()
add_subdirectory(tests)
