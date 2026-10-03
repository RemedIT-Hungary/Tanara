import QtQuick
import QtQuick.Templates as T

// Elválasztó fogantyú SplitView-hoz:  SplitView { handle: TSplitHandle {} }
// 1 px-es vonal, ±3 px megfogható sávval; húzás közben accent.
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
