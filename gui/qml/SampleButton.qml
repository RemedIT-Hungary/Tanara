import QtQuick

// Round play / stop button for a participant's voice sample (a span of one track in meeting
// time: the row's sampleTrackId / sampleStartMs / sampleEndMs, PlayerController.playRange).
// Visible only when the row has a sample and there is a player. Same look as the Tracks row's
// per-track play (26 px, outline, round).
//   SampleButton { player: root.player; row: modelData }
TIconButton {
    id: control

    property var player: null
    property var row: ({})

    readonly property string trackId: (row && row.sampleTrackId) || ""
    readonly property real startMs: row && row.sampleStartMs !== undefined ? row.sampleStartMs : -1
    readonly property real endMs: row && row.sampleEndMs !== undefined ? row.sampleEndMs : -1
    readonly property bool hasSample: trackId !== "" && startMs >= 0 && endMs > startMs
    readonly property string key: trackId + "#" + startMs + "-" + endMs
    readonly property bool active: player !== null && player.rangeKey === key
    readonly property bool playingNow: active && player.rangePlaying

    objectName: "participantSamplePlay"
    visible: hasSample && player !== null
    implicitWidth: 26; implicitHeight: 26
    radius: 13
    iconSize: 11
    iconName: playingNow ? "square" : "play"
    toolTipText: playingNow ? qsTr("Minta leállítása")
                            : qsTr("Minta meghallgatása (%1 s)").arg(Math.max(1, Math.round((endMs - startMs) / 1000)))
    onClicked: player.playRange(trackId, startMs, endMs)
}
