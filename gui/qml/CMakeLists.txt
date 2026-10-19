# Tanara: the Qt Quick / QML user interface (URI: Tanara). The QML and its C++ (view models, the
# App singleton, image providers) live in one static library; the `tanara` executable links the
# plugin (tanara_qmlplugin), the tests/ui tests link the library itself.
#
# EVERYTHING is picked up by globs (CONFIGURE_DEPENDS): a new file needs NO edit here:
#   gui/qml/*.qml          QML types (flat; the file name is the type name)
#   gui/qml/src/*.h|cpp    C++ types (QML_ELEMENT / QML_SINGLETON: declarative registration)
#   gui/qml/icons/*.svg    Lucide icons         -> :/qt/qml/Tanara/icons/
#   gui/qml/fonts/*.ttf    IBM Plex fonts       -> :/qt/qml/Tanara/fonts/
qt_policy(SET QTP0001 NEW)   # the module goes under the :/qt/qml/Tanara/ resource prefix
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

file(GLOB TANARA_QML_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.qml)
file(GLOB TANARA_QML_SOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} src/*.h src/*.cpp)
file(GLOB TANARA_QML_RESOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     icons/*.svg fonts/*.ttf)

# QML singletons: a `pragma Singleton` among the first lines of a file marks it automatically
# (otherwise it would land in the qmldir as a plain type). Reconfigure after adding a singleton.
foreach(qmlfile ${TANARA_QML_FILES})
    file(STRINGS ${qmlfile} _singleton REGEX "^pragma Singleton" LIMIT_COUNT 1)
    if(_singleton)
        set_source_files_properties(${qmlfile} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

qt_add_library(tanara_qml STATIC)
qt_add_qml_module(tanara_qml
    URI Tanara
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Tanara   # so that qmllint / qmlls find the module
    VERSION 1.0
    QML_FILES ${TANARA_QML_FILES}
    SOURCES ${TANARA_QML_SOURCES}
    RESOURCES ${TANARA_QML_RESOURCES}
    DEPENDENCIES QtQuick
)

# qmltyperegistrar includes the headers by bare file name, so src/ must be on the include path
# (PUBLIC: the tests and gui/src reach the view models the same way).
target_include_directories(tanara_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tanara_qml PUBLIC
    tanara_core
    tanara_demo       # the fictional facades + sample data of --demo / --gallery / --qml-shot
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
)
