import QtQuick

// Evidence chip: why we suggest it / why it is doubtful. 20 px, radius 4, an icon per kind (voice,
// track: mic / call, calendar, tag, lines, similarity, manual).
//   polarity: "support"    good: accentSoft + accent (quiet: true -> neutral, as in the V2 rows)
//             "neutral"    neutral: sunken + text
//             "contradict" contradicting: dashed borderStrong border, faint label, x / warning icon
//   EvidenceChip { kind: "voice"; polarity: "support"; text: "voice 84%" }
//   EvidenceChip { kind: "side"; side: "loopback"; polarity: "neutral"; text: "call-audio track" }
// Shared element of the "Who was there?" dialog and the editor (line popover, groups).
Item {
    id: root

    property string kind: "voice"       // voice | side | calendar | tag | lines | similarity | manual
    property string polarity: "neutral" // support | neutral | contradict
    property string side: ""            // for kind "side": "mic" | "loopback" | ""
    property string text: ""
    property bool quiet: false          // supporting evidence also in the neutral color

    readonly property bool good: polarity === "support" && !quiet
    readonly property bool bad: polarity === "contradict"
    readonly property string iconName: {
        if (bad) return kind === "tag" ? "triangle-alert" : "x"
        switch (kind) {
        case "voice": return "audio-lines"
        case "side": return side === "mic" ? "mic" : side === "loopback" ? "phone" : "monitor-speaker"
        case "calendar": return "calendar"
        case "tag": return "tag"
        case "lines": return "list"
        case "similarity": return "audio-lines"
        case "manual": return "hand"
        }
        return "info"
    }
    readonly property color ink: good ? Theme.accent : bad ? Theme.textMuted : Theme.text

    implicitHeight: 20
    implicitWidth: row.implicitWidth + 5 + 7

    Rectangle {
        anchors.fill: parent
        visible: !root.bad
        radius: Theme.radiusTag
        color: root.good ? Theme.accentSoft : Theme.sunken
    }
    TDashedRect {
        anchors.fill: parent
        visible: root.bad
        radius: Theme.radiusTag
        color: Theme.borderStrong
    }
    Row {
        id: row
        x: 5
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        TIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            size: 11
            color: root.ink
        }
        TLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.ink
            font.pixelSize: Theme.fontMicro + 1
        }
    }
}
