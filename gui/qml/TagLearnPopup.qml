import QtQuick
import QtQuick.Templates as T

// A „Ki volt ott?” jóváhagyása utáni tanulási kérdés (vault UX-terv 5.): nem-modális sötét sáv
// az ablak alján (a toast fölött), ~8 mp; rámutatva és nyitott „Egyenként…” listával marad.
// A válasz a toastra „Visszavonás”-sal megy ki (a címke-verem: shell.toast(…, "tags")).
//   TagLearnPopup { vm: participantsViewModel; shell: shellActions }   →   openIfAsked()
T.Popup {
    id: popup
    objectName: "tagLearnPopup"

    property var vm: null                    // ParticipantsViewModel
    property var shell: null                 // ShellActions (toast)
    property int timeoutMs: 8000
    readonly property var question: vm ? vm.learnQuestion : ({})
    readonly property bool asked: !!question && (question.text || "") !== ""

    // true: van kérdés, a sáv nyitva.
    function openIfAsked() {
        if (!asked) return false
        bar.expanded = false
        open()
        return true
    }
    function report(text) {
        if (text !== "" && popup.shell) popup.shell.toast(text, "tags")
        if (!popup.asked) popup.close()
    }

    parent: T.Overlay.overlay
    modal: false
    focus: false
    closePolicy: T.Popup.CloseOnEscape
    padding: 0
    width: Math.min(720, (parent ? parent.width : 720) - 32)
    height: bar.implicitHeight
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? parent.height - height - 72 : 0
    onClosed: if (popup.vm) popup.vm.dismissLearn()

    background: Item {}
    contentItem: TagLearnBar {
        id: bar
        question: popup.question
        onAnswered: (yes) => popup.report(popup.vm.answerLearn(yes))
        onPairAnswered: (index, yes) => popup.report(popup.vm.answerLearnPair(index, yes))
        onDismissed: popup.close()
    }

    Timer {
        interval: popup.timeoutMs
        running: popup.opened && !bar.hovered && !bar.expanded
        onTriggered: popup.close()
    }
}
