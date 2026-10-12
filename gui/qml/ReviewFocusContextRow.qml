import QtQuick
import QtQuick.Layouts

// A fókusz-kártya fölötti / alatti halvány kontextus-sor (az átirat sora, nem csak a kétes):
// idő · beszélő · szöveg (egy sor, elide) · jelölő · a `,` / `.` billentyű-tipp a szomszéd soron.
// A hely akkor is megmarad, ha nincs ott sor (a kártya nem ugrál). Kattintás: meghallgatás.
//   line: TranscriptEditorViewModel.contextLines() egy eleme (üres map: nincs sor)
Item {
    id: root

    property var line: ({})
    property bool playingNow: false
    signal playRequested()

    readonly property bool valid: (line.utteranceId || "") !== ""
    readonly property int offset: line.offset || 0

    implicitHeight: 30
    height: 30

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: root.playingNow ? Theme.accentSoft
             : rowHover.hovered && root.valid ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
    }
    HoverHandler { id: rowHover; cursorShape: root.valid ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler { enabled: root.valid; onTapped: root.playRequested() }

    RowLayout {
        visible: root.valid
        anchors { fill: parent; leftMargin: 8; rightMargin: 10 }
        spacing: 10
        opacity: root.playingNow ? 1 : 0.62
        TIcon {
            name: root.playingNow ? "volume-2" : "play"
            size: 13
            color: root.playingNow ? Theme.accent : Theme.textMuted
        }
        TLabel {
            text: root.line.timeLabel || ""
            mono: true
            muted: true
            font.pixelSize: Theme.fontCaption
            Layout.preferredWidth: 44
        }
        TLabel {
            text: root.line.speakerName || ""
            color: Theme.speakerInk(root.line.colorIndex || 0)
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
            elide: Text.ElideRight
            Layout.preferredWidth: 120
        }
        TLabel {
            text: root.line.text || ""
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            maximumLineCount: 1
            Layout.fillWidth: true
        }
        TPill {
            readonly property string kind: root.line.decidedKind || ""
            visible: kind !== "" || root.line.uncertain === true
            text: kind === "ignored" ? qsTr("? nem azonosítható")
                : kind === "corrected" ? qsTr("javítva")
                : kind === "confirmed" ? qsTr("megerősítve") : qsTr("kétes")
            tone: kind === "" ? "warn" : kind === "ignored" ? "neutral" : "success"
        }
        Rectangle {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            radius: 4
            color: Theme.sunken
            border.width: 1
            border.color: Theme.border
            visible: Math.abs(root.offset) === 1
            TLabel {
                anchors.centerIn: parent
                text: root.offset < 0 ? "," : "."
                mono: true
                muted: true
                font.pixelSize: Theme.fontMicro
                font.weight: Theme.weightSemiBold
            }
        }
    }
}
