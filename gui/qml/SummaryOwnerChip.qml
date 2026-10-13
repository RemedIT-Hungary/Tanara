import QtQuick

// Owner chip next to to-dos (not clickable): for a meeting speaker in the speaker's color
// (dot + soft background + ink text), neutral for any other name. warn (targeted staleness): the
// owner's source row now has another speaker - dashed warn border, "Fehér Gábor -> Varga
// Árpád?", the dot in the new speaker's color (dotIndex). compact: the 22 px chip of the
// summary lists (1 px speaker-color border).
//   SummaryOwnerChip { name: "Kovács Lilla"; speakerIndex: 0 }
//   SummaryOwnerChip { name: "Fehér Gábor → Varga Árpád?"; warn: true; dotIndex: 2; compact: true }
Rectangle {
    id: root
    property string name: ""
    property int speakerIndex: -1          // -1: not a meeting speaker
    property bool warn: false
    property int dotIndex: speakerIndex    // dot color (warn: the new speaker's; -1: no dot)
    property bool compact: false

    property real maximumWidth: 240        // a longer name is elided

    readonly property bool person: warn ? dotIndex >= 0 : speakerIndex >= 0
    readonly property real dotSize: compact ? 16 : 18
    readonly property real gap: compact ? 5 : 6
    readonly property real chrome: person ? 3 + dotSize + gap + (compact ? 8 : 9) : 18

    implicitHeight: compact ? 22 : 24
    implicitWidth: Math.min(maximumWidth, label.implicitWidth + chrome)
    radius: height / 2
    color: warn ? Theme.warnSoft : person ? Theme.speakerSoft(speakerIndex) : Theme.sunken
    border.width: !warn && person && compact ? 1 : 0
    border.color: person && !warn ? Theme.speakerLine(speakerIndex) : "transparent"

    TDashedRect {
        visible: root.warn
        anchors.fill: parent
        radius: root.radius
        color: Theme.warnLine
    }
    Rectangle {
        visible: root.person
        x: 3
        anchors.verticalCenter: parent.verticalCenter
        width: root.dotSize; height: root.dotSize; radius: root.dotSize / 2
        color: root.person ? Theme.speakerLine(root.warn ? root.dotIndex : root.speakerIndex) : "transparent"
    }
    Text {
        id: label
        x: root.person ? 3 + root.dotSize + root.gap : 9
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, root.maximumWidth - root.chrome)
        elide: Text.ElideRight
        text: root.name
        color: root.warn ? Theme.warnInk : root.person ? Theme.speakerInk(root.speakerIndex) : Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontCaption
        font.weight: Theme.weightSemiBold
    }
}
