import QtQuick
import QtQuick.Window

// The floating recorder's window: frameless, (where the platform allows) always on top, 380 px
// wide, height follows the content; in pill mode ~230x40. The content is the RecorderView /
// RecorderPill item. The C++ host (tanara_qml::RecorderWindowHost) creates it and handles the
// position, edge snapping, sending to the background and closing. The host also creates the view
// model (it hands the recorder facade to its constructor) and passes it in as `vm`.
Window {
    id: win

    required property RecorderViewModel vm
    property alias expanded: view.expanded
    property alias sheetOpen: view.sheetOpen
    property bool pinned: true
    property bool pinSupported: true
    property bool pill: false

    signal hideRequested()            // to the background (tray)
    signal closeRequested()           // close (does not come while recording)
    signal userMoved()                // the user moved it (position save, edge snapping)

    title: qsTr("Tanara felvevő")
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint | (pinned ? Qt.WindowStaysOnTopHint : 0)
    width: pill ? pillItem.implicitWidth : view.implicitWidth
    height: pill ? pillItem.implicitHeight : view.implicitHeight
    // Fixed size (frameless window, not resizable): the limits follow the same as the size.
    minimumWidth: pill ? pillItem.implicitWidth : view.implicitWidth
    maximumWidth: minimumWidth
    minimumHeight: pill ? pillItem.implicitHeight : view.implicitHeight
    maximumHeight: minimumHeight

    // The question and the close sheet live in the full view: a pill expands then.
    function showFull() { win.pill = false }

    // Close from the window manager (Alt+F4, taskbar "Close"): while recording it NEVER closes or
    // stops -- it opens the close sheet.
    onClosing: (close) => {
        close.accepted = false
        win.showFull()
        view.requestClose()
    }
    onXChanged: moveDebounce.restart()
    onYChanged: moveDebounce.restart()
    Timer { id: moveDebounce; interval: 250; onTriggered: win.userMoved() }

    Connections {
        target: win.vm
        function onAskRaised() { win.showFull() }
    }

    RecorderView {
        id: view
        visible: !win.pill
        vm: win.vm
        pinned: win.pinned
        pinSupported: win.pinSupported
        onPinnedChanged: win.pinned = pinned
        onMoveRequested: win.startSystemMove()
        onPillRequested: win.pill = true
        onHideRequested: win.hideRequested()
        onCloseRequested: win.closeRequested()
    }
    RecorderPill {
        id: pillItem
        visible: win.pill
        vm: win.vm
        onMoveRequested: win.startSystemMove()
        onExpandRequested: win.pill = false
    }
}
