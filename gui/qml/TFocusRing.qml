import QtQuick

// 3 px focus ring around the parent. It lives as a child of a control's background:
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
