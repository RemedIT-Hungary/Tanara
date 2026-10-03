import QtQuick

// Kapcsoló-sor (Beállítások): a felvevő 30×18-as kapcsolója (RecorderSwitch) + 14/500 felirat
// + opcionális 13 px-es magyarázat; alá további tartalom tehető (pl. léptető).
//   SettingsSwitchRow { text: qsTr("Induljon el a bejelentkezéskor"); checked: vm.watcherAutostart
//                       onToggled: (on) => vm.watcherAutostart = on }
// large: a „fő” kapcsoló (38×22), 15/600 címmel.
Item {
    id: root

    property string text: ""
    property string helper: ""
    property bool checked: false
    property bool large: false
    default property alias extra: extraCol.data
    signal toggled(bool checked)

    implicitWidth: 400
    implicitHeight: Math.max(sw.height + 2, textCol.implicitHeight)

    // A nagy kapcsoló ugyanaz a rajz 38×22-ben (a RecorderSwitch mérete rögzített).
    Rectangle {
        id: bigSwitch
        visible: root.large
        width: 38; height: 22; radius: 11
        y: 1
        color: root.checked ? Theme.accent : "transparent"
        border.width: 1
        border.color: root.checked ? Theme.accent : Theme.borderStrong
        activeFocusOnTab: root.large
        Accessible.role: Accessible.CheckBox
        Accessible.name: root.text
        Accessible.checked: root.checked
        Keys.onSpacePressed: root.toggled(!root.checked)
        Keys.onReturnPressed: root.toggled(!root.checked)
        Rectangle {
            width: 16; height: 16; radius: 8
            y: 3
            x: root.checked ? 19 : 3
            color: root.checked ? Theme.textOnAccent : Theme.borderStrong
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
        TFocusRing { visible: bigSwitch.activeFocus; targetRadius: 11 }
        TapHandler { onTapped: { bigSwitch.forceActiveFocus(); root.toggled(!root.checked) } }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
    RecorderSwitch {
        id: sw
        visible: !root.large
        y: 1
        checked: root.checked
        Accessible.name: root.text
        onToggled: {
            root.toggled(checked)
            checked = Qt.binding(function() { return root.checked })
        }
    }

    Column {
        id: textCol
        x: (root.large ? 38 + 14 : 30 + 12)
        width: root.width - x
        spacing: root.large ? 4 : 2
        TLabel {
            width: parent.width
            text: root.text
            font.pixelSize: root.large ? 15 : Theme.fontBody
            font.weight: root.large ? Theme.weightSemiBold : Theme.weightMedium
            wrapMode: Text.Wrap
            TapHandler { onTapped: root.toggled(!root.checked) }
        }
        TLabel {
            visible: root.helper !== ""
            width: parent.width
            text: root.helper
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
        Column {
            id: extraCol
            width: parent.width
            topPadding: children.length > 0 ? 6 : 0
            spacing: 8
        }
    }
}
