import QtQuick
import QtQuick.Templates as T

// Radio button (Settings): 16 px circle -- off: 1.5 px borderStrong; on: 5 px accent border.
// With a label: SettingsRadio { text: qsTr("Automatically, after the recording"); checked: ...; onClicked: ... }
T.AbstractButton {
    id: control

    implicitWidth: 16 + (text !== "" ? 10 + label.implicitWidth : 0)
    implicitHeight: Math.max(20, label.implicitHeight)
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.role: Accessible.RadioButton
    Accessible.name: text
    Accessible.checked: checked
    Keys.onReturnPressed: click()
    Keys.onSpacePressed: click()

    contentItem: Item {
        Rectangle {
            id: dot
            width: 16; height: 16; radius: 8
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.raised
            border.width: control.checked ? 5 : 1.5
            border.color: control.checked ? Theme.accent
                        : control.hovered ? Theme.textMuted : Theme.borderStrong
            TFocusRing { visible: control.visualFocus; targetRadius: 8 }
        }
        TLabel {
            id: label
            x: 26
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            wrapMode: Text.Wrap
        }
    }
}
