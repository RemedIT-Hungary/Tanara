import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Tanara — főablak (héj). Szerkezet (design/handoff/README.md, „Window structure”; a
// megbeszélés-nézet a design/handoff-v3 szerint):
//   menüsor (36: Fájl · Megbeszélés · Nézet) · oldalsáv (276) · tartalom:
//   egysoros fejléc (56: cím, fülek Áttekintés · Átirat · Vezetői összefoglaló · Memó, „…”) ·
//   fülenkénti eszköz-sor (46) · a fül tartalma · alul a lejátszó (52, Áttekintésen) vagy a
//   térkép-dokk (beszélő-sávok + 44 px vezérlősor, az Átirat és az összefoglaló füleken).
// Itt él a kijelölés, a „melyik nézet látszik” döntés, a menük, a gyorsbillentyűk, az ablak
// megjegyzett mérete és a bezárás-védelem; a régiókat külön komponensek töltik ki. A
// tartalom-komponensek (TranscriptTab, SummaryTab, TracksTab, PreTranscriptView) a szerződés
// (CONTRACT.md) három property-jét kapják: meetingId / player / shell.
ApplicationWindow {
    id: window

    // ---- felülbírálások demóhoz / képernyőképhez / füstteszthez ----
    // "" = az adatokból számolt állapot; különben kényszerített:
    // "empty" (M01) | "noSelection" (M02) | "preTranscript" (M03–M05) | "meeting" (fülek)
    property string shellState: ""
    // Demóban: minta-feladat a feladat-sávban.
    property bool taskRunning: false
    // Demóban: kezdő keresőszöveg (az M05 oldalsáv képéhez).
    property string demoSearch: ""
    // Demóban / képernyőképhez: induláskor megnyíló felugró vagy állapot —
    // "retranscribe" | "delete" | "close" | "stop" | "confirm" | "toast" | "toastError" |
    // "cloudToast" | "filters" | "rename" | "tracks" | "import" | "importSplit" |
    // "importProbing" | "importError" | "importEmpty" | "importProgress" | "importStrip" | "drop" |
    // "tagToast" (címke-lépés toast „Visszavonás”-sal) | "selection" (T05: többes kijelölés)
    property string demoOverlay: ""
    // Demóban: a fejléc címkesorának kitalált állapota (MeetingTagsModel.demoState: none | few |
    // many | computing | similar | cooccur | llm | why).
    property string demoTags: ""
    // Demóban / képernyőképhez a v3 megbeszélés-nézet állapota (design/handoff-v3):
    // "overviewProcessing" (V1) | "overviewDone" (V3) | "overviewEmpty" (V5) |
    // "overviewNobody" (V6) | "readingMap" (V4: Átirat + térkép-dokk) | "tabTooltip" (V4 panel).
    property string demoState: ""
    readonly property bool demoOverview: demoState.startsWith("overview")
    // "summarySources" (S1: 2. fül, forrás-chipek) | "memoSections" (S2: 3. fül) — a bekötött
    // eszköz-sor és a dokk Forrás/Szakaszok sorának képernyőképéhez.
    readonly property bool demoSummary: demoState === "summarySources" || demoState === "memoSections"

    // ---- állapot ----
    readonly property string computedState: sidebar.library.totalCount === 0 ? "empty"
                                          : !currentMeeting.exists ? "noSelection"
                                          : currentMeeting.hasTranscript ? "meeting" : "preTranscript"
    readonly property string viewState: shellState !== "" ? shellState : computedState
    readonly property bool hasMeeting: viewState === "meeting" || viewState === "preTranscript"
    // 0 = Áttekintés, 1 = Átirat, 2 = Vezetői összefoglaló, 3 = Memó
    property alias currentTab: shellActions.currentTab
    // Átirat előtt, amíg az átírás el sem indult, az Áttekintés helyén az átirat előtti lépések
    // (kontextus, előkészítés, „Átírás indítása”, hiba-kártya) látszanak; futás közben és utána
    // az Áttekintés.
    readonly property bool preTranscriptSteps: viewState === "preTranscript" && !demoOverview
                                               && !overviewTab.vm.transcribing
    // Alul a térkép-dokk (Átirat és összefoglaló fülek, ha van átirat), különben a lejátszó.
    readonly property bool mapDockShown: currentTab > 0 && header.hasTranscript && transcriptTab.editor.overview.length > 0
    // A tartalom-komponensek demó-módban üres azonosítót kapnak (→ saját mintatartalom).
    readonly property string contentMeetingId: App.demo ? "" : shellActions.currentMeetingId

    // A tesztek / QA-szkriptek belépőpontjai.
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

    // A Szóköz a lejátszóé, kivéve ha szövegmezőben vagyunk, billentyűzettel fókuszált
    // vezérlőn állunk (ott a Szóköz azt aktiválja), vagy párbeszédablak van nyitva.
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

    // Ctrl+Z: az átirat-szerkesztőben a szerkesztő saját visszavonása; máshol (fejléc, címkesor,
    // lépések) a legutóbbi címke-lépés visszavonása. Szövegmezőben a mező saját visszavonása él.
    readonly property bool focusInTranscript: {
        for (let it = window.activeFocusItem; it; it = it.parent)
            if (it === transcriptTab) return true
        return false
    }
    readonly property bool tagUndoActive: {
        if (!hasMeeting || dialogs.anyOpen || participantsDialog.opened || importDialog.visible || !currentMeeting.tags.canUndo)
            return false
        if (focusInTranscript && transcriptTab.visible)
            return false
        const it = window.activeFocusItem
        return !(it instanceof TextInput || it instanceof TextEdit)
    }

    // A könyvtár szűrése egy címkére (fejléc-chip, Címkék ablak): a keresés, a többi szűrő és a
    // többes kijelölés törlődik, csak ez az egy címke marad.
    function filterLibraryByTag(tagId) {
        if (tagId !== "")
            sidebar.filterByTag(tagId)
    }

    // „Ki volt ott?” (U3): a párbeszédet a ShellActions.openParticipants nyitja; amíg az nincs
    // beépítve, egy rövid jelzés.
    function openParticipants() {
        const id = shellActions.currentMeetingId
        if (typeof shellActions.openParticipants === "function")
            shellActions.openParticipants(id)
        else
            toast.show(qsTr("A „Ki volt ott?” párbeszéd még nincs beépítve ebbe a változatba."), "", "", false)
    }
    // Fülváltás. Az Átiratra lépve a jóváhagyásra váró elemzést felajánlja (U3:
    // ShellActions.maybeOfferParticipants).
    function showTab(index) {
        shellActions.showTab(index)
        if (index === 1 && typeof shellActions.maybeOfferParticipants === "function")
            shellActions.maybeOfferParticipants(shellActions.currentMeetingId)
    }
    // A Vezetői összefoglaló és a Memó ugyanaz a SummaryTab, más szakasszal (U4: a SummaryTab
    // `section` property-je, "exec" | "memo").
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

    // ---- nézetmodellek ----
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
            // Átirat nélküli megbeszélésnél mindig a lépések látszanak először (a fejléc-modell
            // a kijelölés után frissül, ezért a következő körben nézzük meg).
            Qt.callLater(() => { if (!currentMeeting.hasTranscript) shellActions.currentTab = 0 })
        }
        onToastRequested: (text, tone, requestId, usageLink, undoKey, revealPath) =>
                          toast.show(text, tone, requestId, usageLink, undoKey, revealPath)
        // A címke-lépések egy közös visszavonási vermen vannak (TagService): bármelyik modell
        // visszavonása a legutóbbi lépést veszi vissza.
        onUndoRequested: (undoKey) => { if (undoKey === "tags") currentMeeting.tags.undo() }
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
        // A háttérbe tett (bezárt ablakú) importálás hibája értesítésként jelenik meg; az
        // ablak újranyitásakor a részletek is ott vannak.
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
    // v3 demó-állapotok: a fejléc fül-állapota a képernyőnek megfelelően (V1/V3/V4/V5/V6).
    Binding {
        when: App.demo && window.demoState !== ""
        target: header; property: "hasTranscript"
        value: window.demoState !== "overviewProcessing" && window.demoState !== "overviewEmpty"
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
        value: window.demoState === "overviewProcessing" ? 62 : window.demoState === "overviewEmpty" ? 18 : -1
    }
    // A címkesor lépései (elfogadás, elutasítás, levétel) a héj toastján, „Visszavonás”-sal.
    Connections {
        target: currentMeeting.tags
        function onToast(text, undoable) { shellActions.toast(text, undoable ? "tags" : "") }
    }

    Connections {
        target: App.bridge
        ignoreUnknownSignals: true
        function onStopPromptRequested(reason) { dialogs.openStopPrompt(reason) }
        function onShowWindowRequested() { window.show(); window.raise(); window.requestActivate() }
        function onQuitRequested() { window.quitConfirmed = true; window.close() }
        // A Beállításokban mentett téma: megjegyezzük a következő indításra.
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
            // Demó / képernyőkép: a kért állapothoz illő minta-megbeszélés.
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
            // A felugrók a kijelölés lefutása után nyílnak (a fül-visszaállítás utáni körben).
            Qt.callLater(() => Qt.callLater(window.applyDemoOverlay))
        } else {
            const g = uiState.value("window", null)
            if (g && g.w >= minimumWidth && g.h >= minimumHeight) {
                width = g.w
                height = g.h
            }
            if (g && g.maximized)
                Qt.callLater(window.showMaximized)
            // A --theme / TANARA_THEME erősebb a megjegyzett témánál.
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
        // FELVÉTEL KÖZBEN a kilépés megszakítaná a felvételt → előbb megkérdezzük.
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
        case "stop": dialogs.openStopPrompt("Úgy tűnik, véget ért: Teams (az app leállt vagy elengedte a mikrofont)."); break
        case "confirm": dialogs.openConfirm("Törlöd az eldobott sávokat?", "2 eldobott sáv hangfájlja véglegesen törlődik. Ez nem vonható vissza.", "Végleges törlés", true); break
        case "toast": toast.show("Elkészült az átirat: Negyedéves partnertalálkozó", "", "", false); break
        case "toastError": toast.show("Nincs rögzíthető hangeszköz.", "danger", "", false); break
        case "tagToast": toast.show("Javaslat elutasítva: #Nordvik", "", "", false, "tags"); break
        case "cloudToast": toast.show("Az átírás a szolgáltató hibája miatt nem sikerült. A díjat ($0,42) visszaírtuk.", "", "req_8f3a2c71d0", true); break
        case "filters": sidebar.applyDemo("filters"); break       // T04: címke-szűrők + popover
        case "selection": sidebar.applyDemo("selection"); break   // T05
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

    // ---- gyorsbillentyűk ----
    Shortcut { sequences: [StandardKey.Quit]; onActivated: window.close() }
    Shortcut { sequence: "Alt+F"; onActivated: fileMenu.open() }
    Shortcut { sequence: "Alt+M"; enabled: window.hasMeeting; onActivated: meetingMenu.open() }
    Shortcut { sequence: "Alt+N"; onActivated: viewMenu.open() }
    // Ctrl+F: a könyvtár keresője (a mező ezt a tippet mutatja); Ctrl+Shift+F: keresés a
    // megnyitott átiratban.
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
        enabled: window.tagUndoActive
        onActivated: { toast.hide(); currentMeeting.tags.undo() }
    }
    Shortcut {
        sequence: "Space"
        enabled: window.spaceTogglesPlayer
        onActivated: playerController.toggle()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 1. Menüsor (natív ablakkeret marad; a menük a címsor stílusában) ----
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
                        TMenuItem {
                            objectName: "fixModeItem"
                            text: transcriptTab.fixMode ? qsTr("Olvasás") : qsTr("Javítás")
                            iconName: transcriptTab.fixMode ? "book-open" : "pencil"
                            shortcutText: "Ctrl+E"
                            enabled: window.viewState === "meeting"
                            onTriggered: { window.showTab(1); transcriptTab.fixMode = !transcriptTab.fixMode }
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

                // Felvétel fut — mindig látszik, a felvevőt hozza előre.
                TButton {
                    visible: shellActions.recording
                    text: qsTr("Felvétel folyamatban")
                    variant: "ghost"; size: "small"
                    iconName: "circle-dot"
                    onClicked: shellActions.openRecorder()
                }
                // Tanara Cloud egyenleg-chip (K-08) — saját kulcsos módban nincs.
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

            // ---- 2. Oldalsáv ----
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

            // ---- 3–7. Tartalom ----
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

                    // ---- egysoros fejléc (56) ----
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
                    // Futó, megszakítható feladatok (export, azonosítás, újra-átírás …).
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

                    // ---- a fülek tartalma ----
                    StackLayout {
                        id: pages
                        visible: window.hasMeeting
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: Math.min(shellActions.currentTab, 2)

                        // 0 — Áttekintés (átirat előtt, indítás előtt: az átirat előtti lépések)
                        Item {
                            OverviewTab {
                                id: overviewTab
                                objectName: "overviewTab"
                                anchors.fill: parent
                                visible: !window.preTranscriptSteps
                                meetingId: window.contentMeetingId
                                player: playerController
                                shell: shellActions
                                tagsModel: window.demoOverview ? null : currentMeeting.tags
                                editor: transcriptTab.editor
                                demoState: window.demoOverview ? window.demoState : ""
                                onParticipantsRequested: window.openParticipants()
                            }
                            PreTranscriptView {
                                id: preTranscriptView
                                anchors.fill: parent
                                visible: window.preTranscriptSteps
                                meetingId: window.contentMeetingId
                                player: playerController
                                shell: shellActions
                            }
                        }
                        // 1 — Átirat (az eszköz-sor a szerkesztő tetején: TranscriptToolbar)
                        TranscriptTab {
                            id: transcriptTab
                            objectName: "transcriptTab"
                            meetingId: window.contentMeetingId
                            player: playerController
                            shell: shellActions
                            undoAllowed: !window.tagUndoActive
                            onParticipantsRequested: window.openParticipants()
                        }
                        // 2–3 — Vezetői összefoglaló / Memó (ugyanaz a nézet, más szakasszal)
                        ColumnLayout {
                            spacing: 0
                            // U4: ide kerül az összefoglaló eszköz-sora (generálás-meta · Források ·
                            // Másolás · Újragenerálás) — a SummaryTab `toolsRow` komponense.
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
                                // A héj eszköz-sora és térkép-dokkja veszi át (U1 ↔ U4 bekötés).
                                embedToolsRow: false
                                embedMapRows: false
                                showSectionSwitch: false
                                Component.onCompleted: window.syncSummarySection()
                            }
                        }
                    }

                    // ---- alul: lejátszó (Áttekintés) vagy térkép-dokk (Átirat, összefoglaló) ----
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
                        // U4: a SummaryTab `sourceMarks` / `sectionBands` listái ({ startMs, endMs, active }).
                        sourceMarks: shellActions.currentTab === 2 && summaryTab && ("sourceMarks" in summaryTab)
                                     ? summaryTab.sourceMarks : []
                        sectionBands: shellActions.currentTab === 3 && summaryTab && ("sectionBands" in summaryTab)
                                      ? summaryTab.sectionBands : []
                        onSeekRequested: (fraction) => {
                            if (shellActions.currentTab === 1) transcriptTab.seekToFraction(fraction)
                            else playerController.seek(transcriptTab.editor.timeAtFraction(fraction))
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

                // ---- Könyvtár: többes kijelölés (C05, T05) — a tartalom helyén a kijelölés-panel ----
                LibrarySelectionPanel {
                    objectName: "selectionPanel"
                    anchors.fill: parent
                    visible: sidebar.selection.count > 1
                    selection: sidebar.selection
                }
                // ---- /többes kijelölés ----

                // Nem-modális értesítés a tartalom alján (a lejátszó fölött).
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

    // ---- húzd-és-ejtsd: hang- / videófájl az ablakra → importálás; *.zip → archívum-import ----
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

    // „Ki volt ott?” (U3): a ShellActions.participantsDialogRequested jelre nyílik.
    ParticipantsDialog {
        id: participantsDialog
        shell: shellActions
    }

    ShellDialogs {
        id: dialogs
        shell: shellActions
        onBackgroundRequested: {
            // A lebegő felvevő önálló ablak → az app életben marad; a főablak a felvétel
            // végén magától visszajön.
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
