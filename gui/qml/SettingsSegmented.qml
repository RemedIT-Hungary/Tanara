import QtQuick
import QtQuick.Templates as T

// Segmented selector (Settings): a sunken track with 3 px padding, 28 px high items; the selected
// item is raised + 1 px shadow, weight 600.
//   SettingsSegmented {
//       options: [{ value: "system", label: qsTr("System"), iconName: "monitor" }, ...]
//       value: vm.themeMode; onPicked: (v) => vm.themeMode = v
//   }
Rectangle {
    id: root

    // [{ value, label, iconName?, enabled? (default true), toolTip? (explains a disabled item) }]
    property var options: []
    property string value: ""
    property bool stretch: false       // the items fill the width equally
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
                readonly property bool allowed: modelData.enabled === undefined || modelData.enabled

                width: root.stretch ? (root.width - 6 - 2 * (root.options.length - 1)) / root.options.length
                                    : implicitWidth
                implicitWidth: segRow.implicitWidth + 24
                implicitHeight: 28
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: modelData.label
                Accessible.role: Accessible.RadioButton
                Accessible.checked: current
                Accessible.description: modelData.toolTip || ""
                // Disabled item: not selectable, but its tooltip shows on hover.
                onClicked: if (!current && allowed) root.picked(modelData.value)
                Keys.onReturnPressed: click()
                Keys.onSpacePressed: click()

                background: Item {
                    Rectangle {            // 1 px shadow below the selected item
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
                             : seg.hovered && seg.allowed ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
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
                            opacity: seg.allowed || seg.current ? 1 : 0.55
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                TToolTip {
                    visible: seg.hovered && (seg.modelData.toolTip || "") !== ""
                    text: seg.modelData.toolTip || ""
                }
            }
        }
    }
}
