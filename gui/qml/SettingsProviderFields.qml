import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Egy szolgáltató-kártya mező-rácsa (SettingsProviderCard, a Beágyazás kártya): címke +
// szerkesztő + (hiba / súgó) soronként. A szerkesztő a mező típusa szerint: szöveg / cím /
// titok (szem-gombbal) / szám (csúszka vagy mező) / lista („Lekérés”-sel).
//   SettingsProviderFields { card: vm.llm; fields: vm.llm.fields }
GridLayout {
    id: root

    property var card: null            // SettingsProviderModel
    property var fields: []

    columns: 2
    columnSpacing: 12
    rowSpacing: 8

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

    Repeater {
        model: root.fields
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
        model: root.fields
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
        model: root.fields
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
