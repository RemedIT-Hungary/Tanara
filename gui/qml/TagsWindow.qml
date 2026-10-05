import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A Címkék ablaka (design/handoff-tags, C08, T10–T13): külön, átméretezhető, nem modális
// ablak (960 × 660), a Személyek ablak szerkezetével. Bal oldalt kereshető, rendezhető lista
// (288 px), jobb oldalt a kijelölt címke tanult profilja és a megbeszélései. A natív ablakkeret
// marad, ezért a spec 36 px-es saját címsora nincs megrajzolva.
//
// Billentyűk: Ctrl+F kereső · F2 átnevezés · Del törlés (megerősítéssel) · Ctrl+Z visszavonás
// (szövegmezőben a mező saját visszavonása él) · Ctrl+W bezárás.
//
// Önállóan is betölthető képhez (kitalált címkékkel):
//   tanara --qml-shot ki.png --qml-page TagsWindow --size 960x660 --qml-prop 'demoState="T10"'
// demoState: T10 (részletek) · T11 (összevonás) · T12 (törlés) · T13 (üres) · rename
ApplicationWindow {
    id: window

    // A gazda adja át (kezdő property-ként); nélküle az App.controller / demó érvényes.
    property QtObject hostController: null
    property string demoState: ""
    property alias vm: model
    property alias listPane: listPane
    property alias detailPane: detail
    property alias toastItem: toast
    property alias mergeDialog: mergeDialog
    property alias deleteDialog: deleteDialog
    property real listWidth: 288

    // A könyvtár szűrése erre a címkére / egy megbeszélés megnyitása (wave 2: a héj köti be).
    signal openInLibraryRequested(string tagId)
    signal meetingRequested(string meetingId)

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
        }
    }

    function selectTag(id) { model.selectedId = id }
    function openMerge() {
        if (!model.hasSelection || model.totalCount < 2) return
        mergeDialog.openFor(model.selectedId, "")
    }
    function openDelete() {
        if (!model.hasSelection) return
        deleteDialog.tagId = model.selectedId
        deleteDialog.open()
    }

    onVisibleChanged: {
        if (visible) return
        mergeDialog.close(); deleteDialog.close()
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
