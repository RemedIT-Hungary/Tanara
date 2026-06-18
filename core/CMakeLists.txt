# tanara_core — UI-FÜGGETLEN backend könyvtár.
# SZABÁLY: ide SOHA ne kerüljön Qt6::Widgets / Qt6::Gui-widget függőség.
# A linker-határ kényszeríti ki az UI ⟂ backend szétválasztást.

file(GLOB_RECURSE TANARA_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE TANARA_CORE_HEADERS CONFIGURE_DEPENDS include/*.h)

add_library(tanara_core STATIC ${TANARA_CORE_SOURCES} ${TANARA_CORE_HEADERS})

target_include_directories(tanara_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(tanara_core PUBLIC cxx_std_20)

# Csak headless/backend Qt-modulok. Widgets TILOS.
target_link_libraries(tanara_core PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)
# miniaudio + ffmpeg(QProcess) az audio-modulhoz (a capture impl használja).
# miniaudio Linuxon dlopen-nel tölti a backendeket → dl + pthread + m kell.
# A libm csak a POSIX-toolchaineken különálló (MSVC-n nincs); MinGW-n is létezik.
target_link_libraries(tanara_core PRIVATE miniaudio ${CMAKE_DL_LIBS} Threads::Threads)
if(NOT MSVC)
    target_link_libraries(tanara_core PRIVATE m)
endif()
# Windowson a miniaudio WASAPI/DirectSound backend COM-ot használ → ole32/oleaut32;
# az MMDevice enumerációhoz uuid; a WinMM backendhez winmm. MSVC ezeket #pragma
# comment-tel linkeli, MinGW-n explicit kell.
if(WIN32)
    target_link_libraries(tanara_core PRIVATE ole32 oleaut32 uuid winmm avrt ksuser)
endif()

# Beszélő-embedding (opcionális): kaldi-native-fbank (fbank) + onnxruntime (CAM++ modell).
if(TANARA_BUILD_VOICEID)
    target_compile_definitions(tanara_core PRIVATE TANARA_HAVE_VOICEID)
    target_link_libraries(tanara_core PRIVATE kaldi_native_fbank onnxruntime::onnxruntime)
endif()
