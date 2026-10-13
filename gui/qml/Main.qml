import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Tanara main window (shell). Structure (design/handoff/README.md, "Window structure"; the
// meeting view follows design/handoff-v3):
//   menu bar (36: File - Meeting - View) - sidebar (276) - content:
//   one-line header (56: title, tabs Overview - Transcript - Executive summary - Memo, "...") -
//   per-tab tool row (46) - the tab content - at the bottom the player (52, on Overview) or the
//   map dock (speaker tracks + 44 px control row, on the Transcript and summary tabs).
// This is where the selection, the "which view shows" decision, the menus, the shortcuts, the window's
// remembered size and the close guard live; separate components fill the regions. The
// content components (OverviewTab, TranscriptTab, SummaryTab) get three properties of the
// contract (CONTRACT.md): meetingId / player / shell.
ApplicationWindow {
    id: window

    // ---- overrides for demo / screenshots / smoke tests ----
    // "" = the state computed from data; otherwise forced:
    // "empty" | "noSelection" | "preTranscript" | "meeting" (tabs)
    property string shellState: ""
    // In demo: a sample task in the task bar.
    property bool taskRunning: false
    // In demo: initial search text (for the sidebar's search screenshot).
    property string demoSearch: ""
    // In demo / screenshots: a popup or state that opens at startup -
    // "retranscribe" | "delete" | "close" | "stop" | "confirm" | "toast" | "toastError" |
    // "cloudToast" | "filters" | "rename" | "tracks" | "import" | "importSplit" |
    // "importProbing" | "importError" | "importEmpty" | "importProgress" | "importStrip" | "drop" |
    // "tagToast" (tag step toast with "Undo") | "selection" (multi-select) |
    // "overviewChange" (the Overview change bar with "Undo")
    property string demoOverlay: ""
    // In demo: fictional state of the header tag row (MeetingTagsModel.demoState: none | few |
    // many | computing | similar | cooccur | llm | why).
    property string demoTags: ""
    // In demo / screenshots: state of the v3 meeting view (design/handoff-v3):
    // "overviewProcessing" | "overviewDone" | "overviewEmpty" |
    // "overviewNobody" | "overviewRecorded" (after recording, before transcription; "Start
    // transcription" in the step) | "overviewRecordedError" (transcription failed) |
    // "overviewTranscribing" (first transcription running, sections + "Cancel" in the step) |
    // "readingMap" (Transcript + map dock) | "tabTooltip" (tab panel).
    property string demoState: ""
    readonly property bool demoOverview: demoState.startsWith("overview")
    // In demo: states without a transcript (the header's Transcript tab is unavailable / reachable
    // only while running).
    readonly property bool demoNoTranscript: ["overviewProcessing", "overviewEmpty", "overviewRecorded",
                                              "overviewRecordedError", "overviewTranscribing"].indexOf(demoState) >= 0
    // "summarySources" (2nd tab, source chips) | "memoSections" (3rd tab) - for the screenshot of the wired
    // tool row and the dock's Source / Sections row.
    readonly property bool demoSummary: demoState === "summarySources" || demoState === "memoSections"

    // ---- state ----
    readonly property string computedState: sidebar.library.totalCount === 0 ? "empty"
                                          : !currentMeeting.exists ? "noSelection"
                                          : currentMeeting.hasTranscript ? "meeting" : "preTranscript"
    readonly property string viewState: shellState !== "" ? shellState : computedState
    readonly property bool hasMeeting: viewState === "meeting" || viewState === "preTranscript"
    // 0 = Overview, 1 = Transcript, 2 = Executive summary, 3 = Memo
    property alias currentTab: shellActions.currentTab
    // The map dock at the bottom (Transcript and summary tabs, if there is a transcript),
    // otherwise the player.
    readonly property bool mapDockShown: currentTab > 0 && header.hasTranscript && transcriptTab.editor.overview.length > 0
    // In demo mode the content components get an empty ID (-> their own sample content).
    readonly property string contentMeetingId: App.demo ? "" : shellActions.currentMeetingId

    // Entry points for tests / QA scripts.
    readonly property alias shell: shellActions
    readonly property alias player: playerController
    readonly property alias library: sidebar.library
    readonly property alias meetingModel: currentMeeting
    readonly property alias importModel: importModel
    readonly property alias importDialog: importDialog
    readonly property alias meetingHeader: header
    readonly property alias overview: overviewTab
    readonly property alias mapDock: mapDock

    property bool quitConfirmed: false
    property bool restoring: true

    // Space belongs to the player, except in a text field, on a keyboard-focused control (there Space
    // activates it), or while a dialog is open.
    readonly property bool spaceTogglesPlayer: {
        if (!hasMeeting || dialogs.anyOpen || participantsDialog.opened || importDialog.visible)
            return false
        const it = window.activeFocusItem
        if (!it)
            return true
        if (it instanceof TextInput || it instanceof TextEdit)
            return false
        return it.visualFocus !== true
    }

    // Ctrl+Z: ONE key in the meeting view - the meeting's shared undo stack
    // (shellActions.undoMeeting -> MeetingUndoService: editor, tag and participant actions
    // in chronological order), on any tab and focus. In a text field the field's own undo applies; the
    // People / Tags windows undo their own stack. Without a controller (demo) the tag model's
    // and the editor's own undo remain.
    readonly property bool undoKeyFree: {
        if (!hasMeeting || dialogs.anyOpen || participantsDialog.opened || importDialog.visible)
            return false
        const it = window.activeFocusItem
        return !(it instanceof TextInput || it instanceof TextEdit)
    }
    readonly property bool meetingUndoActive: undoKeyFree && shellActions.hasMeetingUndo && shellActions.canUndoMeeting
    readonly property bool demoTagUndoActive: {
        if (!undoKeyFree || shellActions.hasMeetingUndo || !currentMeeting.tags.canUndo)
            return false
        for (let it = window.activeFocusItem; it; it = it.parent)
            if (it === transcriptTab) return !transcriptTab.visible
        return true
    }

    // Filter the library to one tag (header chip, Tags window): search, other filters and the
    // multi-select are cleared, only this one tag stays.
    function filterLibraryByTag(tagId) {
        if (tagId !== "")
            sidebar.filterByTag(tagId)
    }

    // "Who was there?": the dialog is opened by ShellActions.openParticipants; until it is
    // wired in, a short notice.
    function openParticipants() {
        const id = shellActions.currentMeetingId
        if (typeof shellActions.openParticipants === "function")
            shellActions.openParticipants(id)
        else
            toast.show(qsTr("A „Ki volt ott?” párbeszéd még nincs beépítve ebbe a változatba."), "", "", false)
    }
    // Tab switch. Moving to Transcript offers the analysis awaiting approval
    // (ShellActions.maybeOfferParticipants).
    function showTab(index) {
        shellActions.showTab(index)
        if (index === 1 && typeof shellActions.maybeOfferParticipants === "function")
            shellActions.maybeOfferParticipants(shellActions.currentMeetingId)
    }
    // Executive summary and Memo are the same SummaryTab with a different section (SummaryTab's
    // `section` property, "exec" | "memo").
    function syncSummarySection() {
        if (summaryTab && "section" in summaryTab && currentTab >= 2)
            summaryTab.section = currentTab === 3 ? "memo" : "exec"
    }
    onCurrentTabChanged: syncSummarySection()

    width: 1280
    height: 820
    minimumWidth: 960
    minimumHeight: 600
    title: hasMeeting && currentMeeting.title !== "" ? currentMeeting.title + " — Tanara" : "Tanara"
    color: Theme.bg
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody

    // ---- view models ----
    ShellUiState { id: uiState }
    PlayerController {
        id: playerController
        meetingId: shellActions.currentMeetingId
        onVolumeChanged: if (!window.restoring) uiState.setValue("playerVolume", volume)
        onRateChanged: if (!window.restoring) uiState.setValue("playerRate", rate)
        onErrorOccurred: (message) => toast.show(qsTr("Lejátszási hiba: %1").arg(message), "danger", "", false)
    }
    ShellActions {
        id: shellActions
        player: playerController
        onCurrentMeetingIdChanged: {
            if (!window.restoring)
                uiState.setValue("selectedMeetingId", currentMeetingId)
            // For a meeting without a transcript the steps always show first (the header model
            // updates after the selection, so we check in the next round).
            Qt.callLater(() => { if (!currentMeeting.hasTranscript) shellActions.currentTab = 0 })
        }
        onToastRequested: (text, tone, requestId, usageLink, undoKey, revealPath) =>
                          toast.show(text, tone, requestId, usageLink, undoKey, revealPath)
        // Tag steps share one undo stack (TagService): undoing from any model
        // reverts the latest step.
        onUndoRequested: (undoKey) => { if (undoKey === "tags") currentMeeting.tags.undo() }
        // A new step on the shared stack. On Overview the change bar announces it ("... - Undo");
        // elsewhere a participant step (e.g. approving "Who was there?" from the Transcript) gets a toast, an
        // editor step is shown by the Transcript's own bar, a tag step by the tag row's toast.
        onMeetingChangePosted: (text, source) => {
            if (shellActions.currentTab === 0 && window.hasMeeting) {
                overviewTab.showChange(text)
            } else if (source === 2) {
                toast.show(text, "", "", false, "meeting")
            }
        }
        onMeetingChangeUndone: (text, source) => {
            if (shellActions.currentTab === 0 && window.hasMeeting) overviewTab.showUndone(text)
            else toast.show(qsTr("Visszavonva: %1").arg(text), "", "", false)
        }
        onTagFilterRequested: (tagId) => window.filterLibraryByTag(tagId)
        onConfirmRequested: (title, text, confirmLabel, danger) => dialogs.openConfirm(title, text, confirmLabel, danger)
        onRetranscribeDialogRequested: (meetingId) => dialogs.openRetranscribe(meetingId)
        onDeleteDialogRequested: (meetingId, title) => dialogs.openDelete(meetingId, title)
        onRenameRequested: Qt.callLater(header.startRename)
        onImportDialogRequested: (files) => importDialog.openWith(files)
        onWindowActivationRequested: { window.show(); window.raise(); window.requestActivate() }
    }
    ShellImportModel {
        id: importModel
        // An error from a backgrounded import (window closed) appears as a notification; the
        // details are there when the window is reopened.
        onFailed: (message) => {
            if (!importDialog.visible)
                toast.show(qsTr("Az importálás nem sikerült: %1").arg(message), "danger", "", false)
        }
    }
    ShellMeetingModel {
        id: currentMeeting
        meetingId: shellActions.currentMeetingId
        demoTask: App.demo && window.taskRunning
    }
    Binding {
        when: App.demo && window.demoTags !== ""
        target: currentMeeting.tags
        property: "demoState"
        value: window.demoTags
    }
    // v3 demo states: the header's tab state matching the screen.
    Binding {
        when: App.demo && window.demoState !== ""
        target: header; property: "hasTranscript"
        value: !window.demoNoTranscript
    }
    Binding {
        when: App.demo && window.demoState !== ""
        target: header; property: "hasSummary"
        value: window.demoState === "overviewDone" || window.demoSummary
    }
    Binding {
        when: App.demo && window.demoState !== ""
        target: header; property: "summaryStale"
        value: false
    }
    Binding {
        when: App.demo && window.demoState !== ""
        target: header; property: "transcribePercent"
        value: window.demoState === "overviewProcessing" ? 62 : window.demoState === "overviewEmpty" ? 18
             : window.demoState === "overviewTranscribing" ? 0 : -1
    }
    // From the Overview badge: the editor's "Why them?" panel (the tab is Transcript by then).
    Connections {
        target: shellActions
        ignoreUnknownSignals: true
        function onSpeakerWhyRequested(meetingId, speakerKey) {
            Qt.callLater(() => transcriptTab.openSpeakerWhyAt(speakerKey, transcriptTab.width / 2 - 215, 56))
        }
    }
    // Tag row steps (accept, reject, remove) go to the shell toast with "Undo".
    Connections {
        target: currentMeeting.tags
        function onToast(text, undoable) {
            // On Overview the shared change bar announces the undoable tag step.
            if (undoable && shellActions.hasMeetingUndo && shellActions.currentTab === 0) return
            shellActions.toast(text, undoable ? "tags" : "")
        }
    }

    Connections {
        target: App.bridge
        ignoreUnknownSignals: true
        function onStopPromptRequested(reason) { dialogs.openStopPrompt(reason) }
        function onShowWindowRequested() { window.show(); window.raise(); window.requestActivate() }
        function onQuitRequested() { window.quitConfirmed = true; window.close() }
        // Theme saved in Settings: remember it for the next start.
        function onThemeModeSaved(mode) { window.setTheme(mode) }
    }

    function saveGeometry() {
        if (App.demo)
            return
        const maximized = visibility === Window.Maximized || visibility === Window.FullScreen
        const g = uiState.value("window", {})
        if (!maximized && visibility !== Window.Minimized && visibility !== Window.Hidden) {
            g.w = width; g.h = height; g.x = x; g.y = y
        }
        g.maximized = maximized
        uiState.setValue("window", g)
    }

    Component.onCompleted: {
        if (App.demo) {
            // Demo / screenshot: a sample meeting matching the requested state.
            sidebar.forceEmpty = shellState === "empty"
            if (shellState === "preTranscript")
                shellActions.currentMeetingId = "demo-bemutato"
            else if (shellState === "" || shellState === "meeting")
                shellActions.currentMeetingId = "demo-partner"
            if (demoState === "readingMap" || demoState === "tabTooltip")
                Qt.callLater(() => shellActions.showTab(1))
            if (demoState === "summarySources")
                Qt.callLater(() => shellActions.showTab(2))
            if (demoState === "memoSections")
                Qt.callLater(() => shellActions.showTab(3))
            if (demoSearch !== "")
                sidebar.searchText = demoSearch
            // Popups open after the selection has run (in the round after the tab restore).
            Qt.callLater(() => Qt.callLater(window.applyDemoOverlay))
        } else {
            const g = uiState.value("window", null)
            if (g && g.w >= minimumWidth && g.h >= minimumHeight) {
                width = g.w
                height = g.h
            }
            if (g && g.maximized)
                Qt.callLater(window.showMaximized)
            // --theme / TANARA_THEME wins over the remembered theme.
            const theme = uiState.value("themeMode", "")
            if (theme !== "" && App.themeMode === "system")
                App.themeMode = theme
            playerController.volume = uiState.value("playerVolume", 1.0)
            playerController.rate = uiState.value("playerRate", 1.0)
            const last = uiState.value("selectedMeetingId", "")
            if (last !== "" && shellActions.meetingExists(last))
                shellActions.currentMeetingId = last
        }
        restoring = false
        if (App.bridge)
            App.bridge.windowShown()
    }

    onActiveChanged: if (active && App.bridge) App.bridge.windowActivated()

    onClosing: (close) => {
        // WHILE RECORDING, quitting would abort the recording -> ask first.
        if (shellActions.recording && !window.quitConfirmed) {
            close.accepted = false
            dialogs.openCloseWhileRecording()
            return
        }
        saveGeometry()
        uiState.flush()
        playerController.pause()
        if (App.bridge)
            App.bridge.shutdown()
    }

    function applyDemoOverlay() {
        switch (demoOverlay) {
        case "retranscribe": dialogs.openRetranscribe(shellActions.currentMeetingId); break
        case "delete": shellActions.requestDelete(shellActions.currentMeetingId); break
        case "close": dialogs.openCloseWhileRecording(); break
        case "stop": dialogs.openStopPrompt(DemoText.t("Úgy tűnik, véget ért: Teams (az app leállt vagy elengedte a mikrofont).", "Looks like it has ended: Teams (the app quit or released the microphone).")); break
        case "confirm": dialogs.openConfirm(DemoText.t("Törlöd az eldobott sávokat?", "Delete the discarded tracks?"), DemoText.t("2 eldobott sáv hangfájlja véglegesen törlődik. Ez nem vonható vissza.", "The audio files of 2 discarded tracks will be deleted permanently. This cannot be undone."), DemoText.t("Végleges törlés", "Delete permanently"), true); break
        case "toast": toast.show(DemoText.t("Elkészült az átirat: Negyedéves partnertalálkozó", "Transcript ready: Quarterly partner review"), "", "", false); break
        case "toastError": toast.show(DemoText.t("Nincs rögzíthető hangeszköz.", "No audio device to record from."), "danger", "", false); break
        case "tagToast": toast.show(DemoText.t("Javaslat elutasítva: #Nordvik", "Suggestion dismissed: #Nordvik"), "", "", false, "tags"); break
        case "overviewChange": overviewTab.showChange(DemoText.t("Résztvevő eltávolítva: Lantos Réka", "Participant removed: Rachel Lane")); break
        case "cloudToast": toast.show(DemoText.t("Az átírás a szolgáltató hibája miatt nem sikerült. A díjat ($0,42) visszaírtuk.", "Transcription failed because of a provider error. The charge ($0.42) has been refunded."), "", "req_8f3a2c71d0", true); break
        case "filters": sidebar.applyDemo("filters"); break       // tag filters + popover
        case "selection": sidebar.applyDemo("selection"); break   // multi-select
        case "rename": header.startRename(); break
        case "tracks": shellActions.showTab(0); overviewTab.openTracks(); break
        case "import": importModel.demoState = "files"; importDialog.open(); break
        case "importSplit": importModel.demoState = "split"; importDialog.open(); break
        case "importProbing": importModel.demoState = "probing"; importDialog.open(); break
        case "importError": importModel.demoState = "error"; importDialog.open(); break
        case "importEmpty": importDialog.open(); break
        case "importFailed": importModel.demoState = "failed"; importDialog.open(); break
        case "importProgress": importModel.demoState = "progress"; importDialog.open(); break
        case "importStrip": importModel.demoState = "progress"; break
        case "drop": dropOverlay.demo = true; break
        }
    }

    function setTheme(mode) {
        App.themeMode = mode
        uiState.setValue("themeMode", mode)
    }

    // ---- shortcuts ----
    Shortcut { sequences: [StandardKey.Quit]; onActivated: window.close() }
    Shortcut { sequence: "Alt+F"; onActivated: fileMenu.open() }
    Shortcut { sequence: "Alt+M"; enabled: window.hasMeeting; onActivated: meetingMenu.open() }
    Shortcut { sequence: "Alt+N"; onActivated: viewMenu.open() }
    // Ctrl+F: the library search (the field shows this hint); Ctrl+Shift+F: search in the
    // open transcript.
    Shortcut { sequences: [StandardKey.Find]; onActivated: sidebar.focusSearch() }
    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: window.viewState === "meeting"
        onActivated: { window.showTab(1); transcriptTab.openSearch() }
    }
    Shortcut { sequence: "Ctrl+N"; onActivated: shellActions.openRecorder() }
    Shortcut { sequence: "Ctrl+I"; enabled: !dialogs.anyOpen && !participantsDialog.opened && !importDialog.visible; onActivated: shellActions.openImport() }
    Shortcut { sequence: "Ctrl+,"; onActivated: shellActions.openSettings("") }
    Shortcut { sequence: "Ctrl+1"; enabled: window.hasMeeting; onActivated: window.showTab(0) }
    Shortcut { sequence: "Ctrl+2"; enabled: window.viewState === "meeting"; onActivated: window.showTab(1) }
    Shortcut { sequence: "Ctrl+3"; enabled: window.viewState === "meeting"; onActivated: window.showTab(2) }
    Shortcut { sequence: "Ctrl+4"; enabled: window.viewState === "meeting"; onActivated: window.showTab(3) }
    Shortcut { sequence: "F2"; enabled: window.hasMeeting && !dialogs.anyOpen && !participantsDialog.opened; onActivated: header.startRename() }
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: window.meetingUndoActive || window.demoTagUndoActive
        onActivated: {
            toast.hide()
            if (shellActions.hasMeetingUndo) shellActions.undoMeeting()
            else currentMeeting.tags.undo()
        }
    }
    Shortcut {
        sequence: "Space"
        enabled: window.spaceTogglesPlayer
        onActivated: playerController.toggle()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 1. Menu bar (the native window frame stays; the menus use the title bar style) ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.titleBarHeight
            color: Theme.surface

            RowLayout {
                anchors { fill: parent; leftMargin: 14; rightMargin: 8 }
                spacing: 8

                Rectangle {
                    implicitWidth: 14; implicitHeight: 14
                    radius: Theme.radiusLane
                    color: Theme.accent
                }
                TButton {
                    id: fileButton
                    text: qsTr("Fájl")
                    variant: "ghost"; size: "small"
                    leftPadding: 7; rightPadding: 7
                    font.weight: Theme.weightRegular
                    down: pressed || fileMenu.visible
                    onClicked: fileMenu.open()
                    TMenu {
                        id: fileMenu
                        y: fileButton.height + 2
                        TMenuItem {
                            text: qsTr("Új felvétel…")
                            iconName: "circle-dot"
                            shortcutText: "Ctrl+N"
                            onTriggered: shellActions.openRecorder()
                        }
                        TMenuItem {
                            text: qsTr("Hangfájl importálása…")
                            iconName: "import"
                            shortcutText: "Ctrl+I"
                            onTriggered: shellActions.openImport()
                        }
                        TMenuItem {
                            objectName: "importArchiveItem"
                            text: qsTr("Megbeszélés importálása archívumból…")
                            iconName: "folder-open"
                            onTriggered: shellActions.importArchive("")
                        }
                        TMenuItem {
                            objectName: "importFolderItem"
                            text: qsTr("Megbeszélés importálása mappából…")
                            iconName: "folder-open"
                            onTriggered: shellActions.importFolder("")
                        }
                        TMenuSeparator {}
                        TMenuItem {
                            text: qsTr("Beállítások…")
                            iconName: "settings"
                            shortcutText: "Ctrl+,"
                            onTriggered: shellActions.openSettings("")
                        }
                        TMenuItem {
                            text: qsTr("Személyek…")
                            iconName: "users"
                            onTriggered: shellActions.openPeople()
                        }
                        TMenuItem {
                            objectName: "onboardingItem"
                            text: qsTr("Első lépések…")
                            iconName: "sparkles"
                            onTriggered: shellActions.openOnboarding()
                        }
                        TMenuSeparator {}
                        TMenuItem {
                            text: qsTr("Kilépés")
                            reserveIconSpace: true
                            shortcutText: "Ctrl+Q"
                            onTriggered: window.close()
                        }
                    }
                }
                TButton {
                    id: meetingButton
                    objectName: "meetingMenuButton"
                    text: qsTr("Megbeszélés")
                    variant: "ghost"; size: "small"
                    leftPadding: 7; rightPadding: 7
                    font.weight: Theme.weightRegular
                    enabled: window.hasMeeting
                    down: pressed || meetingMenu.visible
                    onClicked: meetingMenu.open()
                    MeetingMenu {
                        id: meetingMenu
                        y: meetingButton.height + 2
                        shell: shellActions
                        meetingId: shellActions.currentMeetingId
                        hasMeeting: window.hasMeeting
                        hasTranscript: header.hasTranscript
                        canIdentify: header.canIdentify
                        onRenameRequested: Qt.callLater(header.startRename)
                        onParticipantsRequested: window.openParticipants()
                    }
                }
                TButton {
                    id: viewButton
                    text: qsTr("Nézet")
                    variant: "ghost"; size: "small"
                    leftPadding: 7; rightPadding: 7
                    font.weight: Theme.weightRegular
                    down: pressed || viewMenu.visible
                    onClicked: viewMenu.open()
                    TMenu {
                        id: viewMenu
                        y: viewButton.height + 2
                        TMenuItem {
                            text: qsTr("Áttekintés")
                            iconName: "info"
                            shortcutText: "Ctrl+1"
                            enabled: window.hasMeeting
                            checked: window.hasMeeting && window.currentTab === 0
                            onTriggered: window.showTab(0)
                        }
                        TMenuItem {
                            text: qsTr("Átirat")
                            iconName: "file-text"
                            shortcutText: "Ctrl+2"
                            enabled: window.viewState === "meeting"
                            checked: window.viewState === "meeting" && window.currentTab === 1
                            onTriggered: window.showTab(1)
                        }
                        TMenuItem {
                            text: qsTr("Vezetői összefoglaló")
                            iconName: "sparkles"
                            shortcutText: "Ctrl+3"
                            enabled: window.viewState === "meeting"
                            checked: window.viewState === "meeting" && window.currentTab === 2
                            onTriggered: window.showTab(2)
                        }
                        TMenuItem {
                            text: qsTr("Memó")
                            iconName: "list-tree"
                            shortcutText: "Ctrl+4"
                            enabled: window.viewState === "meeting"
                            checked: window.viewState === "meeting" && window.currentTab === 3
                            onTriggered: window.showTab(3)
                        }
                        TMenuSeparator {}
                        // The three Transcript views (same as the tool row's segmented control).
                        TMenuItem {
                            objectName: "modeReadItem"
                            text: qsTr("Olvasás")
                            iconName: "book-open"
                            shortcutText: "Ctrl+E"
                            enabled: window.viewState === "meeting" && transcriptTab.editor.hasTranscript
                            checked: window.viewState === "meeting" && transcriptTab.viewMode === "read"
                            onTriggered: { window.showTab(1); transcriptTab.setViewMode("read") }
                        }
                        TMenuItem {
                            objectName: "modeFixItem"
                            text: qsTr("Javítás")
                            iconName: "pencil"
                            shortcutText: "Ctrl+E"
                            enabled: window.viewState === "meeting" && transcriptTab.editor.hasTranscript
                            checked: window.viewState === "meeting" && transcriptTab.viewMode === "fix"
                            onTriggered: { window.showTab(1); transcriptTab.setViewMode("fix") }
                        }
                        TMenuItem {
                            objectName: "modeReviewItem"
                            text: qsTr("Átnézendő (%1)").arg(transcriptTab.editor.reviewCount)
                            iconName: "list-checks"
                            shortcutText: "B"
                            enabled: window.viewState === "meeting" && transcriptTab.editor.hasTranscript
                            checked: window.viewState === "meeting" && transcriptTab.viewMode === "review"
                            onTriggered: { window.showTab(1); transcriptTab.setViewMode("review") }
                        }
                        TMenuItem {
                            objectName: "railItem"
                            text: qsTr("Beszélő-oszlopok")
                            iconName: "panel-left"
                            shortcutText: "Ctrl+L"
                            enabled: window.viewState === "meeting"
                            checked: transcriptTab.editor.railVisible
                            onTriggered: { window.showTab(1); transcriptTab.editor.railVisible = !transcriptTab.editor.railVisible }
                        }
                        TMenuSeparator {}
                        TMenuItem {
                            text: qsTr("Felvétel-ablak előtérbe")
                            iconName: "mic"
                            onTriggered: shellActions.openRecorder()
                        }
                        TMenuSeparator {}
                        TMenuItem {
                            text: qsTr("Téma: a rendszer szerint")
                            iconName: "monitor-speaker"
                            checked: App.themeMode === "system"
                            onTriggered: window.setTheme("system")
                        }
                        TMenuItem {
                            text: qsTr("Világos téma")
                            iconName: "sun"
                            checked: App.themeMode === "light"
                            onTriggered: window.setTheme("light")
                        }
                        TMenuItem {
                            text: qsTr("Sötét téma")
                            iconName: "moon"
                            checked: App.themeMode === "dark"
                            onTriggered: window.setTheme("dark")
                        }
                    }
                }
                Item { Layout.fillWidth: true }

                // Recording is running - always visible, brings the recorder to the front.
                TButton {
                    visible: shellActions.recording
                    text: qsTr("Felvétel folyamatban")
                    variant: "ghost"; size: "small"
                    iconName: "circle-dot"
                    onClicked: shellActions.openRecorder()
                }
                // Tanara Cloud balance chip - absent in own-key mode.
                TButton {
                    visible: App.bridge ? App.bridge.cloudChipVisible : false
                    text: App.bridge ? App.bridge.cloudChipText : ""
                    toolTipText: App.bridge ? App.bridge.cloudChipToolTip : ""
                    variant: App.bridge && App.bridge.cloudChipTone === "danger" ? "dangerGhost" : "ghost"
                    size: "small"
                    iconName: App.bridge && App.bridge.cloudChipTone !== "normal" ? "triangle-alert" : ""
                    font.weight: App.bridge && App.bridge.cloudChipTone !== "normal" ? Theme.weightSemiBold
                                                                                    : Theme.weightRegular
                    onClicked: shellActions.openSettings("cloud")
                }
            }
            TDivider { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- 2. Sidebar ----
            Rectangle {
                Layout.preferredWidth: Theme.sidebarWidth
                Layout.fillHeight: true
                color: Theme.surface

                LibrarySidebar {
                    id: sidebar
                    anchors { fill: parent; rightMargin: 1 }
                    shell: shellActions
                    currentMeetingId: shellActions.currentMeetingId
                    importModel: importModel
                    onImportStripClicked: importDialog.open()
                }
                TDivider { vertical: true; anchors { top: parent.top; bottom: parent.bottom; right: parent.right } }
            }

            // ---- 3-7. Content ----
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    ShellCloudBanners {
                        bridge: App.bridge
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space5
                        Layout.rightMargin: Theme.space5
                        Layout.topMargin: Theme.space3
                    }

                    EmptyLibraryView {
                        visible: window.viewState === "empty"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        shell: shellActions
                    }
                    NoSelectionView {
                        visible: window.viewState === "noSelection"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        shell: shellActions
                    }

                    // ---- one-line header (56) ----
                    MeetingHeader {
                        id: header
                        visible: window.hasMeeting
                        Layout.fillWidth: true
                        shell: shellActions
                        meeting: currentMeeting
                        currentTab: shellActions.currentTab
                        demoState: window.demoState === "tabTooltip" ? "tabTooltip" : ""
                        onTabRequested: (index) => window.showTab(index)
                        onParticipantsRequested: window.openParticipants()
                    }
                    // Running, cancelable tasks (export, identification, re-transcription ...).
                    ColumnLayout {
                        visible: window.hasMeeting && currentMeeting.tasks.length > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space5
                        Layout.rightMargin: Theme.space5
                        Layout.topMargin: Theme.space2
                        Layout.bottomMargin: Theme.space1
                        spacing: Theme.space2
                        Repeater {
                            model: currentMeeting.tasks
                            TaskStrip {
                                required property var modelData
                                Layout.fillWidth: true
                                shell: shellActions
                                meetingId: shellActions.currentMeetingId
                                task: modelData
                            }
                        }
                    }

                    // ---- tab contents ----
                    StackLayout {
                        id: pages
                        visible: window.hasMeeting
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: Math.min(shellActions.currentTab, 2)

                        // 0 - Overview: right after recording, even before transcription. "Start transcription",
                        // the missing setting and the error are in the PROCESSING "Transcript" step; without audio
                        // (during import, damaged folder) this tab states the gap in the steps (the old
                        // PreTranscriptView is no longer needed here).
                        OverviewTab {
                            id: overviewTab
                            objectName: "overviewTab"
                            meetingId: window.contentMeetingId
                            player: playerController
                            shell: shellActions
                            tagsModel: window.demoOverview ? null : currentMeeting.tags
                            editor: transcriptTab.editor
                            demoState: window.demoOverview ? window.demoState : ""
                            onParticipantsRequested: window.openParticipants()
                        }
                        // 1 - Transcript (the tool row is at the top of the editor: TranscriptToolbar)
                        TranscriptTab {
                            id: transcriptTab
                            objectName: "transcriptTab"
                            meetingId: window.contentMeetingId
                            player: playerController
                            shell: shellActions
                            // Under the shared stack the editor's Ctrl+Z belongs to Main (one key).
                            undoAllowed: !shellActions.hasMeetingUndo && !window.demoTagUndoActive
                            onParticipantsRequested: window.openParticipants()
                        }
                        // 2-3 - Executive summary / Memo (the same view with a different section)
                        ColumnLayout {
                            spacing: 0
                            // The summary's tool row goes here (generation meta - Sources - Copy - Regenerate) - the
                            // SummaryTab's `toolsRow` component.
                            MeetingToolsRow {
                                id: summaryTools
                                objectName: "summaryTools"
                                Layout.fillWidth: true
                                SummaryToolsRow {
                                    objectName: "summaryToolsRow"
                                    anchors.fill: parent
                                    visible: summaryTab.sourcesLayout
                                    vm: summaryTab.vm
                                    meetingId: window.contentMeetingId
                                    shell: shellActions
                                }
                            }
                            SummaryTab {
                                id: summaryTab
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                meetingId: window.contentMeetingId
                                player: playerController
                                shell: shellActions
                                demoState: window.demoState === "summarySources" ? "sourcesOn"
                                         : window.demoState === "memoSections" ? "memoSections" : ""
                                // The shell's tool row and map dock take over (wiring between the
                                // tool row and the dock).
                                embedToolsRow: false
                                embedMapRows: false
                                showSectionSwitch: false
                                Component.onCompleted: window.syncSummarySection()
                            }
                        }
                    }

                    // ---- bottom: player (Overview) or map dock (Transcript, summary) ----
                    Rectangle {
                        visible: window.hasMeeting && !window.mapDockShown
                        Layout.fillWidth: true
                        implicitHeight: Theme.playerHeight
                        color: Theme.surface

                        PlayerBar {
                            anchors { fill: parent; topMargin: 1 }
                            player: playerController
                        }
                        TDivider { anchors { left: parent.left; right: parent.right; top: parent.top } }
                    }
                    MapDock {
                        id: mapDock
                        objectName: "mapDock"
                        visible: window.hasMeeting && window.mapDockShown
                        Layout.fillWidth: true
                        player: playerController
                        lanes: transcriptTab.editor.overview
                        viewportStart: shellActions.currentTab === 1 ? transcriptTab.viewportStart : 0
                        viewportSize: shellActions.currentTab === 1 ? transcriptTab.viewportSize : 0
                        collapsedCount: transcriptTab.editor.collapsedCount
                        lanesExpanded: transcriptTab.editor.lanesExpanded
                        timelineMs: transcriptTab.editor.timelineMs > 0 ? transcriptTab.editor.timelineMs
                                                                        : playerController.durationMs
                        // The SummaryTab's `sourceMarks` / `sectionBands` lists ({ startMs, endMs, active }).
                        // The sources of the memo's (3rd tab) claims too (the vm provides them per view).
                        sourceMarks: (shellActions.currentTab === 2 || shellActions.currentTab === 3)
                                     && summaryTab && ("sourceMarks" in summaryTab)
                                     ? summaryTab.sourceMarks : []
                        sectionBands: shellActions.currentTab === 3 && summaryTab && ("sectionBands" in summaryTab)
                                      ? summaryTab.sectionBands : []
                        // Accent marks of rows similar to the new person (only on the Transcript tab).
                        similarMarks: shellActions.currentTab === 1 ? transcriptTab.similarMarks : []
                        // Sample meter (in the Transcript's Fix / To review views); "+N now" -> filter.
                        sampleStats: shellActions.currentTab === 1 && transcriptTab.viewMode !== "read"
                                     ? transcriptTab.editor.sampleStats : []
                        onSamplesClicked: key => transcriptTab.showSessionSamples(key)
                        // Sweep mode (doubtful dots, sections, frozen window) - only on the
                        // Transcript tab.
                        sweep: shellActions.currentTab === 1 ? transcriptTab.sweepDock : ({})
                        onSeekRequested: (fraction) => {
                            if (shellActions.currentTab === 1) transcriptTab.seekToFraction(fraction)
                            else {
                                const ms = transcriptTab.editor.timeAtFraction(fraction)
                                playerController.seek(ms)
                                transcriptTab.selectRowAt(ms)
                            }
                        }
                        // Previous / next row: the Transcript's logic (jump while playing, select
                        // while stopped).
                        onLineStepRequested: delta => {
                            transcriptTab.stepLine(delta)
                            if (shellActions.currentTab === 1) transcriptTab.forceActiveFocus()
                        }
                        onSpeakerClicked: (key, anchor) => {
                            if (shellActions.currentTab !== 1) window.showTab(1)
                            Qt.callLater(() => transcriptTab.openSpeakerPopover(key, anchor))
                        }
                        onVoiceprintClicked: (key, anchor) => {
                            if (shellActions.currentTab !== 1) window.showTab(1)
                            Qt.callLater(() => transcriptTab.openVoiceprintPopover(key, anchor))
                        }
                        onExpandRequested: transcriptTab.editor.lanesExpanded = true
                        onCollapseRequested: transcriptTab.editor.lanesExpanded = false
                    }
                }

                // ---- Library: multi-select - the selection panel replaces the content ----
                LibrarySelectionPanel {
                    objectName: "selectionPanel"
                    anchors.fill: parent
                    visible: sidebar.selection.count > 1
                    selection: sidebar.selection
                }
                // ---- /multi-select ----

                // Non-modal notification at the bottom of the content (above the player).
                ShellToast {
                    id: toast
                    objectName: "toast"
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: (!window.hasMeeting ? 0 : window.mapDockShown ? mapDock.height
                                                                     : Theme.playerHeight) + Theme.space4
                    width: Math.min(implicitWidth, parent.width - 2 * Theme.space5)
                    onUsageLinkActivated: if (App.bridge) App.bridge.openUsageLog()
                    onUndoActivated: (undoKey) => { toast.hide(); shellActions.undoFromToast(undoKey) }
                    onRevealActivated: (path) => { toast.hide(); shellActions.revealFile(path) }
                }
            }
        }
    }

    // ---- drag and drop: audio / video file onto the window -> import; *.zip -> archive import ----
    DropArea {
        id: windowDrop
        anchors.fill: parent
        enabled: !dialogs.anyOpen && !participantsDialog.opened && !importDialog.visible
        onEntered: (drag) => { drag.accepted = drag.hasUrls }
        onDropped: (drop) => {
            if (!drop.hasUrls)
                return
            const audio = []
            for (const url of drop.urls) {
                if (shellActions.isArchiveFile(url)) shellActions.importArchive(url)
                else if (shellActions.isDirectory(url)) shellActions.importFolder(url)
                else audio.push(url)
            }
            if (audio.length > 0)
                shellActions.openImport(audio)
            drop.accept()
        }
    }
    Rectangle {
        id: dropOverlay
        property bool demo: false
        anchors.fill: parent
        visible: windowDrop.containsDrag || demo
        color: Theme.scrim
        TSurface {
            anchors.centerIn: parent
            width: Math.min(420, parent.width - 2 * Theme.space5)
            height: dropColumn.implicitHeight + 2 * Theme.space6
            radius: Theme.radiusDialog
            TDashedRect {
                anchors { fill: parent; margins: 10 }
                radius: Theme.radiusPopup
                color: Theme.accent
            }
            ColumnLayout {
                id: dropColumn
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space6
                spacing: 8
                TIcon { Layout.alignment: Qt.AlignHCenter; name: "import"; size: 26; color: Theme.accent }
                TLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: importModel.running ? qsTr("Előbb fejeződjön be a futó importálás")
                                              : qsTr("Engedd el az importáláshoz")
                    font.pixelSize: Theme.fontHeading
                    font.weight: Theme.weightSemiBold
                    wrapMode: Text.Wrap
                }
                TLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("A hang- vagy videófájlokból új megbeszélés lesz: fájlonként egy sáv. "
                               + "Egy .tanara.zip archívum a teljes megbeszélést hozza be.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.5
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    ShellImportDialog {
        id: importDialog
        model: importModel
        shell: shellActions
    }

    // "Who was there?": opens on the ShellActions.participantsDialogRequested signal.
    ParticipantsDialog {
        id: participantsDialog
        shell: shellActions
    }

    ShellDialogs {
        id: dialogs
        shell: shellActions
        onBackgroundRequested: {
            // The floating recorder is a separate window -> the app stays alive; the main window comes
            // back by itself when the recording ends.
            if (App.bridge)
                App.bridge.continueRecordingInBackground()
            window.hide()
        }
        onStopAndQuitRequested: {
            toast.show(qsTr("Felvétel leállítása, kilépés utána…"), "", "", false)
            if (App.bridge)
                App.bridge.stopRecordingAndQuit()
        }
    }
}
