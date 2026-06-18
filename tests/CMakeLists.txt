file(GLOB_RECURSE TANARA_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp)
add_executable(tanara_tests ${TANARA_TEST_SOURCES})
target_link_libraries(tanara_tests PRIVATE tanara_core Qt6::Test)
add_test(NAME tanara_tests COMMAND tanara_tests)
