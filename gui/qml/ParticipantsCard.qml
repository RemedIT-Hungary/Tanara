import QtQuick
import QtQuick.Layouts

// The "Who was there?" card of the Overview (A variant: the only place; every change is immediate
// and one step of the meeting's shared undo, the change bar / Ctrl+Z).
//   header   the analysis runs (spinner, progress; manual adding and "Only I spoke" already work) |
//            no voice model (points to the PROCESSING step's "Open settings"; no second button) |
//            the decision is pending (blue: "Who was there? The voiceprint analysis found N voices",
//            what to do, the model / age meta) | the decision ("approved · N", "skipped", "only me";
//            the rows stay, changes stay immediate; "approval skipped" says hand-given names apply at
//            once, and reads "skipped · N names by hand" when every row is) | everyone anonymous after the transcript
//   rows     ParticipantCardRow; "Only I spoke" shows the own row only; after the transcript the
//            speakers (anonymous speakers without a line are hidden, counted in one sentence)
//   footer   while the decision is pending / the analysis runs: what is left out, "Only I spoke",
//            "Skip", "Approve · N" (N: the named rows the approval takes)
//   below    the track rule (G10) in one sentence: holds when every named speaker has a manual track
// Panels: the naming panel (ParticipantNamePanel: "Name..." / ⋯) and "Which voice is it?"
// (ParticipantWhichVoicePanel: after "+ Participant" picked a person, or a voiceless row's
// "Which voice?").
// vm: OverviewViewModel · decision: ParticipantsViewModel (approve / skip / the learning question
// on real data) · binder: ParticipantTrackBinder · player: PlayerController · editor: the person list.
ColumnLayout {
    id: root

    property var vm: null
    property var decision: null
    property var binder: null
    property var player: null
    property var editor: null
    // The PROCESSING voice step's state: its fix target ("speakers": no voice model) and progress.
    property string voiceFixTarget: ""
    property int voiceProgress: -1
    property string voiceSub: ""

    signal soloRequested()
    signal approved()
    signal addRequested(Item anchor)
    signal whyRequested(string speakerKey, Item anchor)

    readonly property var rows: vm ? vm.participants : []
    readonly property string decisionState: vm ? vm.decision : ""
    readonly property bool running: vm !== null && vm.analysisRunning
    readonly property bool noModel: voiceFixTarget !== "" && vm !== null && !vm.hasTranscript
    readonly property bool pending: decisionState === "pending"
    readonly property bool solo: decisionState === "solo"
    readonly property bool decided: decisionState === "approved" || decisionState === "skipped" || solo
    readonly property bool nobody: vm !== null && vm.peopleState === "nobody"
    // "Only I spoke" hides the other rows (the core keeps them unapproved).
    readonly property var shownRows: solo ? rows.filter(r => r.self === true) : rows
    readonly property var unnamedVoices: rows.filter(r => r.anonymous === true && r.suggested !== true && r.voiceless !== true)
    readonly property bool showHeader: running || noModel || pending || decided || nobody
    readonly property bool showFooter: pending || running
    readonly property bool empty: shownRows.length === 0
    readonly property int approveCount: vm ? vm.approveCount : 0
    // After "Skip": the rows named by hand (row.manual); when every row is, the header counts them.
    readonly property var ownRows: rows.filter(r => r.suggested !== true)
    readonly property int namedByHand: ownRows.filter(r => r.manual === true).length
    readonly property bool allNamedByHand: ownRows.length > 0 && namedByHand === ownRows.length
    // "Left out: 1 doubtful, 2 unknown voices — their speech stays anonymous".
    readonly property string leftOutText: {
        const doubt = rows.filter(r => r.doubtful === true && r.approved !== true && r.suggested !== true).length
        const anon = unnamedVoices.length
        const parts = []
        if (doubt > 0) parts.push(qsTr("%n kétes", "", doubt))
        if (anon > 0) parts.push(qsTr("%n névtelen hang", "", anon))
        return parts.length === 0 ? qsTr("Minden hang névhez kötve.")
                                  : qsTr("kimarad: %1 — az ő beszédük névtelen marad").arg(parts.join(", "))
    }
    // The track rule (G10, sides::vetoes): it excludes only when every named speaker has a manual track.
    readonly property var namedVoiced: rows.filter(r => r.anonymous !== true && r.suggested !== true
                                                         && r.voiceless !== true && r.unheard !== true)
    readonly property var unboundNames: binder && binder.available
                                        ? (binder.revision, namedVoiced.filter(r => !binder.chips(r.id).some(c => c.manual === true))
                                                                     .map(r => (r.name || "").replace(/ \(.*\)$/, "")))
                                        : []
    readonly property bool trackRuleShown: binder !== null && binder.available && namedVoiced.length > 0 && !solo
    readonly property bool trackRuleHolds: trackRuleShown && unboundNames.length === 0

    spacing: 8

    // ---- actions (OverviewTab calls these too) ----
    function approve() {
        if (vm && vm.decideInDemo("approved")) { root.approved(); return }
        if (!decision) return
        decision.reload()                     // the working copy = the card's current state
        if (decision.approve()) root.approved()
    }
    function skip() {
        if (vm && vm.decideInDemo("skipped")) return
        if (!decision) return
        decision.reload()
        decision.skip()
    }
    // "+ Participant" picked a person: with unnamed voices "Which voice is it?", otherwise a row.
    function addPerson(name, anchor) {
        if (!vm || name.trim() === "") return
        if (unnamedVoices.length > 0) whichPanel.openFor(name.trim(), unnamedVoices, anchor || card, "")
        else vm.addParticipant(name)
    }
    // Draws attention to the card (an entry point landed here): the accent frame blinks twice.
    function flash() { flashAnim.restart() }
    readonly property bool flashing: flashAnim.running
    function rowItem(id) {
        for (let i = 0; i < rowRepeater.count; ++i) {
            const it = rowRepeater.itemAt(i)
            if (it && it.row.id === id) return it
        }
        return null
    }
    function openNaming(id, merge) {
        const it = rowItem(id)
        if (it) namePanel.openFor(it.row, rows, it, merge)
    }
    function openWhichVoice(name, anchorId) {
        const it = anchorId ? rowItem(anchorId) : null
        whichPanel.openFor(name, unnamedVoices, it || card, it ? it.row.id : "")
    }

    // ================= the card =================
    Rectangle {
        id: card
        objectName: "participantsCard"
        Layout.fillWidth: true
        visible: !root.empty || root.showHeader
        implicitHeight: cardColumn.implicitHeight + 2
        radius: Theme.radiusPopup
        color: Theme.surface
        border.width: root.pending ? 1.5 : 1
        border.color: root.pending ? Theme.accentLine : root.noModel ? Theme.warnLine : Theme.border

        ColumnLayout {
            id: cardColumn
            x: 1; y: 1
            width: parent.width - 2
            spacing: 0

            // ---- header ----
            Item {
                objectName: "participantsHeader"
                visible: root.showHeader
                Layout.fillWidth: true
                implicitHeight: headerRow.implicitHeight + 20
                readonly property color fill: root.noModel ? Theme.warnSoft
                                            : root.running ? Theme.sunken
                                            : root.pending ? Theme.accentSoft : Theme.surface
                Rectangle { anchors.fill: parent; radius: Theme.radiusPopup - 1; color: parent.fill }
                Rectangle { y: parent.height / 2; width: parent.width; height: parent.height / 2; color: parent.fill }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 1
                    color: Theme.border
                    visible: !root.empty || root.running
                }
                RowLayout {
                    id: headerRow
                    x: 14
                    width: parent.width - 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    Item {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 18; implicitHeight: 18
                        TSpinner { anchors.centerIn: parent; visible: root.running; size: 16 }
                        TIcon {
                            anchors.centerIn: parent
                            visible: !root.running
                            size: 18
                            name: root.noModel ? "triangle-alert"
                                : root.solo ? "user"
                                : root.decisionState === "skipped" ? "circle-dot"
                                : root.decisionState === "approved" ? "user-check" : "fingerprint"
                            color: root.noModel ? Theme.warnInk : root.pending ? Theme.accent : Theme.textMuted
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        TLabel {
                            objectName: "participantsHeaderTitle"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.weight: Theme.weightSemiBold
                            text: root.running ? qsTr("Hanglenyomat-elemzés fut…") + (root.voiceSub !== "" ? " · " + root.voiceSub : "")
                                : root.noModel ? qsTr("Nincs hangmodell: a jelöltek nem jönnek maguktól")
                                : root.pending ? qsTr("Kik voltak ott? A hanglenyomat-elemzés %n hangot talált", "",
                                                      root.vm ? root.vm.candidateCount : 0)
                                : root.decisionState === "approved" ? qsTr("Jóváhagyva · %n résztvevő", "", root.approveCount)
                                : root.decisionState === "skipped" ? (root.allNamedByHand ? qsTr("Kihagyva · %n név kézzel", "", root.namedByHand)
                                                                                          : qsTr("Jóváhagyás kihagyva"))
                                : root.solo ? qsTr("Csak én beszéltem")
                                : qsTr("Egyik hang sem ismerős")
                        }
                        TLabel {
                            objectName: "participantsHeaderText"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            muted: true
                            font.pixelSize: 12
                            cssLineHeight: 1.4
                            text: root.running ? qsTr("Addig is felvehetsz résztvevőt és sávhoz kötheted, vagy jelezheted, hogy csak te beszéltél.")
                                : root.noModel ? qsTr("Vedd fel kézzel, kik voltak ott, és kösd őket sávhoz. A hangmodellt a Feldolgozás oszlop hanglenyomat-lépésében a „Beállítások megnyitása” tölti le.")
                                : root.pending ? (root.vm && root.vm.hasTranscript
                                                  ? qsTr("Hallgasd meg (▶), nevezd el, kösd sávhoz, majd hagyd jóvá. Ebből kötöm az átirat beszélőit a nevekhez.")
                                                  : qsTr("Hallgasd meg (▶), nevezd el, kösd sávhoz, majd hagyd jóvá. Az átirat ebből köti a beszélőket, amikor megérkezik."))
                                : root.decisionState === "approved" ? (root.vm && root.vm.hasTranscript
                                                  ? qsTr("Az átirat beszélői ezekhez a nevekhez kötve. A módosítás azonnal érvényes; Ctrl+Z visszavonja.")
                                                  : qsTr("Az átirat érkezésekor ezekre a nevekre kötöm a beszélőket. A módosítás azonnal érvényes; Ctrl+Z visszavonja."))
                                : root.decisionState === "skipped" ? qsTr("A jelölteket nem hagytad jóvá; az átirat után a hangfelismerés dönt, a kézzel adott nevek azonnal érvényesek.")
                                : root.solo ? qsTr("Az átirat minden beszélője a te nevedre kerül; a többi jelölt rejtve. Ctrl+Z visszavonja.")
                                : qsTr("Egyikük hangja sem hasonlít egy ismert emberére sem. Nevezd el őket: legközelebb felismerjük a hangjukat.")
                        }
                        TLabel {
                            objectName: "participantsMeta"
                            visible: root.pending && text !== ""
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            text: root.decision ? root.decision.metaText : ""
                            mono: true
                            muted: true
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }
                        TProgressBar {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            visible: root.running
                            indeterminate: root.voiceProgress < 0
                            value: Math.max(0, root.voiceProgress) / 100
                            trackColor: Theme.bg
                        }
                    }
                    TButton {
                        objectName: "approveAfterSkipButton"
                        visible: root.decisionState === "skipped" && root.approveCount > 0
                        Layout.alignment: Qt.AlignTop
                        text: qsTr("Jóváhagyás · %n", "", root.approveCount)
                        size: "small"
                        toolTipText: qsTr("A névvel bíró, megerősített sorok jóváhagyása; az átirat ezekre köti a beszélőket")
                        onClicked: root.approve()
                    }
                }
            }

            // ---- placeholder while the analysis runs, no row yet ----
            Item {
                visible: root.running && root.empty
                Layout.fillWidth: true
                implicitHeight: 92
                Column {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Repeater {
                        model: 2
                        Row {
                            spacing: 10
                            Rectangle { width: 26; height: 26; radius: 13; color: Theme.sunken }
                            Rectangle { width: 26; height: 26; radius: 13; color: Theme.sunken }
                            Rectangle { width: 150; height: 12; radius: 4; color: Theme.sunken; anchors.verticalCenter: parent.verticalCenter }
                            Rectangle { width: 220; height: 12; radius: 4; color: Theme.sunken; opacity: 0.6; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }
            // ---- no model, nobody added yet ----
            TLabel {
                visible: root.noModel && root.empty
                Layout.fillWidth: true
                Layout.margins: 14
                text: qsTr("Még nincs résztvevő. A „+ Résztvevő” felveszi; ha csak te beszéltél, jelezd lent.")
                muted: true
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            // ---- the rows ----
            Repeater {
                id: rowRepeater
                model: root.shownRows
                ParticipantCardRow {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    row: modelData
                    binder: root.binder
                    player: root.player
                    canExplain: root.vm !== null && root.vm.hasTranscript
                    showTopBorder: index > 0
                    wide: card.width >= 880
                    canAssignVoice: root.unnamedVoices.length > 0
                    highlighted: (namePanel.opened && namePanel.row.id === modelData.id)
                    onNameRequested: (r, anchor) => namePanel.openFor(r, root.rows, anchor, false)
                    onWhichVoiceRequested: (r, anchor) => whichPanel.openFor((r.name || "").replace(/ \(.*\)$/, ""),
                                                                             root.unnamedVoices, anchor, r.id)
                    onConfirmRequested: (id, confirmed) => { if (root.vm) root.vm.confirmParticipant(id, confirmed) }
                    onAddRequested: (name) => { if (root.vm) root.vm.addParticipant(name) }
                    onRemoveRequested: (id) => { if (root.vm) root.vm.removeParticipant(id) }
                    onWhyRequested: (key, anchor) => root.whyRequested(key, anchor)
                }
            }

            // ---- anonymous speakers without a line (after the transcript) ----
            Item {
                objectName: "hiddenSpeakersNote"
                visible: root.vm !== null && root.vm.hiddenSpeakers > 0 && !root.solo
                Layout.fillWidth: true
                implicitHeight: 34
                Rectangle { width: parent.width; height: 1; color: Theme.border }
                RowLayout {
                    x: 14
                    width: parent.width - 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    TIcon { name: "eye-off"; size: 13; color: Theme.textMuted }
                    TLabel {
                        Layout.fillWidth: true
                        text: qsTr("%n névtelen beszélő, akinek már nincs sora, elrejtve", "", root.vm ? root.vm.hiddenSpeakers : 0)
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }
                }
            }

            // ---- footer: the decision ----
            Item {
                objectName: "participantsFooter"
                visible: root.showFooter || (root.noModel && !root.decided)
                Layout.fillWidth: true
                implicitHeight: footRow.implicitHeight + 20
                Rectangle { width: parent.width; height: 1; color: Theme.border; visible: !root.empty || root.running }
                RowLayout {
                    id: footRow
                    x: 14
                    width: parent.width - 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    TIcon { name: "info"; size: 14; color: Theme.textMuted; visible: footText.text !== "" }
                    TLabel {
                        id: footText
                        objectName: "participantsLeftOut"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        text: root.running ? qsTr("A jóváhagyás az elemzés után lesz elérhető.")
                            : root.pending ? root.leftOutText : ""
                    }
                    TButton {
                        objectName: "soloButton"
                        text: qsTr("Csak én beszéltem")
                        variant: "ghost"
                        size: "small"
                        toolTipText: qsTr("Minden beszéd a te nevedre kerül, az azonosítás kimarad")
                        onClicked: root.soloRequested()
                    }
                    TButton {
                        objectName: "skipButton"
                        visible: root.pending
                        text: qsTr("Kihagyás")
                        variant: "ghost"
                        size: "small"
                        muted: true
                        toolTipText: qsTr("Nem kötünk senkit; a jelöltek megmaradnak, később is jóváhagyhatod")
                        onClicked: root.skip()
                    }
                    TButton {
                        objectName: "approveButton"
                        visible: root.pending
                        enabled: root.approveCount > 0
                        text: qsTr("Jóváhagyás · %n résztvevő", "", root.approveCount)
                        variant: "primary"
                        size: "small"
                        toolTipText: qsTr("A névvel bíró, megerősített sorok mennek tovább; az átirat ezekre köti a beszélőket")
                        onClicked: root.approve()
                    }
                }
            }
        }
        // Attention flash (flash()).
        Rectangle {
            id: flashFrame
            objectName: "participantsFlash"
            anchors.fill: parent
            radius: card.radius
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            opacity: 0
            SequentialAnimation {
                id: flashAnim
                loops: 2
                NumberAnimation { target: flashFrame; property: "opacity"; to: 1; duration: Theme.durationNormal }
                NumberAnimation { target: flashFrame; property: "opacity"; to: 0; duration: 700 }
            }
        }
    }

    // ---- empty: nobody yet, nothing running ----
    EmptyCard {
        objectName: "peopleEmpty"
        visible: root.empty && !root.showHeader
        Layout.fillWidth: true
        iconName: "users"
        title: qsTr("Még nem tudjuk, kik voltak ott")
        text: root.vm && root.vm.hasTranscript
              ? qsTr("Add meg, kik voltak ott: a beszélők így a nevükre kerülnek, és legközelebb a hangjukról is felismerjük őket.")
              : qsTr("A hanglenyomat-elemzés után ide jönnek a jelöltek. Ha tudod, add meg most: az azonosítás így pontosabb lesz.")
        TButton {
            id: emptyAdd
            objectName: "emptyAddParticipant"
            text: qsTr("+ Résztvevő")
            size: "small"
            onClicked: root.addRequested(emptyAdd)
        }
        TButton {
            objectName: "soloButton"
            text: qsTr("Csak én beszéltem")
            size: "small"
            toolTipText: qsTr("Minden beszéd a te nevedre kerül, az azonosítás kimarad")
            onClicked: root.soloRequested()
        }
    }

    // ---- the track rule (G10) ----
    RowLayout {
        objectName: "trackRuleLine"
        visible: root.trackRuleShown
        Layout.fillWidth: true
        Layout.leftMargin: 2
        spacing: 8
        TIcon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            name: "lock"
            size: 13
            color: root.trackRuleHolds ? Theme.accent : Theme.textMuted
        }
        TLabel {
            objectName: "trackRuleText"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontCaption
            color: root.trackRuleHolds ? Theme.accent : Theme.textMuted
            text: root.trackRuleHolds
                  ? qsTr("A sávszabály él: minden nevesített beszélőnek van kézi sávja, ezért a sáv kizár — más sávon nem keresem őket.")
                  : qsTr("A sávszabály nem él: %1 nincs kézzel sávhoz kötve, ezért a sáv most csak rangsorol.")
                        .arg(root.unboundNames.length <= 2 ? root.unboundNames.join(", ")
                             : root.unboundNames.slice(0, 2).join(", ") + " " + qsTr("és még %n", "", root.unboundNames.length - 2))
        }
    }

    // ---- panels ----
    ParticipantNamePanel {
        id: namePanel
        parent: card
        vm: root.vm
        player: root.player
        editor: root.editor
    }
    ParticipantWhichVoicePanel {
        id: whichPanel
        parent: card
        vm: root.vm
        player: root.player
    }
}
