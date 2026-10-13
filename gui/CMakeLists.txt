# Qt components of the GUI. Svg is needed for runtime tinting and rasterizing of the Lucide icons
# (image provider).
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Multimedia Qml Quick QuickControls2 Svg)

# Az új Qt Quick / QML felület (URI „Tanara”) — statikus modul, lásd gui/qml/README.md.
add_subdirectory(qml)

file(GLOB_RECURSE TANARA_GUI_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h src/*.ui)

add_executable(tanara ${TANARA_GUI_SOURCES})

# Windowson GUI-alkalmazás (ne console-subsystem) → ne nyíljon mellé fekete
# konzolablak, és a bezárása ne állítsa le az appot. A WinMain→main belépőt a
# Qt6::EntryPoint adja (a Qt6::Core hozza Windowson). Nem-Windowson ez no-op.
# (A tanara-cli SZÁNDÉKOSAN marad konzolos.)
set_target_properties(tanara PROPERTIES WIN32_EXECUTABLE ON)

# A gui/src fájljai egymást sima névvel includolják: a src/ az include-gyökér.
target_include_directories(tanara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(tanara PRIVATE
    tanara_core
    tanara_qml
    tanara_qmlplugin      # a statikus QML-modul plugin-oldala (típus-regisztráció + qmldir)
    Qt6::Widgets
    Qt6::Multimedia
    tanara_shell_common
)

tanara_copy_onnxruntime_dll(tanara)
