# Header-only helpers shared by the GUI and the tray watcher (app icon, tray icon). Both
# executables link this target instead of reaching into gui/src.
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)

add_library(tanara_shell_common INTERFACE)
target_include_directories(tanara_shell_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tanara_shell_common INTERFACE Qt6::Widgets)
