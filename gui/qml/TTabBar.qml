import QtQuick
import QtQuick.Templates as T

// Tab row with an underline (1 px bottom line, 2 px accent under the active tab).
//   TTabBar { id: tabs
//       TTabButton { text: qsTr("Transcript") }
//       TTabButton { text: qsTr("Summary"); pillText: qsTr("stale") }
//   }
//   StackLayout { currentIndex: tabs.currentIndex; ... }
T.TabBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth, contentWidth)
    implicitHeight: contentHeight
    spacing: 4

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        snapMode: ListView.SnapToItem
    }
    background: Item {
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.border
        }
    }
}
