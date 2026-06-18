add_executable(tanara src/main.cpp)
target_link_libraries(tanara PRIVATE
    tanara_core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)
