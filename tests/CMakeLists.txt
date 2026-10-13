# Every unit/*.cpp is a separate test exe (so each can bring its own QTEST_*_MAIN).
file(GLOB TANARA_TEST_FILES CONFIGURE_DEPENDS unit/*.cpp)
foreach(testfile ${TANARA_TEST_FILES})
    get_filename_component(tname ${testfile} NAME_WE)
    add_executable(${tname} ${testfile})
    target_link_libraries(${tname} PRIVATE tanara_core Qt6::Test)
    tanara_copy_onnxruntime_dll(${tname})
    add_test(NAME ${tname} COMMAND ${tname})
endforeach()

# The store/settings test gets a main() only with the standalone define.
if(TARGET test_store_settings)
    target_compile_definitions(test_store_settings PRIVATE TANARA_STORE_SETTINGS_STANDALONE)
endif()

# The archive test assembles (broken / malicious) ZIPs by hand, so it needs miniz.
if(TARGET test_meeting_archive)
    target_link_libraries(test_meeting_archive PRIVATE tanara_miniz)
endif()

# The cloud integration tests start the mock-gateway (node). Without node: QSKIP.
foreach(t test_cloud_gateway test_cloud_job_id)
    if(TARGET ${t})
        target_compile_definitions(${t} PRIVATE
            TANARA_MOCK_GATEWAY="${CMAKE_SOURCE_DIR}/tests/mock-gateway/mock-gateway.mjs")
        set_tests_properties(${t} PROPERTIES TIMEOUT 120)
    endif()
endforeach()

# UI tests (tests/ui/*.cpp): the QML module's C++ view models without a display. One exe per
# file; they link the tanara_qml lib (and the plugin, if the test also loads QML). They run
# on the offscreen platform with a software renderer, so they work on CI / over ssh too.
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

# The CLI voice-models / voice-eval / track-sides commands are tested by running tanara-cli (QProcess).
foreach(t test_cli_voice_models test_cli_track_sides test_cli_person_tags test_cli_participants test_cli_review test_cli_reslice)
    if(TARGET ${t} AND TARGET tanara-cli)
        add_dependencies(${t} tanara-cli)
        target_compile_definitions(${t} PRIVATE TANARA_CLI_EXE="$<TARGET_FILE:tanara-cli>")
        set_tests_properties(${t} PROPERTIES TIMEOUT 180)
    endif()
endforeach()
