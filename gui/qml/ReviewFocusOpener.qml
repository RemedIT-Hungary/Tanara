import QtQuick
import QtQuick.Layouts

// The round-start OPENER SUGGESTION of To review ("D" mode): one big, strong group at once, in
// place of the focus card, before the lines come one by one. Title (the group's title), "N lines
// -> Y" (or "-> new speaker"), evidence chips, the effect in advance ("Y +N lines (+n samples) ·
// X -N · uncertain remains: K"), 3 time-spread spot checks (play time + text; after listening
// ok / bad by your ear; "k/3 listened - this is no guarantee"), and the buttons with key
// labels: [Enter All N -> Y] [E One by one] [Right Later]. If a spot check is bad, the default
// (Enter) becomes "One by one" - the highlight switches too. Shift = not a sample.
// ReviewFocusView keeps the state (listened, verdict, playing now); this only draws.
//   opener: an item of ReviewQueueModel.openers() (+ the view's fresh pendingCount)
//   spots:  the spot-check lines [{ utteranceId, timeLabel, lineText }] (from the queue's get())
Rectangle {
    id: root

    property var opener: ({})
    property var spots: []
    property var lanes: []                  // TranscriptEditorViewModel.lanes (for the colors)
    property int position: 0                // index of the opener (from 0)
    property int total: 1
    property int remainAfter: 0             // "uncertain remains": the round's open lines after this opener
    property var heard: [false, false, false]
    property var judged: [true, true, true] // true = ok (matched), false = bad
    property int playingSpot: -1
    property bool shiftHeld: false

    signal acceptClicked()
    signal singleClicked()
    signal laterClicked()
    signal spotClicked(int index)           // play
    signal verdictClicked(int index)        // toggle ok / bad

    readonly property int count: opener.pendingCount !== undefined ? opener.pendingCount : (opener.count || 0)
    readonly property int sampleCount: Math.min(count, opener.sampleCount || 0)
    readonly property string currentName: opener.currentName || ""
    readonly property string newSide: opener.newSide || ""
    readonly property string targetName: (opener.proposedName || "") !== "" ? opener.proposedName
                                        : newSide === "remote" ? qsTr("új beszélő (hívás hangja)")
                                        : newSide === "local" ? qsTr("új beszélő (mikrofon)") : qsTr("új beszélő")
    readonly property bool toNew: (opener.proposedName || "") === "" && (opener.proposedKey || "") === ""
    readonly property int heardCount: heard.filter(h => h).length
    readonly property int matchedCount: heard.filter((h, i) => h && judged[i]).length
    readonly property bool doubt: heard.some((h, i) => h && !judged[i])

    objectName: "reviewFocusOpener"
    implicitHeight: col.implicitHeight + 40
    height: implicitHeight
    radius: Theme.radiusDialog
    color: Theme.raised
    border.width: 2
    border.color: doubt ? Theme.warnLine : Theme.accentLine

    // "All 5 -> Y" / "All 16 -> Y": the Hungarian article (a / az) follows the number's pronunciation.
    function allTo(n, name) {
        const az = n === 1 || (n >= 1000 && n < 2000) || String(n).charAt(0) === "5"
        return az ? qsTr("Mind az %1 → %2").arg(n).arg(name) : qsTr("Mind a %1 → %2").arg(n).arg(name)
    }
    function laneOf(key) {
        for (let i = 0; i < lanes.length; ++i) if (lanes[i].key === key) return lanes[i]
        return null
    }
    function colorOf(key) {
        const l = laneOf(key)
        return l ? (l.colorIndex || 0) : 0
    }

    ColumnLayout {
        id: col
        x: 22
        y: 20
        width: root.width - 44
        spacing: 12

        // ---- header: what this is, which one ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TPill {
                objectName: "reviewOpenerBadge"
                text: root.total > 1 ? qsTr("Nyitó-javaslat %1 / %2 · a kör elején").arg(root.position + 1).arg(root.total)
                                     : qsTr("Nyitó-javaslat · a kör elején")
                tone: "accent"
            }
            TLabel {
                Layout.fillWidth: true
                text: qsTr("nagy, erős csoport — egyben, mielőtt a sorok egyenként jönnek")
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }

        // ---- the group's title + from -> to ----
        TLabel {
            objectName: "reviewOpenerTitle"
            Layout.fillWidth: true
            text: root.opener.title || ""
            font.pixelSize: Theme.fontHeading
            font.weight: Theme.weightSemiBold
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TAvatar {
                name: root.currentName
                speakerIndex: root.colorOf(root.opener.currentKey || "")
                size: 24
            }
            TLabel {
                text: root.currentName
                font.pixelSize: Theme.fontBody
                font.weight: Theme.weightSemiBold
                color: Theme.speakerInk(root.colorOf(root.opener.currentKey || ""))
            }
            TIcon { name: "arrow-right"; size: 16; color: Theme.textMuted }
            TAvatar {
                visible: !root.toNew
                name: root.targetName
                speakerIndex: root.colorOf(root.opener.proposedKey || "")
                size: 24
            }
            TLabel {
                objectName: "reviewOpenerTarget"
                text: root.toNew ? root.targetName : qsTr("%n sor → %1", "", root.count).arg(root.targetName)
                font.pixelSize: Theme.fontBody
                font.weight: Theme.weightSemiBold
                color: root.toNew ? Theme.text : Theme.speakerInk(root.colorOf(root.opener.proposedKey || ""))
            }
            TLabel {
                visible: root.toNew
                text: qsTr("· %n sor", "", root.count)
                muted: true
                font.pixelSize: Theme.fontBody
            }
            Item { Layout.preferredWidth: 4 }
            // Evidence chips; clipped when space is tight.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20
                clip: true
                Row {
                    spacing: 6
                    Repeater {
                        model: root.opener.evidence || []
                        delegate: EvidenceChip {
                            required property var modelData
                            kind: modelData.kind === "manual" ? "side" : (modelData.kind || "voice")
                            polarity: modelData.polarity || "neutral"
                            side: modelData.side || ""
                            text: modelData.text || ""
                        }
                    }
                }
            }
        }

        // ---- effect in advance (diff) ----
        Rectangle {
            objectName: "reviewOpenerImpact"
            Layout.fillWidth: true
            implicitHeight: impact.implicitHeight + 14
            radius: Theme.radiusControl
            color: Theme.sunken
            Flow {
                id: impact
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 12 }
                spacing: 14
                TLabel { text: qsTr("Hatás, ha elfogadod:"); muted: true; font.pixelSize: Theme.fontSmall }
                TLabel {
                    objectName: "reviewOpenerImpactGain"
                    text: root.shiftHeld ? qsTr("%1 +%2 sor (nem minta)").arg(root.toNew ? qsTr("új beszélő") : root.targetName).arg(root.count)
                                         : qsTr("%1 +%2 sor (+%3 minta)").arg(root.toNew ? qsTr("új beszélő") : root.targetName)
                                                                          .arg(root.count).arg(root.sampleCount)
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightSemiBold
                    color: root.toNew ? Theme.text : Theme.speakerInk(root.colorOf(root.opener.proposedKey || ""))
                }
                TLabel {
                    text: qsTr("%1 −%2").arg(root.currentName).arg(root.count)
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightSemiBold
                    color: Theme.speakerInk(root.colorOf(root.opener.currentKey || ""))
                }
                TLabel {
                    objectName: "reviewOpenerRemain"
                    text: qsTr("kétes marad: %1").arg(root.remainAfter)
                    muted: true
                    font.pixelSize: Theme.fontSmall
                }
            }
        }

        // ---- spot check ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TSectionLabel { text: qsTr("Szúrópróba · %n időben távoli sor", "", root.spots.length) }
            TLabel {
                objectName: "reviewOpenerSpotStatus"
                Layout.fillWidth: true
                text: root.heardCount === 0 ? qsTr("1–3: lejátszás · meghallgatás után ✓ / ✗ (Shift+1–3)")
                     : qsTr("%1/%2 meghallgatva · %3 egyezett — ez nem garancia")
                           .arg(root.heardCount).arg(root.spots.length).arg(root.matchedCount)
                color: root.doubt ? Theme.dangerInk : Theme.textMuted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
        Column {
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: root.spots
                delegate: Rectangle {
                    id: spot
                    required property var modelData
                    required property int index
                    readonly property bool playingNow: root.playingSpot === index
                    readonly property bool wasHeard: !!root.heard[index]
                    readonly property bool ok: root.judged[index] !== false
                    objectName: "reviewOpenerSpot"
                    width: parent.width
                    height: 32
                    radius: Theme.radiusControl
                    color: playingNow ? Theme.accentSoft
                         : wasHeard && !ok ? Theme.dangerSoft
                         : spotHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                    HoverHandler { id: spotHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.spotClicked(spot.index) }
                    RowLayout {
                        anchors { fill: parent; leftMargin: 8; rightMargin: 6 }
                        spacing: 10
                        TLabel {
                            text: String(spot.index + 1)
                            mono: true
                            muted: true
                            font.pixelSize: Theme.fontMicro
                            Layout.preferredWidth: 10
                        }
                        TIcon { name: spot.playingNow ? "volume-2" : "play"; size: 14; color: Theme.accent }
                        TLabel {
                            text: spot.modelData.timeLabel || ""
                            mono: true
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            Layout.preferredWidth: 56
                        }
                        TLabel {
                            text: "„" + (spot.modelData.lineText || "") + "”"
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        TLabel {
                            visible: spot.playingNow
                            text: qsTr("szól…")
                            font.pixelSize: Theme.fontCaption
                            color: Theme.accent
                        }
                        // ok / bad: your ear's verdict (only after listening).
                        Rectangle {
                            objectName: "reviewOpenerVerdict"
                            visible: spot.wasHeard
                            implicitWidth: verdictRow.implicitWidth + 14
                            implicitHeight: 24
                            radius: 12
                            color: spot.ok ? Theme.successSoft : Theme.dangerSoft
                            border.width: 1
                            border.color: spot.ok ? Theme.successLine : Theme.dangerLine
                            Row {
                                id: verdictRow
                                anchors.centerIn: parent
                                spacing: 4
                                TIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: spot.ok ? "check" : "x"
                                    size: 13
                                    color: spot.ok ? Theme.success : Theme.danger
                                }
                                TLabel {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: spot.ok ? qsTr("egyezett") : qsTr("nem egyezett")
                                    font.pixelSize: Theme.fontCaption
                                    color: spot.ok ? Theme.successInk : Theme.dangerInk
                                }
                            }
                            HoverHandler { id: verdictHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.verdictClicked(spot.index) }
                            TToolTip {
                                visible: verdictHover.hovered
                                text: qsTr("Kattintás (vagy Shift+%1): ✓ / ✗ váltás — a füled ítélete").arg(spot.index + 1)
                            }
                        }
                        TLabel {
                            visible: !spot.wasHeard && !spot.playingNow
                            text: "—"
                            muted: true
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                }
            }
        }

        // ---- decision ----
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ReviewFocusKeyButton {
                objectName: "reviewOpenerAccept"
                keyText: root.doubt ? "" : "Enter"
                variant: root.doubt ? "secondary" : "primary"
                text: root.allTo(root.count, root.targetName)
                nonSample: root.shiftHeld
                enabled: root.count > 0
                toolTipText: qsTr("Egy visszavonási lépés · Shift: egyik sem lesz minta")
                onClicked: root.acceptClicked()
            }
            ReviewFocusKeyButton {
                objectName: "reviewOpenerSingle"
                keyText: root.doubt ? "Enter · E" : "E"
                variant: root.doubt ? "primary" : "secondary"
                text: qsTr("Egyenként")
                toolTipText: qsTr("A %n sor a munkalista elejére kerül, és egyenként jön", "", root.count)
                onClicked: root.singleClicked()
            }
            ReviewFocusKeyButton {
                objectName: "reviewOpenerLater"
                keyText: "→"
                variant: "ghost"
                text: qsTr("Később")
                toolTipText: qsTr("A nyitó kimarad; a sorok a helyükön maradnak a munkalistán")
                onClicked: root.laterClicked()
            }
            Item { width: 8; height: Theme.controlHeight }
            TLabel {
                height: Theme.controlHeight
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Shift = nem minta")
                font.pixelSize: Theme.fontSmall
                color: root.shiftHeld ? Theme.warnInk : Theme.textMuted
            }
        }
    }
}
