import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Tanara — főablak (héj). Szerkezet (design/handoff/README.md, „Window structure”):
//   menüsor (36) · oldalsáv (276) · tartalom · lejátszó (52)
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
    // "selection" (T05: többes kijelölés)
    property string demoOverlay: ""

    // ---- állapot ----
    readonly property string computedState: sidebar.library.totalCount === 0 ? "empty"
                                          : !currentMeeting.exists ? "noSelection"
                                          : currentMeeting.hasTranscript ? "meeting" : "preTranscript"
    readonly property string viewState: shellState !== "" ? shellState : computedState
    readonly property bool hasMeeting: viewState === "meeting" || viewState === "preTranscript"
    // Átirat előtt a „Sávok” (Nézet menü / Ctrl+3) a lépések helyén mutatja a sávokat.
    readonly property bool tracksBeforeTranscript: viewState === "preTranscript" && shellActions.currentTab === 2
    readonly property bool tabsShown: viewState === "meeting" || tracksBeforeTranscript
    // 0 = Átirat, 1 = Összefoglaló, 2 = Sávok
    property alias currentTab: shellActions.currentTab
    // A tartalom-komponensek demó-módban üres azonosítót kapnak (→ saját mintatartalom).
    readonly property string contentMeetingId: App.demo ? "" : shellActions.currentMeetingId

    // A tesztek / QA-szkriptek belépőpontjai.
    readonly property alias shell: shellActions
    readonly property alias player: playerController
    readonly property alias library: sidebar.library
    readonly property alias meetingModel: currentMeeting
    readonly property alias importModel: importModel
    readonly property alias importDialog: importDialog

    property bool quitConfirmed: false
    property bool restoring: true

    // A Szóköz a lejátszóé, kivéve ha szövegmezőben vagyunk, billentyűzettel fókuszált
    // vezérlőn állunk (ott a Szóköz azt aktiválja), vagy párbeszédablak van nyitva.
    readonly property bool spaceTogglesPlayer: {
        if (!hasMeeting || dialogs.anyOpen || importDialog.visible)
            return false
        const it = window.activeFocusItem
        if (!it)
            return true
        if (it instanceof TextInput || it instanceof TextEdit)
            return false
        return it.visualFocus !== true
    }

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
        onToastRequested: (text, tone, requestId, usageLink) => toast.show(text, tone, requestId, usageLink)
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
        case "cloudToast": toast.show("Az átírás a szolgáltató hibája miatt nem sikerült. A díjat ($0,42) visszaírtuk.", "", "req_8f3a2c71d0", true); break
        case "filters": sidebar.applyDemo("filters"); break       // T04: címke-szűrők + popover
        case "selection": sidebar.applyDemo("selection"); break   // T05
        case "rename": header.startRename(); break
        case "tracks": shellActions.showTab(2); break
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
    Shortcut { sequence: "Alt+N"; onActivated: viewMenu.open() }
    // Ctrl+F: a könyvtár keresője (a mező ezt a tippet mutatja); Ctrl+Shift+F: keresés a
    // megnyitott átiratban.
    Shortcut { sequences: [StandardKey.Find]; onActivated: sidebar.focusSearch() }
    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: window.viewState === "meeting"
        onActivated: { shellActions.showTab(0); transcriptTab.openSearch() }
    }
    Shortcut { sequence: "Ctrl+N"; onActivated: shellActions.openRecorder() }
    Shortcut { sequence: "Ctrl+I"; enabled: !dialogs.anyOpen && !importDialog.visible; onActivated: shellActions.openImport() }
    Shortcut { sequence: "Ctrl+,"; onActivated: shellActions.openSettings("") }
    Shortcut { sequence: "Ctrl+1"; enabled: window.viewState === "meeting"; onActivated: shellActions.showTab(0) }
    Shortcut { sequence: "Ctrl+2"; enabled: window.viewState === "meeting"; onActivated: shellActions.showTab(1) }
    Shortcut { sequence: "Ctrl+3"; enabled: window.hasMeeting; onActivated: shellActions.showTab(2) }
    Shortcut { sequence: "F2"; enabled: window.hasMeeting && !dialogs.anyOpen; onActivated: header.startRename() }
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
                            text: qsTr("Átirat")
                            iconName: "file-text"
                            shortcutText: "Ctrl+1"
                            enabled: window.viewState === "meeting"
                            onTriggered: shellActions.showTab(0)
                        }
                        TMenuItem {
                            text: qsTr("Összefoglaló")
                            iconName: "sparkles"
                            shortcutText: "Ctrl+2"
                            enabled: window.viewState === "meeting"
                            onTriggered: shellActions.showTab(1)
                        }
                        TMenuItem {
                            text: qsTr("Sávok")
                            iconName: "audio-lines"
                            shortcutText: "Ctrl+3"
                            enabled: window.hasMeeting
                            onTriggered: shellActions.showTab(2)
                        }
                        TMenuSeparator {}
                        TMenuItem {
                            text: qsTr("Résztvevők azonosítása (hang alapján)")
                            iconName: "fingerprint"
                            enabled: window.hasMeeting && currentMeeting.canIdentify && !currentMeeting.identifyRunning
                            onTriggered: shellActions.identifyParticipants(shellActions.currentMeetingId)
                        }
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

                    // Fejléc + feladat-sáv + fülek: 16/24/0 belső margó, 12 térköz.
                    ColumnLayout {
                        visible: window.hasMeeting
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space5
                        Layout.rightMargin: Theme.space5
                        Layout.topMargin: Theme.space4
                        spacing: Theme.space3

                        MeetingHeader {
                            id: header
                            Layout.fillWidth: true
                            shell: shellActions
                            meeting: currentMeeting
                        }
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
                        TTabBar {
                            id: tabs
                            visible: window.viewState === "meeting"
                            Layout.fillWidth: true
                            onCurrentIndexChanged: if (shellActions.currentTab !== currentIndex) shellActions.currentTab = currentIndex
                            TTabButton { text: qsTr("Átirat") }
                            TTabButton {
                                text: qsTr("Összefoglaló")
                                pillText: currentMeeting.summaryStale ? qsTr("elavult") : ""
                                pillTone: "warn"
                            }
                            TTabButton { text: qsTr("Sávok") }
                            Connections {
                                target: shellActions
                                function onCurrentTabChanged() {
                                    if (tabs.currentIndex !== shellActions.currentTab)
                                        tabs.currentIndex = shellActions.currentTab
                                }
                            }
                        }
                        // Átirat előtt a sávok nézete: visszaút a lépésekhez.
                        RowLayout {
                            visible: window.tracksBeforeTranscript
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            TButton {
                                text: qsTr("Vissza az előkészítéshez")
                                iconName: "chevron-left"
                                size: "small"
                                onClicked: shellActions.showTab(0)
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: qsTr("A felvétel sávjai — az átírás előtt is visszaállíthatsz eldobott sávot.")
                                muted: true
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }
                    }

                    StackLayout {
                        visible: window.tabsShown
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: shellActions.currentTab
                        TranscriptTab {
                            id: transcriptTab
                            meetingId: window.contentMeetingId
                            player: playerController
                            shell: shellActions
                        }
                        SummaryTab {
                            id: summaryTab
                            meetingId: window.contentMeetingId
                            player: playerController
                            shell: shellActions
                        }
                        TracksTab {
                            id: tracksTab
                            meetingId: window.contentMeetingId
                            player: playerController
                            shell: shellActions
                        }
                    }
                    PreTranscriptView {
                        id: preTranscriptView
                        meetingId: window.contentMeetingId
                        player: playerController
                        shell: shellActions
                        visible: window.viewState === "preTranscript" && !window.tracksBeforeTranscript
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    Rectangle {
                        visible: window.hasMeeting
                        Layout.fillWidth: true
                        implicitHeight: Theme.playerHeight
                        color: Theme.surface

                        PlayerBar {
                            anchors { fill: parent; topMargin: 1 }
                            player: playerController
                        }
                        TDivider { anchors { left: parent.left; right: parent.right; top: parent.top } }
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
                    anchors.bottomMargin: (window.hasMeeting ? Theme.playerHeight : 0) + Theme.space4
                    width: Math.min(implicitWidth, parent.width - 2 * Theme.space5)
                    onUsageLinkActivated: if (App.bridge) App.bridge.openUsageLog()
                }
            }
        }
    }

    // ---- húzd-és-ejtsd: hang- / videófájl az ablakra → importálás ----
    DropArea {
        id: windowDrop
        anchors.fill: parent
        enabled: !dialogs.anyOpen && !importDialog.visible
        onEntered: (drag) => { drag.accepted = drag.hasUrls }
        onDropped: (drop) => {
            if (!drop.hasUrls)
                return
            shellActions.openImport(drop.urls)
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
                    text: qsTr("A hang- vagy videófájlokból új megbeszélés lesz: fájlonként egy sáv.")
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
