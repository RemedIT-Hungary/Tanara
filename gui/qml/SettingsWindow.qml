import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// The Settings window: a separate, resizable window (900 x 680), a left sidebar with five groups
// and their jump points (SettingsSidebar), one scrollable page per group whose blocks are the jump
// points (SettingsSection), a footer with the unsaved-changes indicator and "Cancel" / "Save".
// A jump point scrolls to its block and outlines it once; while scrolling, the sidebar marks the
// section in view. The view model's page / section say where we are (openPage maps the old names).
//
// Changes apply on "Save" (except the theme, which shows immediately). Closing with unsaved
// changes asks (Save / Discard / Cancel). The native window frame stays (as in the main window),
// so the spec's own 36 px title bar is not drawn.
//
// The C++ host (tanara_qml::SettingsWindowHost) creates it and opens the requested page; it can
// also be loaded standalone for screenshots:
//   tanara --qml-shot out.png --qml-page SettingsWindow --size 900x680 --qml-prop 'demoState="B03"'
// demoState: B01 ... B07 · dirty · unsaved · schema · cloudComingSoon · cloudSelected · addApp · reset
// · cloudSignedOut · cloudLowBalance · cloudAdvanced (the Tanara Cloud page in live mode)
// · B04embedding · B04embeddingRunning · B04embeddingError · B04embeddingDone · B04embeddingCloud
// · B04embeddingNone · B04modelChange · rejectedReset (confirmation)
// · B08 · B08download · B08none · B08fallback · B08error · B08offline · B08backfill · B08partial · B08bundled
//   · B08diaroff · B08segmissing · B08segdownload · B08segmismatch · B08seginstalled
//   (Speakers page: voice models, voiceprint backfill, the local speaker separation: the switch + the
//    segmentation models)
ApplicationWindow {
    id: window

    // Passed by the host (as an initial property). The view model takes the App's services (the
    // fictional ones in the demo / screenshot, where demoState picks the design state).
    property QtObject hostDialogs: null
    property string demoState: ""
    property alias vm: model
    property bool closeConfirmed: false

    title: qsTr("Beállítások")
    width: 900
    height: 680
    minimumWidth: 760
    minimumHeight: 560
    color: Theme.bg
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody
    // A separate, non-modal window: the main window stays usable next to it.
    flags: Qt.Window

    SettingsViewModel {
        id: model
        onSaved: saveNote.show()
    }

    Component.onCompleted: {
        if (hostDialogs) model.dialogs = hostDialogs
        if (demoState !== "") {
            // Screenshot / demo: the helper states (popups) also select a page.
            const base = demoState === "addApp" ? "B03" : demoState === "reset" ? "B07" : demoState === "rejectedReset" ? "B04embeddingDone" : demoState
            model.demoState = base
            Qt.callLater(window.applyDemoOverlay)
        }
    }

    function applyDemoOverlay() {
        switch (demoState) {
        case "unsaved": unsavedDialog.open(); break
        case "schema": schemaDialog.open(); break
        case "reset": resetDialog.open(); break
        case "rejectedReset": rejectedDialog.open(); break
        }
    }

    // ---- closing ----
    function requestClose() {
        if (model.dirty) unsavedDialog.open()
        else { window.closeConfirmed = true; window.close() }
    }
    function cancelAndClose() {
        model.discard()
        window.closeConfirmed = true
        window.close()
    }
    onClosing: (close) => {
        if (window.closeConfirmed || !model.dirty) {
            window.closeConfirmed = false
            return
        }
        close.accepted = false
        unsavedDialog.open()
    }
    onVisibleChanged: {
        // Level monitoring and call detection run only while their page is visible.
        if (!visible) {
            window.closeConfirmed = false
            // No question stays open in a hidden window (reopening starts clean).
            unsavedDialog.close(); resetDialog.close(); schemaDialog.close()
            rejectedDialog.close()
        }
    }
    // Both run while the Recording page (devices + call watcher) is visible.
    Binding { target: model; property: "monitoring"; value: window.visible && model.page === "recording" }
    Binding { target: model; property: "watching"; value: window.visible && model.page === "recording" }

    Shortcut { sequences: [StandardKey.Save]; enabled: model.dirty; onActivated: model.save() }
    // Esc deliberately does not close: dropdowns and popups use it (the shortcut would preempt
    // them). Close with Ctrl+W or the window's x button.
    Shortcut { sequences: [StandardKey.Close]; onActivated: window.requestClose() }

    // The sidebar: five groups, the jump points (sections) of a group's page below it. The keys
    // match SettingsViewModel::sectionsOf().
    readonly property var groups: [
        { key: "general", label: qsTr("Általános"), icon: "settings",
          children: [ { key: "appearance", label: qsTr("Megjelenés") },
                      { key: "folders", label: qsTr("Mappák") },
                      { key: "account", label: qsTr("Fiók") } ] },
        { key: "recording", label: qsTr("Rögzítés"), icon: "mic",
          children: [ { key: "devices", label: qsTr("Eszközök") },
                      { key: "watcher", label: qsTr("Hívásfigyelő") } ] },
        { key: "processing", label: qsTr("Feldolgozás"), icon: "wand-sparkles",
          children: [ { key: "stt", label: qsTr("Átírás") },
                      { key: "diar", label: qsTr("Beszélők elkülönítése") },
                      { key: "voice", label: qsTr("Beszélők felismerése") },
                      { key: "summary", label: qsTr("Összefoglaló") },
                      { key: "tags", label: qsTr("Címkejavaslat") } ] },
        { key: "cloud", label: qsTr("Tanara Cloud"), icon: "cloud", children: [] },
        { key: "people", label: qsTr("Személyek és minták"), icon: "users", children: [] }
    ]

    // The jump points that need attention (key -> the dot's tooltip). Only the cheap signals:
    // a provider gap (on its card's jump point), no voice model, no segmentation model. Plain bool
    // properties first: the compiled object-building binding alone did not track the deeper reads.
    readonly property bool sttGap: !model.stt.configured || model.stt.testState === "failed"
    readonly property bool llmGap: !model.llm.configured || model.llm.testState === "failed"
    readonly property bool warnStt: model.servicesWarn && (sttGap || !llmGap)
    readonly property bool warnSummary: model.servicesWarn && llmGap
    readonly property bool warnDiar: model.localDiarization && !model.segmentationModels.anyAvailable
    readonly property bool warnVoice: !model.voiceModels.anyAvailable
    readonly property var warnings: {
        const w = {}
        const providerText = qsTr("Egy szolgáltató beállítása hiányzik vagy nem érhető el")
        if (window.warnStt) w.stt = providerText
        if (window.warnSummary) w.summary = providerText
        if (window.warnDiar) w.diar = qsTr("Nincs szegmentáló modell")
        if (window.warnVoice) w.voice = qsTr("Nincs letöltött hangmodell")
        return w
    }

    // ---- jump points: scrolling to a section, and the sidebar following the scroll ----
    function pageItem(page) {
        switch (page) {
        case "general": return generalPage
        case "recording": return recordingPage
        case "processing": return processingPage
        case "cloud": return cloudPage
        default: return peoplePage
        }
    }
    function sectionItem(key) {
        const find = (item) => {
            if (!item || !item.visible) return null
            if (item.objectName === "section-" + key) return item
            for (let i = 0; i < item.children.length; ++i) {
                const hit = find(item.children[i])
                if (hit) return hit
            }
            return null
        }
        return key === "" ? null : find(pageItem(model.page))
    }
    function sectionKeys(page) {
        for (const g of window.groups) if (g.key === page) return g.children.map(c => c.key)
        return []
    }
    function sectionY(item) { return item.mapToItem(content.contentItem, 0, 0).y }

    property bool jumping: false        // a jump's scroll animation runs: the sidebar does not follow
    function scrollToSection(key, animate, flash) {
        const item = window.sectionItem(key)
        const maxY = Math.max(0, content.contentHeight - content.height)
        const y = item ? Math.max(0, Math.min(maxY, window.sectionY(item) - 16)) : 0
        if (item) model.section = key
        scrollAnim.stop()
        if (animate && Math.abs(y - content.contentY) > 1) {
            window.jumping = true
            scrollAnim.from = content.contentY
            scrollAnim.to = y
            scrollAnim.start()
        } else {
            window.jumping = true
            content.contentY = y
            window.jumping = false
        }
        if (flash && item) item.flash()
    }
    // The section in view: the last one whose top is above the upper third (at the bottom of the
    // page the last visible one, which may be too short to get there).
    function followScroll() {
        if (window.jumping) return
        const keys = window.sectionKeys(model.page)
        if (keys.length === 0) return
        let best = ""
        let last = ""
        const line = content.contentY + Math.max(90, content.height * 0.3)
        for (const key of keys) {
            const item = window.sectionItem(key)
            if (!item || item.height <= 0) continue
            if (best === "") best = key
            last = key
            if (window.sectionY(item) <= line) best = key
        }
        if (content.contentY > 0 && content.contentY >= content.contentHeight - content.height - 2) best = last
        if (best !== "") model.section = best
    }

    // A jump from the view model (deep link, failed save, design state): after the layout settles.
    Connections {
        target: model
        function onSectionRequested(section, flash) {
            pendingJump.key = section
            pendingJump.flash = flash
            pendingJump.restart()
        }
        function onPageChanged() {
            content.contentY = 0
            Qt.callLater(window.followScroll)
        }
    }
    Timer {
        id: pendingJump
        interval: 30
        property string key: ""
        property bool flash: false
        onTriggered: window.scrollToSection(key, flash && window.visible, flash)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- navigation ----
            SettingsSidebar {
                objectName: "settingsSidebar"
                // Narrow window: a narrower sidebar.
                Layout.preferredWidth: window.width < 1000 ? 224 : 244
                Layout.fillHeight: true
                groups: window.groups
                page: model.page
                section: model.section
                warnings: window.warnings
                onPagePicked: (key) => model.page = key
                onSectionPicked: (page, key) => {
                    const switching = model.page !== page
                    model.page = page
                    model.section = key
                    if (!switching) {
                        window.scrollToSection(key, true, true)
                    } else {
                        // The new page lays out first.
                        pendingJump.key = key
                        pendingJump.flash = true
                        pendingJump.restart()
                    }
                }
            }

            // ---- content ----
            Flickable {
                id: content
                objectName: "settingsContent"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: pageHolder.height + 48
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                T.ScrollBar.vertical: TScrollBar {}
                onContentYChanged: window.followScroll()
                // The user's own scroll wins over a running jump.
                onMovementStarted: scrollAnim.stop()

                NumberAnimation {
                    id: scrollAnim
                    target: content
                    property: "contentY"
                    duration: 260
                    easing.type: Easing.OutCubic
                    onFinished: window.jumping = false
                    onStopped: window.jumping = false
                }

                Item {
                    id: pageHolder
                    x: 32; y: 24
                    width: Math.min(content.width - 64, model.page === "processing" ? 640 : 620)
                    height: model.page === "general" ? generalPage.implicitHeight
                          : model.page === "recording" ? recordingPage.implicitHeight
                          : model.page === "processing" ? processingPage.implicitHeight
                          : model.page === "cloud" ? cloudPage.implicitHeight
                          : peoplePage.implicitHeight

                    // ---- General ----
                    SettingsGeneralPage {
                        id: generalPage
                        visible: model.page === "general"
                        width: Math.min(parent.width, 600)
                        vm: model
                    }

                    // ---- Recording: devices + call watcher ----
                    Column {
                        id: recordingPage
                        visible: model.page === "recording"
                        width: parent.width
                        spacing: 28
                        SettingsSection {
                            key: "devices"
                            width: parent.width
                            SettingsRecordingPage {
                                width: parent.width
                                vm: model
                                editingRow: window.demoState === "B02" || window.demoState === "rename" ? 3 : -1
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.border }
                        SettingsSection {
                            key: "watcher"
                            width: Math.min(parent.width, 600)
                            spacing: 14
                            TSectionLabel { text: qsTr("Hívásfigyelő") }
                            SettingsWatcherPage {
                                width: parent.width
                                vm: model
                                demoAddOpen: window.demoState === "addApp"
                            }
                        }
                    }

                    // ---- Processing: today's Services, Speakers and Summary blocks in the chain's
                    // order (transcription, separation, recognition, summary, tags). Earlier
                    // sections stack above later ones (z): a card's dropdown overlaps the next.
                    Column {
                        id: processingPage
                        visible: model.page === "processing"
                        width: parent.width
                        spacing: 28
                        SettingsSection {
                            key: "stt"
                            z: 5
                            width: parent.width
                            SettingsServicesPage {
                                part: "stt"
                                width: parent.width
                                vm: model
                                demoDropdown: window.demoState === "B04"
                            }
                        }
                        SettingsSection {
                            key: "diar"
                            z: 4
                            width: Math.min(parent.width, 600)
                            SettingsSpeakersPage { part: "diar"; width: parent.width; vm: model }
                        }
                        SettingsSection {
                            key: "voice"
                            z: 3
                            width: Math.min(parent.width, 600)
                            SettingsSpeakersPage { part: "voice"; width: parent.width; vm: model }
                        }
                        SettingsSection {
                            key: "summary"
                            z: 2
                            width: parent.width
                            spacing: 20
                            SettingsServicesPage { part: "summary"; width: parent.width; vm: model }
                            SettingsSummaryPage {
                                width: parent.width
                                vm: model
                                onResetRequested: resetDialog.open()
                                onSchemaRequested: schemaDialog.open()
                            }
                        }
                        SettingsSection {
                            key: "tags"
                            z: 1
                            width: parent.width
                            SettingsServicesPage {
                                part: "tags"
                                width: parent.width
                                vm: model
                                onRejectedResetRequested: rejectedDialog.open()
                            }
                        }
                    }

                    // ---- Tanara Cloud: the account and the account-level tier ----
                    SettingsCloudPage {
                        id: cloudPage
                        visible: model.page === "cloud"
                        width: Math.min(parent.width, 600)
                        vm: model
                    }

                    // ---- People and samples: your name and voiceprint, the voiceprint backfill ----
                    SettingsSpeakersPage {
                        id: peoplePage
                        part: "people"
                        visible: model.page === "people"
                        width: Math.min(parent.width, 600)
                        vm: model
                    }
                }
            }
        }

        // ---- footer ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.surface
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 16
                spacing: 10
                Rectangle {
                    visible: model.dirty
                    width: 8; height: 8; radius: 4
                    color: Theme.warn
                }
                TLabel {
                    id: footerLabel
                    Layout.fillWidth: true
                    objectName: "footerText"
                    text: saveNote.running && !model.dirty ? qsTr("Elmentve") : model.footerText
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
                TButton {
                    objectName: "cancelButton"
                    text: qsTr("Mégse")
                    onClicked: window.cancelAndClose()
                }
                TButton {
                    objectName: "saveButton"
                    // When the model changes, the button also says what happens on save.
                    text: model.embedding.restartPending && model.dirty ? qsTr("Mentés és újraindítás") : qsTr("Mentés")
                    variant: "primary"
                    enabled: model.dirty
                    leftPadding: 16; rightPadding: 16
                    onClicked: model.save()
                }
            }
            // After saving, "Saved" shows in the footer for a few seconds.
            Timer {
                id: saveNote
                interval: 2500
                function show() { restart() }
            }
        }
    }

    // ---- dialogs ----
    TDialog {
        id: unsavedDialog
        title: qsTr("Mented a változásokat?")
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.45
            text: qsTr("%n nem mentett változásod van a beállításokban.", "", Math.max(1, model.changeCount))
        }
        actions: [
            TButton { text: qsTr("Mégse"); onClicked: unsavedDialog.reject() },
            TButton {
                text: qsTr("Elvetés")
                variant: "dangerGhost"
                onClicked: { unsavedDialog.accept(); window.cancelAndClose() }
            },
            TButton {
                text: qsTr("Mentés")
                variant: "primary"
                onClicked: {
                    unsavedDialog.accept()
                    // With an invalid field we do not close: the page with the error is shown.
                    if (model.save()) { window.closeConfirmed = true; window.close() }
                }
            }
        ]
    }

    TDialog {
        id: resetDialog
        title: qsTr("Visszaállítod az alapértelmezett utasítást?")
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.45
            text: qsTr("A saját szöveged elvész, és ennél a lapnál újra a beépített utasítás lesz érvényben. A változás a mentéssel lép életbe.")
        }
        actions: [
            TButton { text: qsTr("Mégse"); onClicked: resetDialog.reject() },
            TButton {
                text: qsTr("Visszaállítás")
                variant: "danger"
                onClicked: { resetDialog.accept(); model.resetPrompt() }
            }
        ]
    }

    TDialog {
        id: rejectedDialog
        title: qsTr("Visszaállítod az elutasított javaslatokat?")
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.45
            text: qsTr("%n elutasított javaslatot újra felajánlhatunk a megbeszéléseknél. A felrakott címkékhez nem nyúlunk. A változás azonnal érvényes.", "",
                       Math.max(1, model.embedding.rejectedCount))
        }
        actions: [
            TButton { text: qsTr("Mégse"); onClicked: rejectedDialog.reject() },
            TButton {
                text: qsTr("Visszaállítás")
                variant: "primary"
                onClicked: { rejectedDialog.accept(); model.embedding.clearRejected() }
            }
        ]
    }

    TDialog {
        id: schemaDialog
        width: Math.min(560, window.width - 32)
        title: qsTr("Kimeneti forma")
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.45
            muted: true
            font.pixelSize: Theme.fontSmall
            text: model.schemaKind === "json"
                ? qsTr("A modellnek ezt a JSON-objektumot kell visszaadnia; a Tanara ebből építi fel az összefoglalót. A forma nem szerkeszthető — ha az utasításod mást kér, az összefoglaló üres maradhat.")
                : qsTr("A modellnek ilyen szerkezetű szöveget kell visszaadnia; a Tanara ebből olvassa ki a témákat, döntéseket és teendőket. A forma nem szerkeszthető — a szakaszcímeknek pontosan így kell szerepelniük.")
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: schemaBody.implicitHeight + 24
            radius: Theme.radiusControl
            color: Theme.sunken
            border.width: 1
            border.color: Theme.border
            TextEdit {
                id: schemaBody
                x: 12; y: 12
                width: parent.width - 24
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                text: model.schemaBody
                color: Theme.text
                selectionColor: Theme.accent
                selectedTextColor: Theme.textOnAccent
                font.family: Theme.fontMono
                font.pixelSize: 12
                Accessible.name: qsTr("A kimeneti forma leírása")
            }
        }
        actions: [
            TButton { text: qsTr("Bezárás"); variant: "primary"; onClicked: schemaDialog.accept() }
        ]
    }
}
