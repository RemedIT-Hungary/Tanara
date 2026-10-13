import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Provider card: "Transcription (STT)" or "Summary (LLM)". The fields come from the view model
// (SettingsProviderModel, based on the provider registry's descriptors); here only the editor
// for each field type: text / URL / secret (with an eye button) / number (slider or field) /
// list (with "Fetch"). Below: "Test connection" + "Advanced". An already configured role is a
// one-line collapsed card (title · provider · status); a header click expands / collapses it.
// A deep link and a failed test always expand it.
Rectangle {
    id: root

    property var card: null            // SettingsProviderModel
    property string title: ""
    property string subtitle: ""
    property bool demoDropdown: false  // for screenshots: the provider list is open
    property bool collapsible: true

    readonly property bool failed: card && card.testState === "failed"
    readonly property bool highlighted: card && card.highlighted
    readonly property bool collapsed: collapsible && card !== null && !card.expanded

    implicitHeight: col.implicitHeight + (collapsed ? 22 : 28)
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: highlighted ? 1.5 : 1
    border.color: failed ? Theme.dangerLine : highlighted ? Theme.accent : Theme.border

    Rectangle {                        // 3 px accentSoft ring around the highlighted card
        visible: root.highlighted && !root.failed
        anchors.fill: parent
        anchors.margins: -3
        radius: root.radius + 3
        color: "transparent"
        border.width: 3
        border.color: Theme.accentSoft
        z: -1
    }

    // The header is clickable (expand / collapse), also by keyboard.
    T.AbstractButton {
        id: headerBtn
        visible: root.collapsible
        x: 0; y: 0
        width: parent.width
        height: header.height + (root.collapsed ? 22 : 20)
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: root.title + (root.collapsed ? " — " + qsTr("Kinyitás") : " — " + qsTr("Összecsukás"))
        onClicked: root.card.expanded = !root.card.expanded
        Keys.onReturnPressed: click()
        Keys.onSpacePressed: click()
        background: Rectangle {
            radius: root.radius
            color: Theme.stateLayer
            opacity: headerBtn.down ? Theme.pressedOpacity : headerBtn.hovered && root.collapsed ? Theme.hoverOpacity : 0
            TFocusRing { visible: headerBtn.visualFocus; targetRadius: root.radius }
        }
    }

    ColumnLayout {
        id: col
        x: 16; y: root.collapsed ? 11 : 14
        width: parent.width - 32
        spacing: 12

        // ---- head: title + status ----
        RowLayout {
            id: header
            Layout.fillWidth: true
            spacing: 8
            TIcon {
                visible: root.collapsible
                name: root.collapsed ? "chevron-right" : "chevron-down"
                size: 14
                color: Theme.textMuted
            }
            TLabel {
                text: root.title
                font.pixelSize: 15
                font.weight: Theme.weightSemiBold
            }
            TLabel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBaseline
                text: root.subtitle
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
            TLabel {
                visible: root.collapsed && text !== ""
                Layout.maximumWidth: 260
                text: root.card ? root.card.summaryText : ""
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
            // Collapsed, it shows that the role is set up even without a test.
            SettingsStatusPill {
                readonly property bool untested: root.collapsed && root.card && root.card.testState === ""
                status: !root.card ? "" : untested ? (root.card.configured ? "ok" : "") : root.card.testState
                text: !root.card ? "" : untested ? qsTr("Beállítva") : root.card.statusText
            }
        }

        // ---- card body (hidden when collapsed) ----
        ColumnLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 12

            // ---- provider ----
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                TLabel {
                    Layout.preferredWidth: 110
                    text: qsTr("Szolgáltató")
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightMedium
                }
                SettingsCombo {
                    Layout.fillWidth: true
                    fieldHeight: 32
                    options: root.card ? root.card.providers : []
                    value: root.card ? root.card.providerId : ""
                    placeholder: qsTr("Válassz szolgáltatót…")
                    demoOpen: root.demoDropdown
                    onPicked: (v) => root.card.providerId = v
                }
            }

            // ---- the provider's fields ----
            SettingsProviderFields {
                Layout.fillWidth: true
                visible: root.card && root.card.fields.length > 0
                card: root.card
                fields: root.card ? root.card.fields : []
            }
            TLabel {
                visible: root.card && root.card.loginProvider
                Layout.fillWidth: true
                text: qsTr("Ez a lépés a Tanara Cloudra van állítva, ami még nem érhető el. Válassz saját kulcsos szolgáltatót.")
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
            TLabel {
                visible: root.card && root.card.fetchError !== ""
                Layout.fillWidth: true
                text: root.card ? root.card.fetchError : ""
                color: Theme.dangerInk
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }

            // ---- the test's error / warning ----
            Rectangle {
                visible: root.failed
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
                visible: root.card && root.card.warningText !== ""
                Layout.fillWidth: true
                text: root.card ? root.card.warningText : ""
                color: Theme.warnInk
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
            // After a successful test, the server's data (LM Studio: loaded context, parallelism) -- if the
            // Advanced part is open, it shows there next to the context setting.
            TLabel {
                visible: root.card && root.card.testState === "ok" && root.card.serverInfo !== "" && !advGrid.visible
                Layout.fillWidth: true
                text: root.card ? root.card.serverInfo : ""
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }

            // ---- advanced fields ----
            Rectangle {
                visible: advGrid.visible
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }
            SettingsProviderFields {
                id: advGrid
                Layout.fillWidth: true
                visible: root.card && root.card.advancedOpen && root.card.advancedFields.length > 0
                card: root.card
                fields: root.card ? root.card.advancedFields : []
                rowSpacing: 10
            }
            // The model's "thinking" for the summary (own-key LLM only): off by default.
            ColumnLayout {
                visible: advGrid.visible && root.card && root.card.reasoningAvailable
                Layout.fillWidth: true
                Layout.topMargin: 2
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
                visible: advGrid.visible && root.card && root.card.contextAvailable
                Layout.fillWidth: true
                Layout.topMargin: 2
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

            // ---- actions ----
            RowLayout {
                Layout.fillWidth: true
                visible: root.card && (root.card.testable || root.card.advancedFields.length > 0)
                spacing: 8
                TButton {
                    visible: root.card && root.card.testable
                    implicitHeight: 30
                    leftPadding: 12; rightPadding: 12
                    font.pixelSize: Theme.fontSmall
                    iconName: "plug"
                    iconSize: 13
                    text: qsTr("Kapcsolat tesztelése")
                    enabled: root.card && root.card.testState !== "testing"
                    onClicked: root.card.test()
                }
                T.AbstractButton {
                    id: advToggle
                    visible: root.card && root.card.advancedFields.length > 0
                    implicitWidth: advRow.implicitWidth + 12
                    implicitHeight: 30
                    hoverEnabled: true
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Haladó")
                    onClicked: root.card.advancedOpen = !root.card.advancedOpen
                    Keys.onReturnPressed: click()
                    Keys.onSpacePressed: click()
                    background: Item { TFocusRing { visible: advToggle.visualFocus } }
                    contentItem: Item {
                        Row {
                            id: advRow
                            x: 6
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            TIcon {
                                name: root.card && root.card.advancedOpen ? "chevron-down" : "chevron-right"
                                size: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            TLabel {
                                text: qsTr("Haladó")
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightMedium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
