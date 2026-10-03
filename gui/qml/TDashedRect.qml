import QtQuick
import QtQuick.Shapes

// Szaggatott keretű, lekerekített téglalap (hozzáadás-chip, ejtési cél, letiltott gomb).
Shape {
    id: root
    property color color: Theme.borderStrong
    property color fillColor: "transparent"
    property real radius: Theme.radiusControl
    property real lineWidth: 1
    property var dashPattern: [3, 3]

    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: root.color
        strokeWidth: root.lineWidth
        fillColor: root.fillColor
        strokeStyle: ShapePath.DashLine
        dashPattern: root.dashPattern
        capStyle: ShapePath.FlatCap
        PathRectangle {
            x: root.lineWidth / 2
            y: root.lineWidth / 2
            width: Math.max(0, root.width - root.lineWidth)
            height: Math.max(0, root.height - root.lineWidth)
            radius: Math.min(root.radius, height / 2)
        }
    }
}
