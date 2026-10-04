import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A Személyek ablaka (design/handoff-people, P01–P06): külön, átméretezhető, nem modális
// ablak (960 × 660). Bal oldalt kereshető névsor (a sáv szélessége húzható), jobb oldalt a
// kijelölt személy: becenevek, megjegyzés, hangminták, megbeszélések. A natív ablakkeret
// marad (mint a főablaknál és a Beállításoknál), ezért a spec 36 px-es saját címsora nincs
// megrajzolva.
//
// Billentyűk: Ctrl+F kereső · F2 átnevezés · Del törlés (megerősítéssel) · Szóköz a kijelölt
// minta lejátszása · Ctrl+Z visszavonás (szövegmezőben a mező saját visszavonása él).
//
// A C++ gazda (tanara_qml::PeopleWindowHost) hozza létre; önállóan is betölthető képhez:
//   tanara --qml-shot ki.png --qml-page PeopleWindow --size 960x1100 --qml-prop 'demoState="P04"'
// demoState: P01 … P06 · newPerson · sampleNew · sampleMove · noResult · sort · allSamples ·
//            deleteKeep · creating · toastPlain
ApplicationWindow {
    id: window

    // A gazda adja át (kezdő property-ként); nélküle az App.controller / demó érvényes.
    property QtObject hostController: null
    property string demoState: ""
    property alias vm: model
    property alias detailPane: detail
    property alias listPane: listPane
    property alias toastItem: toast
    property alias mergeDialog: mergeDialog
    property alias deleteDialog: deleteDialog
    property alias newPersonDialog: newPersonDialog
    property alias sampleTargetDialog: sampleTargetDialog
    property real listWidth: 288

    title: qsTr("Személyek")
    width: 960
    height: 660
    minimumWidth: 800
    minimumHeight: 560
    color: Theme.bg
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody
    // Külön ablak, nem modális: a főablak mellette használható marad.
    flags: Qt.Window

    PeopleViewModel {
        id: model
        onToast: (text, undoable) => toast.show(text, undoable)
    }

    Component.onCompleted: {
        if (hostController) model.controller = hostController
        else if (demoState !== "") {
            model.demoState = ["P01", "P02", "P03", "P04", "P05", "P06", "noResult", "creating"].indexOf(demoState) >= 0
                ? demoState : demoState === "toastPlain" ? "P03" : "P01"
            Qt.callLater(window.applyDemoOverlay)
        }
    }

    function applyDemoOverlay() {
        switch (demoState) {
        case "P01": detail.openSampleMenu(2); break
        case "P02":
            detail.demoRenameText = "Bárány Gergő"
            detail.demoAliasText = "Gergő"
            detail.startAlias()
            detail.startRename()
            break
        case "P03": toast.show(qsTr("Minta törölve: %1").arg("Partnerdemó – adatimport"), true); break
        case "toastPlain": toast.show(qsTr("A minta visszakerült."), false); break
        case "P04": mergeDialog.openFor("gerg", "B. Gergő"); break
        case "P05": openDelete(); break
        case "deleteKeep": openDelete(); keepSamples.checked = true; break
        case "newPerson": openNewPerson("Varga Tímea"); break
        case "sampleNew": sampleTargetDialog.openFor("demo-2", "new", "Gál"); break
        case "sampleMove": sampleTargetDialog.openFor("demo-2", "move", ""); break
        case "sort": listPane.demoSortOpen = true; break
        case "allSamples": detail.showAllSamples = true; break
        }
    }

    // ---- műveletek indítása ----
    function selectPerson(name) {
        detail.showSelfDetail = true
        model.selectedName = name
    }
    function openNewPerson(name) {
        newPersonError.text = ""
        newPersonField.text = name || ""
        newPersonDialog.open()
        newPersonField.forceActiveFocus()
    }
    function commitNewPerson() {
        if (newPersonField.text.trim() === "") { newPersonError.text = qsTr("Adj meg egy nevet."); return }
        detail.commitNote()
        const error = model.addPerson(newPersonField.text)
        detail.showSelfDetail = true
        if (error !== "") { newPersonError.text = error; return }
        newPersonDialog.accept()
    }
    function openMerge() {
        if (!model.hasSelection || detail.emptyState || model.totalCount < 2) return
        detail.commitNote()
        mergeDialog.openFor("", "")
    }
    function openDelete() {
        if (!model.hasSelection || detail.emptyState || model.selectedIsSelf) return
        detail.commitNote()
        deleteError.text = ""
        keepSamples.checked = false
        deleteDialog.open()
    }

    onClosing: {
        detail.commitNote()
        model.stopSample()
    }
    onVisibleChanged: {
        if (visible) return
        // Rejtett ablakban ne szóljon minta, és ne maradjon nyitva kérdés.
        model.stopSample()
        mergeDialog.close(); deleteDialog.close(); newPersonDialog.close(); sampleTargetDialog.close()
        toast.hide()
    }

    // Szövegmezőben a billentyűk a mezőé (Del, szóköz, Ctrl+Z).
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool dialogOpen: mergeDialog.opened || deleteDialog.opened || newPersonDialog.opened
                                       || sampleTargetDialog.opened

    Shortcut { sequences: [StandardKey.Find]; enabled: !window.dialogOpen; onActivated: listPane.focusSearch() }
    Shortcut { sequence: "F2"; enabled: !window.dialogOpen; onActivated: detail.startRename() }
    Shortcut { sequences: [StandardKey.Delete]; enabled: !window.typing && !window.dialogOpen; onActivated: window.openDelete() }
    Shortcut { sequence: "Space"; enabled: !window.typing && !window.dialogOpen; onActivated: detail.playSelectedSample() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !window.typing && !window.dialogOpen && model.canUndo; onActivated: { toast.hide(); model.undo() } }
    // Esc szándékosan nem zár: a mezők és a felugrók használják. Bezárás: Ctrl+W vagy az ablak × gombja.
    Shortcut { sequences: [StandardKey.Close]; onActivated: window.close() }

    // ---- tartalom ----
    PeopleListPane {
        id: listPane
        width: Math.max(240, Math.min(window.listWidth, window.width - 520))
        height: parent.height
        vm: model
        showSelection: !detail.emptyState
        onAboutToSelect: detail.commitNote()
        onPersonClicked: (name) => window.selectPerson(name)
        onNewPersonRequested: (name) => window.openNewPerson(name)
    }
    // A sáv szélessége húzható (a nevek így rövidítés nélkül elférnek).
    Rectangle {
        id: splitter
        x: listPane.width
        width: 1
        height: parent.height
        color: drag.pressed ? Theme.accent : dragHover.hovered ? Theme.borderStrong : Theme.border
        MouseArea {
            id: drag
            x: -3
            width: 7
            height: parent.height
            cursorShape: Qt.SplitHCursor
            preventStealing: true
            onPositionChanged: (mouse) => {
                if (pressed) window.listWidth = Math.max(240, Math.min(460, drag.mapToItem(null, mouse.x, 0).x))
            }
            HoverHandler { id: dragHover }
        }
    }
    PeopleDetailPane {
        id: detail
        x: listPane.width + 1
        width: parent.width - x
        height: parent.height
        vm: model
        onMergeRequested: window.openMerge()
        onDeleteRequested: window.openDelete()
        onNewPersonRequested: window.openNewPerson("")
        onSampleNewPersonRequested: (id) => sampleTargetDialog.openFor(id, "new", "")
        onSampleMoveRequested: (id) => sampleTargetDialog.openFor(id, "move", "")
    }

    PeopleUndoToast {
        id: toast
        objectName: "peopleToast"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        maxWidth: Math.min(560, window.width - 48)
        z: 10
        onUndoRequested: model.undo()
    }

    // ---- párbeszédablakok ----
    PeopleMergeDialog { id: mergeDialog; vm: model }
    PeopleSampleTargetDialog { id: sampleTargetDialog; vm: model }

    TDialog {
        id: deleteDialog
        width: Math.min(440, window.width - 32)
        title: qsTr("%1 törlése").arg(model.selectedName)
        TLabel {
            objectName: "deleteText"
            Layout.fillWidth: true
            Layout.topMargin: -4
            text: deleteDialog.opened || deleteDialog.visible ? model.deleteText(keepSamples.checked) : ""
            muted: true
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        TCheckBox {
            id: keepSamples
            objectName: "keepSamples"
            visible: model.sampleCount > 0
            text: qsTr("Hangminták megtartása névtelen személyként")
        }
        TLabel {
            id: deleteError
            visible: text !== ""
            Layout.fillWidth: true
            color: Theme.dangerInk
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
        actions: [
            TButton { text: qsTr("Mégse"); onClicked: deleteDialog.reject() },
            TButton {
                objectName: "deleteConfirm"
                text: qsTr("Törlés")
                variant: "danger"
                leftPadding: 16; rightPadding: 16
                onClicked: {
                    const error = model.deletePerson(keepSamples.checked)
                    if (error !== "") { deleteError.text = error; return }
                    deleteDialog.accept()
                }
            }
        ]
    }

    TDialog {
        id: newPersonDialog
        width: Math.min(400, window.width - 32)
        title: qsTr("Új személy")
        TLabel {
            Layout.fillWidth: true
            Layout.topMargin: -8
            text: qsTr("Előre felvett személy: a személyválasztóban már szerepel, hanglenyomata az első hozzárendelés után lehet.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
        TTextField {
            id: newPersonField
            objectName: "newPersonField"
            Layout.fillWidth: true
            placeholderText: qsTr("Név")
            hasError: newPersonError.text !== ""
            onAccepted: window.commitNewPerson()
            onTextEdited: newPersonError.text = ""
        }
        TLabel {
            id: newPersonError
            objectName: "newPersonError"
            visible: text !== ""
            Layout.fillWidth: true
            Layout.topMargin: -6
            color: Theme.dangerInk
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
        actions: [
            TButton { text: qsTr("Mégse"); onClicked: newPersonDialog.reject() },
            TButton {
                objectName: "newPersonConfirm"
                text: qsTr("Hozzáadás")
                variant: "primary"
                leftPadding: 16; rightPadding: 16
                onClicked: window.commitNewPerson()
            }
        ]
    }
}
