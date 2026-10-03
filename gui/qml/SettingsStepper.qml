import QtQuick
import QtQuick.Templates as T

// Léptető (Beállítások): 32 px magas, „− | érték | +”; az érték Plex Mono 13/500.
//   SettingsStepper { value: vm.silenceAskMinutes; from: 0; to: 60
//                     textFor: (v) => v === 0 ? qsTr("kikapcsolva") : qsTr("%n perc", "", v)
//                     onMoved: (v) => vm.silenceAskMinutes = v }
Rectangle {
    id: root

    property int value: 0
    property int from: 0
    property int to: 100
    property int step: 1
    property var textFor: function(v) { return String(v) }
    property string accessibleName: ""
    signal moved(int value)

    implicitWidth: 30 + label.implicitWidth + 24 + 30
    implicitHeight: 32
    radius: Theme.radiusControl
    color: Theme.raised
    border.width: 1
    border.color: Theme.borderStrong
    activeFocusOnTab: true
    Accessible.role: Accessible.SpinBox
    Accessible.name: accessibleName

    function bump(delta) {
        const v = Math.max(root.from, Math.min(root.to, root.value + delta))
        if (v !== root.value) root.moved(v)
    }
    Keys.onUpPressed: bump(step)
    Keys.onDownPressed: bump(-step)
    Keys.onRightPressed: bump(step)
    Keys.onLeftPressed: bump(-step)

    TFocusRing { visible: root.activeFocus }

    component StepButton: T.AbstractButton {
        id: btn
        property string iconName: ""
        width: 30; height: root.height
        hoverEnabled: true
        autoRepeat: true
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            radius: Theme.radiusControl - 1
            color: Theme.stateLayer
            opacity: !btn.enabled ? 0 : btn.down ? Theme.pressedOpacity : btn.hovered ? Theme.hoverOpacity : 0
        }
        contentItem: Item {
            TIcon {
                anchors.centerIn: parent
                name: btn.iconName
                size: 13
                color: btn.enabled ? Theme.text : Theme.borderStrong
            }
        }
    }

    StepButton {
        id: minus
        x: 0
        iconName: "minus"
        enabled: root.value > root.from
        Accessible.name: qsTr("Kevesebb")
        onClicked: root.bump(-root.step)
    }
    Rectangle { x: 30; width: 1; height: parent.height; color: Theme.border }
    TLabel {
        id: label
        x: 31
        width: root.width - 62
        height: root.height
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: root.textFor(root.value)
        mono: true
        font.pixelSize: Theme.fontSmall
        font.weight: Theme.weightMedium
    }
    Rectangle { x: root.width - 31; width: 1; height: parent.height; color: Theme.border }
    StepButton {
        x: root.width - 30
        iconName: "plus"
        enabled: root.value < root.to
        Accessible.name: qsTr("Több")
        onClicked: root.bump(root.step)
    }
}
