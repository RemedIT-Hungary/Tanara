import QtQuick
import QtQuick.Templates as T

// Jelölőnégyzet (16 px, sugár 4). A felirat tördelődik; a négyzet az első sorhoz igazodik.
T.CheckBox {
    id: control

    property bool stateHovered: hovered
    property bool stateFocused: visualFocus

    implicitWidth: implicitIndicatorWidth + (text !== "" ? spacing + label.implicitWidth : 0)
    implicitHeight: Math.max(implicitIndicatorHeight + 4, implicitContentHeight)
    spacing: 10
    hoverEnabled: true
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        y: 2
        radius: 4
        color: control.checked ? (control.enabled ? Theme.accent : Theme.borderStrong)
             : control.down ? Theme.sunken
             : control.enabled ? "transparent" : Theme.sunken
        border.width: control.checked ? 0 : 1.5
        border.color: control.stateHovered && control.enabled ? Theme.textMuted : Theme.borderStrong

        TIcon {
            anchors.centerIn: parent
            visible: control.checked
            name: "check"
            size: 12
            strokeWidth: 3
            color: Theme.textOnAccent
        }
        TFocusRing { visible: control.stateFocused; targetRadius: 4 }
    }

    contentItem: Item {
        implicitWidth: control.indicator.width + control.spacing + label.implicitWidth
        implicitHeight: label.implicitHeight
        Text {
            id: label
            x: control.indicator.width + control.spacing
            width: parent.width - x
            text: control.text
            font: control.font
            color: control.enabled ? Theme.text : Theme.textMuted
            wrapMode: Text.Wrap
        }
    }
}
