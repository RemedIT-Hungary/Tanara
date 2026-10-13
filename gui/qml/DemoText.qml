pragma Singleton
import QtQuick

// Tanara — a KITALÁLT demo-tartalom nyelvi kapcsolója QML-ben (a C++ DemoText.h-ra épül).
// Magyar UI-nál a magyar, angol UI-nál a természetes angol demo-szöveg / -név szól; NEM qsTr
// (a demo-tartalom nem a felület fordítása). Használat (a modulon belül import nélkül):
//   text: DemoText.t("Negyedéves partnertalálkozó", "Quarterly partner review")
//   name: DemoText.name("Kovács Lilla")          // angolul "Emma Carter"
//   model: DemoText.names(["Kovács Lilla", "Fehér Gábor"])
QtObject {
    id: demoText
    readonly property bool en: DemoTextCpp.en

    function t(hu, enText) { return demoText.en ? enText : hu }
    function name(hu) { return DemoTextCpp.name(hu) }
    function names(hu) { return DemoTextCpp.names(hu) }
    function raw(hu) { return DemoTextCpp.raw(hu) }
}
