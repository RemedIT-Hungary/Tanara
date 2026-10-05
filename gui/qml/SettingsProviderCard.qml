import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Szolgáltató-kártya (B04 / B05): „Átírás (STT)” vagy „Összefoglaló (LLM)”. A mezők a
// nézetmodellből jönnek (SettingsProviderModel — a provider-registry leírói alapján), itt
// csak a típusuk szerinti szerkesztő áll: szöveg / cím / titok (szem-gombbal) / szám
// (csúszka vagy mező) / lista („Lekérés”-sel). Alul: „Kapcsolat tesztelése” + „Haladó”.
// C09: a már beállított szerep egysoros, összecsukott kártya (cím · szolgáltató · állapot);
// a fejlécre kattintva nyílik / csukódik. A mély hivatkozás és a hibás teszt mindig nyitja.
Rectangle {
    id: root

    property var card: null            // SettingsProviderModel
    property string title: ""
    property string subtitle: ""
    property bool demoDropdown: false  // képernyőképhez: a szolgáltató-lista nyitva
    property bool collapsible: true

    readonly property bool failed: card && card.testState === "failed"
    readonly property bool highlighted: card && card.highlighted
    readonly property bool collapsed: collapsible && card !== null && !card.expanded

    implicitHeight: col.implicitHeight + (collapsed ? 22 : 28)
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: highlighted ? 1.5 : 1
    border.color: failed ? Theme.dangerLine : highlighted ? Theme.accent : Theme.border

    Rectangle {                        // B04: 3 px accentSoft gyűrű a kiemelt kártya körül
        visible: root.highlighted && !root.failed
        anchors.fill: parent
        anchors.margins: -3
        radius: root.radius + 3
        color: "transparent"
        border.width: 3
        border.color: Theme.accentSoft
        z: -1
    }

    // A fejléc kattintható (nyit / csuk), billentyűvel is.
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

        // ---- fej: cím + állapot ----
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
            // Összecsukva teszt nélkül is jelzi, hogy a szerep be van állítva.
            SettingsStatusPill {
                readonly property bool untested: root.collapsed && root.card && root.card.testState === ""
                status: !root.card ? "" : untested ? (root.card.configured ? "ok" : "") : root.card.testState
                text: !root.card ? "" : untested ? qsTr("Beállítva") : root.card.statusText
            }
        }

        // ---- a kártya törzse (összecsukva rejtve) ----
        ColumnLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 12

            // ---- szolgáltató ----
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

            // ---- a szolgáltató mezői ----
            SettingsProviderFields {
                Layout.fillWidth: true
                visible: root.card && root.card.fields.length > 0
                card: root.card
                fields: root.card ? root.card.fields : []
            }
            TLabel {
                visible: root.card && root.card.loginProvider
                Layout.fillWidth: true
                text: qsTr("Ezt a lépést a Tanara Cloud végzi: nincs kulcs, a fiókod egyenlegéből megy.")
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

            // ---- a teszt hibája / figyelmeztetése ----
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
            // Sikeres teszt után a szerver adatai (LM Studio: betöltött kontextus, párhuzamosság) —
            // ha a Haladó rész nyitva van, ott látszik a kontextus-beállítás mellett.
            TLabel {
                visible: root.card && root.card.testState === "ok" && root.card.serverInfo !== "" && !advGrid.visible
                Layout.fillWidth: true
                text: root.card ? root.card.serverInfo : ""
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }

            // ---- haladó mezők ----
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
            // A modell „gondolkodása” az összefoglalónál (csak saját kulcsos LLM): alapból ki.
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
            // A modell kontextusa (csak saját kulcsos LLM): automatikus vagy rögzített tokenszám.
            // LM Studiónál a Tanara a feladat előtt ekkorával (egy szálon) tölti be a modellt.
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

            // ---- műveletek ----
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
