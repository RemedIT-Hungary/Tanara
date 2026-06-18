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
target_link_libraries(tanara_core PRIVATE miniaudio ${CMAKE_DL_LIBS} Threads::Threads m)
