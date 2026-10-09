import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// Az „Első lépések” ablak (K14 — nem volt designer-csomag: a Beállítások-ablak és az üres
// könyvtár vizuális nyelvét követi). Külön, nem modális ablak (720 × 640): bal oldalt a hat
// lépés a Beállítások navigációjának elemeivel, jobbra a lépés tartalma, alul a lábléc.
//
// Varázsló, nem egyetlen görgetett oldal: a hat témából négy valódi döntés (név + megjelenés,
// mappák, szolgáltatók, figyelő), és mindegyiknek külön „Kihagyom” kell — lépésenként ez
// egyértelmű (a gomb a látható tartalomra vonatkozik), egy hosszú oldalon nem lenne az. A
// „Tovább” azonnal menti az adott lépést, így a félbehagyott varázsló is megtartja, amit addig
// elfogadtak. SEMMI sem kötelező: „Később” / × bármikor bezár.
//
// A C++ gazda (tanara_qml::OnboardingWindowHost) hozza létre; önállóan is betölthető képhez:
//   tanara --qml-shot ki.png --qml-page OnboardingWindow --size 720x640 --qml-prop 'demoState="you"'
// demoState: welcome · you · folders · providers · watcher · done
ApplicationWindow {
    id: window

    // A gazda adja át (kezdő property-ként); nélkülük az App.controller / demó érvényes.
    property QtObject hostController: null
    property QtObject hostDialogs: null
    property string demoState: ""
    property alias vm: model

    title: qsTr("Első lépések")
    width: 720
    height: 640
    minimumWidth: 640
    minimumHeight: 540
    color: Theme.bg
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody
    flags: Qt.Window

    OnboardingViewModel {
        id: model
        onCloseRequested: window.close()
    }

    Component.onCompleted: {
        if (hostDialogs) model.dialogs = hostDialogs
        if (hostController) model.controller = hostController
        else if (demoState !== "") model.demoState = demoState
    }

    // A Beállításokban közben mentett kulcs / szolgáltató: visszatéréskor friss állapot-sor.
    onActiveChanged: if (active) model.refreshReadiness()

    Shortcut { sequences: [StandardKey.Close]; onActivated: window.close() }

    readonly property string stepCounter: qsTr("%1 / %2").arg(model.stepIndex + 1).arg(model.stepCount)
    readonly property bool lastStep: model.step === "done"
    readonly property bool decisionStep: model.step === "you" || model.step === "folders"
                                         || model.step === "providers" || model.step === "watcher"

    // Egy lépés fejléce: „2 / 6” · cím · bevezető.
    component StepHeader: Column {
        id: header
        property string title: ""
        property string lead: ""
        property string counter: ""
        width: parent ? parent.width : 0
        spacing: 8
        TLabel {
            text: header.counter
            mono: true
            muted: true
            font.pixelSize: Theme.fontCaption
        }
        TLabel {
            width: parent.width
            text: header.title
            font.pixelSize: 22
            font.weight: Theme.weightSemiBold
            wrapMode: Text.Wrap
        }
        TLabel {
            visible: header.lead !== ""
            width: parent.width
            text: header.lead
            muted: true
            font.pixelSize: 15
            cssLineHeight: 1.55
            wrapMode: Text.Wrap
        }
    }

    // Halvány, ikonos megjegyzés-sor (pl. „később is módosítható”).
    component Note: Row {
        id: note
        property string text: ""
        property string iconName: "info"
        width: parent ? parent.width : 0
        spacing: 8
        TIcon {
            name: note.iconName
            size: 15
            color: Theme.textMuted
            y: 2
        }
        TLabel {
            width: note.width - 23
            text: note.text
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
    }

    // Az üres könyvtár számozott lépés-chipje (Felvétel → Átirat → Összefoglaló).
    component FlowStep: Rectangle {
        id: flowStep
        property int number: 1
        property string label: ""
        implicitWidth: flowRow.implicitWidth + 22
        implicitHeight: 40
        radius: Theme.radiusControl
        color: Theme.raised
        border.width: 1
        border.color: Theme.border
        Row {
            id: flowRow
            anchors.centerIn: parent
            spacing: 9
            Rectangle {
                width: 22; height: 22; radius: 11
                color: Theme.sunken
                anchors.verticalCenter: parent.verticalCenter
                TLabel {
                    anchors.centerIn: parent
                    text: flowStep.number
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontMicro
                    font.weight: Theme.weightSemiBold
                }
            }
            TLabel {
                text: flowStep.label
                font.weight: Theme.weightSemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // Állapot-sor a szolgáltatóknál / az összegzésben: ikon + szöveg.
    component StatusLine: Row {
        id: statusLine
        property string text: ""
        property bool ok: false
        property bool neutral: false
        width: parent ? parent.width : 0
        spacing: 10
        TIcon {
            name: statusLine.neutral ? "minus" : statusLine.ok ? "circle-check" : "circle-alert"
            size: 16
            color: statusLine.neutral ? Theme.textMuted : statusLine.ok ? Theme.successInk : Theme.warnInk
            y: 1
        }
        TLabel {
            width: statusLine.width - 26
            text: statusLine.text
            muted: statusLine.neutral
            wrapMode: Text.Wrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- lépések ----
            Rectangle {
                Layout.preferredWidth: 188
                Layout.fillHeight: true
                color: Theme.surface
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
                Column {
                    x: 10; y: 14
                    width: parent.width - 21
                    spacing: 2
                    TSectionLabel {
                        x: 10
                        bottomPadding: 6
                        text: qsTr("Első lépések")
                    }
                    Repeater {
                        model: window.vm.steps
                        SettingsNavItem {
                            required property var modelData
                            objectName: "step-" + modelData.key
                            width: parent.width
                            text: modelData.label
                            iconName: model.stepStatus[modelData.key] === "done" && model.step !== modelData.key
                                      ? "check" : modelData.icon
                            current: model.step === modelData.key
                            onClicked: model.step = modelData.key
                        }
                    }
                }
                TLabel {
                    x: 20
                    width: parent.width - 36
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    text: qsTr("Minden beállítás később is módosítható a Beállításokban.")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
            }

            // ---- tartalom ----
            Flickable {
                id: content
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: pageHolder.height + 56
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                T.ScrollBar.vertical: TScrollBar {}

                Connections {
                    target: model
                    function onStepChanged() { content.contentY = 0 }
                }

                Item {
                    id: pageHolder
                    x: 32; y: 28
                    width: Math.min(content.width - 64, 520)
                    height: welcomePage.visible ? welcomePage.implicitHeight
                          : youPage.visible ? youPage.implicitHeight
                          : foldersPage.visible ? foldersPage.implicitHeight
                          : providersPage.visible ? providersPage.implicitHeight
                          : watcherPage.visible ? watcherPage.implicitHeight
                          : donePage.implicitHeight

                    // ---- 1. Üdvözlés ----
                    Column {
                        id: welcomePage
                        objectName: "page-welcome"
                        visible: model.step === "welcome"
                        width: parent.width
                        spacing: 22
                        StepHeader {
                            counter: window.stepCounter
                            title: qsTr("Üdv a Tanarában!")
                            lead: qsTr("A Tanara rögzíti a megbeszéléseidet — minden hangforrást külön sávra —, "
                                       + "majd átiratot és összefoglalót készít belőlük. A felvételek a gépeden "
                                       + "maradnak, a szolgáltatókat a saját kulcsoddal éred el.")
                        }
                        Flow {
                            width: parent.width
                            spacing: 12
                            FlowStep { number: 1; label: qsTr("Felvétel") }
                            TIcon { name: "arrow-right"; size: 14; color: Theme.textMuted; height: 40 }
                            FlowStep { number: 2; label: qsTr("Átirat") }
                            TIcon { name: "arrow-right"; size: 14; color: Theme.textMuted; height: 40 }
                            FlowStep { number: 3; label: qsTr("Összefoglaló") }
                        }
                        TLabel {
                            width: parent.width
                            text: qsTr("Néhány lépésben beállítjuk a legfontosabbakat. Bármelyiket kihagyhatod, "
                                       + "és az ablakot is bármikor bezárhatod.")
                            cssLineHeight: 1.55
                            wrapMode: Text.Wrap
                        }
                        Note {
                            text: qsTr("Minden beállítás később is módosítható a Beállításokban. Ezt az ablakot "
                                       + "a Fájl › Első lépések… menüből bármikor újranyithatod.")
                        }
                    }

                    // ---- 2. Te ----
                    Column {
                        id: youPage
                        objectName: "page-you"
                        visible: model.step === "you"
                        width: parent.width
                        spacing: 22
                        StepHeader {
                            counter: window.stepCounter
                            title: qsTr("Te")
                            lead: qsTr("A saját mikrofonod sávja ezen a néven szerepel. A nevet az operációs "
                                       + "rendszer fiókjából vettük — írd át, ha másként szólítanak.")
                        }
                        Column {
                            width: parent.width
                            spacing: 6
                            TLabel { text: qsTr("Saját neved"); font.weight: Theme.weightSemiBold }
                            SettingsTextField {
                                objectName: "onboardingNameField"
                                width: Math.min(parent.width, 320)
                                value: model.userName
                                hasError: model.userNameError !== ""
                                Accessible.name: qsTr("Saját neved")
                                onEdited: (t) => model.userName = t
                            }
                            TLabel {
                                width: parent.width
                                text: model.userNameError !== "" ? model.userNameError
                                    : qsTr("Így jelensz meg az átiratokban; a saját hanglenyomatod ehhez a névhez tartozik.")
                                color: model.userNameError !== "" ? Theme.dangerInk : Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                                cssLineHeight: 1.45
                                wrapMode: Text.Wrap
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.border }
                        GridLayout {
                            width: parent.width
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 12
                            TLabel {
                                Layout.preferredWidth: 96
                                text: qsTr("Nyelv")
                                font.weight: Theme.weightSemiBold
                            }
                            SettingsCombo {
                                Layout.preferredWidth: 280
                                Layout.maximumWidth: 280
                                Layout.fillWidth: true
                                options: model.uiLanguageOptions
                                value: model.uiLanguage
                                onPicked: (v) => model.uiLanguage = v
                            }
                            Item { Layout.preferredWidth: 96; implicitHeight: 1 }
                            TLabel {
                                Layout.fillWidth: true
                                Layout.topMargin: -6
                                text: qsTr("A nyelv a Tanara következő indításakor vált.")
                                color: model.languageNeedsRestart ? Theme.warnInk : Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                            }
                            TLabel {
                                Layout.preferredWidth: 96
                                text: qsTr("Téma")
                                font.weight: Theme.weightSemiBold
                            }
                            SettingsSegmented {
                                options: [
                                    { value: "system", label: qsTr("Rendszer"), iconName: "monitor" },
                                    { value: "light", label: qsTr("Világos"), iconName: "sun" },
                                    { value: "dark", label: qsTr("Sötét"), iconName: "moon" }
                                ]
                                value: model.themeMode
                                onPicked: (v) => model.themeMode = v
                            }
                        }
                    }

                    // ---- 3. Mappák ----
                    Column {
                        id: foldersPage
                        objectName: "page-folders"
                        visible: model.step === "folders"
                        width: parent.width
                        spacing: 22
                        StepHeader {
                            counter: window.stepCounter
                            title: qsTr("Mappák")
                            lead: qsTr("Ide kerülnek a felvételek és az összefoglalók Markdown-másolata. "
                                       + "Az alapértelmezés a legtöbbször jó; ha a jegyzeteidet máshol tartod, válaszd azt a mappát.")
                        }
                        Repeater {
                            model: window.vm.folders
                            Column {
                                id: folder
                                required property var modelData
                                width: parent.width
                                spacing: 6
                                Row {
                                    spacing: 10
                                    TLabel {
                                        text: folder.modelData.label
                                        font.weight: Theme.weightSemiBold
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    TPill {
                                        visible: folder.modelData.isDefault
                                        text: qsTr("alapértelmezett")
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                RowLayout {
                                    width: parent.width
                                    spacing: 6
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: Theme.controlHeight
                                        radius: Theme.radiusControl
                                        color: Theme.sunken
                                        border.width: 1
                                        border.color: Theme.border
                                        TLabel {
                                            id: pathLabel
                                            x: 10
                                            width: parent.width - 20
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: folder.modelData.path
                                            mono: true
                                            font.pixelSize: Theme.fontSmall
                                            elide: Text.ElideMiddle
                                            HoverHandler { id: pathHover }
                                            TToolTip { visible: pathHover.hovered && pathLabel.truncated; text: folder.modelData.path }
                                        }
                                    }
                                    TButton {
                                        text: qsTr("Tallózás…")
                                        font.pixelSize: Theme.fontSmall
                                        onClicked: model.browseFolder(folder.modelData.key)
                                    }
                                    TIconButton {
                                        // Elrejtjük, nem letiltjuk (a letiltott gomb szaggatott kerete a
                                        // szoftveres rendererrel kilógna a görgetett terület vágásából).
                                        visible: !folder.modelData.isDefault
                                        iconName: "rotate-ccw"
                                        iconSize: 15
                                        toolTipText: qsTr("Vissza az alapértelmezettre")
                                        onClicked: model.resetFolder(folder.modelData.key)
                                    }
                                }
                                TLabel {
                                    width: parent.width
                                    text: folder.modelData.hint
                                    muted: true
                                    font.pixelSize: Theme.fontSmall
                                    cssLineHeight: 1.45
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                        Note {
                            text: qsTr("A belső adatok mappáját (hanglenyomatok, index) a Beállítások › Általános lapon találod.")
                        }
                    }

                    // ---- 4. Szolgáltatások ----
                    Column {
                        id: providersPage
                        objectName: "page-providers"
                        visible: model.step === "providers"
                        width: parent.width
                        spacing: 20
                        StepHeader {
                            counter: window.stepCounter
                            title: qsTr("Szolgáltatások")
                            lead: model.cloudChosen
                                ? qsTr("A Tanara Cloudot választottad: az átírás és az összefoglaló a fiókodon fut, "
                                       + "saját kulcs nem kell. A Beállításokban bármikor válthatsz saját kulcsra.")
                                : qsTr("A Tanara a saját kulcsoddal dolgozik: az átírást a Soniox végzi (API-kulcs kell "
                                       + "hozzá), az összefoglalót egy OpenAI-kompatibilis végpont — például a gépeden "
                                       + "futó LM Studio vagy egy felhős szolgáltató.")
                        }
                        TCard {
                            width: parent.width
                            padding: 16
                            Column {
                                width: parent.width
                                spacing: 10
                                StatusLine {
                                    objectName: "sttStatus"
                                    text: model.sttStatus
                                    ok: model.sttReady
                                }
                                StatusLine {
                                    objectName: "llmStatus"
                                    text: model.llmStatus
                                    ok: model.llmReady
                                }
                            }
                        }
                        Row {
                            spacing: 12
                            TButton {
                                objectName: "setupNowButton"
                                text: qsTr("Beállítás most")
                                iconName: "settings"
                                onClicked: model.openServices()
                            }
                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Beállítások › Szolgáltatások")
                                muted: true
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                        Note {
                            visible: !model.cloudChosen
                            text: qsTr("A kulcs a gépeden marad. A Beállításokban a kapcsolatot is kipróbálhatod; "
                                       + "átírás nélkül is rögzíthetsz, a kulcs csak a feldolgozáshoz kell.")
                        }
                        Note {
                            visible: !model.cloudLive
                            iconName: "cloud"
                            text: qsTr("Tanara Cloud: hamarosan — átírás és összefoglaló saját kulcs nélkül.")
                        }
                    }

                    // ---- 5. Hívásfigyelő ----
                    Column {
                        id: watcherPage
                        objectName: "page-watcher"
                        visible: model.step === "watcher"
                        width: parent.width
                        spacing: 22
                        StepHeader {
                            counter: window.stepCounter
                            title: qsTr("Hívásfigyelő")
                            lead: qsTr("A hívásfigyelő a tálcán fut, és szól, ha Teams, Meet vagy Zoom hívást "
                                       + "észlel. A felvételt mindig te indítod.")
                        }
                        Rectangle {
                            width: parent.width
                            height: autostartRow.implicitHeight + 28
                            radius: Theme.radiusPopup
                            color: Theme.surface
                            border.width: 1
                            border.color: Theme.border
                            SettingsSwitchRow {
                                id: autostartRow
                                objectName: "autostartSwitch"
                                x: 16; y: 14
                                width: parent.width - 32
                                text: qsTr("Induljon el a figyelő bejelentkezéskor")
                                helper: model.autostartNote
                                checked: model.watcherAutostart
                                onToggled: (on) => model.watcherAutostart = on
                            }
                        }
                        Note {
                            text: qsTr("A figyelt alkalmazásokat és a megbeszélés végi rákérdezést a "
                                       + "Beállítások › Hívásfigyelő lapon állíthatod.")
                        }
                    }

                    // ---- 6. Kész ----
                    Column {
                        id: donePage
                        objectName: "page-done"
                        visible: model.step === "done"
                        width: parent.width
                        spacing: 22
                        StepHeader {
                            counter: window.stepCounter
                            title: qsTr("Kész is vagyunk")
                            lead: qsTr("Vegyük fel az első megbeszélést: indíts felvételt, vagy importálj egy meglévő "
                                       + "hang- vagy videófájlt.")
                        }
                        TCard {
                            width: parent.width
                            padding: 16
                            Column {
                                width: parent.width
                                spacing: 10
                                Repeater {
                                    model: [
                                        { key: "you", label: qsTr("Név és megjelenés") },
                                        { key: "folders", label: qsTr("Mappák") },
                                        { key: "providers", label: qsTr("Szolgáltatások") },
                                        { key: "watcher", label: qsTr("Hívásfigyelő") }
                                    ]
                                    StatusLine {
                                        required property var modelData
                                        readonly property string mark: model.stepStatus[modelData.key] || ""
                                        text: mark === "done" ? modelData.label
                                            : mark === "skipped" ? qsTr("%1 — kihagyva").arg(modelData.label)
                                            : qsTr("%1 — nem nézted meg").arg(modelData.label)
                                        ok: mark === "done"
                                        neutral: mark !== "done"
                                    }
                                }
                            }
                        }
                        Note {
                            text: qsTr("Minden beállítás később is módosítható a Beállításokban (Fájl › Beállítások…, Ctrl+,).")
                        }
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
                anchors.leftMargin: 12
                anchors.rightMargin: 16
                spacing: 10
                TButton {
                    objectName: "laterButton"
                    visible: !window.lastStep
                    text: qsTr("Később")
                    variant: "ghost"
                    toolTipText: qsTr("Bezárás — a Fájl › Első lépések… menüből újranyitható")
                    onClicked: window.close()
                }
                Item { Layout.fillWidth: true; implicitHeight: 1 }
                TButton {
                    objectName: "backButton"
                    visible: model.stepIndex > 0
                    text: qsTr("Vissza")
                    variant: "ghost"
                    iconName: "chevron-left"
                    onClicked: model.back()
                }
                TButton {
                    objectName: "skipButton"
                    visible: window.decisionStep
                    text: qsTr("Kihagyom")
                    onClicked: model.skip()
                }
                TButton {
                    objectName: "nextButton"
                    text: window.lastStep ? qsTr("Kezdjük") : qsTr("Tovább")
                    variant: "primary"
                    trailingIconName: window.lastStep ? "" : "chevron-right"
                    leftPadding: 16; rightPadding: 14
                    onClicked: model.next()
                }
            }
        }
    }
}
