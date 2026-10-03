import QtQuick
import QtQuick.Templates as T

// Kapcsoló (34×20). A felirat 14/500; hosszabb magyarázatot a hívó tegyen alá (13, muted).
T.Switch {
    id: control

    property bool stateHovered: hovered
    property bool stateFocused: visualFocus

    implicitWidth: implicitIndicatorWidth + (text !== "" ? spacing + label.implicitWidth : 0)
    implicitHeight: Math.max(implicitIndicatorHeight, implicitContentHeight)
    spacing: 12
    hoverEnabled: true
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody
    font.weight: Theme.weightMedium

    indicator: Rectangle {
        implicitWidth: 34
        implicitHeight: 20
        y: Math.round((control.height - height) / 2)
        radius: 10
        color: !control.enabled ? Theme.border
             : control.checked ? Theme.accent : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: Theme.stateLayer
            opacity: control.down ? Theme.pressedOpacity : control.stateHovered ? Theme.hoverOpacity : 0
        }
        Rectangle {
            width: 16; height: 16; radius: 8
            y: 2
            x: control.visualPosition * (parent.width - width - 4) + 2
            color: control.checked ? Theme.textOnAccent : (Theme.dark ? Theme.text : Theme.raised)
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
        TFocusRing { visible: control.stateFocused; targetRadius: 10 }
    }

    contentItem: Item {
        implicitWidth: control.indicator.width + control.spacing + label.implicitWidth
        implicitHeight: label.implicitHeight
        Text {
            id: label
            x: control.indicator.width + control.spacing
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.enabled ? Theme.text : Theme.textMuted
            wrapMode: Text.Wrap
        }
    }
}
