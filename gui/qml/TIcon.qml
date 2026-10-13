import QtQuick

// Lucide icon (gui/qml/icons/<name>.svg) in any theme color. The image provider rasterizes at
// the requested physical pixel size (size x devicePixelRatio) -> sharp at fractional scales too.
//   TIcon { name: "search"; size: 15; color: Theme.textMuted }
Item {
    id: root
    property string name: ""
    property color color: Theme.text
    property real size: Theme.iconSize
    property real strokeWidth: 1.75        // on the 24-unit Lucide grid

    implicitWidth: size
    implicitHeight: size

    Image {
        readonly property int px: Math.max(1, Math.round(root.size * Screen.devicePixelRatio))
        anchors.centerIn: parent
        width: root.size
        height: root.size
        sourceSize: Qt.size(px, px)
        source: root.name === "" ? ""
              : "image://tanara/icon/" + root.name + "/" + Theme.hex(root.color) + "/" + root.strokeWidth
        asynchronous: false
        smooth: true
    }
}
