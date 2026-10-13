import QtQuick
import QtQuick.Layouts

// Indicator of the import running in the background, in the sidebar (a narrow relative of the
// task strip): title, real percent, cancel. A click brings the dialog back (clicked).
// `model`: ShellImportModel; without it (standalone screenshot) a made-up sample shows.
Rectangle {
    id: root

    property var model: null
    signal clicked()

    readonly property int percent: model ? model.percent : 42
    readonly property string title: model ? model.runningTitle : DemoText.t("Fókuszcsoport 3", "Focus group 3")
    readonly property bool cancelling: model ? model.cancelling : false

    implicitHeight: 52
    radius: Theme.radiusControl
    color: Theme.accentSoft
    border.width: 1
    border.color: Theme.accentLine

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: 10; rightMargin: 4; topMargin: 6; bottomMargin: 9 }
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TIcon { name: "import"; size: 15; color: Theme.accent }
            TLabel {
                Layout.fillWidth: true
                text: qsTr("Importálás: %1").arg(root.title)
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightSemiBold
                elide: Text.ElideRight
            }
            TLabel {
                visible: root.percent >= 0
                text: root.percent + "%"
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            TIconButton {
                implicitWidth: 24
                implicitHeight: 24
                iconName: "x"
                iconSize: 13
                variant: "flat"
                enabled: !root.cancelling
                toolTipText: qsTr("Importálás megszakítása")
                onClicked: if (root.model) root.model.cancel()
            }
        }
        TProgressBar {
            Layout.fillWidth: true
            Layout.rightMargin: 6
            trackColor: Theme.bg
            indeterminate: root.percent < 0
            value: root.percent < 0 ? 0 : root.percent / 100
        }
    }
}
