import QtQuick

// Egy jelölt a „Kinek a sora ez?" (E1 JAVASOLT) és a „Miért ő?" (E3 NEM Ő? VALÓJÁBAN…)
// listában: 24 px avatar (a meeting beszélőjénél a színében; meetingen kívüli személynél
// semleges, szaggatott), név 13.5/600 + alsor („86 sor itt" / „most ő" / „12 megbeszélés"),
// alatta a bizonyíték-chipek (EvidenceChip), jobbra a gyorsbillentyű (1–3). A másik sávon
// beszélő jelölt és a mostani beszélő halvány (60 %), de választható.
Item {
    id: root

    property string personName: ""
    property int colorIndex: -1
    property string subText: ""
    property var evidence: []
    property string keyHint: ""
    property bool dimmed: false
    property bool highlighted: false
    // A támogató bizonyíték accent színnel (a legjobb jelöltnél); különben semleges.
    property bool strongEvidence: false
    readonly property alias hovered: hover.hovered

    signal clicked()

    implicitHeight: Math.max(38, content.implicitHeight + 14)
    opacity: dimmed ? 0.6 : 1
    Accessible.role: Accessible.ListItem
    Accessible.name: personName

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: root.highlighted || hover.hovered ? Theme.sunken : "transparent"
    }

    TAvatar {
        id: avatar
        visible: root.colorIndex >= 0
        x: 6; y: 7
        size: 24
        name: root.personName
        speakerIndex: Math.max(0, root.colorIndex)
    }
    // Meetingen kívüli személy: semleges, szaggatott avatar.
    Item {
        visible: root.colorIndex < 0
        x: 6; y: 7
        width: 24; height: 24
        TDashedRect { anchors.fill: parent; radius: 12; color: Theme.borderStrong }
        TLabel {
            anchors.centerIn: parent
            text: Theme.monogram(root.personName)
            muted: true
            font.pixelSize: 9
            font.weight: Theme.weightBold
        }
    }

    Column {
        id: content
        x: 40
        y: 7
        width: root.width - x - (keyBox.visible ? keyBox.width + 12 : 8)
        spacing: 4
        Row {
            spacing: 8
            TLabel {
                text: root.personName
                font.pixelSize: Theme.fontSmall + 0.5
                font.weight: Theme.weightSemiBold
                elide: Text.ElideRight
                width: Math.min(implicitWidth, content.width - sub.implicitWidth - 8)
            }
            TLabel {
                id: sub
                anchors.verticalCenter: parent.verticalCenter
                text: root.subText
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }
        Flow {
            visible: root.evidence.length > 0
            width: parent.width
            spacing: 4
            Repeater {
                model: root.evidence
                EvidenceChip {
                    required property var modelData
                    kind: modelData.kind === "manual" ? "side" : modelData.kind
                    polarity: modelData.polarity
                    side: modelData.side || ""
                    text: modelData.text
                    quiet: !root.strongEvidence
                    HoverHandler { id: chipHover }
                    TToolTip { visible: chipHover.hovered && (modelData.detail || "") !== ""; text: modelData.detail || ""; delay: 300 }
                }
            }
        }
    }

    Rectangle {
        id: keyBox
        visible: root.keyHint !== ""
        anchors.right: parent.right
        anchors.rightMargin: 8
        y: 9
        width: keyLabel.implicitWidth + 10
        height: 18
        radius: Theme.radiusTag
        color: "transparent"
        border.width: 1
        border.color: Theme.border
        TLabel {
            id: keyLabel
            anchors.centerIn: parent
            text: root.keyHint
            mono: true
            muted: true
            font.pixelSize: Theme.fontMicro
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }
}
