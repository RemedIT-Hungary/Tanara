import QtQuick
import QtQuick.Templates as T

// Menu item (32 px). iconName: at the start; shortcutText: on the right, mono 11; danger: a
// destructive action (dangerInk); checked: a check mark at the end. Also usable outside a menu (in a column).
T.MenuItem {
    id: control

    property string iconName: ""
    property string shortcutText: ""
    property bool danger: false
    property bool reserveIconSpace: false          // align an icon-less item with the icon ones
    property bool stateHovered: highlighted || hovered

    readonly property color ink: !enabled ? Theme.borderStrong
                               : danger ? Theme.dangerInk : Theme.text

    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    implicitHeight: 32
    leftPadding: 10; rightPadding: 10
    spacing: 10
    hoverEnabled: true
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody

    contentItem: Item {
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: control.spacing
            TIcon {
                visible: control.iconName !== "" || control.reserveIconSpace
                name: control.iconName
                size: 15
                color: control.danger || !control.enabled ? control.ink : Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: control.text
                font: control.font
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            // Room for the right-aligned part (shortcut / check mark).
            Item {
                width: trailing.implicitWidth > 0 ? trailing.implicitWidth + 14 : 0
                height: 1
            }
        }
        Row {
            id: trailing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text {
                visible: control.shortcutText !== ""
                text: control.shortcutText
                color: Theme.textMuted
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontMicro
                anchors.verticalCenter: parent.verticalCenter
            }
            TIcon {
                visible: control.checked
                name: "check"
                size: 15
                color: Theme.accent
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    background: Rectangle {
        radius: 5
        color: !control.enabled ? "transparent"
             : control.down ? Theme.border
             : control.stateHovered ? (control.danger ? Theme.dangerSoft : Theme.sunken) : "transparent"
    }
}
