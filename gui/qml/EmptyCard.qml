import QtQuick
import QtQuick.Layouts

// Az Áttekintés üres részei (design/handoff-v3 V5/V6): szaggatott borderStrong kártya egy
// ikonnal, egy mondatnyi „miért” és a művelet(ek). A gombok / chipek a `actions` sorba
// kerülnek (a szöveg alá, az ikonnal egy vonalban beljebb).
//   EmptyCard { iconName: "users"; title: qsTr("Még nem tudjuk, kik voltak ott"); text: …
//       TButton { text: qsTr("+ Résztvevő") } }
Item {
    id: root

    property string iconName: "info"
    property string title: ""
    property string text: ""
    default property alias actions: actionFlow.data

    implicitHeight: column.implicitHeight + 32

    TDashedRect {
        anchors.fill: parent
        radius: Theme.radiusPopup
        color: Theme.borderStrong
    }
    ColumnLayout {
        id: column
        x: 16; y: 16
        width: root.width - 32
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TIcon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                name: root.iconName
                size: 18
                color: Theme.textMuted
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                TLabel {
                    Layout.fillWidth: true
                    text: root.title
                    font.pixelSize: Theme.fontBody
                    font.weight: Theme.weightSemiBold
                    wrapMode: Text.Wrap
                }
                TLabel {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 560
                    visible: root.text !== ""
                    text: root.text
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
            }
        }
        Flow {
            id: actionFlow
            Layout.fillWidth: true
            Layout.leftMargin: 28
            visible: children.length > 0
            spacing: 8
        }
    }
}
