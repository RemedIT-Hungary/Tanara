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

# macOS app bundle (TANARA_MACOS_BUNDLE, docs/BUILD.md "macOS app bundle"): tanara.app with the
# CLI and the watcher next to the GUI in Contents/MacOS (findWatcherExecutable looks there first).
# tanara_bundle copies Qt, the QML imports and the Homebrew dylibs into the bundle and signs it ad hoc.
if(APPLE AND TANARA_MACOS_BUNDLE)
    set_target_properties(tanara PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME Tanara
        MACOSX_BUNDLE_GUI_IDENTIFIER eu.remedit.Tanara
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER eu.remedit.Tanara)

    set(_helpers tanara-cli)
    if(TANARA_BUILD_WATCHER)
        list(APPEND _helpers tanara-watcher)    # added after gui/, so no if(TARGET) here
    endif()
    add_dependencies(tanara ${_helpers})
    set(_helper_files)
    set(_deploy_args)
    foreach(_h ${_helpers})
        list(APPEND _helper_files $<TARGET_FILE:${_h}>)
        list(APPEND _deploy_args "-executable=$<TARGET_BUNDLE_CONTENT_DIR:tanara>/MacOS/${_h}")
    endforeach()
    add_custom_command(TARGET tanara POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_helper_files} "$<TARGET_BUNDLE_CONTENT_DIR:tanara>/MacOS"
        VERBATIM)

    find_program(TANARA_MACDEPLOYQT macdeployqt HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" REQUIRED)
    # Homebrew splits Qt into kegs; the plugins' frameworks resolve only through the shared lib dir.
    if(QT6_INSTALL_PREFIX MATCHES "^(.*)/opt/qt")
        list(APPEND _deploy_args "-libpath=${CMAKE_MATCH_1}/lib")
    endif()
    add_custom_target(tanara_bundle
        COMMAND ${TANARA_MACDEPLOYQT} "$<TARGET_BUNDLE_DIR:tanara>"
                -qmldir=${CMAKE_CURRENT_SOURCE_DIR}/qml ${_deploy_args} -codesign=- -always-overwrite
        DEPENDS tanara
        COMMENT "Deploying Qt into tanara.app"
        VERBATIM)
endif()
