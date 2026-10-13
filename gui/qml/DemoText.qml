pragma Singleton
import QtQuick

// Language switch for the FICTIONAL demo content in QML (built on the C++ DemoText.h). With a
// Hungarian UI the Hungarian demo text / name sounds, with an English UI the natural English
// one; NOT qsTr (demo content is not a translation of the UI). Usage (inside the module, no import):
//   text: DemoText.t("<Hungarian text>", "Quarterly partner review")
//   name: DemoText.name("<Hungarian name>")          // in English "Emma Carter"
//   model: DemoText.names(["<Hungarian name 1>", "<Hungarian name 2>"])
QtObject {
    id: demoText
    readonly property bool en: DemoTextCpp.en

    function t(hu, enText) { return demoText.en ? enText : hu }
    function name(hu) { return DemoTextCpp.name(hu) }
    function names(hu) { return DemoTextCpp.names(hu) }
    function raw(hu) { return DemoTextCpp.raw(hu) }
}
