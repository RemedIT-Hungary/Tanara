import QtQuick
import QtQuick.Window

// A lebegő felvevő ablaka: keret nélküli, (ahol a platform engedi) mindig felül, 380 px
// széles, a magassága a tartalmat követi; pirula-módban ~230×40. A tartalom a RecorderView /
// RecorderPill elem. A C++ oldali gazda (tanara_qml::RecorderWindowHost) hozza létre, az
// kezeli a pozíciót, az él-illesztést, a háttérbe küldést és a bezárást.
Window {
    id: win

    property alias vm: model
    property alias expanded: view.expanded
    property alias sheetOpen: view.sheetOpen
    property bool pinned: true
    property bool pinSupported: true
    property bool pill: false

    signal hideRequested()            // háttérbe (tálcára)
    signal closeRequested()           // bezárás (felvétel közben nem jön)
    signal userMoved()                // a felhasználó áthelyezte (pozíció-mentés, él-illesztés)

    title: qsTr("Tanara felvevő")
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint | (pinned ? Qt.WindowStaysOnTopHint : 0)
    width: pill ? pillItem.implicitWidth : view.implicitWidth
    height: pill ? pillItem.implicitHeight : view.implicitHeight
    // Fix méret (keret nélküli ablak, nem méretezhető): a korlátok ugyanazt követik, mint a méret.
    minimumWidth: pill ? pillItem.implicitWidth : view.implicitWidth
    maximumWidth: minimumWidth
    minimumHeight: pill ? pillItem.implicitHeight : view.implicitHeight
    maximumHeight: minimumHeight

    // A kérdés (R06) és a bezárás-lap a teljes nézetben él: pirulából ilyenkor kinyitunk.
    function showFull() { win.pill = false }

    // Az ablakkezelő felőli bezárás (Alt+F4, tálca „Bezárás”): felvétel közben SOHA nem zár
    // és nem állít le — a bezárás-lapot nyitja.
    onClosing: (close) => {
        close.accepted = false
        win.showFull()
        view.requestClose()
    }
    onXChanged: moveDebounce.restart()
    onYChanged: moveDebounce.restart()
    Timer { id: moveDebounce; interval: 250; onTriggered: win.userMoved() }

    RecorderViewModel {
        id: model
        onAskRaised: win.showFull()
    }

    RecorderView {
        id: view
        visible: !win.pill
        vm: model
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
        vm: model
        onMoveRequested: win.startSystemMove()
        onExpandRequested: win.pill = false
    }
}
