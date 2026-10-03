# Tanara — Qt Quick / QML felület (URI: Tanara). Egy statikus lib-ben él a QML és a
# hozzá tartozó C++ (nézetmodellek, App-singleton, kép-providerek); a `tanara` exe a
# plugint (tanara_qmlplugin) linkeli, a tests/ui tesztek magát a libet.
#
# MINDEN glob-bal kerül be (CONFIGURE_DEPENDS) — új fájlhoz NEM kell ezt szerkeszteni:
#   gui/qml/*.qml          QML-típusok (laposan; a fájlnév = a típusnév)
#   gui/qml/src/*.h|cpp    C++ típusok (QML_ELEMENT / QML_SINGLETON — deklaratív regisztráció)
#   gui/qml/icons/*.svg    Lucide ikonok        → :/qt/qml/Tanara/icons/
#   gui/qml/fonts/*.ttf    IBM Plex betűk       → :/qt/qml/Tanara/fonts/
qt_policy(SET QTP0001 NEW)   # a modul a :/qt/qml/Tanara/ erőforrás-prefix alá kerül
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

file(GLOB TANARA_QML_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.qml)
file(GLOB TANARA_QML_SOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} src/*.h src/*.cpp)
file(GLOB TANARA_QML_RESOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     icons/*.svg fonts/*.ttf)

# QML-singleton: a fájl első sorai között `pragma Singleton` → automatikusan megjelöljük
# (különben a qmldir-be sima típusként kerülne). Új singleton után újra kell konfigurálni.
foreach(qmlfile ${TANARA_QML_FILES})
    file(STRINGS ${qmlfile} _singleton REGEX "^pragma Singleton" LIMIT_COUNT 1)
    if(_singleton)
        set_source_files_properties(${qmlfile} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

qt_add_library(tanara_qml STATIC)
qt_add_qml_module(tanara_qml
    URI Tanara
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Tanara   # a qmllint/qmlls így találja meg
    VERSION 1.0
    QML_FILES ${TANARA_QML_FILES}
    SOURCES ${TANARA_QML_SOURCES}
    RESOURCES ${TANARA_QML_RESOURCES}
    DEPENDENCIES QtQuick
)

# A qmltyperegistrar a headereket puszta fájlnévvel includolja → a src/ legyen include-úton
# (PUBLIC: a tesztek és a gui/src is így érik el a nézetmodelleket).
target_include_directories(tanara_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tanara_qml PUBLIC
    tanara_core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
)
