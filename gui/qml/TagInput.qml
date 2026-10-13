import QtQuick
import QtQuick.Templates as T

// Tag input field: 24 px inline field (standalone: 28 px, in forms), accent border + 3 px
// accentSoft ring, a "#" icon in front; the TagInputPopover list below.
// Keys: Enter adds the selected row · up/down in the list · Esc closes (closed(); the caller
// gives focus back) · Backspace in an empty field emits backspaceOnEmpty() (delete the last tag).
// bare: no border, background or "#" -- embedded in a multi-chip field (TagField).
//   TagInput { model: tagInputModel; onTagChosen: (name, isNew) => meetingTags.add(name) }
// Without a model it uses its own TagInputModel (made-up set) -- for the gallery and screenshots.
Item {
    id: root

    property var model: null
    property bool compact: false
    property bool standalone: false
    property bool bare: false
    property string placeholderText: qsTr("Címke…")
    property alias text: field.text
    property alias field: field
    property alias popover: popover
    // Gallery / screenshot: active look even without focus.
    property bool stateFocused: field.activeFocus
    signal tagChosen(string name, bool isNew)
    signal backspaceOnEmpty()
    signal closed()

    readonly property var effectiveModel: model ? model : ownModel

    // Focus into the field + open the list.
    function open() {
        field.forceActiveFocus()
        popover.open()
    }
    function close() {
        popover.close()
    }
    function choose(index) {
        if (!effectiveModel) return
        if (index === undefined ? effectiveModel.chooseSelected() : effectiveModel.choose(index))
            field.clear()
    }

    implicitWidth: 220
    implicitHeight: standalone ? 28 : 24

    TagInputModel {
        id: ownModel
        limit: root.compact ? 3 : 5
    }
    Connections {
        target: root.effectiveModel
        function onChosen(name, isNew) { root.tagChosen(name, isNew) }
    }
    Binding {
        target: root.model
        when: root.model !== null
        property: "limit"
        value: root.compact ? 3 : 5
    }

    T.TextField {
        id: field
        anchors.fill: parent
        leftPadding: root.bare ? 2 : 8 + 13 + 6
        rightPadding: root.bare ? 2 : 8
        topPadding: 0; bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        selectByMouse: true
        color: Theme.text
        placeholderTextColor: Theme.textMuted
        selectionColor: Theme.accent
        selectedTextColor: Theme.textOnAccent
        font.family: Theme.fontSans
        font.pixelSize: root.compact ? Theme.fontCaption : root.bare ? Theme.fontBody : Theme.fontSmall
        Accessible.name: qsTr("Címke hozzáadása")

        onTextChanged: if (root.effectiveModel && root.effectiveModel.text !== text) root.effectiveModel.text = text
        onActiveFocusChanged: if (activeFocus && !popover.opened) popover.open()

        Keys.onUpPressed: if (root.effectiveModel) root.effectiveModel.move(-1)
        Keys.onDownPressed: {
            if (!popover.opened) popover.open()
            else if (root.effectiveModel) root.effectiveModel.move(1)
        }
        Keys.onReturnPressed: root.choose()
        Keys.onEnterPressed: root.choose()
        Keys.onEscapePressed: {
            popover.close()
            field.clear()
            root.closed()
        }
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Backspace && field.length === 0) {
                root.backspaceOnEmpty()
                event.accepted = true
            }
        }

        TIcon {
            visible: !root.bare
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            name: "hash"
            size: 13
            color: Theme.textMuted
        }
        Text {
            x: field.leftPadding
            width: field.width - field.leftPadding - field.rightPadding
            height: field.height
            visible: !field.length && !field.preeditText
            text: root.placeholderText
            font: field.font
            color: field.placeholderTextColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        // Screenshot / gallery: a static mark instead of the blinking cursor, so the field looks active.
        Rectangle {
            visible: root.stateFocused && !field.activeFocus
            x: field.leftPadding + (field.length ? field.contentWidth + 2 : placeholderMetrics.advanceWidth + 4)
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: 15
            color: Theme.text
        }
        TextMetrics { id: placeholderMetrics; font: field.font; text: root.placeholderText }

        background: Rectangle {
            visible: !root.bare
            radius: 5
            color: Theme.raised
            border.width: 1
            border.color: root.stateFocused ? Theme.accent : Theme.borderStrong
            TFocusRing { visible: root.stateFocused; targetRadius: 5; border.color: Theme.accentSoft }
        }
    }

    TagInputPopover {
        id: popover
        y: root.height + 4
        model: root.effectiveModel
        compact: root.compact
        onRowClicked: (index) => { root.choose(index); field.forceActiveFocus() }
        onClosed: if (!field.activeFocus) root.closed()
    }
}
