import QtQuick

// Állapot-pirula (11/600). tone: "warn" | "success" | "danger" | "accent" | "neutral".
//   TPill { text: qsTr("elavult"); tone: "warn" }
Rectangle {
    id: root
    property string text: ""
    property string tone: "neutral"

    readonly property color ink: tone === "warn" ? Theme.warnInk
                               : tone === "success" ? Theme.successInk
                               : tone === "danger" ? Theme.dangerInk
                               : tone === "accent" ? Theme.accent : Theme.textMuted

    implicitWidth: label.implicitWidth + 12
    implicitHeight: label.implicitHeight + 4
    radius: height / 2
    color: tone === "warn" ? Theme.warnSoft
         : tone === "success" ? Theme.successSoft
         : tone === "danger" ? Theme.dangerSoft
         : tone === "accent" ? Theme.accentSoft : Theme.sunken

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.ink
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontMicro
        font.weight: Theme.weightSemiBold
    }
}
