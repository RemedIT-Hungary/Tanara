import QtQuick

// Beszéd-csík (G6): egy sáv idővonala a megbeszélés teljes hosszán — a beszédes keretek
// (≥ a sáv beszédszintje − 10 dB) a sáv színével, a csend vékony alapvonallal, ahol a sávnak
// nincs hangja (később indult / szakaszok közti rés), üres. A lejátszás-fej függőleges vonal.
// Kattintás / húzás = ugrás oda és lejátszás EZEN a sávon (seekRequested(ms), megbeszélés-idő);
// hoverre idő-buborék. Kompakt (18 px) az Áttekintés jobb oszlopában, nagy (40 px) a „Sávok
// részletei"-ben. Ha az aktivitás még számol / nincs: szürke, lüktető csík.
//   TrackActivityStrip { activity: activityModel; trackId: model.groupId; positionMs: …; onSeekRequested: (ms) => … }
Item {
    id: root

    property var activity: null              // TrackActivityModel
    property string trackId: ""
    property bool compact: false
    property int durationMs: activity ? activity.durationMs : 0
    property int positionMs: -1              // a lejátszás-fej (megbeszélés-idő); -1 = nincs
    property color color: Theme.accent
    property bool dimmed: false              // kimaradt / eldobott sáv: halványabb

    signal seekRequested(int ms)

    readonly property bool loading: !item.hasData && activity !== null && activity.state === "loading"
    readonly property bool hovered: mouse.containsMouse

    function msAt(x) {
        if (root.durationMs <= 0 || root.width <= 0) return 0
        return Math.round(Math.max(0, Math.min(1, x / root.width)) * root.durationMs)
    }
    function timeText(ms) {
        const s = Math.floor(ms / 1000)
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
        const mm = h > 0 && m < 10 ? "0" + m : "" + m
        return (h > 0 ? h + ":" : "") + mm + ":" + (sec < 10 ? "0" : "") + sec
    }

    objectName: "trackActivityStrip"
    implicitHeight: compact ? 18 : 40
    implicitWidth: 120

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLane
        color: Theme.sunken
    }
    Item {
        anchors { fill: parent; topMargin: root.compact ? 3 : 5; bottomMargin: root.compact ? 3 : 5 }
        opacity: root.dimmed ? 0.55 : 1
        TrackActivityItem {
            id: item
            anchors.fill: parent
            model: root.activity
            trackId: root.trackId
            durationMs: root.durationMs
            color: root.color
            quietColor: Theme.borderStrong
            emptyColor: Theme.border
            // Számolás közben lüktet (a képernyőképen állókép).
            SequentialAnimation on opacity {
                running: root.loading && root.visible
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.45; duration: 700; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.45; to: 1; duration: 700; easing.type: Easing.InOutSine }
                onRunningChanged: if (!running) item.opacity = 1
            }
        }
    }

    // ---- hover: halvány jelölő + idő-buborék ----
    Rectangle {
        visible: mouse.containsMouse && root.durationMs > 0
        x: Math.round(mouse.mouseX)
        width: 1; height: parent.height
        color: Theme.textMuted
        opacity: 0.6
    }
    Item {
        id: hoverAnchor
        x: Math.round(mouse.mouseX)
        width: 1; height: parent.height
        TToolTip {
            objectName: "trackActivityTime"
            visible: mouse.containsMouse && root.durationMs > 0
            delay: 0
            text: root.timeText(root.msAt(mouse.mouseX))
        }
    }

    // ---- lejátszás-fej ----
    Rectangle {
        objectName: "trackActivityPlayhead"
        visible: root.positionMs >= 0 && root.durationMs > 0
        x: Math.round(root.width * Math.max(0, Math.min(1, root.positionMs / Math.max(1, root.durationMs)))) - 1
        width: 2; height: parent.height
        radius: 1
        color: Theme.text
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.durationMs > 0
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        onPressed: (ev) => root.seekRequested(root.msAt(ev.x))
        onPositionChanged: (ev) => { if (pressed) root.seekRequested(root.msAt(ev.x)) }
    }
}
