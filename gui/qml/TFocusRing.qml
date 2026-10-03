import QtQuick

// 3 px-es fókuszgyűrű a szülő köré. A vezérlők hátterének gyerekeként él:
//   TFocusRing { visible: control.stateFocused; targetRadius: control.radius }
Rectangle {
    property real targetRadius: Theme.radiusControl

    anchors.fill: parent
    anchors.margins: -Theme.focusRingWidth
    radius: targetRadius + Theme.focusRingWidth
    color: "transparent"
    border.width: Theme.focusRingWidth
    border.color: Theme.accentLine
}
