import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Tags window: a separate, resizable, non-modal window (960 x 660) built like the People
// window. On the left a searchable, sortable list (288 px), on the right the selected tag's
// learned profile and its meetings. The native window frame stays, so the spec's own 36 px
// title bar is not drawn.
//
// Keys: Ctrl+F search · F2 rename · Del delete (with confirmation) · Ctrl+Z undo (in a text
// field the field's own undo applies) · Ctrl+W close.
//
// Can load standalone to render an image (with fictional tags):
//   tanara --qml-shot out.png --qml-page TagsWindow --size 960x660 --qml-prop 'demoState="T10"'
// demoState: T10 (details) · T11 (merge) · T12 (delete) · T13 (empty) · rename ·
//            people (People list) · peopleEmpty (nobody yet) · peopleAdd (+ Person picker)
ApplicationWindow {
    id: window

    // The host passes it in (as an initial property); without it App.controller / the demo applies.
    property QtObject hostController: null
    property string demoState: ""
    property alias vm: model
    property alias listPane: listPane
    property alias detailPane: detail
    property alias toastItem: toast
    property alias mergeDialog: mergeDialog
    property alias deleteDialog: deleteDialog
    property alias personPicker: personPicker
    property real listWidth: 288

    // Filter the library by this tag / open a meeting (wave 2: the shell binds it).
    signal openInLibraryRequested(string tagId)
    signal meetingRequested(string meetingId)
    // A People list row: the People window on this person (the host binds it).
    signal personRequested(string name)

    title: qsTr("Címkék")
    width: 960
    height: 660
    minimumWidth: 800
    minimumHeight: 560
    color: Theme.bg
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody
    flags: Qt.Window

    TagsViewModel {
        id: model
        onToast: (text, undoable) => toast.show(text, undoable)
    }

    Component.onCompleted: {
        if (hostController) model.controller = hostController
        else if (demoState !== "") {
            model.demoState = demoState
            Qt.callLater(window.applyDemoOverlay)
        }
    }

    function applyDemoOverlay() {
        switch (demoState) {
        case "T11": openMerge(); break
        case "T12": openDelete(); break
        case "rename": detail.demoRenameText = "Nordvik partner"; detail.startRename(); break
        case "peopleAdd": openPersonPicker(); break
        }
    }

    function selectTag(id) { model.selectedId = id }
    function openMerge() {
        if (!model.hasSelection || model.totalCount < 2) return
        mergeDialog.openFor(model.selectedId, "")
    }
    function openPersonPicker() {
        if (!model.hasSelection) return
        const anchor = detail.addPersonButton
        const p = anchor.mapToItem(window.contentItem, 0, anchor.height)
        personPicker.x = Math.max(8, Math.min(window.width - personPicker.width - 8, p.x + anchor.width - personPicker.width))
        personPicker.y = p.y + 6
        personPicker.open()
    }
    function openDelete() {
        if (!model.hasSelection) return
        deleteDialog.tagId = model.selectedId
        deleteDialog.open()
    }

    onVisibleChanged: {
        if (visible) return
        mergeDialog.close(); deleteDialog.close(); personPicker.close()
        toast.hide()
    }

    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool dialogOpen: mergeDialog.opened || deleteDialog.opened

    Shortcut { sequences: [StandardKey.Find]; enabled: !window.dialogOpen; onActivated: listPane.focusSearch() }
    Shortcut { sequence: "F2"; enabled: !window.dialogOpen; onActivated: detail.startRename() }
    Shortcut { sequences: [StandardKey.Delete]; enabled: !window.typing && !window.dialogOpen; onActivated: window.openDelete() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !window.typing && !window.dialogOpen && model.canUndo; onActivated: { toast.hide(); model.undo() } }
    Shortcut { sequences: [StandardKey.Close]; onActivated: window.close() }

    TagsListPane {
        id: listPane
        width: Math.max(240, Math.min(window.listWidth, window.width - 520))
        height: parent.height
        vm: model
        onTagClicked: (id) => window.selectTag(id)
    }
    Rectangle {
        x: listPane.width
        width: 1
        height: parent.height
        color: Theme.border
    }
    TagsDetailPane {
        id: detail
        x: listPane.width + 1
        width: parent.width - x
        height: parent.height
        vm: model
        onMergeRequested: window.openMerge()
        onDeleteRequested: window.openDelete()
        onTagClicked: (id) => window.selectTag(id)
        onMeetingRequested: (id) => window.meetingRequested(id)
        onOpenInLibraryRequested: (id) => window.openInLibraryRequested(id)
        onPersonRequested: (name) => window.personRequested(name)
        onAddPersonRequested: window.openPersonPicker()
    }

    PersonPicker {
        id: personPicker
        objectName: "tagPersonPicker"
        parent: window.contentItem
        tagId: model.selectedId
        anonymousText: ""
        hintText: qsTr("Akin már rajta van a címke, nem szerepel a listában.")
        onPersonChosen: (name) => model.addPerson(name)
    }

    PeopleUndoToast {
        id: toast
        objectName: "tagsToast"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        maxWidth: Math.min(560, window.width - 48)
        z: 10
        onUndoRequested: model.undo()
    }

    TagsMergeDialog { id: mergeDialog; vm: model }

    TDialog {
        id: deleteDialog
        property string tagId: ""
        width: Math.min(440, window.width - 32)
        title: tagId !== "" ? model.deleteTitle(tagId) : ""
        TLabel {
            objectName: "tagDeleteText"
            Layout.fillWidth: true
            Layout.topMargin: -4
            text: deleteDialog.tagId !== "" ? model.deleteText(deleteDialog.tagId) : ""
            muted: true
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        actions: [
            TButton { text: qsTr("Mégse"); onClicked: deleteDialog.reject() },
            TButton {
                objectName: "tagDeleteConfirm"
                text: qsTr("Törlés")
                variant: "danger"
                leftPadding: 16; rightPadding: 16
                onClicked: {
                    model.remove(deleteDialog.tagId)
                    deleteDialog.accept()
                }
            }
        ]
    }
}
