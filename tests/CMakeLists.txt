# Minden unit/*.cpp külön teszt-exe (így mindegyik hozhatja a saját QTEST_*_MAIN-jét).
file(GLOB TANARA_TEST_FILES CONFIGURE_DEPENDS unit/*.cpp)
foreach(testfile ${TANARA_TEST_FILES})
    get_filename_component(tname ${testfile} NAME_WE)
    add_executable(${tname} ${testfile})
    target_link_libraries(${tname} PRIVATE tanara_core Qt6::Test)
    add_test(NAME ${tname} COMMAND ${tname})
endforeach()

# A store/settings teszt csak a standalone define-nal kap main()-t.
if(TARGET test_store_settings)
    target_compile_definitions(test_store_settings PRIVATE TANARA_STORE_SETTINGS_STANDALONE)
endif()
