import QtQuick
import QtQuick.Templates as T

// Divider handle for SplitView:  SplitView { handle: TSplitHandle {} }
// 1 px line with a +-3 px grab area; accent while dragging.
Rectangle {
    id: root
    implicitWidth: 1
    implicitHeight: 1
    color: T.SplitHandle.pressed ? Theme.accent
         : T.SplitHandle.hovered ? Theme.borderStrong : Theme.border
    containmentMask: Item {
        x: -3; y: -3
        width: root.width + 6
        height: root.height + 6
    }
}
