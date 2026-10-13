import QtQuick

// TRACKS list on the Overview: raised, border, radius 8. A row is the compact mode of the shared
// TrackRow: play / pause on the track (from the first speech) · source icon + name · subtext
// ("speech 38% · Emma Carter") · mixdown switch / "Mix in" · below it an 18 px speech stripe
// (a click seeks there and plays on the track). The "Details" panel (TracksTab) uses the same
// component in full mode (rename, locate, restore, permanent delete).
// tracks: TrackListModel · player: PlayerController · sideWho: OverviewViewModel.sideWho
Rectangle {
    id: root

    property var tracks: null
    property var player: null
    property var sideWho: ({})

    implicitHeight: column.implicitHeight
    radius: Theme.radiusPopup
    color: Theme.raised
    border.width: 1
    border.color: Theme.border

    // Data of the speech stripes (the core's track activity; if missing, it computes in the background).
    TrackActivityModel {
        id: activityModel
        meetingId: root.tracks ? root.tracks.meetingId : ""
        demoState: root.tracks ? root.tracks.demoState : ""
    }

    Column {
        id: column
        width: parent.width
        Repeater {
            model: root.tracks
            TrackRow {
                width: column.width
                compact: true
                tracks: root.tracks
                player: root.player
                activity: activityModel
                sideWho: root.sideWho
            }
        }
    }
}
