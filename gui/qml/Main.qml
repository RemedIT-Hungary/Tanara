import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Tanara — főablak (héj-váz). Szerkezet (design/handoff/README.md, „Window structure”):
//   menüsor (36) · oldalsáv (276) · tartalom · lejátszó (52)
// A régiókat külön fájlban élő komponensek töltik ki; itt csak az elrendezés és a
// „melyik nézet látszik” drótozás él. A szeleteket építő agentek a shellState / taskRunning
// property-ket kötik valódi állapotra (nézetmodellből), és a komponenseknek adnak property-ket.
ApplicationWindow {
    id: window

    // "empty" (M01) | "noSelection" (M02) | "preTranscript" (M03–M05) | "meeting" (fülek)
    property string shellState: "meeting"
    // Fut-e megszakítható háttérfeladat a kijelölt megbeszélésen (→ TaskStrip).
    property bool taskRunning: false
    // 0 = Átirat, 1 = Összefoglaló, 2 = Sávok
    property alias currentTab: tabs.currentIndex

    readonly property bool hasMeeting: shellState === "meeting" || shellState === "preTranscript"

    width: 1280
    height: 820
    minimumWidth: 960
    minimumHeight: 600
    title: "Tanara"
    color: Theme.bg
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody

    Shortcut { sequences: [StandardKey.Quit]; onActivated: Qt.quit() }
    Shortcut { sequence: "Alt+F"; onActivated: fileMenu.open() }
    Shortcut { sequence: "Alt+N"; onActivated: viewMenu.open() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 1. Menüsor (natív ablakkeret marad; a menük a címsor stílusában) ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.titleBarHeight
            color: Theme.surface

            RowLayout {
                anchors { fill: parent; leftMargin: 14; rightMargin: 6 }
                spacing: 8

                Rectangle {
                    implicitWidth: 14; implicitHeight: 14
                    radius: Theme.radiusLane
                    color: Theme.accent
                }
                TButton {
                    id: fileButton
                    text: qsTr("Fájl")
                    variant: "ghost"; size: "small"
                    leftPadding: 7; rightPadding: 7
                    font.weight: Theme.weightRegular
                    down: pressed || fileMenu.visible
                    onClicked: fileMenu.open()
                    TMenu {
                        id: fileMenu
                        y: fileButton.height + 2
                        TMenuItem {
                            text: qsTr("Kilépés")
                            shortcutText: "Ctrl+Q"
                            onTriggered: Qt.quit()
                        }
                    }
                }
                TButton {
                    id: viewButton
                    text: qsTr("Nézet")
                    variant: "ghost"; size: "small"
                    leftPadding: 7; rightPadding: 7
                    font.weight: Theme.weightRegular
                    down: pressed || viewMenu.visible
                    onClicked: viewMenu.open()
                    TMenu {
                        id: viewMenu
                        y: viewButton.height + 2
                        TMenuItem {
                            text: qsTr("Téma: a rendszer szerint")
                            iconName: "monitor-speaker"
                            checked: App.themeMode === "system"
                            onTriggered: App.themeMode = "system"
                        }
                        TMenuItem {
                            text: qsTr("Világos téma")
                            iconName: "sun"
                            checked: App.themeMode === "light"
                            onTriggered: App.themeMode = "light"
                        }
                        TMenuItem {
                            text: qsTr("Sötét téma")
                            iconName: "moon"
                            checked: App.themeMode === "dark"
                            onTriggered: App.themeMode = "dark"
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            TDivider { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- 2. Oldalsáv ----
            Rectangle {
                Layout.preferredWidth: Theme.sidebarWidth
                Layout.fillHeight: true
                color: Theme.surface

                LibrarySidebar { anchors { fill: parent; rightMargin: 1 } }
                TDivider { vertical: true; anchors { top: parent.top; bottom: parent.bottom; right: parent.right } }
            }

            // ---- 3–7. Tartalom ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                EmptyLibraryView {
                    visible: window.shellState === "empty"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                NoSelectionView {
                    visible: window.shellState === "noSelection"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                // Fejléc + feladat-sáv + fülek: 16/24/0 belső margó, 12 térköz.
                ColumnLayout {
                    visible: window.hasMeeting
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space5
                    Layout.rightMargin: Theme.space5
                    Layout.topMargin: Theme.space4
                    spacing: Theme.space3

                    MeetingHeader { Layout.fillWidth: true }
                    TaskStrip {
                        visible: window.taskRunning
                        Layout.fillWidth: true
                    }
                    TTabBar {
                        id: tabs
                        visible: window.shellState === "meeting"
                        Layout.fillWidth: true
                        TTabButton { text: qsTr("Átirat") }
                        TTabButton { text: qsTr("Összefoglaló") }
                        TTabButton { text: qsTr("Sávok") }
                    }
                }

                StackLayout {
                    visible: window.shellState === "meeting"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabs.currentIndex
                    TranscriptTab {}
                    SummaryTab {}
                    TracksTab {}
                }
                PreTranscriptView {
                    visible: window.shellState === "preTranscript"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                Rectangle {
                    visible: window.hasMeeting
                    Layout.fillWidth: true
                    implicitHeight: Theme.playerHeight
                    color: Theme.surface

                    PlayerBar { anchors { fill: parent; topMargin: 1 } }
                    TDivider { anchors { left: parent.left; right: parent.right; top: parent.top } }
                }
            }
        }
    }
}
