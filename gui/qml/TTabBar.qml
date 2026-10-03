import QtQuick
import QtQuick.Templates as T

// Fülsor aláhúzással (1 px alsó vonal, az aktív fül alatt 2 px accent).
//   TTabBar { id: tabs
//       TTabButton { text: qsTr("Átirat") }
//       TTabButton { text: qsTr("Összefoglaló"); pillText: qsTr("elavult") }
//   }
//   StackLayout { currentIndex: tabs.currentIndex; … }
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
