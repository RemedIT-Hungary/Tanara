import QtQuick
import QtQuick.Layouts

// M01 — üres könyvtár: mit csinál a Tanara, a három lépés (Felvétel → Átirat →
// Összefoglaló), „Felvétel indítása”, „Hangfájl importálása” (a második út az első
// megbeszéléshez) és „Hívásfigyelő beállítása” (Beállítások › Figyelő). Alul egy sor a
// másik gépről hozott megbeszéléshez: „Archívum importálása” (*.tanara.zip).
Item {
    id: root

    property var shell: null                 // ShellActions vagy null

    component Step: Rectangle {
        id: step
        property int number: 1
        property string label: ""
        implicitWidth: stepRow.implicitWidth + 22
        implicitHeight: 40
        radius: Theme.radiusControl
        color: Theme.raised
        border.width: 1
        border.color: Theme.border
        Row {
            id: stepRow
            anchors.centerIn: parent
            spacing: 9
            Rectangle {
                width: 22; height: 22; radius: 11
                color: Theme.sunken
                anchors.verticalCenter: parent.verticalCenter
                TLabel {
                    anchors.centerIn: parent
                    text: step.number
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontMicro
                    font.weight: Theme.weightSemiBold
                }
            }
            TLabel {
                text: step.label
                font.weight: Theme.weightSemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(580, parent.width - 2 * Theme.space5)
        spacing: 0

        TLabel {
            Layout.fillWidth: true
            text: qsTr("Vegyük fel az első megbeszélést")
            font.pixelSize: 26
            font.weight: Theme.weightSemiBold
            wrapMode: Text.Wrap
        }
        TLabel {
            Layout.fillWidth: true
            Layout.topMargin: 10
            text: qsTr("A Tanara minden hangforrást külön sávra rögzít, utána átiratot és "
                       + "összefoglalót készít. A felvétel a gépeden marad.")
            muted: true
            font.pixelSize: 15
            cssLineHeight: 1.6
            wrapMode: Text.Wrap
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 22
            spacing: 12
            Step { number: 1; label: qsTr("Felvétel") }
            TIcon { name: "arrow-right"; size: 14; color: Theme.textMuted; height: 40 }
            Step { number: 2; label: qsTr("Átirat") }
            TIcon { name: "arrow-right"; size: 14; color: Theme.textMuted; height: 40 }
            Step { number: 3; label: qsTr("Összefoglaló") }
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 22
            spacing: 10
            TButton {
                implicitHeight: 40
                text: qsTr("Felvétel indítása")
                variant: "record"
                onClicked: if (root.shell) root.shell.openRecorder()
            }
            TButton {
                objectName: "emptyImport"
                implicitHeight: 40
                text: qsTr("Hangfájl importálása")
                iconName: "import"
                font.weight: Theme.weightSemiBold
                onClicked: if (root.shell) root.shell.openImport()
            }
            TButton {
                implicitHeight: 40
                text: qsTr("Hívásfigyelő beállítása")
                iconName: "radar"
                font.weight: Theme.weightSemiBold
                onClicked: if (root.shell) root.shell.openSettings("watcher")
            }
        }

        TLabel {
            Layout.fillWidth: true
            Layout.topMargin: 26
            text: qsTr("Már megvan a felvétel? Importálj hang- vagy videófájlt (ide is húzhatod): "
                       + "ugyanúgy készül belőle átirat és összefoglaló.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 6
            TLabel {
                Layout.fillWidth: true
                text: qsTr("Másik gépen rögzített megbeszélést hoznál át (.tanara.zip)?")
                muted: true
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.5
                wrapMode: Text.Wrap
            }
            TButton {
                objectName: "emptyImportArchive"
                text: qsTr("Archívum importálása")
                iconName: "folder-open"
                size: "small"
                variant: "ghost"
                onClicked: if (root.shell) root.shell.importArchive("")
            }
        }
        TLabel {
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: qsTr("A hívásfigyelő a tálcán fut, és szól, ha Teams, Meet vagy Zoom hívást "
                       + "észlel. A felvételt mindig te indítod.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
    }
}
