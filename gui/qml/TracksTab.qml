import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Sávok fül (M09): a felvétel hangsávjai érthető forrásnévvel, hullámformával és állapottal
// (eldobott · csendes → visszaállítható; hiányzó fájl → megkereshető), alatta a lekeverés
// sora (kész / elavult → frissíthető / fut % megszakítással), végül az eldobott sávok
// végleges törlése megerősítéssel.
// Képernyőképhez: --qml-prop 'demoState="default|loading|idle"'.
Item {
    id: root

    property string meetingId: ""      // a kijelölt megbeszélés; "" = nincs
    property var player: null          // PlayerController vagy null
    property var shell: null           // ShellActions vagy null
    property string demoState: ""

    readonly property string previewPath: player ? player.previewPath : ""
    readonly property real nameWidth: Math.max(180, Math.min(300, width * 0.3))

    TrackListModel {
        id: tracks
        meetingId: root.meetingId
        demoState: root.demoState
    }

    // A hullámformákat a fül első megjelenésekor (és meeting-váltáskor) kérjük.
    function ensureWaveforms() { if (visible) tracks.requestWaveforms() }
    onVisibleChanged: ensureWaveforms()
    // A következő körben: addigra a modell meetingId-kötése is lefutott (különben a kérés még
    // az ELŐZŐ megbeszélésre menne, és az új sávjai hullámforma nélkül maradnának).
    onMeetingIdChanged: Qt.callLater(ensureWaveforms)
    Component.onCompleted: ensureWaveforms()

    function togglePreview(path) {
        if (!root.player) return
        if (root.previewPath === path) root.player.stopPreview()
        else root.player.playFile(path)
    }
    // A fájlválasztó és a megerősítés beágyazott eseményhurkot futtat: közben a kijelölés
    // másik megbeszélésre válthat (pl. véget ér egy felvétel). Ezért a megbeszélést és a sáv
    // azonosítóját ELŐTTE jegyezzük meg, és a modell csak akkor hajtja végre, ha még egyezik.
    function locate(row) {
        if (!root.shell) return
        const meeting = tracks.meetingId
        const trackId = tracks.trackIdAt(row)
        const file = root.shell.pickAudioFile()
        if (!file) return
        const error = tracks.relocateTrack(meeting, trackId, file)
        root.shell.toast(error !== "" ? error : qsTr("A sáv hangfájlja a megbeszélés mappájába került."))
    }
    function deleteDropped() {
        const n = tracks.droppedCount
        if (n === 0 || !root.shell) return
        const meeting = tracks.meetingId
        if (!root.shell.confirm(qsTr("Végleg törlöd az eldobott sávokat?"),
                                qsTr("%n eldobott sáv hangfájlja véglegesen törlődik a lemezről. Ez nem vonható vissza. Az aktív sávok és a lekeverés megmaradnak.", "", n),
                                qsTr("Végleges törlés"), true))
            return
        if (tracks.meetingId !== meeting) {
            root.shell.toast(qsTr("Közben másik megbeszélésre váltottál, ezért semmi nem törlődött."))
            return
        }
        // Ha épp egy törlendő sáv szól előnézetként, előbb elengedjük a fájlt.
        if (root.player && root.previewPath !== "") root.player.stopPreview()
        const removed = tracks.deleteDroppedIn(meeting)
        if (removed >= 0) root.shell.toast(qsTr("%n eldobott sáv törölve.", "", removed))
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 18 + 24
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: TScrollBar {}

        ColumnLayout {
            id: col
            x: 24; y: 18
            width: flick.width - 48
            spacing: 14

            // ---- sávok ----
            Rectangle {
                visible: tracks.count > 0
                Layout.fillWidth: true
                implicitHeight: rows.implicitHeight + 2
                radius: Theme.radiusPopup
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                Column {
                    id: rows
                    x: 1; y: 1
                    width: parent.width - 2
                    Repeater {
                        model: tracks
                        TrackRow {
                            required property int index
                            required property var model
                            width: rows.width
                            first: index === 0
                            nameWidth: root.nameWidth
                            displayName: model.displayName
                            friendlyName: model.friendlyName
                            renamed: model.renamed
                            rawName: model.rawName
                            iconName: model.iconName
                            active: model.active
                            missing: model.missing
                            durationText: model.durationText
                            peaks: model.peaks
                            peaksState: model.peaksState
                            peakReference: { model.peaks; tracks.peakReference; return tracks.rowReference(index) }
                            colorIndex: model.colorIndex
                            canPlay: !model.missing && (root.player !== null || tracks.demo)
                            playing: root.previewPath !== "" && root.previewPath === model.path
                            onPlayRequested: root.togglePreview(model.path)
                            onRenameRequested: (name) => tracks.rename(index, name)
                            onRestoreRequested: tracks.restore(index)
                            onLocateRequested: root.locate(index)
                        }
                    }
                }
            }
            TLabel {
                visible: tracks.count === 0
                Layout.fillWidth: true
                text: qsTr("Ehhez a megbeszéléshez nem tartozik hangsáv.")
                muted: true
                wrapMode: Text.Wrap
            }

            // ---- lekeverés ----
            Rectangle {
                visible: tracks.count > 0
                Layout.fillWidth: true
                implicitHeight: Math.max(62, mixText.implicitHeight + 24)
                radius: Theme.radiusPopup
                color: "transparent"
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                    spacing: 14
                    TIcon { name: "audio-lines"; size: 16; color: Theme.textMuted }
                    ColumnLayout {
                        id: mixText
                        Layout.preferredWidth: root.nameWidth
                        Layout.minimumWidth: root.nameWidth
                        Layout.maximumWidth: root.nameWidth
                        spacing: 2
                        RowLayout {
                            spacing: 8
                            TLabel { text: qsTr("Lekeverés"); font.weight: Theme.weightSemiBold }
                            TPill {
                                visible: tracks.mixdownState === "stale"
                                text: qsTr("elavult")
                                tone: "warn"
                            }
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: tracks.activeCount === 0
                                    ? qsTr("nincs aktív sáv, amiből készülhetne")
                                : tracks.mixdownState === "stale"
                                    ? qsTr("a sávok változtak azóta; a lejátszó még a régit szólaltatja meg")
                                : tracks.mixdownState === "none"
                                    ? qsTr("még nem készült el; ebből szól a lejátszó és ebből készül az átirat")
                                    : qsTr("%n aktív sávból; ebből szól a lejátszó és ebből készül az átirat", "", tracks.activeCount)
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }

                    // fut: valós százalék (az ffmpeg haladásából)
                    RowLayout {
                        visible: tracks.mixdownState === "running"
                        Layout.fillWidth: true
                        spacing: 10
                        TProgressBar {
                            Layout.fillWidth: true
                            thickness: 6
                            indeterminate: tracks.mixdownPercent < 0
                            value: tracks.mixdownPercent < 0 ? 0 : tracks.mixdownPercent / 100
                        }
                        TLabel {
                            visible: tracks.mixdownPercent >= 0
                            text: tracks.mixdownPercent + "%"
                            mono: true; muted: true
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                    // kész / elavult: a keverék hullámformája
                    WaveformItem {
                        id: mixWave
                        visible: tracks.mixdownState === "ready" || tracks.mixdownState === "stale"
                        Layout.fillWidth: true
                        implicitHeight: 30
                        peaks: tracks.mixdownPeaks
                        loading: tracks.mixdownPeaksLoading
                        color: loading ? Theme.border : Theme.borderStrong
                        opacity: tracks.mixdownState === "stale" ? 0.6 : 1
                    }
                    Item { visible: tracks.mixdownState === "none"; Layout.fillWidth: true }

                    TLabel {
                        visible: tracks.mixdownState === "ready" || tracks.mixdownState === "stale"
                        Layout.preferredWidth: 52
                        text: tracks.mixdownDurationText
                        mono: true; muted: true
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignRight
                    }
                    Item {
                        Layout.preferredWidth: actionRow.implicitWidth
                        Layout.minimumWidth: tracks.mixdownState === "ready" ? 120 : 0
                        implicitHeight: 28
                        Row {
                            id: actionRow
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            TButton {
                                visible: tracks.mixdownState === "running" && tracks.mixdownCancellable
                                text: tracks.mixdownCancelling ? qsTr("Megszakítás…") : qsTr("Megszakítás")
                                enabled: !tracks.mixdownCancelling
                                variant: "ghost"; size: "small"
                                onClicked: if (root.shell) root.shell.cancelJob(root.meetingId, JobKinds.Mixdown)
                            }
                            TLabel {
                                visible: tracks.mixdownState === "running" && !tracks.mixdownCancellable
                                text: qsTr("az átírás részeként")
                                muted: true
                                font.pixelSize: Theme.fontCaption
                                height: 28
                                verticalAlignment: Text.AlignVCenter
                            }
                            TButton {
                                visible: tracks.mixdownState === "stale" || tracks.mixdownState === "none"
                                text: tracks.mixdownState === "stale" ? qsTr("Lekeverés frissítése") : qsTr("Lekeverés készítése")
                                size: "small"
                                enabled: tracks.activeCount > 0
                                onClicked: {
                                    // A lejátszó elengedi a régi keveréket, hogy felülírható legyen.
                                    if (root.player && root.player.playing) root.player.pause()
                                    tracks.refreshMixdown()
                                }
                            }
                        }
                    }
                }
            }

            // ---- eldobott sávok ----
            RowLayout {
                visible: tracks.droppedCount > 0
                Layout.fillWidth: true
                spacing: 8
                TLabel {
                    Layout.fillWidth: true
                    text: qsTr("Az eldobott sávok megmaradnak, amíg végleg nem törlöd őket.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                TButton {
                    text: qsTr("Eldobott sávok végleges törlése")
                    variant: "dangerGhost"; size: "small"
                    iconName: "trash-2"
                    iconSize: 14
                    spacing: 6
                    font.weight: Theme.weightSemiBold
                    onClicked: root.deleteDropped()
                }
            }
        }
    }
}
