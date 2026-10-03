import QtQuick
import QtQuick.Layouts

// A héj megerősítő párbeszédablakai az M10 minta szerint (fátyol + 440 px-es ablak, konkrét
// következmény, jobbra a megerősítő gomb): újra-átírás, törlés, bezárás felvétel közben,
// „Vége a meetingnek?”, és a ShellActions.confirm() általános ablaka.
Item {
    id: root

    property var shell: null                 // ShellActions
    readonly property bool anyOpen: retranscribeDialog.visible || deleteDialog.visible
                                    || closeDialog.visible || stopDialog.visible || confirmDialog.visible

    // A felhasználó a háttérben folytatja a felvételt / leállítja és kilép (a Main.qml kezeli).
    signal backgroundRequested()
    signal stopAndQuitRequested()

    function openRetranscribe(meetingId) {
        retranscribeDialog.meetingId = meetingId
        retranscribeDialog.impact = root.shell ? root.shell.retranscribeImpact(meetingId).text : ""
        keepBackup.checked = true
        retranscribeDialog.open()
    }
    function openDelete(meetingId, title) {
        deleteDialog.meetingId = meetingId
        deleteDialog.meetingTitle = title
        deleteDialog.open()
    }
    function openCloseWhileRecording() { closeDialog.open() }
    function openStopPrompt(reason) {
        stopDialog.reason = reason
        stopDialog.open()
    }
    function openConfirm(title, text, confirmLabel, danger) {
        confirmDialog.title = title
        confirmDialog.text = text
        confirmDialog.confirmLabel = confirmLabel
        confirmDialog.danger = danger
        confirmDialog.answer = false
        confirmDialog.open()
    }

    // ---- M10: újra-átírás ----
    TDialog {
        id: retranscribeDialog
        objectName: "retranscribeDialog"
        property string meetingId: ""
        property string impact: ""
        title: qsTr("Újra-átírod a megbeszélést?")
        TLabel {
            Layout.fillWidth: true
            text: retranscribeDialog.impact
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        TCheckBox {
            id: keepBackup
            objectName: "keepBackup"
            Layout.fillWidth: true
            checked: true
            text: qsTr("A mostani átirat maradjon meg másolatként")
        }
        actions: [
            TButton { text: qsTr("Mégse"); font.weight: Theme.weightSemiBold; onClicked: retranscribeDialog.reject() },
            TButton {
                objectName: "retranscribeConfirm"
                text: qsTr("Újra-átírás")
                variant: "danger"
                onClicked: {
                    retranscribeDialog.accept()
                    if (root.shell)
                        root.shell.confirmRetranscribe(retranscribeDialog.meetingId, keepBackup.checked)
                }
            }
        ]
    }

    // ---- törlés ----
    TDialog {
        id: deleteDialog
        objectName: "deleteDialog"
        property string meetingId: ""
        property string meetingTitle: ""
        title: qsTr("Törlöd a megbeszélést?")
        TLabel {
            Layout.fillWidth: true
            text: qsTr("„%1” felvétele (a hangsávok), az átirata és az összefoglalója véglegesen "
                       + "törlődik a gépedről. Ez nem vonható vissza.").arg(deleteDialog.meetingTitle)
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        actions: [
            TButton { text: qsTr("Mégse"); font.weight: Theme.weightSemiBold; onClicked: deleteDialog.reject() },
            TButton {
                objectName: "deleteConfirm"
                text: qsTr("Törlés")
                variant: "danger"
                onClicked: {
                    deleteDialog.accept()
                    if (root.shell)
                        root.shell.deleteMeeting(deleteDialog.meetingId)
                }
            }
        ]
    }

    // ---- bezárás felvétel közben ----
    TDialog {
        id: closeDialog
        objectName: "closeDialog"
        title: qsTr("Éppen felvétel megy")
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Ha most kilépsz, a felvétel megszakadna. Folytathatod a háttérben (a "
                       + "felvevő ablaka nyitva marad, a főablak a felvétel végén visszajön), vagy "
                       + "leállíthatod a felvételt, és utána lépünk ki.")
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        actions: [
            TButton { text: qsTr("Mégse"); variant: "ghost"; onClicked: closeDialog.reject() },
            TButton {
                text: qsTr("Leállítom és kilépek")
                variant: "dangerSoft"
                onClicked: { closeDialog.accept(); root.stopAndQuitRequested() }
            },
            TButton {
                text: qsTr("Háttérben folytatom")
                variant: "primary"
                onClicked: { closeDialog.accept(); root.backgroundRequested() }
            }
        ]
    }

    // ---- „Vége a meetingnek?” (hívás-vég / csend észlelve; magától nem állítunk le) ----
    TDialog {
        id: stopDialog
        objectName: "stopDialog"
        property string reason: ""
        title: qsTr("Vége a meetingnek?")
        TLabel {
            Layout.fillWidth: true
            text: stopDialog.reason + " " + qsTr("Leállítsam a rögzítést? Ha nem válaszolsz, a felvétel megy tovább.")
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        actions: [
            TButton { text: qsTr("Folytatom a felvételt"); font.weight: Theme.weightSemiBold; onClicked: stopDialog.reject() },
            TButton {
                text: qsTr("Leállítás")
                variant: "danger"
                onClicked: { stopDialog.accept(); if (root.shell) root.shell.stopRecording() }
            }
        ]
    }

    // ---- ShellActions.confirm() ----
    TDialog {
        id: confirmDialog
        objectName: "confirmDialog"
        property string text: ""
        property string confirmLabel: ""
        property bool danger: false
        property bool answer: false
        TLabel {
            Layout.fillWidth: true
            text: confirmDialog.text
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        actions: [
            TButton { text: qsTr("Mégse"); font.weight: Theme.weightSemiBold; onClicked: confirmDialog.close() },
            TButton {
                objectName: "confirmAccept"
                text: confirmDialog.confirmLabel !== "" ? confirmDialog.confirmLabel : qsTr("Rendben")
                variant: confirmDialog.danger ? "danger" : "primary"
                onClicked: { confirmDialog.answer = true; confirmDialog.close() }
            }
        ]
        // Bármilyen bezárás (gomb, Esc, mellé kattintás) válasz: a confirm() erre vár.
        onClosed: if (root.shell) root.shell.resolveConfirm(confirmDialog.answer)
    }
}
