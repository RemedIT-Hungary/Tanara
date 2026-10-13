# tanara_core: the UI-INDEPENDENT backend library.
# RULE: never add a Qt6::Widgets / Qt6::Gui-widget dependency here.
# The linker boundary enforces the UI / backend separation.

file(GLOB_RECURSE TANARA_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE TANARA_CORE_HEADERS CONFIGURE_DEPENDS include/*.h)

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
