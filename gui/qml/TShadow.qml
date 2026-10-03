import QtQuick

// Felugrók / párbeszédablakok árnyéka: 0 12 32 + 0 1 3 (Theme.shadowColor / shadowTightColor).
// Shader nélkül: előre elmosott kilencfoltos kép a kép-providerből, ezért a szoftveres
// rendererrel is ugyanúgy néz ki. A háttér-téglalap gyerekeként használd:
//   background: Rectangle { radius: 8; TShadow { radius: parent.radius } }
Item {
    id: root
    property real radius: Theme.radiusPopup

    anchors.fill: parent
    z: -1

    BorderImage {
        readonly property int blur: Theme.shadowBlur
        readonly property int edge: 2 * blur + Math.round(root.radius)
        x: -blur
        y: -blur + Theme.shadowOffsetY
        width: root.width + 2 * blur
        height: root.height + 2 * blur
        border { left: edge; right: edge; top: edge; bottom: edge }
        source: "image://tanara/shadow/" + blur + "/" + Math.round(root.radius) + "/" + Theme.hex(Theme.shadowColor)
    }
    BorderImage {
        readonly property int blur: 3
        readonly property int edge: 2 * blur + Math.round(root.radius)
        x: -blur
        y: -blur + 1
        width: root.width + 2 * blur
        height: root.height + 2 * blur
        border { left: edge; right: edge; top: edge; bottom: edge }
        source: "image://tanara/shadow/" + blur + "/" + Math.round(root.radius) + "/" + Theme.hex(Theme.shadowTightColor)
    }
}
