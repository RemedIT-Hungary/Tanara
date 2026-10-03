import QtQuick
import QtQuick.Layouts

// Feladat-sáv: a kijelölt megbeszélésen futó megszakítható háttérfeladat (azonosítás,
// lekeverés, összefoglaló, újra-átírás …) — accentSoft doboz, ikon, cím, haladás-szöveg,
// 4 px-es folyamatjelző (százalék nélkül futó csík) és „Megszakítás”.
// `task`: a ShellMeetingModel.tasks egy eleme ({ kind, title, detail, eta, iconName, percent,
// cancellable, cancelling }); nélküle (önálló képernyőkép) kitalált minta látszik.
Rectangle {
    id: root

    property var shell: null
    property string meetingId: ""
    property var task: null

    readonly property var shown: task ? task : ({
        kind: 5, title: "Résztvevők azonosítása…", detail: "3 / 5 beszélő", eta: "",
        iconName: "fingerprint", percent: 60, cancellable: true, cancelling: false })

    implicitHeight: 44
    radius: Theme.radiusControl
    color: Theme.accentSoft
    border.width: 1
    border.color: Theme.accentLine

    RowLayout {
        anchors { fill: parent; leftMargin: 14; rightMargin: 6 }
        spacing: 10

        TIcon { name: root.shown.iconName; size: 16; color: Theme.accent }
        TLabel {
            text: root.shown.title
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
            Layout.maximumWidth: root.width * 0.4
            elide: Text.ElideRight
        }
        TLabel {
            visible: text !== ""
            text: root.shown.eta !== "" && root.shown.detail !== ""
                  ? root.shown.detail + " · " + root.shown.eta
                  : root.shown.detail + root.shown.eta
            muted: true
            font.pixelSize: Theme.fontSmall
            Layout.maximumWidth: root.width * 0.3
            elide: Text.ElideRight
        }
        TProgressBar {
            Layout.fillWidth: true
            Layout.leftMargin: 2
            Layout.rightMargin: 8
            trackColor: Theme.bg
            indeterminate: root.shown.percent < 0
            value: root.shown.percent < 0 ? 0 : root.shown.percent / 100
        }
        TButton {
            visible: root.shown.cancellable
            text: root.shown.cancelling ? qsTr("Megszakítás…") : qsTr("Megszakítás")
            enabled: !root.shown.cancelling
            variant: "ghost"
            size: "small"
            font.weight: Theme.weightSemiBold
            onClicked: if (root.shell) root.shell.cancelJob(root.meetingId, root.shown.kind)
        }
    }
}
