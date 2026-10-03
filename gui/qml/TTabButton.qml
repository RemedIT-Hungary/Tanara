import QtQuick
import QtQuick.Templates as T

// Egy fül a TTabBar-ban. Aktív: 600 + 2 px accent aláhúzás; inaktív: halvány. Opcionális
// pirula (pillText / pillTone), pl. „elavult” az Összefoglaló fülön.
T.TabButton {
    id: control

    property string pillText: ""
    property string pillTone: "warn"
    property bool stateHovered: hovered
    property bool stateFocused: visualFocus

    // Explicit szélesség: különben a TabBar egyenlően szétosztaná a füleket a teljes sávon.
    width: implicitWidth
    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    implicitHeight: Math.max(row.implicitHeight, 20) + topPadding + bottomPadding
    leftPadding: 12; rightPadding: 12; topPadding: 8; bottomPadding: 8
    hoverEnabled: true
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody
    font.weight: checked ? Theme.weightSemiBold : Theme.weightRegular

    contentItem: Item {
        Row {
            id: row
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter
            Text {
                text: control.text
                font: control.font
                color: control.checked || control.stateHovered ? Theme.text : Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
            TPill {
                visible: control.pillText !== ""
                text: control.pillText
                tone: control.pillTone
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    background: Item {
        Rectangle {
            anchors { fill: parent; bottomMargin: 3; topMargin: 2 }
            radius: Theme.radiusControl
            color: "transparent"
            border.width: control.stateFocused ? 2 : 0
            border.color: Theme.accentLine
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 2
            color: Theme.accent
            visible: control.checked
        }
    }
}
