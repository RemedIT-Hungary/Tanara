import QtQuick
import QtQuick.Templates as T

// Icon-only button. variant: "outline" (1 px border) | "flat" (no border) | "solid"
// (text background + bg icon; for a round play button set radius to height/2).
// checkable + checked: accentSoft background, accent border and icon (e.g. the "Tracks" toggle).
// Always give a toolTipText: it is also the accessible name.
//   TIconButton { iconName: "ellipsis"; toolTipText: qsTr("More actions") }
T.Button {
    id: control

    property string iconName: ""
    property real iconSize: 16
    property string variant: "outline"
    property string size: "normal"                  // "normal" 34 | "small" 28
    property real radius: Theme.radiusControl
    property color iconColor: !enabled ? Theme.borderStrong
                            : variant === "solid" ? Theme.bg
                            : checked ? Theme.accent : Theme.text
    property string toolTipText: ""
    property bool stateHovered: hovered
    property bool stateFocused: visualFocus

    implicitWidth: size === "small" ? Theme.controlHeightSmall : Theme.controlHeight
    implicitHeight: implicitWidth
    hoverEnabled: true
    Accessible.name: toolTipText

    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    contentItem: Item {
        TIcon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.iconColor
        }
    }

    background: Rectangle {
        id: bg
        radius: Math.min(control.radius, height / 2)
        color: control.variant === "solid" ? (control.enabled ? Theme.text : Theme.sunken)
             : control.checked ? Theme.accentSoft : "transparent"
        border.width: control.variant === "outline" || control.checked ? 1 : 0
        border.color: control.checked ? Theme.accent : Theme.border

        Rectangle {
            anchors.fill: parent
            radius: bg.radius
            color: control.variant === "solid" ? Theme.bg : Theme.stateLayer
            visible: control.enabled
            opacity: control.down ? Theme.pressedOpacity * 1.5
                   : control.stateHovered ? Theme.hoverOpacity * 1.5 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
        TFocusRing { visible: control.stateFocused; targetRadius: bg.radius }
    }

    TToolTip {
        visible: control.toolTipText !== "" && control.hovered
        text: control.toolTipText
    }
}
