import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// The Settings window: a separate, resizable window (900 x 680), left navigation with six pages,
// scrollable content, a footer with the unsaved-changes indicator and "Cancel" / "Save" buttons.
//
// Changes apply on "Save" (except the theme, which shows immediately). Closing with unsaved
// changes asks (Save / Discard / Cancel). The native window frame stays (as in the main window),
// so the spec's own 36 px title bar is not drawn.
//
// The C++ host (tanara_qml::SettingsWindowHost) creates it and opens the requested page; it can
// also be loaded standalone for screenshots:
//   tanara --qml-shot out.png --qml-page SettingsWindow --size 900x680 --qml-prop 'demoState="B03"'
// demoState: B01 ... B07 · dirty · unsaved · schema · cloudComingSoon · cloudSelected · addApp · reset
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
    Binding { target: model; property: "monitoring"; value: window.visible && model.page === "recording" }
    Binding { target: model; property: "watching"; value: window.visible && model.page === "watcher" }

    Shortcut { sequences: [StandardKey.Save]; enabled: model.dirty; onActivated: model.save() }
    // Esc deliberately does not close: dropdowns and popups use it (the shortcut would preempt
    // them). Close with Ctrl+W or the window's x button.
    Shortcut { sequences: [StandardKey.Close]; onActivated: window.requestClose() }

    readonly property var pages: [
        { key: "general", label: qsTr("Általános"), icon: "settings" },
        { key: "recording", label: qsTr("Rögzítés"), icon: "mic" },
        { key: "watcher", label: qsTr("Hívásfigyelő"), icon: "radar" },
        { key: "services", label: qsTr("Szolgáltatások"), icon: "plug" },
        { key: "speakers", label: qsTr("Beszélők"), icon: "audio-lines" },
        { key: "summary", label: qsTr("Összefoglaló"), icon: "sparkles" }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- navigation ----
            Rectangle {
                Layout.preferredWidth: 208
                Layout.fillHeight: true
                color: Theme.surface
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
                Column {
                    x: 10; y: 12
                    width: parent.width - 21
                    spacing: 2
                    Repeater {
                        model: window.pages
                        SettingsNavItem {
                            required property var modelData
                            objectName: "nav-" + modelData.key
                            width: parent.width
                            text: modelData.label
                            iconName: modelData.icon
                            current: model.page === modelData.key
                            warn: modelData.key === "services" && model.servicesWarn
                            warnText: qsTr("Egy szolgáltató beállítása hiányzik vagy nem érhető el")
                            onClicked: model.page = modelData.key
                        }
                    }
                }
                // Version at the bottom of the navigation (a bug report shows which build runs).
                TLabel {
                    objectName: "versionLabel"
                    x: 20
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    text: "Tanara " + App.version
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontMicro
                }
            }

            // ---- content ----
            Flickable {
                id: content
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: pageHolder.height + 48
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                T.ScrollBar.vertical: TScrollBar {}

                // Back to the top on page change.
                Connections {
                    target: model
                    function onPageChanged() { content.contentY = 0 }
                }

                Item {
                    id: pageHolder
                    x: 32; y: 24
                    width: Math.min(content.width - 64, model.page === "summary" ? 640 : 620)
                    height: (general.visible ? general.implicitHeight : 0)
                          + (recording.visible ? recording.implicitHeight : 0)
                          + (watcher.visible ? watcher.implicitHeight : 0)
                          + (services.visible ? services.implicitHeight : 0)
                          + (speakers.visible ? speakers.implicitHeight : 0)
                          + (summary.visible ? summary.implicitHeight : 0)

                    SettingsGeneralPage {
                        id: general
                        visible: model.page === "general"
                        width: Math.min(parent.width, 600)
                        vm: model
                    }
                    SettingsRecordingPage {
                        id: recording
                        visible: model.page === "recording"
                        width: parent.width
                        vm: model
                        editingRow: window.demoState === "B02" || window.demoState === "rename" ? 3 : -1
                    }
                    SettingsWatcherPage {
                        id: watcher
                        visible: model.page === "watcher"
                        width: Math.min(parent.width, 600)
                        vm: model
                        demoAddOpen: window.demoState === "addApp"
                    }
                    SettingsServicesPage {
                        id: services
                        visible: model.page === "services"
                        width: parent.width
                        vm: model
                        demoDropdown: window.demoState === "B04"
                        onRejectedResetRequested: rejectedDialog.open()
                    }
                    SettingsSpeakersPage {
                        id: speakers
                        visible: model.page === "speakers"
                        width: Math.min(parent.width, 600)
                        vm: model
                    }
                    SettingsSummaryPage {
                        id: summary
                        visible: model.page === "summary"
                        width: parent.width
                        vm: model
                        onResetRequested: resetDialog.open()
                        onSchemaRequested: schemaDialog.open()
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
