file(GLOB_RECURSE TANARA_GUI_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h src/*.ui)

add_executable(tanara ${TANARA_GUI_SOURCES})

# Windowson GUI-alkalmazás (ne console-subsystem) → ne nyíljon mellé fekete
# konzolablak, és a bezárása ne állítsa le az appot. A WinMain→main belépőt a
# Qt6::EntryPoint adja (a Qt6::Core hozza Windowson). Nem-Windowson ez no-op.
# (A tanara-cli SZÁNDÉKOSAN marad konzolos.)
set_target_properties(tanara PROPERTIES WIN32_EXECUTABLE ON)

# A promotált widgetek headerei (ui_*.h "TranscriptPlayer.h"-t includol) a src/-ben vannak.
target_include_directories(tanara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(tanara PRIVATE
    tanara_core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)
