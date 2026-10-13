import QtQuick
import QtQuick.Layouts

// Task bar: the cancelable background task running on the selected meeting (identification,
// mixdown, summary, re-transcribe ...) -- accentSoft box, icon, title, progress text, 4 px
// progress bar (indeterminate stripe without a percentage) and "Cancel".
// `task`: an item of ShellMeetingModel.tasks ({ kind, title, detail, eta, iconName, percent,
// cancellable, cancelling }); without it (standalone screenshot) a made-up sample shows.
Rectangle {
    id: root

    property var shell: null
    property string meetingId: ""
    property var task: null

    readonly property var shown: task ? task : ({
        kind: 5, title: DemoText.t("Résztvevők azonosítása…", "Identifying participants…"), detail: DemoText.t("3 / 5 beszélő", "3 / 5 speakers"), eta: "",
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
