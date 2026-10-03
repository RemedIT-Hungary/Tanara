import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// A Beállítások ablaka (design/handoff-settings, B01–B07): külön, átméretezhető ablak
// (900 × 680), bal oldali navigáció öt lappal, görgethető tartalom, lábléc a mentetlen
// változások jelzésével és a „Mégse” / „Mentés” gombokkal.
//
// A változások a „Mentés”-re érvényesülnek (kivétel a téma: azonnal látszik). Bezáráskor,
// ha van mentetlen változás, rákérdez (Mentés / Elvetés / Mégse). A natív ablakkeret marad
// (mint a főablaknál), ezért a spec 36 px-es saját címsora nincs megrajzolva.
//
// A C++ gazda (tanara_qml::SettingsWindowHost) hozza létre és nyitja a kért lapon; önállóan
// is betölthető képernyőképhez:
//   tanara --qml-shot ki.png --qml-page SettingsWindow --size 900x680 --qml-prop 'demoState="B03"'
// demoState: B01 … B07 · dirty · unsaved · schema · teaser · cloudOut · addApp · logout · reset
ApplicationWindow {
    id: window

    // A gazda adja át (kezdő property-ként); nélkülük az App.controller / demó érvényes.
    property QtObject hostController: null
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
    // Külön ablak, nem modális: a főablak mellette használható marad.
    flags: Qt.Window

    SettingsViewModel {
        id: model
        onSaved: saveNote.show()
    }

    Component.onCompleted: {
        if (hostDialogs) model.dialogs = hostDialogs
        if (hostController) {
            model.controller = hostController
        } else if (demoState !== "") {
            // Képernyőkép / demó: a segéd-állapotok (felugrók) egy lapot is kijelölnek.
            const base = demoState === "addApp" ? "B03" : demoState === "logout" ? "B06"
                       : demoState === "reset" ? "B07" : demoState
            model.demoState = base
            Qt.callLater(window.applyDemoOverlay)
        }
    }

    function applyDemoOverlay() {
        switch (demoState) {
        case "unsaved": unsavedDialog.open(); break
        case "schema": schemaDialog.open(); break
        case "logout": logoutDialog.open(); break
        case "reset": resetDialog.open(); break
        }
    }

    // ---- bezárás ----
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
        // A szintfigyelés és a hívás-észlelés csak addig megy, amíg a lapjuk látszik.
        if (!visible) {
            window.closeConfirmed = false
            // Rejtett ablakban ne maradjon nyitva kérdés (újranyitáskor tiszta lappal indul).
            unsavedDialog.close(); resetDialog.close(); logoutDialog.close(); schemaDialog.close()
        }
    }
    Binding { target: model; property: "monitoring"; value: window.visible && model.page === "recording" }
    Binding { target: model; property: "watching"; value: window.visible && model.page === "watcher" }

    Shortcut { sequences: [StandardKey.Save]; enabled: model.dirty; onActivated: model.save() }
    // Esc szándékosan nem zár: a lenyíló listák és felugrók használják (a gyorsbillentyű
    // megelőzné őket). Bezárás: Ctrl+W vagy az ablak × gombja.
    Shortcut { sequences: [StandardKey.Close]; onActivated: window.requestClose() }

    readonly property var pages: [
        { key: "general", label: qsTr("Általános"), icon: "settings" },
        { key: "recording", label: qsTr("Rögzítés"), icon: "mic" },
        { key: "watcher", label: qsTr("Hívásfigyelő"), icon: "radar" },
        { key: "services", label: qsTr("Szolgáltatások"), icon: "plug" },
        { key: "summary", label: qsTr("Összefoglaló"), icon: "sparkles" }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- navigáció ----
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
            }

            // ---- tartalom ----
            Flickable {
                id: content
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: pageHolder.height + 48
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                T.ScrollBar.vertical: TScrollBar {}

                // Lapváltáskor a tetejére.
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
                        onLogoutRequested: logoutDialog.open()
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

        // ---- lábléc ----
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
                    text: qsTr("Mentés")
                    variant: "primary"
                    enabled: model.dirty
                    leftPadding: 16; rightPadding: 16
                    onClicked: model.save()
                }
            }
            // Mentés után pár másodpercig „Elmentve” áll a láblécben.
            Timer {
                id: saveNote
                interval: 2500
                function show() { restart() }
            }
        }
    }

    // ---- párbeszédablakok ----
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
                    // Hibás mezővel nem zárunk: a hibás lap látszik.
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
        id: logoutDialog
        title: qsTr("Kijelentkezel a Tanara Cloudból?")
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.45
            text: qsTr("A kulcsot ezen a gépen töröljük és visszavonjuk. A felvételeid és az átirataid megmaradnak.")
        }
        actions: [
            TButton { text: qsTr("Mégse"); onClicked: logoutDialog.reject() },
            TButton {
                text: qsTr("Kijelentkezés")
                variant: "danger"
                onClicked: { logoutDialog.accept(); model.cloud.logout() }
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
