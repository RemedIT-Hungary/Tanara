# Qt components of the GUI. Svg is needed for runtime tinting and rasterizing of the Lucide icons
# (image provider).
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Multimedia Qml Quick QuickControls2 Svg)

# The Qt Quick / QML user interface (URI "Tanara"): a static module, see gui/qml/README.md.
add_subdirectory(qml)

file(GLOB_RECURSE TANARA_GUI_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h src/*.ui)

add_executable(tanara ${TANARA_GUI_SOURCES})

# On Windows a GUI application (not the console subsystem): no black console window opens
# next to it, and closing that window cannot kill the app. The WinMain -> main entry comes from
# Qt6::EntryPoint (pulled in by Qt6::Core on Windows). A no-op on other platforms.
# (tanara-cli stays a console application ON PURPOSE.)
set_target_properties(tanara PROPERTIES WIN32_EXECUTABLE ON)

# The files under gui/src include each other by plain name: src/ is the include root.
target_include_directories(tanara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(tanara PRIVATE
    tanara_core
    tanara_qml
    tanara_qmlplugin      # the plugin side of the static QML module (type registration + qmldir)
    Qt6::Widgets
    Qt6::Multimedia
    tanara_shell_common
)

tanara_copy_onnxruntime_dll(tanara)
