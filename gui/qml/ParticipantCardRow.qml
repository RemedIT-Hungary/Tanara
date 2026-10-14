import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// One row of the "Who was there?" card (ParticipantsCard; OverviewViewModel.participants[i]).
// Line 1: ▶ voice sample (SampleButton: before the transcript the cluster's sample, after it the
// speaker's longest line; reserved space when the row has none) · avatar (dashed "?" for an
// unknown voice / a suggestion) · name + subline ("Unknown voice N · anonymous · nameable", "24
// lines", "added without a voice"; while the sample plays: "▶ sample · 12:41–12:47") · talk share ·
// actions by state:
//   unknown voice      "Name..." (the naming panel) + ⋯
//   doubtful (named)   "doubtful" pill + "Was there" (ParticipantService::confirmParticipant; the
//                      pressed button undoes it) + ⋯
//   voiceless (manual) "Which voice?" (the "Which voice is it?" panel) + ⋯
//   suggestion         "Was there" (adds the person) + x (rejects for this meeting)
//   participant        ⋯ (the naming panel: another name, "Same voice as...", "Was not there")
// and a hover x ("Was not there": removal, Ctrl+Z brings it back) on removable rows.
// Line 2 (under the name): the evidence badges (rendered from keys in the facade; tooltip = the
// explanation; after the transcript a click opens "Why them?") | a divider | the "Track:" control
// (ParticipantTrackControl; a control, not a badge).
Item {
    id: root

    property var row: ({})
    property var binder: null                  // ParticipantTrackBinder
    property var player: null                  // PlayerController
    property bool canExplain: false            // badges open "Why them?" (after the transcript)
    property bool showTopBorder: false
    property bool highlighted: false           // its panel is open
    property bool wide: false                  // room for the track state in words
    property bool canAssignVoice: true         // there are unnamed voices ("Which voice?")

    signal nameRequested(var row, Item anchor)
    signal whichVoiceRequested(var row, Item anchor)
    signal confirmRequested(string id, bool confirmed)
    signal addRequested(string name)
    signal removeRequested(string id)
    signal whyRequested(string speakerKey, Item anchor)

    readonly property bool suggested: row.suggested === true
    readonly property bool anonymous: row.anonymous === true && !suggested
    readonly property bool voiceless: row.voiceless === true && !suggested
    readonly property bool doubtful: row.doubtful === true && !suggested
    readonly property bool confirmed: row.approved === true
    readonly property bool hasTalk: row.talkShare !== undefined && row.talkShare >= 0
    readonly property bool hasSample: (row.sampleTrackId || "") !== "" && row.sampleStartMs >= 0
                                      && row.sampleEndMs > row.sampleStartMs
    readonly property bool sampling: hasSample && player !== null && player.rangePlaying
                                     && player.rangeKey === row.sampleTrackId + "#" + row.sampleStartMs + "-" + row.sampleEndMs
    readonly property bool trackable: !suggested && row.unheard !== true && (row.id || "") !== ""

    function clock(ms) {
        const s = Math.max(0, Math.round(ms / 1000))
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
        return (h > 0 ? h + ":" + String(m).padStart(2, "0") : String(m)) + ":" + String(sec).padStart(2, "0")
    }
    readonly property string subText: {
        if (sampling) return "▶ " + qsTr("minta · %1–%2").arg(clock(row.sampleStartMs)).arg(clock(row.sampleEndMs))
        const parts = []
        if ((row.lineCount || 0) > 0) parts.push(qsTr("%n sor", "", row.lineCount))
        if (voiceless && row.unheard !== true) parts.push(qsTr("hang nélkül vettük fel"))
        else if (doubtful && !confirmed) parts.push(qsTr("kétes · hallgasd meg"))
        if ((row.sub || "") !== "") parts.push(row.sub)
        return parts.join(" · ")
    }

    objectName: "participantRow"
    implicitHeight: Math.max(50, col.implicitHeight + 16)
    opacity: row.dimmed === true ? 0.55 : 1

    Rectangle { anchors.fill: parent; color: Theme.accentSoft; opacity: 0.55; visible: root.highlighted }
    Rectangle { visible: root.showTopBorder; width: parent.width; height: 1; color: Theme.border }
    HoverHandler { id: rowHover }

    // Source badge (evidence; rendered from keys by the facade).
    component SourceBadge: T.AbstractButton {
        id: badge
        property var source: ({})
        property string speakerKey: ""
        readonly property bool negative: source.negative === true
        readonly property bool accent: source.accent === true
        readonly property bool clickable: root.canExplain && speakerKey !== ""
        readonly property string tip: (source.detail || "") !== "" ? source.detail : (source.text || "")
        objectName: "participantBadge"
        hoverEnabled: true
        focusPolicy: clickable ? Qt.TabFocus : Qt.NoFocus
        implicitWidth: badgeRow.implicitWidth + 12
        implicitHeight: 20
        Accessible.name: badge.tip
        Accessible.role: clickable ? Accessible.Button : Accessible.StaticText
        onClicked: if (clickable) root.whyRequested(speakerKey, badge)
        HoverHandler { cursorShape: badge.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
        TToolTip {
            visible: badge.hovered && badge.tip !== ""
            delay: 300
            text: badge.clickable ? badge.tip + "\n" + qsTr("Kattints: Miért ő? (javítás a szerkesztőben)") : badge.tip
        }
        background: Item {}
        contentItem: Item {}
        Rectangle {
            anchors.fill: parent
            visible: !badge.negative
            radius: Theme.radiusTag
            color: badge.accent ? Theme.accentSoft : Theme.sunken
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusTag
            color: Theme.stateLayer
            opacity: badge.clickable ? (badge.down ? Theme.pressedOpacity : badge.hovered ? Theme.hoverOpacity : 0) : 0
        }
        TFocusRing { visible: badge.visualFocus; targetRadius: Theme.radiusTag }
        TDashedRect {
            anchors.fill: parent
            visible: badge.negative
            radius: Theme.radiusTag
            color: Theme.borderStrong
        }
        Row {
            id: badgeRow
            x: 5
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            TIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: badge.source.icon || "info"
                size: 11
                color: badge.accent ? Theme.accent : Theme.textMuted
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: badge.source.text || ""
                color: badge.accent ? Theme.accent : badge.negative ? Theme.textMuted : Theme.text
                font.pixelSize: 12
            }
        }
    }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                  leftMargin: 12; rightMargin: 10 }
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            // The voice sample (the space is kept when there is none: the avatars align).
            Item {
                implicitWidth: 26; implicitHeight: 26
                SampleButton {
                    anchors.fill: parent
                    player: root.player
                    row: root.row
                }
                Rectangle {
                    anchors.fill: parent
                    visible: !root.hasSample || root.player === null
                    radius: 13
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.border
                    opacity: 0.6
                    TIcon { anchors.centerIn: parent; name: "play"; size: 11; color: Theme.borderStrong }
                    HoverHandler { id: noSampleHover }
                    TToolTip {
                        visible: noSampleHover.hovered
                        delay: 300
                        text: root.suggested || root.voiceless ? qsTr("Még nem hallottuk: nincs minta")
                                                                 : qsTr("Ehhez a sorhoz nincs meghallgatható minta")
                    }
                }
            }
            Item {
                implicitWidth: 26; implicitHeight: 26
                TAvatar {
                    anchors.fill: parent
                    visible: !root.suggested && !root.anonymous
                    size: 26
                    speakerIndex: Math.max(0, root.row.colorIndex || 0)
                    monogram: root.row.monogram || "?"
                }
                TDashedRect {
                    anchors.fill: parent
                    visible: root.suggested || root.anonymous
                    radius: 13
                    lineWidth: 1.5
                    color: Theme.borderStrong
                }
                TLabel {
                    anchors.centerIn: parent
                    visible: root.suggested || root.anonymous
                    text: root.anonymous ? "?" : (root.row.monogram || "?")
                    muted: true
                    font.pixelSize: root.anonymous ? 11 : 9
                    font.weight: Theme.weightBold
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                TLabel {
                    objectName: "participantName"
                    Layout.fillWidth: true
                    text: root.row.name || ""
                    color: root.suggested || root.anonymous || root.row.dimmed === true ? Theme.textMuted : Theme.text
                    font.pixelSize: 13
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                TLabel {
                    objectName: "participantSub"
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: root.subText
                    color: root.sampling ? Theme.accent : Theme.textMuted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            RowLayout {
                visible: root.hasTalk
                Layout.preferredWidth: 92
                Layout.maximumWidth: 92
                spacing: 6
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 5
                    radius: 3
                    color: Theme.sunken
                    Rectangle {
                        width: parent.width * Math.min(1, Math.max(0.02, root.row.talkShare || 0))
                        height: parent.height
                        radius: 3
                        color: Theme.speakerLine(Math.max(0, root.row.colorIndex || 0))
                    }
                }
                TLabel {
                    Layout.preferredWidth: 30
                    horizontalAlignment: Text.AlignRight
                    text: Math.round((root.row.talkShare || 0) * 100) + "%"
                    mono: true
                    muted: true
                    font.pixelSize: 12
                }
            }

            // ---- actions ----
            TPill {
                objectName: "participantDoubtPill"
                visible: root.doubtful && !root.confirmed
                text: qsTr("kétes")
                tone: "warn"
            }
            TButton {
                id: nameButton
                objectName: "participantNameButton"
                visible: root.anonymous
                text: qsTr("Elnevezés…")
                iconName: "user-plus"
                iconSize: 14
                variant: "ghost"
                size: "small"
                toolTipText: qsTr("Kié ez a hang? Meghallgatod, majd nevet adsz neki")
                onClicked: root.nameRequested(root.row, nameButton)
            }
            TButton {
                id: whichButton
                objectName: "participantWhichVoiceButton"
                visible: root.voiceless && root.row.unheard !== true && root.canAssignVoice
                text: qsTr("Melyik hang?")
                iconName: "ear"
                iconSize: 14
                variant: "ghost"
                size: "small"
                toolTipText: qsTr("Hozzárendeled egy talált hanghoz: a két sor egy lesz")
                onClicked: root.whichVoiceRequested(root.row, whichButton)
            }
            TButton {
                objectName: "participantConfirmButton"
                visible: root.doubtful || root.suggested
                text: qsTr("Ott volt")
                iconName: "check"
                iconSize: 14
                size: "small"
                variant: root.confirmed && !root.suggested ? "primary" : "secondary"
                toolTipText: root.suggested ? qsTr("Felveszed a résztvevők közé")
                           : root.confirmed ? qsTr("Megerősítetted, hogy ott volt · kattintva visszavonod")
                           : qsTr("Meghallgattad, ő az: jóváhagyáskor a beszéde a nevére kerül")
                onClicked: root.suggested ? root.addRequested(root.row.name)
                                          : root.confirmRequested(root.row.id, !root.confirmed)
            }
            TIconButton {
                id: moreButton
                objectName: "participantMoreButton"
                visible: !root.suggested && root.row.unheard !== true
                variant: "flat"
                size: "small"
                iconName: "ellipsis"
                iconSize: 14
                iconColor: Theme.textMuted
                toolTipText: root.anonymous ? qsTr("Ugyanaz a hang, mint… · Nem résztvevő")
                                            : qsTr("Másik név… · Ugyanaz a hang, mint… · Nem volt ott")
                onClicked: root.nameRequested(root.row, moreButton)
            }
            TIconButton {
                objectName: "participantRemove"
                // The space is reserved (the row does not jump on hover); only the visibility toggles.
                visible: root.row.removable === true
                readonly property bool shown: root.suggested || rowHover.hovered || activeFocus
                opacity: shown ? 1 : 0
                enabled: shown
                variant: "flat"
                size: "small"
                iconName: "x"
                iconSize: 13
                iconColor: Theme.textMuted
                toolTipText: root.suggested ? qsTr("Nem volt ott: a javaslat ennél a megbeszélésnél nem jön elő (Ctrl+Z visszahozza)")
                           : root.voiceless ? qsTr("Törlés a résztvevők közül (Ctrl+Z visszavonja)")
                           : qsTr("Nem volt ott: törlés a résztvevők közül, a sorai névtelenek lesznek (Ctrl+Z visszavonja)")
                onClicked: root.removeRequested(root.row.id)
            }
        }

        // Line 2: why we think so (badges) | which track (control).
        RowLayout {
            readonly property bool anyBadge: badgeRepeater.count > 0
            visible: anyBadge || trackControl.chips.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 72
            spacing: 8
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    id: badgeRepeater
                    // The track control shows the side: the (non-contradicting) mic / call badge is omitted then.
                    model: trackControl.chips.length > 0
                           ? (root.row.sources || []).filter(s => s.negative === true || (s.icon !== "mic" && s.icon !== "phone"))
                           : (root.row.sources || [])
                    SourceBadge {
                        required property var modelData
                        source: modelData
                        speakerKey: root.row.speakerKey || ""
                    }
                }
            }
            Rectangle {
                visible: parent.anyBadge && trackControl.chips.length > 0
                implicitWidth: 1
                implicitHeight: 18
                color: Theme.border
            }
            ParticipantTrackControl {
                id: trackControl
                binder: root.binder
                participantId: root.trackable ? (root.row.id || "") : ""
                showState: root.wide
            }
        }
    }
}
