import QtQuick
import QtQuick.Templates as T

// Card (surface, 1 px border, radius 8). tone: "default" | "raised" | "accent" (the recommended
// choice: 1.5 px accent border) | "danger" (error card: dangerSoft / dangerLine).
// The content goes in as children (typically a ColumnLayout with anchors.fill: parent).
T.Pane {
    id: control

    property string tone: "default"
    property real radius: Theme.radiusPopup

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: 16

    background: Rectangle {
        radius: control.radius
        color: control.tone === "danger" ? Theme.dangerSoft
             : control.tone === "raised" ? Theme.raised : Theme.surface
        border.width: control.tone === "accent" ? 1.5 : 1
        border.color: control.tone === "danger" ? Theme.dangerLine
                    : control.tone === "accent" ? Theme.accent : Theme.border
    }
}
