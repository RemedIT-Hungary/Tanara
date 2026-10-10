import QtQuick
import QtQuick.Layouts

// SÁVOK lista az Áttekintésen (design/handoff-v3 V1, 7. döntés): raised, keret, sugár 8.
// Sor: 26 px-es kerek lejátszás (a sáv előnézete) · forrás-ikon + név 13/600 · alszöveg 11.5
// („beszéd 38% · Kovács Lilla”) · kapcsoló 30×18 = benne van-e a lekeverésben. A beszéd nélküli
// (kimaradt) sor 60 %, warnInk alszöveg „nincs beszéd · kimaradt a lekeverésből” és „Beemelem”.
// A hiányzó fájl megkeresése, a visszaállítás, az átnevezés és a végleges törlés a
// „Részletek” panelen (TracksTab) van.
// tracks: TrackListModel · player: PlayerController · sideWho: OverviewViewModel.sideWho
Rectangle {
    id: root

    property var tracks: null
    property var player: null
    property var sideWho: ({})
    readonly property string previewPath: player ? player.previewPath : ""

    function togglePreview(path) {
        if (!root.player) return
        if (root.previewPath === path) root.player.stopPreview()
        else root.player.playFile(path)
    }

    implicitHeight: column.implicitHeight
    radius: Theme.radiusPopup
    color: Theme.raised
    border.width: 1
    border.color: Theme.border

    Column {
        id: column
        width: parent.width
        Repeater {
            model: root.tracks
            Item {
                id: row
                required property int index
                required property string displayName
                required property string iconName
                required property string path
                required property bool missing
                required property bool active
                required property bool included
                required property real speechRatio
                required property string excludedReason
                required property string kind
                objectName: "overviewTrack"
                readonly property bool excluded: excludedReason !== ""
                readonly property bool previewing: root.previewPath !== "" && root.previewPath === path
                readonly property string subText: {
                    if (missing) return qsTr("hiányzik a hangfájl")
                    if (excludedReason === "noSpeech") return qsTr("nincs beszéd · kimaradt a lekeverésből")
                    if (excludedReason === "manual") return qsTr("kivetted a lekeverésből")
                    if (!active) return qsTr("eldobott · csendes")
                    if (speechRatio < 0) return qsTr("beszéd-ellenőrzés…")
                    const who = root.sideWho ? (root.sideWho[kind] || "") : ""
                    return qsTr("beszéd %1%").arg(Math.round(speechRatio * 100)) + (who !== "" ? " · " + who : "")
                }
                width: column.width
                height: 50

                Rectangle {
                    visible: row.index > 0
                    width: parent.width; height: 1
                    color: Theme.border
                }
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    spacing: 10
                    TIconButton {
                        objectName: "trackPlay"
                        implicitWidth: 26; implicitHeight: 26
                        radius: 13
                        iconName: row.previewing ? "pause" : "play"
                        iconSize: 11
                        enabled: !row.missing
                        toolTipText: row.missing ? qsTr("A hangfájl hiányzik")
                                   : row.previewing ? qsTr("Előnézet leállítása") : qsTr("Sáv meghallgatása")
                        onClicked: root.togglePreview(row.path)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        opacity: row.excluded || !row.active ? 0.6 : 1
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TIcon {
                                name: row.kind === "loopback" && row.iconName === "monitor-speaker" ? "phone" : row.iconName
                                size: 12
                                color: Theme.textMuted
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: row.displayName
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightSemiBold
                                elide: Text.ElideRight
                            }
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: row.subText
                            color: row.missing ? Theme.dangerInk : row.excluded ? Theme.warnInk : Theme.textMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    TButton {
                        objectName: "trackInclude"
                        visible: row.excluded && !row.missing
                        text: qsTr("Beemelem")
                        size: "small"
                        toolTipText: qsTr("A sáv visszakerül a lekeverésbe (utána újrakeverés)")
                        onClicked: root.tracks.setIncluded(row.index, true)
                    }
                    RecorderSwitch {
                        objectName: "trackSwitch"
                        visible: !row.excluded && row.active && !row.missing
                        checked: row.included
                        toolTipText: qsTr("A lekeverésben")
                        onToggled: root.tracks.setIncluded(row.index, checked)
                        TToolTip { visible: parent.hovered; text: qsTr("A lekeverésben") }
                    }
                }
            }
        }
    }
}
