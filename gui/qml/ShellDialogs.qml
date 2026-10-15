import QtQuick
import QtQuick.Layouts

// The shell's confirmation dialogs following the M10 pattern (veil + 440 px window, concrete
// consequence, confirm button on the right): re-transcribe, delete, close while recording,
// "End of the meeting?", and the general window of ShellActions.confirm().
// S5: the route dialog ("Hol fusson?"): a start without an explicit route found an Ask route (or a Cloud
// run that needs the estimate's confirmation) -> one row per route of the step (the Overview's
// routeOptions), the answer is ShellActions.startWithRoute.
Item {
    id: root

    property var shell: null                 // ShellActions
    readonly property bool anyOpen: retranscribeDialog.visible || deleteDialog.visible || routeDialog.visible
                                    || closeDialog.visible || stopDialog.visible || confirmDialog.visible

    // The user continues recording in the background / stops and quits (handled by Main.qml).
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
    function openRoute(meetingId, action, step) {
        routeDialog.meetingId = meetingId
        routeDialog.action = action
        routeDialog.step = step
        routeVm.meetingId = meetingId
        routeVm.prepareRoutes(step)
        routeList.makeDefault = false
        routeDialog.open()
        routeList.focusFirst()
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

    // ---- M10: re-transcribe ----
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

    // ---- S5: where should this run go? ----
    OverviewViewModel { id: routeVm }
    TDialog {
        id: routeDialog
        objectName: "routeDialog"
        property string meetingId: ""
        property string action: ""
        property string step: "transcript"
        title: routeDialog.step === "transcript" ? qsTr("Hol fusson az átírás?") : qsTr("Hol fusson az összefoglaló?")
        onClosed: routeVm.meetingId = ""
        RouteOptionList {
            id: routeList
            Layout.fillWidth: true
            title: ""
            options: {
                for (const s of routeVm.steps)
                    if (s.key === routeDialog.step) return s.routeOptions || []
                return []
            }
            onChosen: (route, makeDefault) => {
                const id = routeDialog.meetingId
                const action = routeDialog.action
                if (makeDefault) routeVm.setDefaultRoute(routeDialog.step, route)
                routeDialog.accept()
                if (root.shell) root.shell.startWithRoute(id, action, route)
            }
        }
        actions: [
            TButton { text: qsTr("Mégse"); font.weight: Theme.weightSemiBold; onClicked: routeDialog.reject() }
        ]
    }

    // ---- delete ----
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

    // ---- close while recording ----
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

    // ---- "End of the meeting?" (call end / silence detected; we never stop by ourselves) ----
    // With the QML recorder the recorder's own box asks this; this dialog opens only if a bridge
    // sends the stopPromptRequested signal (QmlShellBridge does not).
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
        // Any close (button, Esc, click outside) is an answer: confirm() waits for it.
        onClosed: if (root.shell) root.shell.resolveConfirm(confirmDialog.answer)
    }
}
