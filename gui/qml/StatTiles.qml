import QtQuick
import QtQuick.Layouts

// ADATOK (design/handoff-v3 V3): 2 × 3 csempe — raised, keret, sugár 7; címke 11/600 (halvány,
// betűközzel), érték 19 px mono, alszöveg 11.5. stats: [{ label, value, sub }]
GridLayout {
    id: root

    property var stats: []

    columns: 2
    columnSpacing: 8
    rowSpacing: 8

    Repeater {
        model: root.stats
        Rectangle {
            required property var modelData
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            implicitHeight: tileColumn.implicitHeight + 20
            radius: 7
            color: Theme.raised
            border.width: 1
            border.color: Theme.border
            ColumnLayout {
                id: tileColumn
                x: 12; y: 10
                width: parent.width - 24
                spacing: 4
                TLabel {
                    Layout.fillWidth: true
                    text: modelData.label
                    muted: true
                    font.pixelSize: Theme.fontMicro
                    font.weight: Theme.weightSemiBold
                    font.letterSpacing: 0.55
                    elide: Text.ElideRight
                }
                TLabel {
                    Layout.fillWidth: true
                    objectName: "statValue"
                    text: modelData.value
                    mono: true
                    font.pixelSize: 19
                    font.weight: Theme.weightMedium
                    elide: Text.ElideRight
                }
                TLabel {
                    Layout.fillWidth: true
                    text: modelData.sub
                    muted: true
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }
}
