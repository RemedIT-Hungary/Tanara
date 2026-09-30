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

# A cloud-integrációs teszt a mock-gateway-t indítja (node). Node nélkül QSKIP.
if(TARGET test_cloud_gateway)
    target_compile_definitions(test_cloud_gateway PRIVATE
        TANARA_MOCK_GATEWAY="${CMAKE_SOURCE_DIR}/tests/mock-gateway/mock-gateway.mjs")
    set_tests_properties(test_cloud_gateway PROPERTIES TIMEOUT 120)
endif()
