# tanara-watcher — pehelysúlyú háttér-figyelő (rendszertálca).
# Csak a detektor-magot (tanara_core) + a QSystemTrayIcon-t (Qt6::Widgets) használja;
# NINCS MainWindow / Multimedia / AppController → minimális, mindig-futó folyamat.

add_executable(tanara-watcher
    main.cpp
    TrayWatcher.cpp
    TrayWatcher.h
)

# WIN32_EXECUTABLE: ne nyisson konzol-ablakot Windowson (háttér-folyamat).
set_target_properties(tanara-watcher PROPERTIES WIN32_EXECUTABLE ON)

target_link_libraries(tanara-watcher PRIVATE tanara_core Qt6::Widgets)

# Linuxon a hívás-notification a freedesktop Notifications D-Bus API-n megy (valódi
# akció-gombokkal — a QSystemTrayIcon::showMessage kattintása Plasmán nem működik).
# D-Bus nélkül (vagy nem-Linuxon) a showMessage-fallback él.
if(UNIX AND NOT APPLE AND TARGET Qt6::DBus)
    target_link_libraries(tanara-watcher PRIVATE Qt6::DBus)
    target_compile_definitions(tanara-watcher PRIVATE TANARA_HAVE_DBUS)
endif()

# Az app-ikon (makeTanaraIcon) a gui/src/AppIcon.h header-only helperből (QtGui-only) —
# a tálca-ikon konzisztenciájáért. Nem a gui targettől függünk, csak ezt az egy fejlécet
# használjuk újra.
target_include_directories(tanara-watcher PRIVATE ${CMAKE_SOURCE_DIR}/gui/src)
