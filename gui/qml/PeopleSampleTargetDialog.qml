import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// New owner of a sample: "New person from this sample..." (mode "new": asks for a name; an
// existing name turns it into a move) and "Move to another person..." (mode "move": searchable
// list). In both cases the sample leaves the current voiceprint; the step is undoable.
TDialog {
    id: dialog

    property var vm: null
    property string mode: "new"
    property string sampleId: ""
    property var choices: []
    property string errorText: ""
    property alias nameText: field.text

    readonly property string typed: field.text.trim()
    readonly property bool typedExists: vm && typed !== "" && vm.personExists(typed)
    readonly property bool typedIsCurrent: vm && typed !== "" && typed.toLowerCase() === vm.selectedName.toLowerCase()

    function reload() { choices = vm ? vm.personChoices(field.text) : [] }
    function openFor(sampleId, mode, text) {
        dialog.sampleId = sampleId
        dialog.mode = mode
        errorText = ""
        field.text = text || ""
        reload()
        open()
        field.forceActiveFocus()
    }
    function commit(name) {
        const error = vm.moveSample(sampleId, name)
        if (error !== "") { errorText = error; return }
        accept()
    }

    width: Math.min(440, (parent ? parent.width : 440) - 32)
    title: mode === "move" ? qsTr("Minta áthelyezése") : qsTr("Új személy ebből a mintából")

    TLabel {
        Layout.fillWidth: true
        Layout.topMargin: -8
        text: dialog.mode === "move"
            ? qsTr("A minta kikerül a mostani hanglenyomatból, és a választott személyhez kerül.")
            : qsTr("Ha a minta valaki más hangja: add meg a nevét. A minta kikerül a mostani hanglenyomatból. Létező névnél a minta ahhoz a személyhez kerül.")
        muted: true
        font.pixelSize: Theme.fontSmall
        cssLineHeight: 1.45
        wrapMode: Text.Wrap
    }
    TSearchField {
        id: field
        objectName: "targetField"
        Layout.fillWidth: true
        font.pixelSize: Theme.fontBody
        placeholderText: dialog.mode === "move" ? qsTr("Név vagy becenév") : qsTr("Az új személy neve")
        stateFocused: true
        onTextChanged: { dialog.errorText = ""; dialog.reload() }
        onAccepted: {
            if (dialog.typed === "" || dialog.typedIsCurrent) return
            // When moving, the single hit is the target; otherwise the typed name (existing or new).
            if (!dialog.typedExists && dialog.mode === "move" && dialog.choices.length === 1)
                dialog.commit(dialog.choices[0].name)
            else
                dialog.commit(dialog.typed)
        }
    }
    ListView {
        id: list
        objectName: "targetList"
        // For a new person it shows only while typing (similarly named existing people), always
        // when moving.
        visible: count > 0 && (dialog.mode === "move" || dialog.typed !== "")
        Layout.fillWidth: true
        Layout.topMargin: -4
        Layout.preferredHeight: Math.min(contentHeight, 5 * 40)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: dialog.choices
        T.ScrollBar.vertical: TScrollBar {}
        delegate: PersonRow {
            required property var modelData
            width: list.width
            compact: true
            personName: modelData.name
            monogram: modelData.monogram
            subText: modelData.meta
            highlighted: hovered
            onClicked: dialog.commit(modelData.name)
        }
    }
    TLabel {
        visible: dialog.mode === "move" && list.count === 0 && dialog.typed !== "" && !dialog.typedIsCurrent
        Layout.fillWidth: true
        text: qsTr("Nincs ilyen személy. Enter vagy „Új személy”: ezzel a névvel létrejön, és megkapja a mintát.")
        muted: true
        font.pixelSize: Theme.fontSmall
        cssLineHeight: 1.45
        wrapMode: Text.Wrap
    }
    TLabel {
        visible: dialog.errorText !== "" || dialog.typedIsCurrent
        Layout.fillWidth: true
        text: dialog.errorText !== "" ? dialog.errorText : qsTr("A minta most is ennél a személynél van.")
        color: Theme.dangerInk
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
    }

    actions: [
        TButton { text: qsTr("Mégse"); onClicked: dialog.reject() },
        TButton {
            objectName: "targetConfirm"
            visible: dialog.typed !== "" && !dialog.typedIsCurrent
            text: dialog.typedExists ? qsTr("Áthelyezés") : qsTr("Új személy")
            variant: "primary"
            leftPadding: 16; rightPadding: 16
            onClicked: dialog.commit(dialog.typed)
        }
    ]
}
