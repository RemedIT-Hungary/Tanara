import QtQuick
import QtQuick.Layouts

// The faint context row above / below the focus card (the transcript's line by time -- not only
// the doubtful ones): play · time · speaker · text (one line, elided) · time gap ("+6 s": the
// silence between the line and its neighbor toward the card). The space stays even when there
// is no line (the card does not jump). Click: listen (the two lines nearest the card: `,` / `.`).
//   line:  an item of TranscriptEditorViewModel.contextLines() (empty map: no line)
//   gapMs: the time gap in ms (< 0: no indicator)
Item {
    id: root

    property var line: ({})
    property int gapMs: -1
    property bool playingNow: false
    signal playRequested()

    readonly property bool valid: (line.utteranceId || "") !== ""
    readonly property int offset: line.offset || 0
    readonly property string gapText: gapMs < 0 ? "" : qsTr("+%1 s").arg(Math.round(gapMs / 1000))

    implicitHeight: 30
    height: 30

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: root.playingNow ? Theme.sunken
             : rowHover.hovered && root.valid ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
    }
    HoverHandler { id: rowHover; cursorShape: root.valid ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler { enabled: root.valid; onTapped: root.playRequested() }
    TToolTip {
        visible: rowHover.hovered && root.valid && Math.abs(root.offset) === 1
        text: root.offset < 0 ? qsTr("Meghallgatás (,)") : qsTr("Meghallgatás (.)")
        delay: 500
    }

    RowLayout {
        visible: root.valid
        anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
        spacing: 10
        opacity: root.playingNow ? 1 : 0.7
        TIcon {
            name: root.playingNow ? "volume-2" : "play"
            size: 13
            color: root.playingNow ? Theme.text : Theme.textMuted
            Layout.preferredWidth: 18
        }
        TLabel {
            text: root.line.timeLabel || ""
            mono: true
            muted: true
            font.pixelSize: Theme.fontCaption
            Layout.preferredWidth: 54
        }
        TLabel {
            text: root.line.speakerName || ""
            color: Theme.speakerInk(root.line.colorIndex || 0)
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
            elide: Text.ElideRight
            Layout.preferredWidth: 160
        }
        TLabel {
            text: root.line.text || ""
            muted: true
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            maximumLineCount: 1
            Layout.fillWidth: true
        }
        Rectangle {
            objectName: "reviewFocusContextGap"
            visible: root.gapText !== ""
            Layout.preferredWidth: gapLabel.implicitWidth + 12
            Layout.preferredHeight: 18
            radius: 4
            color: Theme.sunken
            TLabel {
                id: gapLabel
                anchors.centerIn: parent
                text: root.gapText
                mono: true
                muted: true
                font.pixelSize: Theme.fontMicro
            }
        }
    }
}
