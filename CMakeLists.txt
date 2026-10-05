cmake_minimum_required(VERSION 3.21)
project(tanara VERSION 0.5.11 LANGUAGES CXX)

# A megjelenített verzió EGY helyről jön: a numerikus rész a project(VERSION)-ből, az utótag
# (pl. "a" = alfa; üres = kiadás) innen. A core/src/Version.cpp ezt kapja TANARA_VERSION-ként.
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

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Widgets Multimedia MultimediaWidgets Test LinguistTools
             OPTIONAL_COMPONENTS DBus)
find_package(Threads REQUIRED)

# third-party (header-only)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_SOURCE_DIR}/third_party/miniaudio)

enable_testing()

option(TANARA_BUILD_GUI "Build the Qt Widgets GUI" ON)
option(TANARA_BUILD_WATCHER "Build the tray meeting-watcher" ON)
# A beszélő-ujjlenyomat (voice-ID) ONNX Runtime-ot + KISS FFT-t igényel. Kikapcsolva
# (OFF) a core/cli/gui ezek nélkül fordul (gyors Windows bring-up); a VoiceEmbedder
# ilyenkor stub (isValid()==false → az app automatikusan kihagyja a beszélő-címkézést).
option(TANARA_BUILD_VOICEID "Build with ONNX-based voice fingerprinting" ON)
# Tanara Cloud (zárt, fizetős szolgáltatás) — a nyílt kliensben tisztán lekapcsolható:
#  TANARA_BUILD_CLOUD  — a bejelentkezős cloud-kliens (tanara-cloud STT/LLM provider,
#                        device flow, becslés, egyenleg). OFF → a kód nem regisztrál semmit
#                        (kill-kritérium). Futásidőben: settings.cloudEnabled / TANARA_CLOUD.
#  TANARA_CLOUD_TEASER — a „Hamarosan” (várólista) panel + a readiness-sor az indulás előtt.
option(TANARA_BUILD_CLOUD "Build the Tanara Cloud client (login, cloud providers)" ON)
option(TANARA_CLOUD_TEASER "Build the Tanara Cloud 'coming soon' waitlist panel" ON)

# Az új (Qt Quick / QML) felülethez — gui/qml, URI „Tanara”. A Svg a Lucide ikonok
# futásidejű színezéséhez/rasztereléséhez kell (kép-provider).
if(TANARA_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Qml Quick QuickControls2 Svg)
endif()

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

# Windows + voice-ID: a helyes onnxruntime.dll-t az exe mellé másoljuk (POST_BUILD).
# A Windows a System32-ben SAJÁT, régebbi onnxruntime.dll-t szállít (Windows ML, ~1.17);
# ha az exe mellett nincs DLL, azt tölti be → "API version [20] is not available", és a
# voice-ID némán elhal. Az add_custom_command(TARGET …) csak a target saját könyvtárában
# hívható, ezért segédfüggvény, amit a cli/gui/watcher/tests CMakeLists hív.
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
if(TANARA_BUILD_GUI)
    add_subdirectory(gui)
endif()
if(TANARA_BUILD_WATCHER)
    add_subdirectory(watcher)
endif()
add_subdirectory(tests)

# i18n — a FORRÁSNYELV MAGYAR, az angol az i18n/tanara_en.ts fordításból épül.
#  - stringek frissítése a .ts-be:  cmake --build build --target update_translations
#  - a .qm build-időben készül (lrelease) és minden futtatható targetbe beágyazódik
#    (:/i18n/tanara_en.qm) — betöltés: tanara::installAppTranslator().
set(TANARA_I18N_TARGETS tanara-cli)
set(TANARA_I18N_SOURCE_TARGETS tanara_core)
if(TANARA_BUILD_GUI)
    list(APPEND TANARA_I18N_TARGETS tanara)
    # A QML-modul (gui/qml) qsTr() szövegei is a közös .ts-be kerülnek; a .qm a tanara exe-be.
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

# Windows-telepítő (Inno Setup, packaging/windows/tanara.iss): a verzió innen jön.
if(WIN32)
    configure_file(${CMAKE_SOURCE_DIR}/packaging/windows/tanara-version.iss.in
                   ${CMAKE_BINARY_DIR}/tanara-version.iss @ONLY)
endif()
