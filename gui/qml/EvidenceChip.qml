import QtQuick

// Bizonyíték-chip (handoff-v3, 13. döntés): miért javasoljuk / miért kétes. 20 px, sugár 4,
// ikon fajtánként (hang, sáv: mikrofon / hívás, naptár, címke, sorok, hasonlóság, kézi).
//   polarity: "support"    jó: accentSoft + accent (quiet: true → semleges, mint a V2 soraiban)
//             "neutral"    semleges: sunken + text
//             "contradict" ellentmondó: szaggatott borderStrong keret, halvány felirat, × / ⚠ ikon
//   EvidenceChip { kind: "voice"; polarity: "support"; text: "hang 84%" }
//   EvidenceChip { kind: "side"; side: "loopback"; polarity: "neutral"; text: "hívás-sáv" }
// A „Ki volt ott?" párbeszéd és a szerkesztő (E1 popover, E2 csoportok) közös eleme.
Item {
    id: root

    property string kind: "voice"       // voice | side | calendar | tag | lines | similarity | manual
    property string polarity: "neutral" // support | neutral | contradict
    property string side: ""            // side fajtánál: "mic" | "loopback" | ""
    property string text: ""
    property bool quiet: false          // a támogató bizonyíték is semleges színnel

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
