import QtQuick
import QtQuick.Templates as T

// A felvevő pirula-mérete (R05): állapot-pötty, idő, sávonként egy 4 px-es mini-mérő,
// kerek leállítás-gomb, kinyitás. ~230×40, sugár 20. Húzással mozgatható.
Item {
    id: root
    property var vm: null
    signal moveRequested()
    signal expandRequested()

    readonly property string st: vm ? vm.state : "idle"
    readonly property bool recording: st === "recording" || st === "stopping"

    implicitHeight: 40
    implicitWidth: row.implicitWidth + 14 + 4 + 2
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: 20
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }
    DragHandler { target: null; onActiveChanged: if (active) root.moveRequested() }
    TapHandler { onDoubleTapped: root.expandRequested() }

    Row {
        id: row
        x: 15
        height: parent.height
        spacing: 10

        Rectangle {                           // 10 px pötty 3 px recLine gyűrűvel
            anchors.verticalCenter: parent.verticalCenter
            width: 16; height: 16; radius: 8
            color: root.recording ? Theme.recLine : "transparent"
            Rectangle {
                anchors.centerIn: parent
                width: 10; height: 10; radius: 5
                color: root.recording ? Theme.rec : root.st === "done" ? Theme.accent : Theme.borderStrong
            }
        }
        TLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: root.vm ? root.vm.elapsedText : "00:00:00"
            mono: true
            font.pixelSize: 15
            font.weight: Theme.weightMedium
        }
        Row {                                 // sávonként egy mini-mérő
            anchors.verticalCenter: parent.verticalCenter
            height: 16
            spacing: 2
            Repeater {
                model: root.vm ? root.vm.devices : null
                Rectangle {
                    required property bool selected
                    required property int level
                    visible: selected
                    width: visible ? 4 : 0
                    height: level > 0 ? Math.min(16, 4 + Math.round(level * 12 / Theme.vuSegments)) : 4
                    y: 16 - height
                    radius: 1
                    color: level > 0 ? Theme.vuLow : Theme.vuOff
                }
            }
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: 20
            color: Theme.border
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            T.Button {
                id: stopBtn
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 32; implicitHeight: 32
                hoverEnabled: true
                enabled: root.st === "recording" || root.st === "idle"
                Accessible.name: root.st === "idle" ? qsTr("Felvétel indítása") : qsTr("Leállítás")
                onClicked: root.st === "idle" ? root.vm.start() : root.vm.stop()
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 10; height: 10
                        radius: root.st === "idle" ? 5 : 2
                        color: root.st === "idle" ? "#ffffff" : Theme.bg
                    }
                }
                background: Rectangle {
                    radius: 16
                    color: root.st === "idle" ? Theme.rec : stopBtn.enabled ? Theme.text : Theme.borderStrong
                    Rectangle {
                        anchors.fill: parent; radius: 16
                        color: Theme.bg
                        opacity: stopBtn.down ? 0.2 : stopBtn.hovered ? 0.1 : 0
                    }
                    TFocusRing { visible: stopBtn.visualFocus; targetRadius: 16 }
                }
                TToolTip { visible: stopBtn.hovered; text: stopBtn.Accessible.name }
            }
            T.Button {
                id: expBtn
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 28; implicitHeight: 32
                hoverEnabled: true
                Accessible.name: qsTr("Kinyitás")
                onClicked: root.expandRequested()
                contentItem: Item {
                    TIcon { anchors.centerIn: parent; name: "maximize-2"; size: 14; color: expBtn.hovered ? Theme.text : Theme.textMuted }
                }
                background: Item { TFocusRing { visible: expBtn.visualFocus; targetRadius: 14 } }
                TToolTip { visible: expBtn.hovered; text: qsTr("Kinyitás") }
            }
        }
    }
}
