import QtQuick
import QtQuick.Templates as T

// Vékony görgetősáv:
//   import QtQuick.Templates as T
//   ListView { T.ScrollBar.vertical: TScrollBar {} }
T.ScrollBar {
    id: control

    implicitWidth: orientation === Qt.Vertical ? 10 : 40
    implicitHeight: orientation === Qt.Vertical ? 40 : 10
    padding: 2
    minimumSize: 0.08
    visible: policy !== T.ScrollBar.AlwaysOff && size < 1.0

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: control.pressed ? Theme.textMuted : Theme.borderStrong
        opacity: control.policy === T.ScrollBar.AlwaysOn || control.active || control.hovered ? 1 : 0.55
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
    }
}
