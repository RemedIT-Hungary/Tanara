import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Overview tab: the first tab, two columns.
//   Left (what and who): ABOUT (source card: manual description; calendar later), tag field with
//       suggestions, WHO WAS THERE (participants with source badges and talk share; when empty
//       "+ Participant" / "Only I spoke"; when everyone is unknown: the raw speakers + "Name
//       them"), and the approval banner ("Voiceprint analysis is done: N candidates").
//   Right, 340 px (technical): PROCESSING steps, TRACKS (playback, mixdown switch, "Mix in");
//       for a finished meeting STATS as a 2x3 tile grid, with steps / tracks collapsed to one
//       row each. The full view of the old Tracks tab (rename, locate, restore, permanent
//       delete, mixdown) is in the "Details" panel.
// This tab also shows before a transcript exists: tracks can be played / toggled, participants
// and tags can be added, and "Start transcription" (provider state, missing setup, error with
// "Retry") lives in the PROCESSING "Transcript" step (TranscribeStepPanel); for a manual mixdown
// the "Start mixdown" button is in the "Mixdown" step, the identification switch in the voiceprint
// step. During the first transcription the same step shows the sections, the "Keep working
// meanwhile" row and "Cancel". Before transcription, below the source card: note templates from
// similar meetings and the detected call; the tag row suggests from the title when there is
// no other suggestion (MeetingTagsModel.draftTitle).
// Participant row badges: tooltip with the evidence; after transcription a click opens the
// editor's "Why them?" panel (shell.openSpeakerWhy). An anonymous row can be named by click /
// "Name..." button: after transcription a person picker -> editor.reassignSpeaker; before it the
// "Who was there?" dialog (the name is editable there). The voiceprint step has "Reanalyze".
// The "Who was there?" dialog opens on the participantsRequested signal (Main: shell.openParticipants).
// Change bar: every Overview action (approval, "Only I spoke", track on / off, description,
// adding / removing a participant, rejecting a suggestion, tag, naming) goes onto the meeting's
// SHARED undo stack (MeetingUndoService); Main calls showChange(text) on the shell's
// meetingChangePosted signal -> dark bar at the bottom of the left column:
// "<what happened> · Undo · Ctrl+Z · x"; after an undo "Undone: ..." (showUndone).
// For screenshots: --qml-prop 'demoState="overviewProcessing|overviewDone|overviewEmpty|overviewNobody|
// overviewRecorded|overviewRecordedError|overviewTranscribing"'.
Item {
    id: root

    property string meetingId: ""
    property var player: null                // PlayerController or null
    property var shell: null                 // ShellActions or null
    property var tagsModel: null             // MeetingTagsModel (ShellMeetingModel.tags) or null
    property var editor: null                // TranscriptEditorViewModel (for the person picker) or null
    property string demoState: ""

    readonly property alias vm: vm
    readonly property alias tracks: tracks
    readonly property alias changeBar: changeBar

    signal participantsRequested()

    // "Later": the banner does not come back in this session for this meeting.
    property var laterFor: ({})
    readonly property bool bannerShown: vm.approvalPending && !laterFor[meetingId]
    // Opening the collapsed rows in the done state.
    property bool stepsOpen: false
    property bool tracksOpen: false
    onMeetingIdChanged: { stepsOpen = false; tracksOpen = false; changeBar.dismiss() }

    // The V5-like (two-track, empty) demo states.
    readonly property bool demoTwoTracks: ["overviewEmpty", "overviewNobody", "overviewRecorded",
                                           "overviewRecordedError", "overviewTranscribing"].indexOf(root.demoState) >= 0
    // Before transcription starts: the start controls inside the steps.
    readonly property bool beforeTranscription: !vm.hasTranscript && !vm.transcribing
    // State of the voiceprint step: while waiting "Start analysis", after a finished analysis "Reanalyze".
    readonly property string voiceState: {
        for (const s of vm.steps) if (s.key === "voice") return s.state
        return ""
    }
    // Settings page that fixes a blocked voice step (no voice model installed), "" = not blocked.
    readonly property string voiceFixTarget: {
        for (const s of vm.steps) if (s.key === "voice") return s.fixTarget || ""
        return ""
    }
    readonly property bool identifyShown: root.beforeTranscription && pre.identifyAvailable
    readonly property bool reanalyzeShown: vm.canReanalyze && (root.voiceState === "done" || root.voiceState === "attention"
                                                               || root.voiceState === "waiting")

    OverviewViewModel {
        id: vm
        meetingId: root.meetingId
        demoState: root.demoState
    }
    // Transcription gating, its error, the mixdown state and the identification switch (the model of
    // the old pre-transcript view) -- only while there is no transcript.
    PreTranscriptViewModel {
        id: pre
        meetingId: vm.hasTranscript ? "" : root.meetingId
        demoState: root.demoState === "overviewRecordedError" ? "failed"
                 : root.demoState === "overviewRecorded" ? "readyNote"
                 : root.demoState === "overviewTranscribing" ? "uploading"
                 : root.demoState === "overviewProcessing" || root.demoState === "overviewEmpty" ? "running" : ""
    }
    TrackListModel {
        id: tracks
        meetingId: root.meetingId
        demoState: root.demoTwoTracks ? "overviewTwo" : "overview"
    }
    // Speaker -> track binding in the WHO WAS THERE rows (track chips).
    ParticipantTrackBinder {
        id: trackBinder
        objectName: "participantTrackBinder"
        meetingId: root.meetingId
    }
    // Own tag model when the host gives none (standalone screenshot): a made-up set.
    MeetingTagsModel {
        id: ownTags
        demoState: root.demoTwoTracks ? "none"
                 : root.demoState === "overviewDone" ? "few" : "similar"
    }
    // Re-read the gating after Settings closes (a key was added / removed).
    onVisibleChanged: if (visible && root.beforeTranscription) pre.refresh()
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive && root.visible && root.beforeTranscription)
                pre.refresh()
        }
    }
    readonly property var effectiveTags: tagsModel ? tagsModel : ownTags
    // Title-based fallback suggestion before transcription (when there is no real suggestion).
    Binding {
        target: root.effectiveTags
        property: "draftTitle"
        value: vm.hasTranscript ? "" : vm.title
    }

    function openPicker(anchor) {
        const p = anchor.mapToItem(root, 0, anchor.height)
        picker.x = Math.max(8, Math.min(root.width - picker.width - 8, p.x + anchor.width - picker.width))
        picker.y = p.y + 6
        picker.open()
    }
    // The "Track details" panel (old Tracks tab; Main's demo state opens it too).
    function openTracks() { tracksDialog.open() }
    // Before transcription starts (step button, error "Retry") the unfinished description is saved.
    function commitSource() { if (sourceCard.editing) sourceCard.finish(true) }
    function solo() {
        if (vm.setSolo() || !root.shell) return
        root.shell.toast(qsTr("Ehhez add meg a saját neved: Beállítások › Beszélők › Te."))
        root.shell.openSettings("speakers")
    }
    // The badge -> the editor's "Why them?" panel for that speaker (Transcript tab).
    function explainSpeaker(speakerKey) {
        if (root.shell && typeof root.shell.openSpeakerWhy === "function")
            root.shell.openSpeakerWhy(root.meetingId, speakerKey)
    }
    // Naming an anonymous row. After transcription a person picker (-> editor.reassignSpeaker);
    // before it the "Who was there?" dialog (the core lets the name be set there).
    function nameRow(row, anchor) {
        if (!vm.hasTranscript || (row.speakerKey || "") === "") {
            root.participantsRequested()
            return
        }
        namePicker.speakerKey = row.speakerKey
        const p = anchor.mapToItem(root, 0, anchor.height)
        namePicker.x = Math.max(8, Math.min(root.width - namePicker.width - 8, p.x + anchor.width - namePicker.width))
        namePicker.y = p.y + 6
        namePicker.open()
    }
    // The change bar (Main calls it on the shared stack's signals; in the demo, for screenshots).
    function showChange(text) { changeBar.post(text, false) }
    function showUndone(text) { changeBar.post(qsTr("Visszavonva: %1").arg(text), true) }
    function hideChange() { changeBar.dismiss() }

    // "Start analysis" / "Reanalyze": after transcription the same as the menu's "Redo voiceprint
    // analysis" (shell); before it, voice analysis of the raw tracks (analyzeParticipants).
    function reanalyze() {
        if (vm.hasTranscript) { if (root.shell) root.shell.identifyParticipants(root.meetingId) }
        else vm.reanalyze()
    }

    // Section header: 12/600 uppercase label · dim hint · accent link on the right.
    component SectionHeader: RowLayout {
        id: sh
        property string title: ""
        property string hint: ""
        property string actionText: ""
        property string actionIcon: ""
        property string actionName: ""
        signal actionClicked(Item anchor)
        spacing: 10
        TLabel {
            text: sh.title
            muted: true
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
            font.letterSpacing: 0.7
        }
        TLabel {
            Layout.fillWidth: true
            text: sh.hint
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
        T.AbstractButton {
            id: link
            objectName: sh.actionName
            visible: sh.actionText !== ""
            hoverEnabled: true
            implicitWidth: linkRow.implicitWidth + 6
            implicitHeight: 22
            onClicked: sh.actionClicked(link)
            contentItem: Item {
                Row {
                    id: linkRow
                    anchors.centerIn: parent
                    spacing: 4
                    TIcon { visible: sh.actionIcon !== ""; anchors.verticalCenter: parent.verticalCenter; name: sh.actionIcon; size: 13; color: Theme.accent }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: sh.actionText
                        color: Theme.accent
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightMedium
                        font.underline: link.hovered
                    }
                }
            }
            background: Item { TFocusRing { visible: link.visualFocus; targetRadius: Theme.radiusTag } }
        }
    }
    // Collapsed row (done state): icon circle · title + subtext · >
    component CollapsedRow: T.AbstractButton {
        id: cr
        property string iconName: ""
        property string title: ""
        property string sub: ""
        property bool open: false
        property bool first: false
        hoverEnabled: true
        implicitHeight: 52
        contentItem: RowLayout {
            spacing: 10
            Item {
                implicitWidth: 26; implicitHeight: 26
                Rectangle { anchors.fill: parent; radius: 13; color: "transparent"; border.width: 1; border.color: Theme.borderStrong }
                TIcon { anchors.centerIn: parent; name: "check"; size: 12; color: Theme.text }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    spacing: 5
                    TIcon { name: cr.iconName; size: 12; color: Theme.textMuted }
                    TLabel { text: cr.title; font.pixelSize: Theme.fontSmall; font.weight: Theme.weightSemiBold }
                }
                TLabel { Layout.fillWidth: true; text: cr.sub; muted: true; font.pixelSize: 12; elide: Text.ElideRight }
            }
            TIcon { name: cr.open ? "chevron-down" : "chevron-right"; size: 14; color: Theme.textMuted }
        }
        leftPadding: 10; rightPadding: 10
        background: Item {
            Rectangle { visible: !cr.first; width: parent.width; height: 1; color: Theme.border }
            Rectangle { anchors.fill: parent; color: Theme.stateLayer; opacity: cr.hovered ? Theme.hoverOpacity : 0 }
        }
    }

    // Dark change bar (in the style of TranscriptChangeBar): message · Undo · Ctrl+Z · x. It does not
    // expire while hovered; a new message replaces the old one.
    component ChangeBar: Rectangle {
        id: bar
        property string text: ""
        property bool undone: false     // "Undone: ..." -- nothing left to do on it
        property bool shown: false
        function post(message, isUndone) {
            bar.text = message
            bar.undone = isUndone
            bar.shown = message !== ""
            expiry.restart()
        }
        function dismiss() { bar.shown = false; expiry.stop() }
        objectName: "overviewChangeBar"
        visible: shown
        height: 44
        radius: Theme.radiusPopup
        color: Theme.text
        TShadow { radius: bar.radius }
        HoverHandler { id: barHover }
        Timer {
            id: expiry
            interval: bar.undone ? 5000 : 12000
            running: bar.shown && !barHover.hovered
            onTriggered: bar.shown = false
        }
        TIcon {
            id: barIcon
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            name: bar.undone ? "undo-2" : "circle-check"
            size: 16
            color: Theme.bg
        }
        TLabel {
            id: barText
            objectName: "overviewChangeText"
            anchors { left: barIcon.right; leftMargin: 8; right: barActions.left; rightMargin: 12
                      verticalCenter: parent.verticalCenter }
            text: bar.text
            color: Theme.bg
            elide: Text.ElideRight
            font.weight: Theme.weightSemiBold
        }
        Row {
            id: barActions
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 6
            BarAction {
                objectName: "overviewChangeUndo"
                visible: !bar.undone
                strong: true
                iconName: "undo-2"
                text: qsTr("Visszavonás · Ctrl+Z")
                onClicked: {
                    const hasUndo = root.shell && root.shell.hasMeetingUndo === true
                    if (hasUndo) root.shell.undoMeeting()
                    else bar.post(qsTr("Visszavonva: %1").arg(bar.text), true)   // demo
                }
            }
            BarAction {
                objectName: "overviewChangeClose"
                iconName: "x"
                toolTipText: qsTr("Bezárás")
                onClicked: bar.dismiss()
            }
        }
    }
    // Action written on the dark bar (label + optional icon). `strong`: framed, emphasized.
    component BarAction: Item {
        id: action
        property string text: ""
        property string iconName: ""
        property bool strong: false
        property string toolTipText: ""
        signal clicked()
        implicitWidth: actionRow.implicitWidth + 20
        width: implicitWidth
        height: 28
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text !== "" ? text : toolTipText
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Theme.alpha(Theme.bg, actionTap.pressed ? 0.24 : actionHover.hovered ? 0.16 : action.strong ? 0.08 : 0)
            border.width: action.strong ? 1 : 0
            border.color: Theme.alpha(Theme.bg, 0.4)
            TFocusRing { visible: action.activeFocus; targetRadius: parent.radius }
        }
        Row {
            id: actionRow
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            TIcon {
                visible: action.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: action.iconName
                size: 14
                color: Theme.bg
            }
            TLabel {
                visible: action.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: action.text
                color: Theme.bg
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
            }
        }
        HoverHandler { id: actionHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: actionTap; onTapped: action.clicked() }
        TToolTip { visible: action.toolTipText !== "" && actionHover.hovered; text: action.toolTipText }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ================= left column =================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Flickable {
                id: leftFlick
                objectName: "overviewLeft"
                anchors.fill: parent
                // The floating change bar must not cover the last row.
                contentHeight: left.implicitHeight + 36 + (changeBar.visible ? changeBar.height + 12 : 0)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: TScrollBar {}

                ColumnLayout {
                    id: left
                    x: Theme.space5
                    y: 18
                    width: leftFlick.width - 2 * Theme.space5
                    spacing: 10

                    SectionHeader {
                        Layout.fillWidth: true
                        title: vm.hasTranscript ? qsTr("MIRŐL SZÓLT") : qsTr("MIRŐL SZÓL")
                        actionText: qsTr("Forrás")
                        actionIcon: "plus"
                        actionName: "addSourceLink"
                        onActionClicked: (anchor) => sourceMenu.popup(anchor, anchor.width - sourceMenu.width, anchor.height + 4)
                        TMenu {
                            id: sourceMenu
                            TMenuItem { text: qsTr("Leírás"); iconName: "file-text"; onTriggered: sourceCard.startEdit() }
                            TMenuItem { text: qsTr("Naptár-esemény (hamarosan)"); iconName: "calendar"; enabled: false }
                        }
                    }
                    SourceCard {
                        id: sourceCard
                        Layout.fillWidth: true
                        note: vm.contextNote
                        suggestions: vm.hasTranscript ? [] : pre.note.suggestions
                        detectedCallApp: vm.hasTranscript ? "" : pre.note.detectedCallApp
                        onSaveRequested: (note) => vm.setContextNote(note)
                    }
                    TagRowOverview {
                        objectName: "overviewTags"
                        Layout.fillWidth: true
                        model: root.effectiveTags
                        onTagClicked: (id) => { if (root.shell) root.shell.filterByTag(id) }
                        onMeetingRequested: (id) => { if (root.shell) root.shell.showMeeting(id) }
                    }

                    SectionHeader {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        title: qsTr("KIK VOLTAK OTT")
                        hint: vm.peopleHint
                        actionText: vm.approved ? qsTr("Módosítás") : vm.peopleState === "list" ? qsTr("+ Résztvevő") : ""
                        actionName: "participantsAction"
                        onActionClicked: (anchor) => vm.approved ? root.participantsRequested() : root.openPicker(anchor)
                    }
                    ParticipantList {
                        objectName: "participantList"
                        visible: vm.peopleState === "list"
                        Layout.fillWidth: true
                        people: vm.participants
                        canExplain: vm.hasTranscript
                        trackBinder: trackBinder
                        onAddRequested: (name) => vm.addParticipant(name)
                        onRemoveRequested: (id) => vm.removeParticipant(id)
                        onWhyRequested: (key) => root.explainSpeaker(key)
                        onNameRequested: (row, anchor) => root.nameRow(row, anchor)
                    }
                    EmptyCard {
                        objectName: "peopleEmpty"
                        visible: vm.peopleState === "empty"
                        Layout.fillWidth: true
                        iconName: "users"
                        title: qsTr("Még nem tudjuk, kik voltak ott")
                        text: vm.hasTranscript ? qsTr("Add meg, kik voltak ott: a beszélők így a nevükre kerülnek, és legközelebb a hangjukról is felismerjük őket.")
                                               : qsTr("A hanglenyomat-elemzés után ide jönnek a jelöltek. Ha tudod, add meg most: az azonosítás így pontosabb lesz.")
                        TButton {
                            id: addPersonButton
                            objectName: "emptyAddParticipant"
                            text: qsTr("+ Résztvevő")
                            size: "small"
                            onClicked: root.openPicker(addPersonButton)
                        }
                        TButton {
                            objectName: "soloButton"
                            text: qsTr("Csak én beszéltem")
                            size: "small"
                            toolTipText: qsTr("Minden beszéd a te nevedre kerül, az azonosítás kimarad")
                            onClicked: root.solo()
                        }
                    }
                    EmptyCard {
                        objectName: "peopleNobody"
                        visible: vm.peopleState === "nobody"
                        Layout.fillWidth: true
                        iconName: "fingerprint"
                        title: qsTr("%n beszélő, ismeretlen hanggal", "", vm.anonymousSpeakers.length)
                        text: qsTr("Egyikük hangja sem hasonlít egy ismert emberére sem. Elnevezheted őket; legközelebb már felismerjük a hangjukat.")
                        Repeater {
                            model: vm.anonymousSpeakers
                            Rectangle {
                                required property var modelData
                                width: chipRow.implicitWidth + 12
                                height: 26
                                radius: 13
                                color: Theme.speakerSoft(modelData.colorIndex)
                                border.width: 1
                                border.color: Theme.speakerLine(modelData.colorIndex)
                                Row {
                                    id: chipRow
                                    x: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6
                                    TAvatar {
                                        anchors.verticalCenter: parent.verticalCenter
                                        size: 20
                                        variant: "solid"
                                        speakerIndex: modelData.colorIndex
                                        monogram: "B" + (modelData.colorIndex + 1)
                                    }
                                    TLabel {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.name
                                        color: Theme.speakerInk(modelData.colorIndex)
                                        font.pixelSize: 12
                                        font.weight: Theme.weightSemiBold
                                    }
                                    TLabel {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: Math.round(modelData.share * 100) + "%"
                                        mono: true
                                        color: Theme.speakerInk(modelData.colorIndex)
                                        opacity: 0.75
                                        font.pixelSize: Theme.fontMicro
                                    }
                                }
                            }
                        }
                        TButton {
                            objectName: "nameThemButton"
                            text: qsTr("Elnevezem őket")
                            variant: "primary"
                            size: "small"
                            onClicked: root.participantsRequested()
                        }
                    }

                    // Approval banner (voice analysis is done, no decision yet).
                    Rectangle {
                        objectName: "approvalBanner"
                        visible: root.bannerShown
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        implicitHeight: bannerRow.implicitHeight + 22
                        radius: Theme.radiusPopup
                        color: Theme.accentSoft
                        border.width: 1
                        border.color: Theme.accentLine
                        RowLayout {
                            id: bannerRow
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 28
                            spacing: 12
                            TIcon { name: "fingerprint"; size: 18; color: Theme.accent }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                TLabel {
                                    Layout.fillWidth: true
                                    text: qsTr("A hanglenyomat-elemzés kész: %n jelölt", "", vm.candidateCount)
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Theme.weightSemiBold
                                    wrapMode: Text.Wrap
                                }
                                TLabel {
                                    Layout.fillWidth: true
                                    text: vm.hasTranscript ? qsTr("Nézd át, kik voltak ott. Ebből kötöm az átirat beszélőit a nevekhez.")
                                                           : qsTr("Nézd át, kik voltak ott. Ebből köti az átirat a beszélőket, amikor megérkezik.")
                                    muted: true
                                    font.pixelSize: 12
                                    cssLineHeight: 1.4
                                    wrapMode: Text.Wrap
                                }
                            }
                            TButton {
                                objectName: "reviewButton"
                                text: qsTr("Átnézem")
                                variant: "primary"
                                onClicked: root.participantsRequested()
                            }
                            TButton {
                                objectName: "laterButton"
                                text: qsTr("Később")
                                variant: "ghost"
                                muted: true
                                onClicked: {
                                    const later = Object.assign({}, root.laterFor)
                                    later[root.meetingId] = true
                                    root.laterFor = later
                                }
                            }
                        }
                    }
                }
            }
            ChangeBar {
                id: changeBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                          leftMargin: Theme.space5; rightMargin: Theme.space5; bottomMargin: 10 }
            }
        }

        // ================= right column (340) =================
        Rectangle {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            color: Theme.surface
            Rectangle { width: 1; height: parent.height; color: Theme.border }

            Flickable {
                id: rightFlick
                objectName: "overviewRight"
                anchors { fill: parent; leftMargin: 1 }
                contentHeight: right.implicitHeight + 36
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: TScrollBar {}

                ColumnLayout {
                    id: right
                    x: 18; y: 18
                    width: rightFlick.width - 36
                    spacing: 10

                    // ---- done: STATS + collapsed rows ----
                    SectionHeader { visible: vm.allDone && vm.stats.length > 0; Layout.fillWidth: true; title: qsTr("ADATOK") }
                    StatTiles {
                        objectName: "statTiles"
                        visible: vm.allDone && vm.stats.length > 0
                        Layout.fillWidth: true
                        stats: vm.stats
                    }
                    SectionHeader {
                        visible: vm.allDone
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        title: qsTr("FELDOLGOZÁS ÉS SÁVOK")
                    }
                    Rectangle {
                        visible: vm.allDone
                        Layout.fillWidth: true
                        implicitHeight: collapsedColumn.implicitHeight
                        radius: Theme.radiusPopup
                        color: Theme.raised
                        border.width: 1
                        border.color: Theme.border
                        clip: true
                        Column {
                            id: collapsedColumn
                            width: parent.width
                            CollapsedRow {
                                objectName: "stepsCollapsed"
                                width: parent.width
                                first: true
                                iconName: "circle-check"
                                title: qsTr("Minden lépés kész")
                                sub: vm.stepsSummary
                                open: root.stepsOpen
                                onClicked: root.stepsOpen = !root.stepsOpen
                            }
                            CollapsedRow {
                                objectName: "tracksCollapsed"
                                width: parent.width
                                iconName: "audio-lines"
                                title: qsTr("Sávok")
                                sub: tracks.excludedCount > 0
                                     ? qsTr("%n aktív", "", tracks.includedCount) + " · " + qsTr("%n kimaradt", "", tracks.excludedCount)
                                     : qsTr("%n aktív", "", tracks.includedCount)
                                open: root.tracksOpen
                                onClicked: root.tracksOpen = !root.tracksOpen
                            }
                        }
                    }

                    // ---- steps ----
                    SectionHeader {
                        visible: !vm.allDone
                        Layout.fillWidth: true
                        title: qsTr("FELDOLGOZÁS")
                    }
                    PipelineSteps {
                        objectName: "pipelineSteps"
                        visible: !vm.allDone || root.stepsOpen
                        Layout.fillWidth: true
                        steps: vm.steps
                        // Before transcription the start control, during the first transcription the sections + "Cancel".
                        transcriptContent: !vm.hasTranscript && (root.beforeTranscription || vm.transcribing)
                                           ? transcribeStep : null
                        mixdownContent: root.beforeTranscription && (pre.mixdownState === "missing" || pre.mixdownState === "stale")
                                        ? mixdownStep : null
                        voiceContent: root.identifyShown || root.reanalyzeShown || root.voiceFixTarget !== "" ? voiceStep : null
                    }

                    // ---- tracks ----
                    SectionHeader {
                        visible: !vm.allDone || root.tracksOpen
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        title: qsTr("SÁVOK")
                        actionText: qsTr("Részletek")
                        actionName: "tracksDetailsLink"
                        onActionClicked: tracksDialog.open()
                    }
                    TrackList {
                        objectName: "trackList"
                        visible: !vm.allDone || root.tracksOpen
                        Layout.fillWidth: true
                        tracks: tracks
                        player: root.player
                        sideWho: vm.sideWho
                    }
                }
            }
        }
    }

    // ---- controls embedded in the steps (before transcription) ----
    Component {
        id: transcribeStep
        TranscribeStepPanel {
            vm: pre
            shell: root.shell
            meetingId: root.meetingId
            onStartRequested: root.commitSource()
        }
    }
    Component {
        id: mixdownStep
        RowLayout {
            spacing: 8
            TButton {
                objectName: "startMixdownButton"
                text: pre.mixdownState === "stale" ? qsTr("Lekeverés frissítése") : qsTr("Lekeverés indítása")
                size: "small"
                iconName: "audio-lines"
                iconSize: 14
                onClicked: pre.startMixdown()
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: voiceStep
        ColumnLayout {
            spacing: 6
            RowLayout {
                visible: root.identifyShown
                Layout.fillWidth: true
                spacing: 8
                TSwitch {
                    objectName: "identifySwitch"
                    checked: pre.identifyEnabled
                    onToggled: pre.identifyEnabled = checked
                    Accessible.name: qsTr("Résztvevők azonosítása hang alapján")
                }
                TLabel {
                    Layout.fillWidth: true
                    text: pre.identifyEnabled ? qsTr("Azonosítás hang alapján") : qsTr("Kihagyva: a beszélők névtelenek maradnak")
                    muted: true
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
            TButton {
                objectName: "voiceFixButton"
                visible: root.voiceFixTarget !== "" && !!root.shell
                text: qsTr("Beállítások megnyitása")
                size: "small"
                iconName: "settings"
                iconSize: 14
                onClicked: root.shell.openSettings(root.voiceFixTarget)
            }
            TButton {
                objectName: "reanalyzeButton"
                visible: root.reanalyzeShown
                text: root.voiceState === "waiting" ? qsTr("Elemzés indítása") : qsTr("Újraelemzés")
                size: "small"
                iconName: root.voiceState === "waiting" ? "play" : "refresh-cw"
                iconSize: 14
                toolTipText: vm.hasTranscript ? qsTr("A hanglenyomat-elemzés újra: a névtelen beszélők a mentett hangmintákhoz")
                           : root.voiceState === "waiting" ? qsTr("Hanglenyomat-elemzés a nyers sávokon, átirat nélkül")
                                                           : qsTr("A hanglenyomat-elemzés újra a sávokon")
                onClicked: root.reanalyze()
            }
        }
    }

    // ---- "+ Participant" ----
    PersonPicker {
        id: picker
        objectName: "overviewPersonPicker"
        editor: root.editor
        anonymousText: ""
        onPersonChosen: name => vm.addParticipant(name)
    }
    // ---- "Name..." (anonymous speaker, after transcription) ----
    PersonPicker {
        id: namePicker
        objectName: "overviewNamePicker"
        property string speakerKey: ""
        editor: root.editor
        anonymousText: ""
        hintText: qsTr("A beszélő minden sora a választott névre kerül; Ctrl+Z visszavonja.")
        onPersonChosen: name => {
            if (vm.nameSpeakerInDemo(namePicker.speakerKey, name)) return
            if (root.editor) root.editor.reassignSpeaker(namePicker.speakerKey, name, false)
        }
    }

    // ---- the old Tracks tab's full view ----
    TDialog {
        id: tracksDialog
        objectName: "tracksDialog"
        title: qsTr("Sávok részletei")
        width: Math.min(900, (parent ? parent.width : 900) - 32)
        TracksTab {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(520, root.height - 120)
            meetingId: root.meetingId
            player: root.player
            shell: root.shell
        }
        actions: [ TButton { text: qsTr("Bezárás"); onClicked: tracksDialog.close() } ]
    }
}
