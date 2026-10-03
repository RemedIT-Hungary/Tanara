import QtQuick

// Lucide ikon (gui/qml/icons/<name>.svg) tetszőleges témaszínnel. A kép-provider a kért
// fizikai pixelméretben raszterel (méret × devicePixelRatio) → törtléptéken is éles.
//   TIcon { name: "search"; size: 15; color: Theme.textMuted }
Item {
    id: root
    property string name: ""
    property color color: Theme.text
    property real size: Theme.iconSize
    property real strokeWidth: 1.75        // a 24-es Lucide-rácson értve

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
