import QtQuick
import QtQuick.Templates as T

// A felvevő eszköz-kapcsolója (30×18). Be: accent; ki: borderStrong körvonal.
// locked: épp rögzített sáv — lakat a gombban, nem kapcsolható ki.
T.Switch {
    id: control
    property bool locked: false
    property string toolTipText: ""

    implicitWidth: 30
    implicitHeight: 18
    hoverEnabled: true
    Accessible.name: toolTipText

    indicator: Rectangle {
        width: 30; height: 18; radius: 9
        color: control.checked ? Theme.accent : "transparent"
        border.width: 1
        border.color: control.checked ? Theme.accent : Theme.borderStrong
        opacity: control.enabled || control.locked ? 1 : 0.5

        Rectangle {
            anchors.fill: parent
            radius: 9
            color: Theme.stateLayer
            opacity: !control.enabled ? 0 : control.down ? Theme.pressedOpacity
                   : control.hovered ? Theme.hoverOpacity : 0
        }
        Rectangle {
            width: 12; height: 12; radius: 6
            y: 3
            x: control.checked ? 15 : 3
            color: control.checked ? Theme.textOnAccent : Theme.borderStrong
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            TIcon {
                anchors.centerIn: parent
                visible: control.locked
                name: "lock"
                size: 8
                strokeWidth: 2.5
                color: Theme.accent
            }
        }
        TFocusRing { visible: control.visualFocus; targetRadius: 9 }
    }

    TToolTip {
        visible: control.toolTipText !== "" && control.hovered
        text: control.toolTipText
    }
}
