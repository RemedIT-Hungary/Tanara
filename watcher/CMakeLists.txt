# tanara-watcher: lightweight background watcher (system tray).
# Uses only the detector core (tanara_core) + QSystemTrayIcon (Qt6::Widgets); NO MainWindow /
# Multimedia / AppController, so it is a minimal always-running process.

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OPTIONAL_COMPONENTS DBus)

add_executable(tanara-watcher
    main.cpp
    TrayWatcher.cpp
    TrayWatcher.h
)

# WIN32_EXECUTABLE: do not open a console window on Windows (background process).
set_target_properties(tanara-watcher PROPERTIES WIN32_EXECUTABLE ON)

target_link_libraries(tanara-watcher PRIVATE tanara_core Qt6::Widgets tanara_shell_common)
# tanara_shell_common (gui/common): header-only AppIcon.h + RecorderTrayIcon.h (tray icon).
tanara_copy_onnxruntime_dll(tanara-watcher)

# On Linux the call notification goes through the freedesktop Notifications D-Bus API (with
# real action buttons; clicking QSystemTrayIcon::showMessage does not work on Plasma).
# Without D-Bus (or off Linux) the showMessage fallback is used.
if(UNIX AND NOT APPLE AND TARGET Qt6::DBus)
    target_link_libraries(tanara-watcher PRIVATE Qt6::DBus)
    target_compile_definitions(tanara-watcher PRIVATE TANARA_HAVE_DBUS)
endif()
