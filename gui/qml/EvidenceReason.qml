import QtQuick

// Egy ok a „Miért nem X?" (E1) és a „Miért ő?" (E3) blokkban: ikon (a bizonyíték fajtája;
// zöld = mellette szól, warn = ellene / figyelmeztet), „Hang:" + a mondat, jobbra a javítás
// helye (accent link: Sáv-beosztás · Minták · Címkéi · Módosítás · Kettejük átnézése).
//   EvidenceReason { evidence: editorVm.whyNot(id)[0]; onFixClicked: ev => … }
// `evidence`: a TranscriptEditorViewModel.evidenceMap() térképe.
Item {
    id: root

    property var evidence: ({})
    property bool fixEnabled: true
    signal fixClicked(var evidence)

    readonly property string kind: evidence.kind || ""
    readonly property string polarity: evidence.polarity || "neutral"
    readonly property string iconName: {
        switch (kind) {
        case "voice": return "audio-lines"
        case "side": return polarity === "contradict" ? "arrow-right-left" : evidence.side === "mic" ? "mic" : "phone"
        case "tag": return "tag"
        case "similarity": return "triangle-alert"
        case "calendar": return "calendar"
        case "lines": return "list"
        }
        return "info"
    }
    readonly property color iconColor: kind === "similarity" || polarity === "contradict" ? Theme.warnInk
                                     : polarity === "support" ? Theme.successInk : Theme.textMuted
    readonly property bool hasFix: fixEnabled && (evidence.fixLabel || "") !== ""

    function escaped(t) { return String(t || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }

    implicitWidth: 320
    implicitHeight: Math.max(26, label.implicitHeight + 10)

    TIcon {
        x: 4
        y: 6
        name: root.iconName
        size: 14
        color: root.iconColor
    }
    TLabel {
        id: label
        x: 27
        y: 5
        width: root.width - x - (fix.visible ? fix.implicitWidth + 14 : 4)
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.pixelSize: Theme.fontSmall
        cssLineHeight: 1.4
        text: "<span style=\"font-weight:500\">" + root.escaped(root.evidence.label) + "</span> "
              + "<font color=\"" + Theme.textMuted + "\">" + root.escaped(root.evidence.sentence || root.evidence.text) + "</font>"
    }
    TLabel {
        id: fix
        objectName: "fixLink"
        visible: root.hasFix
        anchors.right: parent.right
        anchors.rightMargin: 4
        y: 5
        text: root.evidence.fixLabel || ""
        color: Theme.accent
        font.pixelSize: Theme.fontSmall - 0.5
        font.weight: Theme.weightMedium
        font.underline: fixHover.hovered
        HoverHandler { id: fixHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.fixClicked(root.evidence) }
        Accessible.role: Accessible.Link
        Accessible.name: text
    }
}
