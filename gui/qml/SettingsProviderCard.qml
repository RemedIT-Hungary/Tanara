import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Szolgáltató-kártya (B04 / B05): „Átírás (STT)” vagy „Összefoglaló (LLM)”. A mezők a
// nézetmodellből jönnek (SettingsProviderModel — a provider-registry leírói alapján), itt
// csak a típusuk szerinti szerkesztő áll: szöveg / cím / titok (szem-gombbal) / szám
// (csúszka vagy mező) / lista („Lekérés”-sel). Alul: „Kapcsolat tesztelése” + „Haladó”.
Rectangle {
    id: root

    property var card: null            // SettingsProviderModel
    property string title: ""
    property string subtitle: ""
    property bool demoDropdown: false  // képernyőképhez: a szolgáltató-lista nyitva

    readonly property bool failed: card && card.testState === "failed"
    readonly property bool highlighted: card && card.highlighted

    implicitHeight: col.implicitHeight + 28
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

    // Egy mező szerkesztője a típusa szerint.
    component FieldEditor: RowLayout {
        id: editor
        required property var field
        // Az érték / hiba / helykitöltő a nézetmodellből, a `revision` kötésbe vételével.
        readonly property var fieldValue: (root.card.revision, root.card.value(field.key))
        readonly property string fieldError: (root.card.revision, root.card.fieldError(field.key))
        readonly property string fieldPlaceholder: (root.card.revision, root.card.placeholder(field.key))
        spacing: 6

        // szöveg / cím
        SettingsTextField {
            visible: editor.field.type === "text" || editor.field.type === "url"
            Layout.fillWidth: true
            implicitHeight: 32
            mono: editor.field.type === "url" || editor.field.key === "model"
            font.pixelSize: Theme.fontSmall
            value: visible ? String(editor.fieldValue) : ""
            placeholderText: editor.fieldPlaceholder
            hasError: editor.fieldError !== ""
            Accessible.name: editor.field.label
            onEdited: (t) => root.card.setValue(editor.field.key, t)
        }
        // titok: maszkolva, szem-gombbal
        SettingsTextField {
            id: secret
            property bool shown: false
            visible: editor.field.type === "secret"
            Layout.fillWidth: true
            implicitHeight: 32
            rightPadding: 34
            mono: true
            font.pixelSize: Theme.fontSmall
            echoMode: shown ? TextInput.Normal : TextInput.Password
            passwordCharacter: "•"
            value: visible ? String(editor.fieldValue) : ""
            placeholderText: editor.fieldPlaceholder
            Accessible.name: editor.field.label
            Accessible.passwordEdit: !shown
            onEdited: (t) => root.card.setValue(editor.field.key, t)

            T.AbstractButton {
                id: eye
                anchors.right: parent.right
                width: 32; height: parent.height
                visible: secret.text !== ""
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: secret.shown ? qsTr("Kulcs elrejtése") : qsTr("Kulcs megmutatása")
                onClicked: secret.shown = !secret.shown
                Keys.onReturnPressed: click()
                Keys.onSpacePressed: click()
                background: Item { TFocusRing { visible: eye.visualFocus; anchors.margins: 2 } }
                contentItem: Item {
                    TIcon {
                        anchors.centerIn: parent
                        name: secret.shown ? "eye-off" : "eye"
                        size: 15
                        color: eye.hovered ? Theme.text : Theme.textMuted
                    }
                }
                TToolTip { visible: eye.hovered; text: eye.Accessible.name }
            }
        }
        // lista (a „Lekérés” feltölti; szabad szöveg is beírható)
        SettingsCombo {
            visible: editor.field.type === "combo"
            Layout.fillWidth: true
            fieldHeight: 32
            editable: true
            mono: true
            options: (root.card.revision, root.card.options(editor.field.key))
            value: visible ? String(editor.fieldValue) : ""
            placeholder: editor.fieldPlaceholder
            hasError: editor.fieldError !== ""
            onPicked: (v) => root.card.setValue(editor.field.key, v)
        }
        TButton {
            visible: editor.field.type === "combo" && editor.field.dynamic && root.card.testable
            implicitHeight: 32
            leftPadding: 10; rightPadding: 10
            font.pixelSize: Theme.fontSmall
            iconName: "refresh-cw"
            iconSize: 13
            text: root.card.fetching ? qsTr("Lekérés…") : qsTr("Lekérés")
            enabled: !root.card.fetching
            toolTipText: qsTr("A modellek lekérése a megadott címről")
            onClicked: root.card.fetchModels()
        }
        // szám: tartománnyal és tizedesekkel csúszka (hőmérséklet), különben mező
        ShellSlider {
            id: slider
            readonly property real span: Math.max(editor.field.maxValue, Number(editor.fieldValue)) - editor.field.minValue
            visible: editor.field.type === "number" && editor.field.decimals > 0
            Layout.fillWidth: true
            knobAlwaysVisible: true
            trackColor: Theme.sunken
            keyStep: 0.05
            accessibleName: editor.field.label
            value: visible && span > 0 ? (Number(editor.fieldValue) - editor.field.minValue) / span : 0
            onMoved: (v) => root.card.setValue(editor.field.key,
                                               Math.round((editor.field.minValue + v * span) * 100) / 100)
        }
        TLabel {
            visible: slider.visible
            Layout.preferredWidth: 40
            horizontalAlignment: Text.AlignRight
            text: Number(editor.fieldValue).toLocaleString(Qt.locale(), "f", 2)
            mono: true
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightMedium
        }
        SettingsTextField {
            visible: editor.field.type === "number" && editor.field.decimals === 0
            Layout.preferredWidth: 140
            implicitHeight: 32
            mono: true
            font.pixelSize: Theme.fontSmall
            validator: IntValidator { bottom: 0; top: 100000000 }
            inputMethodHints: Qt.ImhDigitsOnly
            value: visible ? String(editor.fieldValue) : ""
            hasError: editor.fieldError !== ""
            Accessible.name: editor.field.label
            onEdited: (t) => root.card.setValue(editor.field.key, parseInt(t) || 0)
        }
        Item { visible: editor.field.type === "number" && editor.field.decimals === 0; Layout.fillWidth: true }
    }

    // Címke + szerkesztő + (hiba / súgó) sorok egy mező-listához.
    component FieldGrid: GridLayout {
        id: grid
        property var fields: []
        columns: 2
        columnSpacing: 12
        rowSpacing: 8
        Repeater {
            model: grid.fields
            TLabel {
                required property var modelData
                required property int index
                Layout.row: index * 2
                Layout.column: 0
                Layout.preferredWidth: 110
                text: modelData.label
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
                elide: Text.ElideRight
            }
        }
        Repeater {
            model: grid.fields
            FieldEditor {
                required property var modelData
                required property int index
                field: modelData
                Layout.row: index * 2
                Layout.column: 1
                Layout.fillWidth: true
            }
        }
        Repeater {
            model: grid.fields
            TLabel {
                required property var modelData
                required property int index
                readonly property string error: (root.card.revision, root.card.fieldError(modelData.key))
                readonly property string note: error !== "" ? error : modelData.help
                visible: note !== ""
                Layout.row: index * 2 + 1
                Layout.column: 1
                Layout.fillWidth: true
                Layout.topMargin: -4
                text: note
                color: error !== "" ? Theme.dangerInk : Theme.textMuted
                font.pixelSize: Theme.fontCaption
                cssLineHeight: 1.4
                wrapMode: Text.Wrap
            }
        }
    }

    ColumnLayout {
        id: col
        x: 16; y: 14
        width: parent.width - 32
        spacing: 12

        // ---- fej: cím + állapot ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
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
            SettingsStatusPill {
                status: root.card ? root.card.testState : ""
                text: root.card ? root.card.statusText : ""
            }
        }

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
        FieldGrid {
            Layout.fillWidth: true
            visible: root.card && root.card.fields.length > 0
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

        // ---- haladó mezők ----
        Rectangle {
            visible: advGrid.visible
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        FieldGrid {
            id: advGrid
            Layout.fillWidth: true
            visible: root.card && root.card.advancedOpen && root.card.advancedFields.length > 0
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
