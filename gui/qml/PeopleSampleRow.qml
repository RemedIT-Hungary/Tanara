import QtQuick
import QtQuick.Templates as T

// Egy hangminta sora (P01): lejátszás · forrás (eszköz-ikon + név) és alatta a megbeszélés
// teljes címe (törik, nem vágódik) · dátum · hossz · „…” menü, amely a sora alatt nyílik.
Item {
    id: root

    property var sample: ({})
    property bool playing: false
    property bool selected: false
    property bool first: false
    readonly property alias menuOpen: menu.opened

    signal clicked()
    signal playRequested()
    signal newPersonRequested()
    signal moveRequested()
    signal deleteRequested()

    function openMenu() { menu.open() }

    implicitHeight: Math.max(28, textColumn.implicitHeight) + 16

    Rectangle {
        anchors.fill: parent
        color: root.playing ? Theme.accentSoft : root.menuOpen || root.selected ? Theme.raised : "transparent"
    }
    Rectangle { visible: !root.first; width: parent.width; height: 1; color: Theme.border }
    TapHandler { onTapped: root.clicked() }

    T.AbstractButton {
        id: playButton
        objectName: "samplePlay"
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 28; height: 28
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.name: root.playing ? qsTr("Szünet") : qsTr("Minta meghallgatása")
        onClicked: root.playRequested()
        Keys.onReturnPressed: click()
        background: Rectangle {
            radius: 14
            color: root.playing ? Theme.accent : Theme.raised
            border.width: root.playing ? 0 : 1
            border.color: playButton.hovered ? Theme.borderStrong : Theme.border
            TFocusRing { visible: playButton.visualFocus; targetRadius: 14 }
        }
        contentItem: Item {
            TIcon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: root.playing ? 0 : 1
                name: root.playing ? "pause" : "play"
                size: 12
                color: root.playing ? Theme.textOnAccent : Theme.text
            }
        }
        TToolTip { visible: playButton.hovered; text: playButton.Accessible.name }
    }

    Column {
        id: textColumn
        x: 52
        width: parent.width - 52 - 12 - 86 - 12 - 40 - 12 - 28 - 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Row {
            width: parent.width
            spacing: 6
            TIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.sample.icon || "speaker"
                size: 13
                color: Theme.textMuted
            }
            TLabel {
                width: parent.width - 19
                text: root.sample.label || ""
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
                elide: Text.ElideRight
            }
        }
        TLabel {
            width: parent.width
            text: root.sample.meetingTitle || ""
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.35
            wrapMode: Text.Wrap
        }
    }
    TLabel {
        anchors.right: lengthLabel.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 86
        text: root.sample.date || ""
        mono: true
        muted: true
        font.pixelSize: Theme.fontCaption
    }
    TLabel {
        id: lengthLabel
        anchors.right: menuButton.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 40
        horizontalAlignment: Text.AlignRight
        text: root.sample.length || ""
        mono: true
        muted: true
        font.pixelSize: Theme.fontCaption
    }
    TIconButton {
        id: menuButton
        objectName: "sampleMenuButton"
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 28; height: 28
        variant: "flat"
        iconName: "ellipsis"
        iconSize: 14
        iconColor: Theme.textMuted
        toolTipText: qsTr("A minta műveletei")
        onClicked: menu.open()
        Rectangle { z: -1; anchors.fill: parent; radius: Theme.radiusControl; color: Theme.sunken; visible: root.menuOpen }

        TMenu {
            id: menu
            width: 270
            x: menuButton.width + 2 - width
            y: menuButton.height + 6
            TMenuItem {
                implicitHeight: 34
                text: root.playing ? qsTr("Szünet") : qsTr("Meghallgatás")
                iconName: root.playing ? "pause" : "play"
                onTriggered: root.playRequested()
            }
            TMenuItem {
                implicitHeight: 34
                text: qsTr("Új személy ebből a mintából…")
                iconName: "user-plus"
                onTriggered: root.newPersonRequested()
            }
            TMenuItem {
                implicitHeight: 34
                text: qsTr("Áthelyezés másik személyhez…")
                iconName: "arrow-right-left"
                onTriggered: root.moveRequested()
            }
            TMenuSeparator {}
            TMenuItem {
                implicitHeight: 34
                text: qsTr("Minta törlése")
                iconName: "trash-2"
                danger: true
                onTriggered: root.deleteRequested()
            }
        }
    }
}
