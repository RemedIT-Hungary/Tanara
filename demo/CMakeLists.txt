# tanara_demo: the FICTIONAL content of --demo, --gallery, --qml-shot and the UI tests, as
# demo implementations of the modules' facade interfaces (e.g. tanara::recording::IRecorderFacade)
# and plain sample data. It builds on tanara_core only: no Widgets, no QML (the QML bridges of
# the demo switches live in gui/qml/src).
file(GLOB_RECURSE TANARA_DEMO_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE TANARA_DEMO_HEADERS CONFIGURE_DEPENDS include/*.h)

add_library(tanara_demo STATIC ${TANARA_DEMO_SOURCES} ${TANARA_DEMO_HEADERS})
target_include_directories(tanara_demo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tanara_demo PUBLIC tanara_core)
