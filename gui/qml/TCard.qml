import QtQuick
import QtQuick.Templates as T

// Kártya (surface, 1 px keret, sugár 8). tone: "default" | "raised" | "accent" (ajánlott
// választás: 1.5 px accent keret) | "danger" (hibakártya: dangerSoft / dangerLine).
// A tartalom gyerekként adható meg (tipikusan egy ColumnLayout anchors.fill: parent-tel).
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
