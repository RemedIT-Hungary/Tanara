import QtQuick
import QtQuick.Layouts

// Sáv-értesítés ikonnal és műveletekkel. tone: "warn" | "accent" | "danger".
// A gyerekei (TButton size: "small") a jobb oldali művelet-sorba kerülnek.
//   TBanner { tone: "warn"; text: qsTr("Az összefoglaló óta 3 beszélőt javítottál…")
//       TButton { text: qsTr("Frissítés"); size: "small" }
//       TButton { text: qsTr("Rendben így"); size: "small"; variant: "ghost" }
//   }
Rectangle {
    id: root

    property string tone: "warn"
    property string title: ""
    property string text: ""
    property string iconName: tone === "warn" ? "triangle-alert" : tone === "danger" ? "circle-alert" : "info"
    default property alias actions: actionRow.data

    readonly property color ink: tone === "warn" ? Theme.warnInk
                               : tone === "danger" ? Theme.dangerInk : Theme.accent

    implicitWidth: 480
    implicitHeight: layout.implicitHeight + 20
    radius: Theme.radiusControl
    color: tone === "warn" ? Theme.warnSoft : tone === "danger" ? Theme.dangerSoft : Theme.accentSoft
    border.width: 1
    border.color: tone === "warn" ? Theme.warnLine : tone === "danger" ? Theme.dangerLine : Theme.accentLine

    RowLayout {
        id: layout
        anchors { fill: parent; leftMargin: 14; rightMargin: 10; topMargin: 10; bottomMargin: 10 }
        spacing: 12

        TIcon {
            visible: root.iconName !== ""
            name: root.iconName
            color: root.ink
            Layout.alignment: root.title !== "" ? Qt.AlignTop : Qt.AlignVCenter
            Layout.topMargin: root.title !== "" ? 2 : 0
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            TLabel {
                visible: root.title !== ""
                Layout.fillWidth: true
                text: root.title
                font.weight: Theme.weightSemiBold
                wrapMode: Text.Wrap
            }
            TLabel {
                visible: root.text !== ""
                Layout.fillWidth: true
                text: root.text
                font.pixelSize: root.title !== "" ? Theme.fontSmall : Theme.fontBody
                cssLineHeight: 1.4
                wrapMode: Text.Wrap
            }
        }
        Row {
            id: actionRow
            spacing: 4
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
