import QtQuick

// Split button (S5, "Átírás indítása ▾"): the main part runs the default action, the ▾ part opens the
// caller's menu. Keyboard: Enter / Space on the main part = clicked(); Down on either part =
// menuRequested(). showMenuButton false: a plain button (no ▾).
//   TSplitButton { text: qsTr("Átírás indítása"); iconName: "audio-lines"
//                  onClicked: vm.startDefault("transcript"); onMenuRequested: menu.open() }
Row {
    id: root

    property string text: ""
    property string iconName: ""
    property string variant: "primary"
    property string size: "small"
    property real iconSize: 14
    property string toolTipText: ""
    property string menuToolTipText: qsTr("Hol fusson?")
    property bool showMenuButton: true
    // The objectNames of the two parts (tests, screenshots).
    property string mainObjectName: ""
    property string menuObjectName: ""
    readonly property alias mainButton: main
    readonly property alias menuButton: chevron

    signal clicked()
    signal menuRequested()

    spacing: 1

    TButton {
        id: main
        objectName: root.mainObjectName
        text: root.text
        variant: root.variant
        size: root.size
        iconName: root.iconName
        iconSize: root.iconSize
        enabled: root.enabled
        toolTipText: root.toolTipText
        Keys.onDownPressed: if (root.showMenuButton) root.menuRequested()
        onClicked: root.clicked()
    }
    TButton {
        id: chevron
        objectName: root.menuObjectName
        visible: root.showMenuButton
        width: main.height
        leftPadding: 0
        rightPadding: 0
        variant: root.variant
        size: root.size
        iconName: "chevron-down"
        iconSize: root.iconSize
        enabled: root.enabled
        toolTipText: root.menuToolTipText
        Accessible.name: root.menuToolTipText
        Keys.onDownPressed: root.menuRequested()
        onClicked: root.menuRequested()
    }
}
