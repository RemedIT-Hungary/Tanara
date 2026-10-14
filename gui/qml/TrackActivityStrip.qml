import QtQuick

// Speech strip: one track's timeline over the meeting's full length - speech frames (>= the
// track's speech level - 10 dB) in the track's color, silence a thin baseline, empty where the
// track has no sound (started later / gaps between segments). The playhead is a vertical line.
// Click / drag = seek there and play ON THIS track (seekRequested(ms), meeting time); a time
// bubble on hover. Compact (18 px) in the Overview's right column, large (40 px) in "Track
// details". While the activity is still computing / absent: a gray, pulsing strip.
//   TrackActivityStrip { activity: activityModel; trackId: model.groupId; positionMs: ...; onSeekRequested: (ms) => ... }
Item {
    id: root

    property var activity: null              // TrackActivityModel
    property string trackId: ""
    property bool compact: false
    property int durationMs: activity ? activity.durationMs : 0
    property int positionMs: -1              // the playhead (meeting time); -1 = none
    property color color: Theme.accent
    property bool dimmed: false              // left-out / dropped track: fainter

    signal seekRequested(int ms)

    readonly property bool loading: !item.hasData && activity !== null && activity.state === "loading"
    readonly property bool hovered: mouse.containsMouse

    function msAt(x) {
        if (root.durationMs <= 0 || root.width <= 0) return 0
        return Math.round(Math.max(0, Math.min(1, x / root.width)) * root.durationMs)
    }
    function timeText(ms) { return ShellFormat.clock(ms) }

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
            // Pulses while computing (static in screenshots).
            SequentialAnimation on opacity {
                running: root.loading && root.visible
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.45; duration: 700; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.45; to: 1; duration: 700; easing.type: Easing.InOutSine }
                onRunningChanged: if (!running) item.opacity = 1
            }
        }
    }

    // ---- hover: faint marker + time bubble ----
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

    // ---- playhead ----
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
