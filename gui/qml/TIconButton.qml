import QtQuick
import QtQuick.Templates as T

// Csak-ikon gomb. variant: "outline" (1 px keret) | "flat" (keret nélkül) | "solid"
// (text háttér + bg ikon; kerek lejátszás-gombhoz a radius-t állítsd height/2-re).
// checkable + checked: accentSoft háttér, accent keret és ikon (pl. „Sávok” kapcsoló).
// Mindig adj toolTipText-et: az a hozzáférhető neve is.
//   TIconButton { iconName: "ellipsis"; toolTipText: qsTr("További műveletek") }
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
