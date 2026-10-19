# One source list: the registry, the per-module command files (commands/) and the legacy-people helper.
file(GLOB TANARA_CLI_COMMAND_SOURCES CONFIGURE_DEPENDS commands/*.cpp commands/*.h)
add_executable(tanara-cli main.cpp CommandRegistry.cpp CommandRegistry.h ${TANARA_CLI_COMMAND_SOURCES}
    legacy/LegacyPeople.cpp legacy/LegacyPeople.h)
target_include_directories(tanara-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tanara-cli PRIVATE tanara_core)
tanara_copy_onnxruntime_dll(tanara-cli)
if(APPLE)   # usage strings for a build outside the bundle (platform::becomeResponsibleProcess)
    target_link_options(tanara-cli PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_SOURCE_DIR}/packaging/macos/embedded-Info.plist")
endif()
