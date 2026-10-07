# Minden unit/*.cpp külön teszt-exe (így mindegyik hozhatja a saját QTEST_*_MAIN-jét).
file(GLOB TANARA_TEST_FILES CONFIGURE_DEPENDS unit/*.cpp)
foreach(testfile ${TANARA_TEST_FILES})
    get_filename_component(tname ${testfile} NAME_WE)
    add_executable(${tname} ${testfile})
    target_link_libraries(${tname} PRIVATE tanara_core Qt6::Test)
    tanara_copy_onnxruntime_dll(${tname})
    add_test(NAME ${tname} COMMAND ${tname})
endforeach()

# A store/settings teszt csak a standalone define-nal kap main()-t.
if(TARGET test_store_settings)
    target_compile_definitions(test_store_settings PRIVATE TANARA_STORE_SETTINGS_STANDALONE)
endif()

# Az archívum-teszt kézzel rak össze (hibás / rosszindulatú) ZIP-eket → miniz kell neki.
if(TARGET test_meeting_archive)
    target_link_libraries(test_meeting_archive PRIVATE tanara_miniz)
endif()

# A cloud-integrációs tesztek a mock-gateway-t indítják (node). Node nélkül QSKIP.
foreach(t test_cloud_gateway test_cloud_job_id)
    if(TARGET ${t})
        target_compile_definitions(${t} PRIVATE
            TANARA_MOCK_GATEWAY="${CMAKE_SOURCE_DIR}/tests/mock-gateway/mock-gateway.mjs")
        set_tests_properties(${t} PROPERTIES TIMEOUT 120)
    endif()
endforeach()

# UI-tesztek (tests/ui/*.cpp): a QML-modul C++ nézetmodelljei kijelző nélkül. Fájlonként
# egy exe; a tanara_qml libet (és a plugint, ha a teszt QML-t is tölt) linkelik. Offscreen
# platformon + szoftveres rendererrel futnak, így CI-n / ssh-n is mennek.
if(TARGET tanara_qml)
    file(GLOB TANARA_UI_TEST_FILES CONFIGURE_DEPENDS ui/*.cpp)
    foreach(testfile ${TANARA_UI_TEST_FILES})
        get_filename_component(tname ${testfile} NAME_WE)
        add_executable(${tname} ${testfile})
        target_link_libraries(${tname} PRIVATE tanara_qml tanara_qmlplugin Qt6::Widgets Qt6::Test)
        tanara_copy_onnxruntime_dll(${tname})
        add_test(NAME ${tname} COMMAND ${tname})
        set_tests_properties(${tname} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endforeach()
endif()
