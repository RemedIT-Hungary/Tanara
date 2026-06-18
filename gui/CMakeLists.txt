file(GLOB_RECURSE TANARA_GUI_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_executable(tanara ${TANARA_GUI_SOURCES})

target_link_libraries(tanara PRIVATE
    tanara_core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)
