import QtQuick
import QtQuick.Templates as T

// Szegmentált választó (Beállítások): sunken sáv 3 px belső térrel, 28 px magas elemek; a
// kiválasztott elem raised + 1 px árnyék, 600-as betű.
//   SettingsSegmented {
//       options: [{ value: "system", label: qsTr("Rendszer"), iconName: "monitor" }, …]
//       value: vm.themeMode; onPicked: (v) => vm.themeMode = v
//   }
Rectangle {
    id: root

    property var options: []           // [{ value, label, iconName? }]
    property string value: ""
    property bool stretch: false       // az elemek egyenlően kitöltik a szélességet
    signal picked(string value)

    implicitWidth: row.implicitWidth + 6
    implicitHeight: 34
    radius: 7
    color: Theme.sunken

    Row {
        id: row
        x: 3; y: 3
        spacing: 2
        Repeater {
            model: root.options
            T.AbstractButton {
                id: seg
                required property var modelData
                required property int index
                readonly property bool current: modelData.value === root.value
                readonly property bool hasIcon: (modelData.iconName || "") !== ""

                width: root.stretch ? (root.width - 6 - 2 * (root.options.length - 1)) / root.options.length
                                    : implicitWidth
                implicitWidth: segRow.implicitWidth + 24
                implicitHeight: 28
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: modelData.label
                Accessible.role: Accessible.RadioButton
                Accessible.checked: current
                onClicked: if (!current) root.picked(modelData.value)
                Keys.onReturnPressed: click()
                Keys.onSpacePressed: click()

                background: Item {
                    Rectangle {            // 1 px árnyék a kiválasztott elem alatt
                        visible: seg.current
                        x: 0; y: 1
                        width: parent.width; height: parent.height
                        radius: 5
                        color: Theme.shadowTightColor
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 5
                        color: seg.current ? Theme.raised
                             : seg.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                    }
                    TFocusRing { visible: seg.visualFocus; targetRadius: 5 }
                }
                contentItem: Item {
                    Row {
                        id: segRow
                        anchors.centerIn: parent
                        spacing: 6
                        TIcon {
                            visible: seg.hasIcon
                            name: seg.modelData.iconName || ""
                            size: 14
                            color: seg.current ? Theme.text : Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        TLabel {
                            text: seg.modelData.label
                            font.pixelSize: Theme.fontSmall
                            font.weight: seg.current ? Theme.weightSemiBold : Theme.weightRegular
                            color: seg.current ? Theme.text : Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
    }
}
