import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// „Átirat" fül: az átirat-szerkesztő (3a). Felül eszköztár + áttekintő, alatta a
// megszólalások listája; bal oldalt a bekapcsolható beszélő-sín (soronkénti átsorolás:
// kattintás másik oszlopba, húzás, több sor kijelölése + alsó sáv / 1–9 billentyű). A névre
// kattintva a teljes beszélő javítható. Az állapotot a TranscriptEditorViewModel adja.
//
// Képernyőképhez (kitalált mintaadat, lásd TranscriptDemoSession):
//   demoVariant: "" | "two" | "many" | "long" | "novoice" | "none"
//   demoState:   "" | "rail" | "playing" | "selection" | "suggestion" | "suggestionShown" |
//                "filter" | "search" | "searchEmpty" | "speakerPopover" | "personPicker" |
//                "drag" | "expanded"
Item {
    id: root

    // ---- a szelet-szerződés bemenetei (CONTRACT.md) ----
    property string meetingId: ""      // selected meeting; "" = none
    property var player: null          // PlayerController or null
    property var shell: null           // ShellActions or null

    property string demoVariant: ""
    property string demoState: ""

    // ---- a sorok (TranscriptRow) ezt olvassák ----
    readonly property bool railShown: editorVm.railVisible && editorVm.hasTranscript
    readonly property real railX: 12
    readonly property real railWidth: railHeader.implicitWidth
    readonly property real textX: railShown ? railX + railWidth + 1 : 0
    readonly property int laneCount: editorVm.lanes.length
    readonly property bool groupShown: railHeader.groupShown
    property bool dragActive: false
    property int dragFromRow: -1
    property int dragToRow: -1
    property int dragLane: -1
    readonly property int dragColorIndex: dragLane >= 0 && dragLane < laneCount ? editorVm.lanes[dragLane].colorIndex : 0
    property int speakerPopoverRow: -1
    readonly property bool canPlay: !!player && player.available !== false

    // ---- lejátszó ----
    readonly property int playerPosition: (player && player.positionMs) || 0
    readonly property bool playerActive: !!player && !(player.previewPath) && (player.playing === true || playerPosition > 0)
    onPlayerPositionChanged: editorVm.setPlaybackPosition(playerPosition, playerActive)
    onPlayerActiveChanged: editorVm.setPlaybackPosition(playerPosition, playerActive)
    // A lejátszás követése: csak akkor görgetünk, ha az előző lejátszott sor látszott (a
    // felhasználó nem görgetett el), vagy ugrás történt (keresősáv, időbélyeg).
    property int lastPlayingRow: -1
    property bool followNext: false

    property real viewportStart: 0
    property real viewportSize: 0
    property string pickerTarget: "participant"     // "participant" | "selection"
    property int pendingRevealRow: -1

    focus: true

    function rowClicked(index, modifiers) {
        root.forceActiveFocus()
        editorVm.selectRow(index, (modifiers & Qt.ControlModifier) !== 0, (modifiers & Qt.ShiftModifier) !== 0)
    }
    function playFrom(ms) {
        if (!player || ms < 0) return
        followNext = true
        player.seek(ms)
        if (player.playing !== true) player.play()
    }
    function playLine(startMs, endMs) {
        if (player) player.playRange(startMs, endMs)
    }
    function revealRow(row, mode) {
        if (row < 0 || row >= list.count) return
        if (list.height <= 0) { pendingRevealRow = row; return }    // még nincs méret: később
        list.positionViewAtIndex(row, mode === undefined ? ListView.Center : mode)
        updateViewport()
    }
    function isRowVisible(row) {
        const item = row >= 0 ? list.itemAtIndex(row) : null
        if (!item) return false
        const top = item.y - list.contentY
        return top + item.height > 0 && top < list.height
    }
    function updateViewport() {
        if (list.count === 0 || list.height <= 0) { viewportStart = 0; viewportSize = 0; return }
        let first = list.indexAt(list.width / 2, list.contentY + 1)
        let last = list.indexAt(list.width / 2, list.contentY + list.height - 2)
        if (first < 0) first = 0
        if (last < 0) last = list.count - 1
        const start = editorVm.rowStartFraction(first)
        viewportStart = start
        viewportSize = Math.max(0, editorVm.rowEndFraction(last) - start)
    }
    function openSpeakerPopover(speakerKey, anchorItem, rowIndex) {
        const p = anchorItem.mapToItem(root, 0, anchorItem.height)
        speakerPopover.speakerKey = speakerKey
        speakerPopover.x = Math.round(p.x - 7)
        speakerPopover.y = Math.round(p.y + 6)
        speakerPopoverRow = rowIndex === undefined ? -1 : rowIndex
        speakerPopover.open()
    }
    function openPicker(target, anchorItem, above) {
        pickerTarget = target
        const p = anchorItem.mapToItem(root, 0, 0)
        picker.x = Math.round(p.x)
        picker.y = Math.round(above ? p.y - picker.implicitHeight - 8 : p.y + anchorItem.height + 2)
        picker.open()
    }

    TranscriptEditorViewModel {
        id: editorVm
        meetingId: root.meetingId
        demoVariant: root.demoVariant
        highlightColor: Theme.warnSoft
        highlightCurrentColor: Theme.warnLine
        timelineMs: (root.player && root.player.durationMs) || 0

        onRevealRequested: row => root.revealRow(row)
        onNotice: text => { if (root.shell && root.shell.toast) root.shell.toast(text) }
        onOverviewChanged: Qt.callLater(root.updateViewport)
        onSessionChanged: {
            root.lastPlayingRow = -1
            root.speakerPopoverRow = -1
            speakerPopover.close()
            picker.close()
            toolbar.searchOpen = false
            Qt.callLater(root.updateViewport)
        }
        onPlayingRowChanged: {
            const row = playingRow
            if (row >= 0) {
                const jumped = root.lastPlayingRow < 0 || Math.abs(row - root.lastPlayingRow) > 1
                if (root.followNext || jumped || root.isRowVisible(root.lastPlayingRow))
                    list.positionViewAtIndex(row, ListView.Contain)
                root.followNext = false
            }
            root.lastPlayingRow = row
        }
    }

    // A Shell kérése (pl. az összefoglaló időbélyeg-linkje): görgetés az adott időhöz.
    Connections {
        target: root.shell
        ignoreUnknownSignals: true
        function onTranscriptPositionRequested(ms) {
            if (editorVm.uncertainOnly) editorVm.uncertainOnly = false
            root.followNext = true
            root.revealRow(editorVm.rowForTime(ms))
        }
    }

    // ---- billentyűk ----
    Shortcut {
        sequences: ["Ctrl+L"]
        enabled: root.visible && editorVm.hasTranscript
        onActivated: editorVm.railVisible = !editorVm.railVisible
    }
    Shortcut {
        sequences: ["Ctrl+Z"]
        enabled: root.visible && editorVm.canUndo
        onActivated: editorVm.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        enabled: root.visible && editorVm.canRedo
        onActivated: editorVm.redo()
    }
    Keys.onPressed: event => {
        if (!editorVm.hasTranscript) return
        const plain = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            const row = editorVm.stepSelection(event.key === Qt.Key_Down ? 1 : -1)
            if (row >= 0) list.positionViewAtIndex(row, ListView.Contain)
            event.accepted = true
        } else if (plain && event.key >= Qt.Key_1 && event.key <= Qt.Key_9 && editorVm.selectedCount > 0) {
            editorVm.moveSelectionToLane(event.key - Qt.Key_1)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape && editorVm.selectedCount > 0) {
            editorVm.clearSelection()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape && toolbar.searchOpen) {
            toolbar.closeSearch()
            event.accepted = true
        } else if (plain && event.key === Qt.Key_Space && root.player) {
            root.player.toggle()
            event.accepted = true
        } else if (plain && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && editorVm.currentRow >= 0) {
            root.playFrom(editorVm.rowStartMs(editorVm.currentRow))
            event.accepted = true
        }
    }

    // ---- nincs átirat ----
    Column {
        visible: !editorVm.hasTranscript
        anchors.centerIn: parent
        spacing: 10
        TIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "file-text"; size: 28; color: Theme.borderStrong }
        TLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, root.width - 48)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: editorVm.legacyTranscript
                  ? qsTr("Ez az átirat régebbi formátumú, ezért itt nem szerkeszthető. Újra-átírás után a megszólalások itt jelennek meg.")
                  : qsTr("Ehhez a megbeszéléshez még nincs átirat.")
            muted: true
        }
        TButton {
            visible: editorVm.legacyTranscript && !!root.shell && root.meetingId !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            iconName: "rotate-ccw"
            text: qsTr("Újra-átírás…")
            onClicked: root.shell.retranscribe(root.meetingId)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: editorVm.hasTranscript
        spacing: 0

        TranscriptToolbar {
            id: toolbar
            Layout.fillWidth: true
            vm: editorVm
            viewportStart: root.viewportStart
            viewportSize: root.viewportSize
            onSeekRequested: fraction => {
                const ms = editorVm.timeAtFraction(fraction)
                root.revealRow(editorVm.rowForTime(ms), ListView.Beginning)
                if (root.player) { root.followNext = false; root.player.seek(ms) }
            }
            onSearchStepRequested: direction => editorVm.searchStep(direction)
        }

        // Rögzített fejléc: a sín oszlopfejei + a megszólalás-számláló + a hang-elemzés állapota.
        Rectangle {
            id: headRow
            Layout.fillWidth: true
            implicitHeight: root.railShown ? railHeader.implicitHeight : 26
            color: Theme.surface

            SpeakerRailHeader {
                id: railHeader
                visible: root.railShown
                x: root.railX
                vm: editorVm
                addOpen: picker.opened && root.pickerTarget === "participant"
                onSpeakerClicked: (speakerKey, anchor) => root.openSpeakerPopover(speakerKey, anchor)
                onAddClicked: root.openPicker("participant", addItem)
            }
            Rectangle {
                visible: root.railShown
                x: root.textX - 1
                width: 1
                height: parent.height
                color: Theme.border
            }
            TLabel {
                id: countLabel
                x: root.textX + 24
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.railShown ? 8 : 5
                text: qsTr("%1 megszólalás · %2 beszélő").arg(editorVm.utteranceCount).arg(editorVm.speakerCount)
                muted: true
                font.pixelSize: Theme.fontCaption
            }

            // Hang-elemzés: futás közben halk folyamatjelző; ha nem érhető el, egy rövid jelzés.
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: countLabel.verticalCenter
                spacing: 8
                visible: editorVm.embeddingRunning
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Hangelemzés… %1%").arg(Math.round(editorVm.embeddingProgress * 100))
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
                TProgressBar {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 72
                    value: editorVm.embeddingProgress
                }
            }
            Row {
                id: voiceNote
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: countLabel.verticalCenter
                spacing: 6
                visible: !editorVm.embeddingRunning && editorVm.voiceNote !== ""
                TIcon { anchors.verticalCenter: parent.verticalCenter; name: "info"; size: 13; color: Theme.textMuted }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Hangelemzés nélkül")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
                HoverHandler { id: voiceNoteHover }
                TToolTip { visible: voiceNoteHover.hovered; text: editorVm.voiceNote; delay: 150 }
            }
            TDivider { anchors.bottom: parent.bottom; width: parent.width }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                objectName: "transcriptList"
                anchors.fill: parent
                model: editorVm.rows
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                cacheBuffer: 800
                footer: Item { width: 1; height: 16 }
                T.ScrollBar.vertical: TScrollBar {}
                delegate: TranscriptRow {
                    tab: root
                    vm: editorVm
                }
                onContentYChanged: root.updateViewport()
                onHeightChanged: {
                    if (height > 0 && root.pendingRevealRow >= 0) {
                        const row = root.pendingRevealRow
                        root.pendingRevealRow = -1
                        Qt.callLater(root.revealRow, row)
                    }
                    root.updateViewport()
                }
                onCountChanged: Qt.callLater(root.updateViewport)

                // A sín egérkezelése egy közös rétegben (a húzás több soron át is tarthat):
                //  kattintás a sor saját blokkjára → kijelölés (Ctrl / Shift: több sor)
                //  kattintás másik oszlopba        → a sor (vagy a kijelölés) átkerül oda
                //  kattintás a „+" oszlopba         → személyválasztó erre a sorra
                //  húzás                            → az érintett sorok a cél-oszlopba
                MouseArea {
                    id: railArea
                    visible: root.railShown
                    x: root.railX
                    width: root.railWidth
                    height: list.height
                    z: 2
                    preventStealing: true

                    property int pressRow: -1
                    property int pressLane: -9
                    property real pressX: 0
                    property real pressY: 0
                    property real lastY: 0

                    function rowAt(y) {
                        return list.indexAt(list.width / 2, list.contentY + Math.max(1, Math.min(list.height - 2, y)))
                    }
                    // Oszlop az x alapján: >= 0 beszélő; -1 „+N" csoport; -3 „+"; -9 semmi.
                    function laneAt(x) {
                        let lx = x - 4
                        if (lx < 0) return -9
                        const n = root.laneCount
                        if (lx < n * Theme.laneWidth) return Math.floor(lx / Theme.laneWidth)
                        lx -= n * Theme.laneWidth
                        if (railHeader.groupShown || railHeader.collapseShown) {
                            if (lx < Theme.laneGroupWidth) return -1
                            lx -= Theme.laneGroupWidth
                        }
                        return lx < Theme.laneWidth ? -3 : -9
                    }
                    function resetDrag() {
                        root.dragActive = false
                        root.dragFromRow = -1
                        root.dragToRow = -1
                        root.dragLane = -1
                        pressRow = -1
                    }
                    function updateDrag(x, y) {
                        const row = rowAt(y)
                        if (row >= 0) root.dragToRow = row
                        const lane = laneAt(x)
                        root.dragLane = lane >= 0 ? lane : -1
                        dragChip.x = Math.min(list.width - dragChip.width - 12, railArea.x + x + 14)
                        dragChip.y = Math.max(2, Math.min(list.height - dragChip.height - 2, y - 26))
                    }

                    onPressed: mouse => {
                        root.forceActiveFocus()
                        pressRow = list.indexAt(list.width / 2, list.contentY + mouse.y)
                        pressLane = laneAt(mouse.x)
                        pressX = mouse.x
                        pressY = mouse.y
                        lastY = mouse.y
                        if (pressRow < 0 || editorVm.rowStartMs(pressRow) < 0) {
                            pressRow = -1
                            mouse.accepted = false
                        }
                    }
                    onPositionChanged: mouse => {
                        if (pressRow < 0) return
                        lastY = mouse.y
                        if (!root.dragActive) {
                            if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) < 7) return
                            root.dragFromRow = pressRow
                            root.dragToRow = pressRow
                            root.dragActive = true
                        }
                        updateDrag(mouse.x, mouse.y)
                    }
                    onReleased: mouse => {
                        if (pressRow < 0) return
                        if (root.dragActive) {
                            if (root.dragLane >= 0)
                                editorVm.moveRowsToLane(root.dragFromRow, root.dragToRow, root.dragLane)
                            resetDrag()
                            return
                        }
                        const row = pressRow
                        const lane = pressLane
                        resetDrag()
                        const item = list.itemAtIndex(row)
                        if (!item) return
                        if (lane === item.lane && lane !== -9) {
                            root.rowClicked(row, mouse.modifiers)
                        } else if (lane >= 0) {
                            editorVm.moveRowToLane(row, lane)
                        } else if (lane === -1) {
                            editorVm.lanesExpanded = !editorVm.lanesExpanded
                        } else if (lane === -3) {
                            if (!editorVm.isRowSelected(row)) editorVm.selectOnly(row)
                            root.openPicker("selection", railHeader.addItem)
                        }
                    }
                    onCanceled: resetDrag()

                    // Húzás közben a lista széleinél magától görög.
                    Timer {
                        interval: 30
                        repeat: true
                        running: root.dragActive && railArea.pressed
                        onTriggered: {
                            const edge = 28
                            let step = 0
                            if (railArea.lastY < edge) step = -14
                            else if (railArea.lastY > list.height - edge) step = 14
                            if (step === 0) return
                            const min = list.originY
                            const max = min + Math.max(0, list.contentHeight - list.height)
                            list.contentY = Math.max(min, Math.min(max, list.contentY + step))
                            railArea.updateDrag(railArea.mouseX, railArea.lastY)
                        }
                    }
                }

                // Húzás közbeni lebegő címke: „→ Név".
                Rectangle {
                    id: dragChip
                    visible: root.dragActive && root.dragLane >= 0
                    z: 3
                    width: dragLabel.implicitWidth + 18
                    height: dragLabel.implicitHeight + 6
                    radius: 10
                    color: Theme.accent
                    TLabel {
                        id: dragLabel
                        anchors.centerIn: parent
                        text: root.dragLane >= 0 && root.dragLane < root.laneCount
                              ? "→ " + editorVm.lanes[root.dragLane].name : ""
                        color: Theme.textOnAccent
                        font.pixelSize: Theme.fontCaption
                        font.weight: Theme.weightSemiBold
                    }
                }
            }

            // Üres szűrő.
            TLabel {
                visible: editorVm.uncertainOnly && editorVm.uncertainCount === 0
                anchors.centerIn: parent
                text: qsTr("Nincs bizonytalan sor — minden megszólalás beszélője rendben van.")
                muted: true
            }
        }

        TranscriptSelectionBar {
            id: selectionBar
            objectName: "selectionBar"
            visible: editorVm.selectedCount > 0
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 10
            vm: editorVm
            onNewParticipantRequested: root.openPicker("selection", newParticipantItem, true)
        }
    }

    PersonPicker {
        id: picker
        objectName: "personPicker"
        editor: editorVm
        anonymousText: root.pickerTarget === "participant" ? qsTr("Névtelen résztvevő") : qsTr("Új névtelen résztvevő")
        onPersonChosen: name => {
            if (root.pickerTarget === "participant") editorVm.addParticipant(name)
            else editorVm.moveSelectionToPerson(name)
        }
        onAnonymousChosen: {
            if (root.pickerTarget === "participant") editorVm.addParticipant("")
            else editorVm.moveSelectionToNewParticipant()
        }
        onClosed: root.forceActiveFocus()
    }

    SpeakerPopover {
        id: speakerPopover
        objectName: "speakerPopover"
        editor: editorVm
        onClosed: {
            root.speakerPopoverRow = -1
            root.forceActiveFocus()
        }
    }

    // ---- demó-állapotok (képernyőkép) ----
    Timer {
        id: demoTimer
        interval: 120
        onTriggered: {
            const s = root.demoState
            if (s === "speakerPopover") {
                const item = list.itemAtIndex(0)
                if (item) root.openSpeakerPopover(item.speakerKey, item.nameItem, 0)
            } else if (s === "personPicker") {
                picker.initialQuery = "Bal"
                root.openPicker("participant", railHeader.addItem)
            } else if (s === "drag") {
                root.dragFromRow = 2
                root.dragToRow = 2
                root.dragLane = Math.min(root.laneCount - 1, 3)
                root.dragActive = true
                dragChip.x = list.width - dragChip.width - 20
                dragChip.y = 150
            }
        }
    }
    Component.onCompleted: {
        Qt.callLater(root.updateViewport)
        const s = demoState
        if (s === "") return
        if (s === "playing") {
            editorVm.setPlaybackPosition(77000, true)
        } else if (s === "expanded") {
            editorVm.railVisible = true
            editorVm.lanesExpanded = true
        } else if (s === "speakerPopover" || s === "personPicker" || s === "drag") {
            editorVm.railVisible = true
            demoTimer.start()
        } else {
            editorVm.applyDemoState(s)
        }
    }
}
