import QtQuick

// The connection test's state pill: "Connected · 210 ms" (successSoft / successInk,
// circle-check), "Unreachable" (dangerSoft / dangerInk, circle-x), "Testing..." (spinner),
// "neutral": neutral information with no icon (sunken / textMuted, e.g. "Basic tier").
//   SettingsStatusPill { state: card.testState; text: card.statusText }
Rectangle {
    id: root

    property string status: ""        // "" | "testing" | "ok" | "failed" | "neutral"
    property string text: ""

    readonly property color ink: status === "ok" ? Theme.successInk
                               : status === "failed" ? Theme.dangerInk : Theme.textMuted

    visible: status !== ""
    implicitWidth: row.implicitWidth + 16
    implicitHeight: 22
    radius: 11
    color: status === "ok" ? Theme.successSoft : status === "failed" ? Theme.dangerSoft : Theme.sunken

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        TSpinner {
            visible: root.status === "testing"
            size: 13
            color: Theme.textMuted
            anchors.verticalCenter: parent.verticalCenter
        }
        TIcon {
            visible: root.status !== "testing" && root.status !== "neutral"
            name: root.status === "ok" ? "circle-check" : "circle-x"
            size: 13
            color: root.ink
            anchors.verticalCenter: parent.verticalCenter
        }
        TLabel {
            text: root.text
            color: root.ink
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
