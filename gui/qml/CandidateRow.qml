import QtQuick

// One candidate in the "Whose line is this?" (SUGGESTED) and "Why them?" (NOT THEM? ACTUALLY...)
// lists: 24 px avatar (in the meeting speaker's color; for a person outside the meeting neutral,
// dashed), name 13.5/600 + sub-row ("86 lines here" / "this one now" / "12 meetings"), below
// it the evidence chips (EvidenceChip), on the right the shortcut (1-3). A candidate who speaks
// on the other track and the current speaker are faint (60%) but selectable.
// quickTagLabel: not empty -> at the end of the chips a dashed accent "+ #Nordvik" (the meeting's
// tag is not on the person yet): one click applies it (quickTagClicked), without choosing the row.
Item {
    id: root

    property string personName: ""
    property int colorIndex: -1
    property string subText: ""
    property var evidence: []
    property string keyHint: ""
    property bool dimmed: false
    property bool highlighted: false
    // The supporting evidence in the accent color (for the best candidate); otherwise neutral.
    property bool strongEvidence: false
    property string quickTagLabel: ""
    readonly property alias hovered: hover.hovered

    signal clicked()
    signal quickTagClicked()

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
    // Person outside the meeting: neutral, dashed avatar.
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
            visible: root.evidence.length > 0 || root.quickTagLabel !== ""
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
            // Quick tag: dashed accent chip (does not trigger the row's click).
            Item {
                id: quickTag
                objectName: "quickTagChip"
                visible: root.quickTagLabel !== ""
                width: quickLabel.implicitWidth + 12
                height: 20
                activeFocusOnTab: visible
                Accessible.role: Accessible.Button
                Accessible.name: root.quickTagLabel
                Keys.onSpacePressed: root.quickTagClicked()
                Keys.onReturnPressed: root.quickTagClicked()
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusTag
                    color: quickHover.hovered ? Theme.accentSoft : "transparent"
                    TFocusRing { visible: quickTag.activeFocus; targetRadius: parent.radius }
                }
                TDashedRect { anchors.fill: parent; radius: Theme.radiusTag; color: Theme.accent }
                TLabel {
                    id: quickLabel
                    anchors.centerIn: parent
                    text: root.quickTagLabel
                    color: Theme.accent
                    font.pixelSize: Theme.fontMicro + 1
                    font.weight: Theme.weightSemiBold
                }
                HoverHandler { id: quickHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.quickTagClicked() }
                TToolTip { visible: quickHover.hovered; text: qsTr("Rátesszük a címkét a személyre — egy kattintás, visszavonható") }
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
    TapHandler {
        id: rowTap
        // A click on the quick tag chip does not choose the candidate. We decide on press: the chip
        // disappears on its own click, before this handler receives the release.
        property bool onQuickTag: false
        onPressedChanged: if (pressed) onQuickTag = quickTag.visible
                                                   && quickTag.contains(quickTag.mapFromItem(root, point.position))
        onTapped: if (!onQuickTag) root.clicked()
    }
}
