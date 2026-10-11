import QtQuick

// SÁVOK lista az Áttekintésen (design/handoff-v3 V1, 7. döntés): raised, keret, sugár 8.
// A sor a közös TrackRow kompakt módja (G6): lejátszás/szünet a sávon (az első beszédtől) ·
// forrás-ikon + név · alszöveg („beszéd 38% · Kovács Lilla”) · lekeverés-kapcsoló / „Beemelem” ·
// alatta 18 px-es beszéd-csík (kattintva oda ugrik és a sávon szól). A „Részletek” panel
// (TracksTab) ugyanezt a komponenst használja teljes módban (átnevezés, megkeresés,
// visszaállítás, végleges törlés).
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

    // A beszéd-csíkok adatai (a core sáv-aktivitása; ha nincs, a háttérben számol).
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
