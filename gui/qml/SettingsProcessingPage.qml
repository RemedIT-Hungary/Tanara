import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Settings > Processing (S2): one scrollable page, two subtitles in the chain's order: Hang (Átírás,
// Beszélők elkülönítése, Beszélők felismerése) and Szöveg (Összefoglaló, Címkejavaslat). Every card
// is a SettingsCapabilityCard (same shape, collapsible, "Hol fusson?" first, then only the chosen
// route's settings); each one is a jump point (SettingsSection: stt, diar, voice, summary, tags).
//  - Ask ("Kérdezzen"): why it asks (a paid run) and the two paths as one summary row each;
//  - Cloud: where its level is chosen (the Tanara Cloud page); a stored Cloud route that cannot run
//    yet says so;
//  - Összefoglaló › Haladó: the provider's advanced fields, thinking and context, the prompts; on the
//    Cloud route the prompts are read-only (the Cloud prompt lock).
//   SettingsProcessingPage { vm: model; onResetRequested: …; onSchemaRequested: …; onRejectedResetRequested: … }
Column {
    id: root

    property var vm: null
    property bool demoDropdown: false      // screenshot: the transcription provider list is open
    signal resetRequested()                // the prompt's "Default": the window confirms
    signal schemaRequested()               // the output format's "View"
    signal rejectedResetRequested()        // the rejected suggestions' "Reset": the window confirms

    spacing: 12

    // ---- building blocks of the card bodies ----

    // The deep link's reason above the card it came for ("stt" | "llm").
    component FocusBanner: Rectangle {
        id: fb
        property string field: ""
        objectName: "focusBanner-" + field
        visible: root.vm !== null && root.vm.focusField === field
        Layout.fillWidth: true
        implicitHeight: fbText.implicitHeight + 20
        radius: Theme.radiusControl
        color: Theme.accentSoft
        border.width: 1
        border.color: Theme.accentLine
        TIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; name: "info"; size: 15; color: Theme.accent }
        TLabel {
            id: fbText
            x: 37; y: 10
            width: parent.width - 49
            text: root.vm ? root.vm.focusBannerText : ""
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
    }

    // One path of the "Kérdezzen" mode: icon, name, one line of detail, a status pill, an action.
    component AskRow: RowLayout {
        id: ar
        property string icon: ""
        property string title: ""
        property string detail: ""
        property bool ready: false
        property bool showPill: true
        property string actionText: ""
        signal action()
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            implicitWidth: 28; implicitHeight: 28; radius: 6; color: Theme.sunken
            TIcon { anchors.centerIn: parent; name: ar.icon; size: 15; color: Theme.textMuted }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            TLabel { text: ar.title; font.weight: Theme.weightSemiBold; font.pixelSize: Theme.fontSmall }
            TLabel {
                Layout.fillWidth: true
                visible: text !== ""
                text: ar.detail
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
        TPill { visible: ar.showPill; text: ar.ready ? qsTr("kész") : qsTr("nincs beállítva"); tone: ar.ready ? "success" : "neutral" }
        TButton {
            visible: ar.actionText !== ""
            size: "small"
            variant: "ghost"
            text: ar.actionText
            onClicked: ar.action()
        }
    }

    // The "Kérdezzen" body: why it asks, the own path and the Cloud path; the own path's settings open
    // below on "Beállítás".
    component AskBlock: ColumnLayout {
        id: ask
        property var card: null            // the SettingsCapabilityCard
        property var provider: null        // the own provider's SettingsProviderModel
        property bool ownOpen: false
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            objectName: "askNote-" + (ask.card ? ask.card.key : "")
            Layout.fillWidth: true
            implicitHeight: askText.implicitHeight + 20
            radius: Theme.radiusControl
            color: Theme.accentSoft
            TLabel {
                id: askText
                x: 12; y: 10
                width: parent.width - 24
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                text: qsTr("A Tanara Cloud fizetős, ezért minden futás előtt megkérdezzük, merre menjen: az indítógomb melletti ▾ menüben választasz, és ott látod a becsült időt és költséget is. Ha mindig ugyanazt választod, ott beállíthatod alapértelmezettnek.")
            }
        }
        AskRow {
            objectName: "askOwn-" + (ask.card ? ask.card.key : "")
            icon: ask.card && ask.card.ownRoute === "external" ? "key-round" : "monitor"
            title: ask.card && ask.card.ownRoute === "external" ? qsTr("Külső szolgáltató") : qsTr("Helyben")
            detail: ask.provider ? ask.provider.summaryText : ""
            ready: ask.provider !== null && ask.provider.configured
            actionText: ask.ownOpen ? qsTr("Elrejtés") : qsTr("Beállítás")
            onAction: ask.ownOpen = !ask.ownOpen
        }
        AskRow {
            objectName: "askCloud-" + (ask.card ? ask.card.key : "")
            icon: "cloud"
            title: qsTr("Tanara Cloud")
            detail: qsTr("a fiókod keretéből; a szintet a Tanara Cloud oldalon választod")
            showPill: false
            actionText: qsTr("Megnyitás")
            onAction: root.vm.showSection("cloud", "")
        }
        SettingsProviderPanel {
            visible: ask.ownOpen
            Layout.fillWidth: true
            Layout.topMargin: 4
            card: ask.provider
            routeKind: ask.card ? ask.card.ownRoute : ""
        }
    }

    // The Cloud body: where the level is chosen; a stored Cloud route that cannot run yet says so.
    component CloudBlock: ColumnLayout {
        id: cb
        property var card: null
        Layout.fillWidth: true
        spacing: 10
        TBanner {
            objectName: "cloudUnavailable-" + (cb.card ? cb.card.key : "")
            visible: cb.card !== null && cb.card.status === "soon"
            Layout.fillWidth: true
            tone: "warn"
            text: qsTr("A beállítás a Tanara Cloudot választja, ami még nem érhető el. Válassz helyi vagy külső utat.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                muted: true
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                text: qsTr("A Tanara Cloud szintjét a Tanara Cloud oldalon, a fiókodhoz választod; itt csak azt döntöd el, hogy ez a lépés oda menjen-e.")
            }
            TButton {
                size: "small"
                variant: "ghost"
                text: qsTr("Tanara Cloud")
                iconName: "cloud"
                iconSize: 13
                onClicked: root.vm.showSection("cloud", "")
            }
        }
    }

    // A collapsible "Haladó" block's header.
    component Expander: T.AbstractButton {
        id: ex
        property bool open: false
        property string label: qsTr("Haladó")
        Layout.fillWidth: true
        Layout.topMargin: 4
        implicitHeight: 28
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: ex.label
        Keys.onReturnPressed: click()
        Keys.onSpacePressed: click()
        background: Item { TFocusRing { visible: ex.visualFocus } }
        contentItem: RowLayout {
            spacing: 6
            TIcon { name: ex.open ? "chevron-down" : "chevron-right"; size: 15; color: Theme.textMuted }
            TLabel { text: ex.label; font.weight: Theme.weightSemiBold; font.pixelSize: Theme.fontSmall }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        }
    }

    // A subtitle of the chain: name + what that part does.
    component ChainHeading: RowLayout {
        property string title: ""
        property string caption: ""
        width: root.width
        spacing: 10
        TSectionLabel { text: parent.title }
        TLabel {
            Layout.fillWidth: true
            text: parent.caption
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }

    // ================= HANG =================
    ChainHeading {
        title: qsTr("Hang")
        caption: qsTr("a felvételből szöveg és beszélők — ebben a sorrendben fut")
    }

    // ---- Átírás ----
    SettingsSection {
        key: "stt"
        z: 5                                   // its dropdowns go over the later cards
        width: parent.width
        SettingsCapabilityCard {
            id: sttCard
            width: parent.width
            vm: root.vm
            key: "stt"
            title: qsTr("Átírás")
            blurb: qsTr("A felvételből időbélyeges szöveg lesz; minden további lépés erre épül.")

            FocusBanner { field: "stt" }
            SettingsProviderPanel {
                visible: sttCard.route === "local" || sttCard.route === "external"
                Layout.fillWidth: true
                card: root.vm ? root.vm.stt : null
                routeKind: sttCard.route
                demoDropdown: root.demoDropdown
            }
            TLabel {
                visible: sttCard.route === "local"
                Layout.fillWidth: true
                text: qsTr("Helyben: a gépeden vagy a hálózatodon futó, kulcs nélküli szerver (pl. whisper.cpp).")
                muted: true
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
            AskBlock {
                visible: sttCard.route === "ask"
                card: sttCard
                provider: root.vm ? root.vm.stt : null
            }
            CloudBlock {
                visible: sttCard.route === "cloud"
                card: sttCard
            }
        }
    }

    // ---- Speaker separation (diarization) ----
    SettingsSection {
        key: "diar"
        z: 4
        width: parent.width
        SettingsCapabilityCard {
            id: diarCard
            width: parent.width
            vm: root.vm
            key: "diar"
            title: qsTr("Beszélők elkülönítése")
            blurb: qsTr("Megmondja, ki mikor beszél egy sávon belül („Beszélő 1”, „Beszélő 2”).")

            SettingsModelList {
                objectName: "diarizationSection"
                visible: diarCard.route === "local"
                Layout.fillWidth: true
                vm: root.vm
                list: root.vm ? root.vm.segmentationModels : null
            }
            // There is no global speaker-count setting: it is asked per meeting, before the run.
            TLabel {
                visible: diarCard.route === "local"
                Layout.fillWidth: true
                text: qsTr("Hány beszélő? Automatikusan becsüljük; ha tudod, megbeszélésenként, a futtatás előtt adhatod meg.")
                muted: true
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
            TLabel {
                objectName: "diarExternalNote"
                visible: diarCard.route === "external"
                Layout.fillWidth: true
                text: diarCard.status === "ok"
                      ? qsTr("A Soniox-átírás maga elkülöníti a beszélőket.")
                      : qsTr("A Soniox-átírás maga elkülöníti a beszélőket; a mostani átíró nem, ezért Whisper-átírásnál válaszd a Helyben utat, különben az átirat beszélők nélkül marad.")
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
        }
    }

    // ---- Speaker recognition (voiceprints) ----
    SettingsSection {
        key: "voice"
        z: 3
        width: parent.width
        SettingsCapabilityCard {
            id: voiceCard
            width: parent.width
            vm: root.vm
            key: "voice"
            title: qsTr("Beszélők felismerése")
            blurb: qsTr("Hanglenyomatból felismeri a visszatérő embereket, és nevet javasol.")
            choiceHint: qsTr("A hanglenyomat a gépeden marad; ez a lépés akkor sem kerül Cloudra, ha az átírás ott fut.")

            SettingsModelList {
                visible: voiceCard.route === "local"
                Layout.fillWidth: true
                vm: root.vm
                list: root.vm ? root.vm.voiceModels : null
            }
        }
    }

    // ================= SZÖVEG =================
    ChainHeading {
        title: qsTr("Szöveg")
        caption: qsTr("az átiratból — összefoglaló és címkék")
    }

    // ---- Összefoglaló ----
    SettingsSection {
        key: "summary"
        z: 2
        width: parent.width
        SettingsCapabilityCard {
            id: sumCard
            width: parent.width
            vm: root.vm
            key: "summary"
            title: qsTr("Összefoglaló")
            blurb: qsTr("Az átiratból témák, döntések és teendők; egy nyelvi modell írja.")

            FocusBanner { field: "llm" }
            // Just switched to Cloud while an own prompt exists: it stays, for the local path.
            TBanner {
                objectName: "promptNotice"
                visible: root.vm !== null && root.vm.summaryPromptNotice
                Layout.fillWidth: true
                tone: "accent"
                text: qsTr("A saját utasításod megmarad, és helyben vagy saját kulccsal futtatva érvényes; Tanara Cloudon a kötött sablon fut.")
            }
            SettingsProviderPanel {
                visible: sumCard.route === "local" || sumCard.route === "external"
                Layout.fillWidth: true
                card: root.vm ? root.vm.llm : null
                routeKind: sumCard.route
            }
            TLabel {
                visible: sumCard.route === "local"
                Layout.fillWidth: true
                text: qsTr("Helyben: LM Studio vagy más OpenAI-kompatibilis szerver a gépeden vagy a hálózatodon, kulcs nélkül.")
                muted: true
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
            AskBlock {
                visible: sumCard.route === "ask"
                card: sumCard
                provider: root.vm ? root.vm.llm : null
            }
            CloudBlock {
                visible: sumCard.route === "cloud"
                card: sumCard
            }

            // The summary's language: every route.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 12
                TLabel {
                    Layout.preferredWidth: 110
                    text: qsTr("Nyelve")
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightMedium
                }
                SettingsCombo {
                    Layout.preferredWidth: 220
                    fieldHeight: 32
                    editable: true
                    options: root.vm ? root.vm.summaryLanguageOptions : []
                    value: root.vm ? root.vm.summaryLanguage : ""
                    placeholder: qsTr("pl. magyar")
                    onPicked: (v) => root.vm.summaryLanguage = v
                }
                TLabel {
                    Layout.fillWidth: true
                    text: qsTr("Bármilyen nyelv; független a felület nyelvétől.")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                }
            }

            // ---- Haladó: the provider's advanced settings, the prompts, the output format ----
            Expander {
                objectName: "summaryAdvancedToggle"
                open: root.vm !== null && root.vm.summaryAdvancedOpen
                label: qsTr("Haladó — utasítások, kimeneti forma, kontextus")
                onClicked: root.vm.summaryAdvancedOpen = !root.vm.summaryAdvancedOpen
            }
            ColumnLayout {
                objectName: "summaryAdvanced"
                visible: root.vm !== null && root.vm.summaryAdvancedOpen
                Layout.fillWidth: true
                spacing: 14
                SettingsProviderPanel {
                    visible: sumCard.route !== "cloud"
                    Layout.fillWidth: true
                    card: root.vm ? root.vm.llm : null
                    advanced: true
                }
                SettingsPromptsPanel {
                    Layout.fillWidth: true
                    vm: root.vm
                    locked: root.vm !== null && root.vm.summaryPromptsLocked
                    onResetRequested: root.resetRequested()
                    onSchemaRequested: root.schemaRequested()
                }
            }
        }
    }

    // ---- Címkejavaslat ----
    SettingsSection {
        key: "tags"
        z: 1
        width: parent.width
        SettingsCapabilityCard {
            id: tagCard
            width: parent.width
            vm: root.vm
            key: "tags"
            title: qsTr("Címkejavaslat")
            blurb: qsTr("A hasonló megbeszélések címkéiből javasol címkét; szöveg-beágyazással pontosabb.")
            choiceHint: qsTr("A címkejavaslat a háttérben fut, ezért itt nincs „Kérdezzen”.")

            TLabel {
                Layout.fillWidth: true
                text: root.vm ? root.vm.embedding.description : ""
                muted: true
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
            SettingsEmbeddingPanel {
                objectName: "embeddingCard"
                visible: tagCard.route === "local" || (root.vm !== null && root.vm.embedding.prepVisible)
                Layout.fillWidth: true
                emb: root.vm ? root.vm.embedding : null
                endpoint: tagCard.route === "local"
            }
            CloudBlock {
                visible: tagCard.route === "cloud"
                card: tagCard
            }

            // ---- tag suggestions: every route ----
            ColumnLayout {
                objectName: "tagSuggestionSettings"
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 12
                SettingsSwitchRow {
                    Layout.fillWidth: true
                    text: qsTr("Címkék javaslása")
                    helper: qsTr("Hasonló korábbi megbeszélések és együtt járó címkék alapján. Sosem kerül fel magától.")
                    checked: root.vm ? root.vm.embedding.tagSuggestions : true
                    onToggled: (on) => root.vm.embedding.tagSuggestions = on
                }
                SettingsSwitchRow {
                    Layout.fillWidth: true
                    enabled: root.vm ? root.vm.embedding.tagSuggestions : true
                    opacity: enabled ? 1 : 0.55
                    text: qsTr("A nyelvi modell is javasoljon az összefoglaló után")
                    helper: qsTr("Meglévő címkéket, és legfeljebb 2 új címke ötletét.")
                    checked: root.vm ? root.vm.embedding.llmTagSuggestions : true
                    onToggled: (on) => root.vm.embedding.llmTagSuggestions = on
                }
            }

            // ---- Haladó: the rejected suggestions ----
            Expander {
                id: tagsAdvanced
                objectName: "tagsAdvancedToggle"
                open: false
                onClicked: open = !open
            }
            RowLayout {
                visible: tagsAdvanced.open
                Layout.fillWidth: true
                spacing: 8
                TLabel {
                    objectName: "rejectedCount"
                    Layout.fillWidth: true
                    text: qsTr("Elutasított javaslatok: %1").arg(root.vm ? root.vm.embedding.rejectedCount : 0)
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                TButton {
                    objectName: "rejectedReset"
                    visible: root.vm !== null && root.vm.embedding.rejectedCount > 0
                    size: "small"
                    text: qsTr("Visszaállítás")
                    toolTipText: qsTr("Az elutasított javaslatokat újra felajánlhatjuk")
                    onClicked: root.rejectedResetRequested()
                }
            }
        }
    }

    Item { width: 1; height: 8 }
}
