# tanara_core: the UI-INDEPENDENT backend library.
# RULE: never add a Qt6::Widgets / Qt6::Gui-widget dependency here.
# The linker boundary enforces the UI / backend separation.

file(GLOB_RECURSE TANARA_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE TANARA_CORE_HEADERS CONFIGURE_DEPENDS include/*.h)

# Platform adapters: every */platform/<os>/ folder holds one platform's implementation of a port
# (no `#if Q_OS_*` in the common code, see tools/check_modules.py). Only the current platform's
# folders are compiled:
#   Linux: linux + posix | Windows: windows | otherwise (macOS until docs/macos.md): posix + unsupported
if(WIN32)
    set(TANARA_PLATFORM_DIRS windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TANARA_PLATFORM_DIRS linux posix)
else()
    set(TANARA_PLATFORM_DIRS posix unsupported)
endif()
foreach(_list TANARA_CORE_SOURCES TANARA_CORE_HEADERS)
    set(_all ${${_list}})
    list(FILTER ${_list} EXCLUDE REGEX "/platform/[^/]+/")
    foreach(_dir IN LISTS TANARA_PLATFORM_DIRS)
        set(_mine ${_all})
        list(FILTER _mine INCLUDE REGEX "/platform/${_dir}/")
        list(APPEND ${_list} ${_mine})
    endforeach()
endforeach()
message(STATUS "Tanara: platform adapter folders: ${TANARA_PLATFORM_DIRS}")

add_library(tanara_core STATIC ${TANARA_CORE_SOURCES} ${TANARA_CORE_HEADERS})

target_include_directories(tanara_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The version comes from the root CMakeLists (TANARA_VERSION); only Version.cpp gets it, so a version bump
# recompiles a single file.
set_source_files_properties(src/foundation/Version.cpp PROPERTIES
    COMPILE_DEFINITIONS "TANARA_VERSION=\"${TANARA_VERSION}\"")
target_compile_features(tanara_core PUBLIC cxx_std_20)

# Headless/backend Qt modules only. Widgets are FORBIDDEN.
target_link_libraries(tanara_core PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)
# miniaudio + ffmpeg (QProcess) for the audio module (used by the capture implementation).
# On Linux miniaudio loads its backends with dlopen, so dl + pthread + m are needed.
# libm is separate only on POSIX toolchains (none on MSVC); it exists on MinGW too.
target_link_libraries(tanara_core PRIVATE miniaudio ${CMAKE_DL_LIBS} Threads::Threads)
if(NOT MSVC)
    target_link_libraries(tanara_core PRIVATE m)
endif()
# On Windows miniaudio's WASAPI/DirectSound backend uses COM, so ole32/oleaut32;
# uuid for MMDevice enumeration; winmm for the WinMM backend. MSVC links these with #pragma
# comment; on MinGW they must be explicit.
if(WIN32)
    target_link_libraries(tanara_core PRIVATE ole32 oleaut32 uuid winmm avrt ksuser secur32)
endif()

# miniz (vendored, MIT, third_party/miniz): ZIP writing/reading for the meeting archive
# (store/MeetingArchive). Pure C, so the C language must be enabled; the zlib-compatible
# names are switched off so they do not collide with the zlib pulled in by Qt.
enable_language(C)
add_library(tanara_miniz STATIC ${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c)
target_include_directories(tanara_miniz PUBLIC ${CMAKE_SOURCE_DIR}/third_party/miniz)
target_compile_definitions(tanara_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_compile_definitions(tanara_miniz PRIVATE _LARGEFILE64_SOURCE=1)
set_target_properties(tanara_miniz PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)   # tiszta C-lib, nincs MOC
target_link_libraries(tanara_core PRIVATE tanara_miniz)

# Speaker embedding (optional): kaldi-native-fbank (fbank) + onnxruntime (CAM++ model).
if(TANARA_BUILD_VOICEID)
    target_compile_definitions(tanara_core PRIVATE TANARA_HAVE_VOICEID)
    target_link_libraries(tanara_core PRIVATE kaldi_native_fbank onnxruntime::onnxruntime)
endif()

# Tanara Cloud compile switches (see the root CMakeLists.txt).
if(TANARA_BUILD_CLOUD)
    target_compile_definitions(tanara_core PRIVATE TANARA_HAVE_CLOUD)
endif()
if(TANARA_CLOUD_TEASER)
    target_compile_definitions(tanara_core PRIVATE TANARA_HAVE_CLOUD_TEASER)
endif()

# OS keychain for the secrets (foundation/secrets, QtKeychain, BSD-3-Clause).
#  - Linux: the distro package (Fedora: qtkeychain-qt6-devel; Debian/Ubuntu: qtkeychain-qt6-dev).
#  - Windows: if no installed package is found, FetchContent builds a pinned QtKeychain release
#    (the Credential Manager backend needs no extra dependency).
#  - Otherwise: built without TANARA_HAVE_KEYCHAIN (the 0600 secrets.json file backend only).
option(TANARA_USE_KEYCHAIN "Store secrets in the OS keychain (QtKeychain) when available" ON)
if(TANARA_USE_KEYCHAIN)
    find_package(Qt6Keychain CONFIG QUIET)
    if(NOT Qt6Keychain_FOUND AND WIN32)
        include(FetchContent)
        set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
        set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
        set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
        set(QTKEYCHAIN_STATIC ON CACHE BOOL "" FORCE)
        FetchContent_Declare(qtkeychain
            GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
            GIT_TAG 0.16.0)   # same as Fedora 44 qtkeychain-qt6
        FetchContent_MakeAvailable(qtkeychain)
        if(TARGET qt6keychain AND NOT TARGET Qt6Keychain::Qt6Keychain)
            add_library(Qt6Keychain::Qt6Keychain ALIAS qt6keychain)
        endif()
        # FetchContent puts the headers under <src>; the code includes <qt6keychain/keychain.h>.
        set(_tanara_kc_inc ${CMAKE_BINARY_DIR}/qtkeychain-include)
        file(MAKE_DIRECTORY ${_tanara_kc_inc}/qt6keychain)
        file(GLOB _tanara_kc_headers ${qtkeychain_SOURCE_DIR}/qtkeychain/*.h)
        file(COPY ${_tanara_kc_headers} DESTINATION ${_tanara_kc_inc}/qt6keychain)
        file(GLOB _tanara_kc_gen ${qtkeychain_BINARY_DIR}/qtkeychain/*.h ${qtkeychain_BINARY_DIR}/*.h)
        if(_tanara_kc_gen)
            file(COPY ${_tanara_kc_gen} DESTINATION ${_tanara_kc_inc}/qt6keychain)
        endif()
        target_include_directories(tanara_core PRIVATE ${_tanara_kc_inc})
    endif()
    if(TARGET Qt6Keychain::Qt6Keychain)
        target_compile_definitions(tanara_core PRIVATE TANARA_HAVE_KEYCHAIN)
        target_link_libraries(tanara_core PRIVATE Qt6Keychain::Qt6Keychain)
        message(STATUS "Tanara: OS keychain (QtKeychain) enabled")
    else()
        message(STATUS "Tanara: QtKeychain not found - secrets in the 0600 file only")
    endif()
endif()
