file(GLOB_RECURSE TANARA_GUI_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h src/*.ui)

add_executable(tanara ${TANARA_GUI_SOURCES})

# A promotált widgetek headerei (ui_*.h "TranscriptPlayer.h"-t includol) a src/-ben vannak.
target_include_directories(tanara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(tanara PRIVATE
    tanara_core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)
