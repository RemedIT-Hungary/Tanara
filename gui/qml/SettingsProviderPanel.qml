import QtQuick
import QtQuick.Layouts

// A provider's settings inside a Processing card (S2: Átírás, Összefoglaló): the provider list of the
// chosen route's kind (Local: whisper-compat / LM Studio; External: Soniox / own-key providers), the
// provider's fields from the view model (SettingsProviderModel, built from the registry's
// descriptors: text / URL / secret / number / list with "Fetch"), the test's error / warning, the
// LM Studio server data and "Test connection".
// `advanced`: the other half instead: the provider's "Advanced" fields (temperature, max tokens), the
// model's thinking and context (own-key LLM only) -- the summary card shows it in its "Haladó" block.
//   SettingsProviderPanel { card: vm.stt; routeKind: "local" }
//   SettingsProviderPanel { card: vm.llm; advanced: true }
ColumnLayout {
    id: root

    property var card: null            // SettingsProviderModel
    property string routeKind: ""      // "local" | "external": the providers listed ("" = all)
    property bool advanced: false
    property bool demoDropdown: false  // for screenshots: the provider list is open

    readonly property bool failed: card !== null && card.testState === "failed"
    // The providers of the route's kind (the view model's `kind` role).
    readonly property var providerOptions: {
        const all = root.card ? root.card.providers : []
        return root.routeKind === "" ? all : all.filter(p => p.kind === root.routeKind)
    }

    spacing: 12

    // ---- provider ----
    RowLayout {
        visible: !root.advanced
        Layout.fillWidth: true
        spacing: 12
        TLabel {
            Layout.preferredWidth: 110
            text: qsTr("Szolgáltató")
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightMedium
        }
        SettingsCombo {
            objectName: "providerCombo-" + (root.card ? root.card.kind : "")
            Layout.fillWidth: true
            fieldHeight: 32
            options: root.providerOptions
            value: root.card ? root.card.providerId : ""
            placeholder: qsTr("Válassz szolgáltatót…")
            demoOpen: root.demoDropdown
            onPicked: (v) => root.card.providerId = v
        }
    }

    // ---- the provider's fields ----
    SettingsProviderFields {
        Layout.fillWidth: true
        visible: !root.advanced && root.card !== null && root.card.fields.length > 0
        card: root.card
        fields: root.card ? root.card.fields : []
    }
    TLabel {
        visible: !root.advanced && root.card !== null && root.card.fetchError !== ""
        Layout.fillWidth: true
        text: root.card ? root.card.fetchError : ""
        color: Theme.dangerInk
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.Wrap
    }

    // ---- the test's error / warning ----
    Rectangle {
        visible: !root.advanced && root.failed
        Layout.fillWidth: true
        implicitHeight: errRow.implicitHeight + 16
        radius: Theme.radiusControl
        color: Theme.dangerSoft
        border.width: 1
        border.color: Theme.dangerLine
        RowLayout {
            id: errRow
            x: 10; y: 8
            width: parent.width - 20
            spacing: 10
            TLabel {
                Layout.fillWidth: true
                text: root.card ? root.card.errorText : ""
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.4
                wrapMode: Text.Wrap
            }
            TLabel {
                text: root.card ? root.card.errorCode : ""
                mono: true; muted: true
                font.pixelSize: 11
            }
        }
    }
    TLabel {
        visible: !root.advanced && root.card !== null && root.card.warningText !== ""
        Layout.fillWidth: true
        text: root.card ? root.card.warningText : ""
        color: Theme.warnInk
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
    }
    // After a successful test, the server's data (LM Studio: loaded context, parallelism).
    TLabel {
        visible: !root.advanced && root.card !== null && root.card.testState === "ok" && root.card.serverInfo !== ""
        Layout.fillWidth: true
        text: root.card ? root.card.serverInfo : ""
        muted: true
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
    }

    // ---- test ----
    RowLayout {
        visible: !root.advanced && root.card !== null && root.card.testable
        Layout.fillWidth: true
        spacing: 10
        TButton {
            objectName: "providerTest-" + (root.card ? root.card.kind : "")
            implicitHeight: 30
            leftPadding: 12; rightPadding: 12
            font.pixelSize: Theme.fontSmall
            iconName: "plug"
            iconSize: 13
            text: qsTr("Kapcsolat tesztelése")
            enabled: root.card !== null && root.card.testState !== "testing"
            onClicked: root.card.test()
        }
        SettingsStatusPill {
            status: root.card ? root.card.testState : ""
            text: root.card ? root.card.statusText : ""
        }
        Item { Layout.fillWidth: true }
    }

    // ---- advanced: the provider's own fields ----
    SettingsProviderFields {
        visible: root.advanced && root.card !== null && root.card.advancedFields.length > 0
        Layout.fillWidth: true
        card: root.card
        fields: root.card ? root.card.advancedFields : []
        rowSpacing: 10
    }
    // The model's "thinking" for the summary (own-key LLM only): off by default.
    ColumnLayout {
        visible: root.advanced && root.card !== null && root.card.reasoningAvailable
        Layout.fillWidth: true
        spacing: 6
        TLabel {
            Layout.fillWidth: true
            text: qsTr("A modell gondolkodása az összefoglalónál")
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightMedium
            wrapMode: Text.Wrap
        }
        SettingsSegmented {
            Layout.maximumWidth: parent.width
            value: root.card ? root.card.reasoning : "auto"
            options: [
                { value: "auto", label: qsTr("Automatikus (kikapcsolva)") },
                { value: "off", label: qsTr("Kikapcsolva") },
                { value: "on", label: qsTr("Bekapcsolva") },
            ]
            onPicked: (v) => root.card.reasoning = v
        }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Kikapcsolva gyorsabb, és a kis modellek nem élik fel a válaszkeretet gondolkodásra, mielőtt válaszolnának. Az Automatikus a modellcsaládnak megfelelő módon kapcsolja ki.")
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.4
            wrapMode: Text.Wrap
        }
    }
    // The model's context (own-key LLM only): automatic or a fixed token count. With LM Studio,
    // Tanara loads the model with this size (single thread) before the task.
    ColumnLayout {
        objectName: "llmContext"
        visible: root.advanced && root.card !== null && root.card.contextAvailable
        Layout.fillWidth: true
        spacing: 6
        TLabel {
            Layout.fillWidth: true
            text: qsTr("A modell kontextusa")
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightMedium
            wrapMode: Text.Wrap
        }
        SettingsCombo {
            Layout.fillWidth: true
            Layout.maximumWidth: 320
            options: root.card ? root.card.contextOptions : []
            value: root.card ? String(root.card.contextLength) : "0"
            onPicked: (v) => root.card.contextLength = parseInt(v)
        }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("LM Studiónál a Tanara a feladat előtt ekkorával, egy szálon tölti be a modellt; más szervernél ott kell beállítani.")
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.4
            wrapMode: Text.Wrap
        }
        TLabel {
            visible: text !== ""
            Layout.fillWidth: true
            text: root.card ? root.card.serverInfo : ""
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.4
            wrapMode: Text.Wrap
        }
    }
}
