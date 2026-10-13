import QtQuick
import QtQuick.Layouts

// One track row - ONE component, two modes:
//  * compact (Overview > TRACKS): 26 px round play/pause · source icon + name 13/600 · subtext
//    ("speech 38% · Kovács Lilla") · 30x18 switch (whether the track is in the mixdown; for a
//    left-out track "Include") · an 18 px speech strip below.
//  * full ("Track details", TracksTab): the same with a 40 px strip, + rename (pencil / double
//    click), raw device name, "Locate..." for a missing file, "Restore" for a dropped track,
//    permanent delete (with confirmation).
// Listening plays the track in BOTH modes, in meeting time (PlayerController::playTrack): the play
// button starts at the first speech (if there is activity); clicking the strip seeks there.
//
// As a Repeater delegate the model row (TrackListModel) comes from the required index / model:
//   Repeater { model: tracks; TrackRow { compact: true; tracks: tracks; player: player; activity: activity } }
// tracks: TrackListModel · player: PlayerController (or null) · activity: TrackActivityModel ·
// shell: ShellActions (file picker, confirmation, toast; full mode) · sideWho: who speaks on
// each side (OverviewViewModel.sideWho; empty: not shown).
Item {
    id: root

    required property int index
    required property var model

    property bool compact: false
    property var tracks: null
    property var player: null
    property var activity: null
    property var shell: null
    property var sideWho: ({})
    property real nameWidth: 300
    property bool first: index === 0

    // ---- row data (from the model row's roles) ----
    readonly property string trackId: model.trackId || ""
    readonly property string groupId: model.groupId || trackId
    readonly property bool missing: !!model.missing
    readonly property bool active: !!model.active
    readonly property bool included: !!model.included
    readonly property string excludedReason: model.excludedReason || ""
    readonly property bool excluded: excludedReason !== ""
    // A silent track dropped after recording that neither the speech check nor a manual exclusion set aside.
    readonly property bool dropped: !active && !excluded
    readonly property bool dim: excluded || !active

    // ---- playback ----
    readonly property bool previewing: player !== null && player.previewTrackId !== ""
                                       && player.previewTrackId === groupId
    readonly property bool playingNow: previewing && player.previewPlaying
    readonly property bool canPlay: !missing && player !== null

    readonly property string subText: {
        if (missing) return qsTr("hiányzik a hangfájl")
        if (excludedReason === "noSpeech") return qsTr("nincs beszéd · kimaradt a lekeverésből")
        if (excludedReason === "manual") return qsTr("kivetted a lekeverésből")
        if (!active) return qsTr("eldobott · csendes")
        const ratio = model.speechRatio
        if (ratio === undefined || ratio < 0) return qsTr("beszéd-ellenőrzés…")
        const who = root.sideWho ? (root.sideWho[model.kind] || "") : ""
        return qsTr("beszéd %1%").arg(Math.round(ratio * 100)) + (who !== "" ? " · " + who : "")
    }

    function togglePlay() {
        if (!root.canPlay) return
        if (root.previewing) { root.player.toggleTrack(root.groupId); return }
        // First listen: start where there is actually speech.
        const at = root.activity ? root.activity.nextSpeechMs(root.groupId, 0) : -1
        root.player.playTrack(root.groupId, at)
    }
    function seek(ms) {
        if (root.canPlay) root.player.playTrack(root.groupId, ms)
    }
    function setIncluded(on) {
        if (root.tracks) root.tracks.setIncluded(root.index, on)
    }
    // The file picker / confirmation runs a nested event loop: the selection may switch to another
    // meeting meanwhile. Remember the meeting and the track id BEFORE it.
    function locate() {
        if (!root.shell || !root.tracks) return
        const meeting = root.tracks.meetingId
        const id = root.trackId
        const file = root.shell.pickAudioFile()
        if (!file) return
        const error = root.tracks.relocateTrack(meeting, id, file)
        root.shell.toast(error !== "" ? error : qsTr("A sáv hangfájlja a megbeszélés mappájába került."))
    }
    function deleteTrack() {
        if (!root.shell || !root.tracks) return
        const meeting = root.tracks.meetingId
        const id = root.trackId
        const name = model.displayName
        if (!root.shell.confirm(qsTr("Végleg törlöd a sávot?"),
                                qsTr("A(z) „%1” sáv hangfájlja véglegesen törlődik a lemezről, és a sáv kikerül a megbeszélésből. Ez nem vonható vissza.").arg(name),
                                qsTr("Végleges törlés"), true))
            return
        if (root.tracks.meetingId !== meeting) {
            root.shell.toast(qsTr("Közben másik megbeszélésre váltottál, ezért semmi nem törlődött."))
            return
        }
        // If this very track is playing, release the file first.
        if (root.player && root.previewing) root.player.stopPreview()
        if (root.tracks.deleteTrackIn(meeting, id)) root.shell.toast(qsTr("A sáv törölve."))
    }

    // ---- rename (full mode) ----
    property bool editing: false
    onEditingChanged: if (!editing) Qt.callLater(root.releaseHiddenFocus)
    function releaseHiddenFocus() {
        const it = root.Window.activeFocusItem
        if (it && !it.visible) root.forceActiveFocus()
    }
    function beginRename() {
        if (root.compact) return
        nameField.text = model.displayName
        root.editing = true
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function commitRename() {
        if (!root.editing) return
        root.editing = false
        if (nameField.text.trim() !== model.displayName && root.tracks)
            root.tracks.rename(root.index, nameField.text.trim())
    }

    objectName: root.compact ? "overviewTrack" : "detailTrack"
    implicitHeight: root.compact ? compactCol.implicitHeight + 16 : 72

    Rectangle {
        visible: !root.first
        width: parent.width; height: 1
        color: Theme.border
    }
    HoverHandler { id: hover }

    // Shared pieces (the inline component cannot see the `root` id: `r` is the row).
    component PlayButton: TIconButton {
        property var r: null
        objectName: "trackPlay"
        iconName: r.playingNow ? "pause" : "play"
        enabled: r.canPlay
        opacity: r.dim ? 0.75 : 1
        toolTipText: r.missing ? qsTr("A hangfájl hiányzik")
                   : r.playingNow ? qsTr("Szünet") : qsTr("Sáv meghallgatása (az első beszédtől)")
        onClicked: r.togglePlay()
    }
    component IncludeControl: Item {
        property var r: null
        implicitWidth: includeButton.visible ? includeButton.implicitWidth
                     : includeSwitch.visible ? includeSwitch.implicitWidth : 0
        implicitHeight: 28
        TButton {
            id: includeButton
            objectName: "trackInclude"
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            visible: r.excluded && !r.missing
            text: qsTr("Beemelem")
            size: "small"
            toolTipText: qsTr("A sáv visszakerül a lekeverésbe (utána újrakeverés)")
            onClicked: r.setIncluded(true)
        }
        RecorderSwitch {
            id: includeSwitch
            objectName: "trackSwitch"
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            visible: !r.excluded && r.active && !r.missing
            checked: r.included
            toolTipText: qsTr("A lekeverésben")
            onToggled: r.setIncluded(checked)
        }
    }
    component Strip: TrackActivityStrip {
        property var r: null
        activity: r.activity
        trackId: r.groupId
        compact: r.compact
        dimmed: r.dim
        color: Theme.accent
        positionMs: r.previewing ? r.player.previewMeetingMs : -1
        onSeekRequested: (ms) => r.seek(ms)
    }

    // ================= compact (Overview) =================
    ColumnLayout {
        id: compactCol
        visible: root.compact
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: 10; rightMargin: 10; topMargin: 8 }
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PlayButton {
                r: root
                implicitWidth: 26; implicitHeight: 26
                radius: 13
                iconSize: 11
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                opacity: root.dim ? 0.6 : 1
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    TIcon {
                        name: model.kind === "loopback" && model.iconName === "monitor-speaker" ? "phone" : (model.iconName || "audio-lines")
                        size: 12
                        color: Theme.textMuted
                    }
                    TLabel {
                        Layout.fillWidth: true
                        text: model.displayName || ""
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightSemiBold
                        elide: Text.ElideRight
                    }
                }
                TLabel {
                    objectName: "trackSub"
                    Layout.fillWidth: true
                    text: root.subText
                    color: root.missing ? Theme.dangerInk : root.excluded ? Theme.warnInk : Theme.textMuted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            IncludeControl { r: root }
        }
        Strip {
            r: root
            visible: !root.missing
            Layout.fillWidth: true
            Layout.leftMargin: 36
            Layout.preferredHeight: 18
        }
    }

    // ================= full ("Track details") =================
    RowLayout {
        visible: !root.compact
        anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
        spacing: 14
        PlayButton {
            r: root
            implicitWidth: 32; implicitHeight: 32
            radius: 16
            iconSize: 13
        }
        TIcon {
            name: model.iconName || "audio-lines"
            size: 16
            color: Theme.textMuted
            opacity: root.dim ? 0.75 : 1
        }

        // ---- name + state + raw device name ----
        ColumnLayout {
            Layout.preferredWidth: root.nameWidth
            Layout.minimumWidth: root.nameWidth
            Layout.maximumWidth: root.nameWidth
            spacing: 2
            opacity: root.dim ? 0.75 : 1

            RowLayout {
                visible: !root.editing
                Layout.fillWidth: true
                spacing: 8
                TLabel {
                    Layout.maximumWidth: root.nameWidth - (pill.visible ? pill.implicitWidth + 8 : 0)
                                         - (renameButton.visible ? 30 : 0)
                    text: model.displayName || ""
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                    TapHandler { onDoubleTapped: root.beginRename() }
                }
                TPill {
                    id: pill
                    visible: root.missing || root.dropped
                    text: root.missing ? qsTr("hiányzik a fájl") : qsTr("eldobott · csendes")
                    tone: root.missing ? "danger" : "neutral"
                }
                TIconButton {
                    id: renameButton
                    objectName: "trackRename"
                    visible: hover.hovered || activeFocus
                    iconName: "pencil"
                    iconSize: 13
                    variant: "flat"
                    implicitWidth: 22; implicitHeight: 22
                    iconColor: Theme.textMuted
                    toolTipText: qsTr("Sáv átnevezése")
                    onClicked: root.beginRename()
                }
                Item { Layout.fillWidth: true }
            }
            TTextField {
                id: nameField
                visible: root.editing
                Layout.fillWidth: true
                implicitHeight: 26
                leftPadding: 6; rightPadding: 6
                font.weight: Theme.weightSemiBold
                placeholderText: model.friendlyName || ""
                Accessible.name: qsTr("A sáv neve")
                Keys.onReturnPressed: root.commitRename()
                Keys.onEnterPressed: root.commitRename()
                Keys.onEscapePressed: root.editing = false
                onActiveFocusChanged: if (!activeFocus) root.commitRename()
            }
            TLabel {
                objectName: "trackSub"
                visible: !root.editing
                Layout.fillWidth: true
                text: root.subText
                color: root.missing ? Theme.dangerInk : root.excluded ? Theme.warnInk : Theme.textMuted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
            TLabel {
                Layout.fillWidth: true
                text: root.editing ? qsTr("Enter: mentés · Esc: mégse · üresen: „%1”").arg(model.friendlyName || "")
                                   : (model.rawName || "")
                mono: !root.editing
                muted: true
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }
        }

        Strip {
            r: root
            visible: !root.missing
            Layout.fillWidth: true
            Layout.preferredHeight: 40
        }
        Item { visible: root.missing; Layout.fillWidth: true }

        TLabel {
            Layout.preferredWidth: 52
            text: model.durationText || ""
            mono: true; muted: true
            font.pixelSize: Theme.fontCaption
            horizontalAlignment: Text.AlignRight
            opacity: root.dim ? 0.75 : 1
        }
        // Action by state: switch / Include / Restore / Locate...
        Item {
            Layout.preferredWidth: 110
            implicitHeight: 28
            IncludeControl {
                r: root
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                visible: !root.dropped && !root.missing
            }
            TButton {
                objectName: root.missing ? "trackLocate" : "trackRestore"
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                visible: root.missing || root.dropped
                text: root.missing ? qsTr("Megkeresés…") : qsTr("Visszaállítás")
                size: "small"
                onClicked: {
                    if (root.missing) root.locate()
                    else if (root.tracks) root.tracks.restore(root.index)
                }
            }
        }
        TIconButton {
            objectName: "trackDelete"
            variant: "flat"
            iconName: "trash-2"
            iconSize: 14
            implicitWidth: 28; implicitHeight: 28
            iconColor: Theme.textMuted
            enabled: root.shell !== null
            toolTipText: qsTr("A sáv végleges törlése (a hangfájl is)")
            onClicked: root.deleteTrack()
        }
    }
}
