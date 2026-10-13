import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// "Transcript" tab: the transcript editor. A toolbar and overview sit on top of the list of
// utterances; an optional speaker rail on the left reassigns lines (click another column, drag,
// or select several lines and use the bottom bar / the 1-9 keys). State comes from
// TranscriptEditorViewModel.
//
// Fix scope (agreed with the owner, deliberately different from the designer spec):
//  - Clicking a line's NAME (or "Someone else said...") moves ONLY THAT LINE by default (the
//    selected lines if it is part of a multi-line selection); moving the whole speaker is an
//    explicit choice.
//  - The WHOLE speaker can be fixed from speaker-level places only: rail header avatar, map dock
//    name, and the "Why them?" link of the line popover (SpeakerWhyPopover).
//  - The line popover (LinePopover) shows candidates with evidence and "Why not X?".
//  - Three views on the toolbar segment: Read, Fix, To review (N). The last one is the focus view
//    (ReviewFocusView): one doubtful line on a pinned card, key-driven verdicts, on the frozen
//    work list (reviewQueue); with 0 lines it shows an empty state plus a recheck button.
//  - After a new person is named, similar lines show on the rail (accent border) and on the map.
//  - A voiceprint is created only on an explicit action: the map's fingerprint mark (own panel),
//    the "Why them?" Samples link, the whole-speaker scope of the line popover, and the notice
//    bar's "Make voiceprint" button after naming a whole speaker who has none yet.
//  - Every reassignment shows a notice bar: what happened + Undo / "N similar lines too" /
//    "All N lines of <source>".
//  - Merging two speakers asks for confirmation with numbers (a single line / selection never asks).
//
// For screenshots (fictional sample data, see TranscriptDemoSession):
//   demoVariant: "" | "two" | "many" | "long" | "novoice" | "none"
//   demoState:   see TranscriptEditorViewModelQueue.cpp and TranscriptDemoSession for the full
//                list of values (rail, playing, selection, suggestion, filter, search,
//                linePopover, speakerPopover, personPicker, drag, mergeConfirm, voiceprint*,
//                review*, sweep*, imentWarning, shiftPreview, ignoredRows, sampleFilter, ...)
//
// Keys (by physical position, TranscriptKeys): 1-9 move the selection to the rail's nth speaker;
// Shift+1-9 / Shift+rail click / Shift+drag = same but "not a sample"; 0 = not identifiable
// (again: identifiable again); J = Looks right (Shift+J: not a sample); M = Someone else...
// (the line popover); B / Shift+B = next / previous line to review; arrows, Space, Enter, Esc,
// Ctrl+C / A / Z; [ / ] = speed down / up (1.0-2.0x).
// In sweep mode (the "Sweep" switch of the Fix view) the keys act on the TARGET LINE (stop >
// trailing window > playing line; no selection needed, the selection does not change): Enter =
// default (suggestion, otherwise Looks right), J, 0, 1-9, M, Shift = not a sample; Right = Later
// (at a stop) / next stop; Left = line start; Shift+Left / Right = previous / next line; Space
// at a stop = continue without a decision.
Item {
    id: root

    // ---- slice contract inputs (CONTRACT.md) ----
    property string meetingId: ""      // selected meeting; "" = none
    property var player: null          // PlayerController or null
    property var shell: null           // ShellActions or null

    property string demoVariant: ""
    property string demoState: ""
    // The shell can disable Ctrl+Z while focus is not in the editor (the tag step undo lives there;
    // see Main.qml tagUndoActive).
    property bool undoAllowed: true
    // Read (plain list) / Fix (rail, markers, review) -- Ctrl+E, toolbar segment. On entry the
    // meeting's remembered rail state decides; Fix turns the rail on.
    property bool fixMode: false
    onFixModeChanged: if (editorVm.hasTranscript && editorVm.railVisible !== fixMode) editorVm.railVisible = fixMode
    // The three views (toolbar segment, View menu): "read" | "fix" | "review". Review is part of Fix
    // (rail, markers, per-line actions; editorVm.uncertainOnly underneath); leaving it restores the
    // mode from before (Read or Fix).
    readonly property string viewMode: reviewShown ? "review" : fixMode ? "fix" : "read"
    property bool reviewEntered: false
    property bool reviewReturnFix: false
    function setViewMode(mode) {
        if (!editorVm.hasTranscript) return
        if (mode === "review") {
            editorVm.uncertainOnly = true
            return
        }
        const fix = mode === "fix"
        if (editorVm.uncertainOnly) {
            root.reviewReturnFix = fix          // onUncertainOnlyChanged restores this
            editorVm.uncertainOnly = false
        }
        root.fixMode = fix
    }
    // Click on the sample meter's "+N now" (MapDock.samplesClicked): Fix filters to those lines.
    function showSessionSamples(speakerKey) {
        if (!editorVm.hasTranscript) return false
        if (root.viewMode !== "fix") root.setViewMode("fix")
        root.forceActiveFocus()
        return editorVm.showSessionSamples(speakerKey)
    }
    // Ctrl+E: Read <-> Fix; from To review (a Fix-like view) back to Read.
    function toggleFixMode() { setViewMode(viewMode === "read" ? "fix" : "read") }
    // "Who was there?" (toolbar): the shell opens the participants dialog.
    signal participantsRequested()

    // ---- Fix mode (the map dock and the toolbar read these) ----
    readonly property var v3DemoStates: ["markers", "fixLinePopover", "reviewGroups", "reviewExpanded",
                                         "contaminatedCore", "speakerWhy", "newPersonSimilar", "newPersonTag",
                                         "imentWarning", "imentLog", "shiftPreview", "ignoredRows", "sampleFilter",
                                         "sweepStop", "sweepContinuous", "sweepSectionDone",
                                         "reviewFocus", "reviewFocusDecided", "reviewFocusIncoming",
                                         "reviewFocusSummary", "reviewFocusAlreadyDecided", "reviewFocusSkipped",
                                         "reviewFocusOpener", "reviewFocusOpenerDoubt", "reviewFocusOffer",
                                         "reviewFocusOfferList"]
    readonly property bool v3Demo: v3DemoStates.indexOf(demoState) >= 0
    // The To review focus view is showing (third segment item = editorVm.uncertainOnly).
    readonly property bool reviewShown: editorVm.uncertainOnly && editorVm.hasTranscript
    // Lines similar to the new person, for the map: [x, w, ...] (0..1); the same ones are in
    // editorVm.overview `marks` (the accent marks of MapDock).
    readonly property var similarMarks: editorVm.similarMarks
    // Column of the just-added person on the rail (-1 = none / collapsed).
    readonly property int newPersonLane: {
        const key = editorVm.newPersonKey
        if (key === "") return -1
        const lanes = editorVm.lanes
        for (let i = 0; i < lanes.length; ++i) if (lanes[i].key === key) return i
        return -1
    }
    // Next to the rail header: what a click does here / what the highlight means.
    readonly property string railNote: reviewShown ? qsTr("Átnézendő: csak a kétes sorok, okok szerint")
        : sweepOn ? qsTr("Söprés: hallgasd végig; a billentyűk a futó (megálló) sorra vonatkoznak · 1–%1 beszélő · J: Jó így · 0: nem azonosítható · Shift: nem minta").arg(Math.min(9, laneCount))
        : editorVm.newPersonKey !== "" && editorVm.similarShown && newPersonLane >= 0
          ? qsTr("új oszlop: %1 · kék keret: hozzá hasonló sorok").arg(editorVm.lanes[newPersonLane].name)
        : root.shiftHeld ? qsTr("Shift: nem minta — a sor átkerül, de a hangja nem tanítja a felismerést")
        : qsTr("másik oszlopra kattintás: áthelyezés · 1–%1: a kijelöltek áthelyezése · Shift: nem minta · 0: nem azonosítható").arg(Math.min(9, laneCount))

    // ---- the rows (TranscriptRow) read these ----
    readonly property bool railShown: editorVm.railVisible && editorVm.hasTranscript
    readonly property real railX: 12
    readonly property real railWidth: railHeader.implicitWidth
    readonly property real textX: railShown ? railX + railWidth + 1 : 0
    readonly property int laneCount: editorVm.lanes.length
    readonly property bool groupShown: railHeader.groupShown
    // Rail column from the x offset to the rail's left edge (rail click in Fix and To review):
    // >= 0 speaker; -1 "+N" group; -3 "+"; -9 nothing.
    function railLaneAt(x) {
        let lx = x - 4
        if (lx < 0) return -9
        const n = laneCount
        if (lx < n * Theme.laneWidth) return Math.floor(lx / Theme.laneWidth)
        lx -= n * Theme.laneWidth
        if (railHeader.groupShown || railHeader.collapseShown) {
            if (lx < Theme.laneGroupWidth) return -1
            lx -= Theme.laneGroupWidth
        }
        return lx < Theme.laneWidth ? -3 : -9
    }
    property bool dragActive: false
    property int dragFromRow: -1
    property int dragToRow: -1
    property int dragLane: -1
    // Hover on the rail (another column for a line): same preview as while dragging (target cell in
    // the speaker's color, "-> Name" label); same behavior as the To review rail.
    property int hoverRow: -1
    property int hoverLane: -1
    readonly property int previewLane: dragActive ? dragLane : hoverLane
    readonly property int previewColorIndex: previewLane >= 0 && previewLane < laneCount ? editorVm.lanes[previewLane].colorIndex : 0
    // Shift is held down ("not a sample" modifier): the preview label reads "-> Name · not a sample",
    // the target cell has a dashed border, button labels get a "· not a sample" suffix. Updated from
    // key events and rail mouse modifiers; reset on focus loss.
    property bool shiftHeld: false
    onActiveFocusChanged: if (!activeFocus && !reviewView.activeFocus) shiftHeld = false
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive) root.shiftHeld = false }
    }
    readonly property string nonSampleSuffix: qsTr(" · nem minta")

    // ---- sweep mode (in the Fix view; the VM's sweep block: TranscriptEditorViewModel) ----
    readonly property bool sweepOn: editorVm.sweepActive && root.viewMode === "fix"
    // The map dock's sweep layer (Main.qml -> MapDock.sweep); empty map: no sweep.
    readonly property var sweepDock: sweepOn ? editorVm.sweepDock : ({})
    onViewModeChanged: if (viewMode !== "fix" && editorVm.sweepActive) editorVm.sweepActive = false
    function setSweep(on) {
        if (on && root.viewMode !== "fix") return false
        editorVm.sweepActive = on
        root.forceActiveFocus()
        Qt.callLater(root.updateViewport)
        return editorVm.sweepActive === on
    }
    // Target line of the sweep keys: stop > trailing window > playing line; otherwise the selected line.
    function sweepTarget() {
        const t = editorVm.sweepTargetRow
        return t >= 0 ? t : editorVm.currentRow
    }
    function sweepDecideAt(row, action, lane) {
        root.forceActiveFocus()
        if (row < 0) return false
        return editorVm.sweepDecide(row, action, lane === undefined ? -1 : lane, root.shiftHeld)
    }
    function sweepOpenOther(row) {
        if (row < 0) return false
        list.positionViewAtIndex(row, ListView.Contain)
        const item = list.itemAtIndex(row)
        if (!item) return false
        root.openLinePopover(row, item.head ? item.nameItem : item)
        return true
    }
    // Jump to a time and play from there (sweep paths: next stop, summary buttons).
    function sweepSeekPlay(ms) {
        if (ms < 0) return
        const row = editorVm.rowForTime(ms)
        if (row >= 0) list.positionViewAtIndex(row, ListView.Contain)
        if (!root.player) return
        root.followNext = true
        root.cursorSync = true
        root.player.seek(ms)
        root.cursorSync = false
        if (root.player.playing !== true) root.player.play()
    }
    // Left: start of the target line (the stop at the line's end stops again).
    function sweepReplay() {
        const row = sweepTarget()
        if (row < 0) return
        editorVm.sweepDecide(row, "replay", -1, false)
        root.sweepSeekPlay(editorVm.rowStartMs(row))
    }
    // Right: start of the next stop (playback sounds from there).
    function sweepJumpNext() {
        const from = Math.max(sweepTarget(), editorVm.playingRow)
        const row = editorVm.sweepNextStopRow(from)
        if (row < 0) {
            if (root.shell && root.shell.toast) root.shell.toast(qsTr("Nincs több megálló."))
            return
        }
        root.sweepSeekPlay(editorVm.rowStartMs(row))
    }
    // [ / ]: speed 1.0-2.0x in quarter steps (shown by the player row and the sweep bar).
    function changeTempo(direction) {
        if (!root.player) return
        const now = root.player.rate || 1.0
        const next = Math.round((now + 0.25 * direction) * 4) / 4
        root.player.rate = Math.max(1.0, Math.min(2.0, next))
    }
    function tempoLabel(r) {
        return String(Math.round(r * 100) / 100).replace(".", Qt.locale().decimalPoint) + "×"
    }
    // Sweep keys (true: consumed). Does nothing outside sweep mode.
    function sweepKey(k) {
        if (!root.sweepOn) return false
        const stop = editorVm.sweepStopRow
        const target = sweepTarget()
        if (k.plain && k.name === "digit") {
            if (k.digit === 0) { if (!k.shift) root.sweepDecideAt(target, "ignore") }
            else root.sweepDecideAt(target, "lane", k.digit - 1)
            return true
        }
        if (k.plain && k.name === "J") { root.sweepDecideAt(target, "confirm"); return true }
        if (k.plain && k.name === "Enter") { root.sweepDecideAt(target, "default"); return true }
        if (k.plain && !k.shift && k.name === "M") { root.sweepOpenOther(target); return true }
        if (k.plain && k.name === "Right") {
            if (k.shift) root.stepLine(1)
            else if (stop >= 0) root.sweepDecideAt(stop, "later")
            else root.sweepJumpNext()
            return true
        }
        if (k.plain && k.name === "Left") {
            if (k.shift) root.stepLine(-1)
            else root.sweepReplay()
            return true
        }
        if (k.plain && !k.shift && k.name === "Space" && stop >= 0) {
            root.sweepDecideAt(stop, "continue")
            return true
        }
        return false
    }
    property int speakerPopoverRow: -1
    readonly property bool canPlay: !!player && player.available !== false

    // ---- player ----
    readonly property int playerPosition: (player && player.positionMs) || 0
    readonly property bool playerActive: !!player && !(player.previewPath) && (player.playing === true || playerPosition > 0)
    onPlayerPositionChanged: editorVm.setPlaybackPosition(playerPosition, playerActive)
    onPlayerActiveChanged: editorVm.setPlaybackPosition(playerPosition, playerActive)
    // Playback following: scroll only if the previously played line was visible (the user did not
    // scroll away), or a jump happened (seek bar, timestamp).
    property int lastPlayingRow: -1
    property bool followNext: false
    // One cursor: the selected line and the player position move together in both directions. While
    // stopped, selecting a line seeks to its start (no playback); while playing, the line under the
    // player becomes selected (a multi-line selection is kept). The cursorSync guard prevents
    // feedback (selection -> seek -> position -> selection).
    property bool cursorSync: false
    // Seek the player to the line start (plain seek; playback continues). If the position is already
    // inside the line, no seek.
    function seekToRow(row) {
        if (!root.player || row < 0 || root.cursorSync) return
        const start = editorVm.rowStartMs(row)
        if (start < 0) return
        const pos = root.playerPosition
        if (root.playerActive && pos >= start && pos < editorVm.rowEndMs(row)) return
        root.cursorSync = true
        root.followNext = true
        root.player.seek(start)
        root.cursorSync = false
    }
    // The line under the position becomes selected (player follow, map jump).
    function selectRowAtCursor(row) {
        if (row < 0 || root.cursorSync || editorVm.selectedCount > 1 || editorVm.currentRow === row) return
        root.cursorSync = true
        editorVm.selectOnly(row)
        root.cursorSync = false
    }

    property real viewportStart: 0
    property real viewportSize: 0
    property string pickerTarget: "participant"     // "participant" | "selection"
    property int pendingRevealRow: -1

    focus: true

    // In a text field (search, person picker) the editor keys do not apply.
    readonly property bool textInputFocused: {
        const it = root.Window.activeFocusItem
        return !!it && (it instanceof TextInput || it instanceof TextEdit)
    }

    function openSearch() { toolbar.openSearch() }
    function releaseHiddenFocus() {
        const it = root.Window.activeFocusItem
        if (it && !it.visible) root.forceActiveFocus()
    }
    // "Next uncertain": steps on from the selected line (otherwise the first visible one). Outside
    // To review it switches there (with 0 lines the empty state shows, no notice).
    function nextUncertain(direction) {
        root.forceActiveFocus()
        if (!root.reviewShown) {
            root.setViewMode("review")      // the focus view starts on the first open line
            return
        }
        if (!reviewView.step(direction) && root.shell) root.shell.toast(qsTr("Nincs átnézendő sor."))
    }
    // Recheck based on the sound of confirmed / fixed lines. Through the shell (confirm dialog +
    // feedback); without a shell (demo, screenshot) directly.
    function recheckSpeakers() {
        if (root.shell && root.shell.recheckSpeakers && root.meetingId !== "") {
            root.shell.recheckSpeakers(root.meetingId)
            return
        }
        const r = editorVm.recheckSpeakers()
        if (!r.ran && r.blocker && root.shell) root.shell.toast(r.blocker)
        else if (r.ran && r.message && root.shell) root.shell.toast(r.message)
    }
    function copySelectionOrCurrent() {
        if (editorVm.selectedCount > 0) editorVm.copySelection()
        else if (editorVm.playingRow >= 0) editorVm.copyRow(editorVm.playingRow)
    }
    function openRowMenu(row, item, x, y) {
        const p = item.mapToItem(root, x, y)
        rowMenu.row = row
        rowMenu.x = Math.round(Math.min(p.x, root.width - rowMenu.implicitWidth - 8))
        rowMenu.y = Math.round(Math.min(p.y, root.height - rowMenu.implicitHeight - 8))
        rowMenu.open()
    }

    function rowClicked(index, modifiers) {
        root.forceActiveFocus()
        const toggle = (modifiers & Qt.ControlModifier) !== 0
        const range = (modifiers & Qt.ShiftModifier) !== 0
        editorVm.selectRow(index, toggle, range)
        // Selecting a line = cursor goes there (stopped: seek; playing: jump and keep playing).
        if (!toggle && !range && editorVm.selectedCount === 1 && editorVm.isRowSelected(index))
            root.seekToRow(index)
    }
    function playFrom(ms) {
        if (!player || ms < 0) return
        followNext = true
        root.cursorSync = true
        editorVm.selectOnly(editorVm.rowForTime(ms))
        root.cursorSync = false
        player.seek(ms)
        if (player.playing !== true) player.play()
    }
    // Listen to one line with a short lead-in / tail, up to the neighbors' boundaries (PlayerController).
    function playLine(startMs, endMs) {
        if (!player) return
        root.cursorSync = true
        if (editorVm.selectedCount <= 1) editorVm.selectOnly(editorVm.rowForTime(startMs))
        root.cursorSync = false
        const b = editorVm.playBounds(startMs, endMs)
        player.playRange(startMs, endMs, b.minStartMs, b.maxEndMs)
    }
    // Previous / next line (map dock buttons, arrows). While playing, steps from the line under the
    // player, seeks to the target's start and keeps playing (plain jump, not playRange); while
    // stopped, selects it, reveals it and puts the player at its start (no playback; Space starts
    // from there). In To review it steps over the groups' lines (like B).
    function stepLine(delta) {
        if (!editorVm.hasTranscript) return -1
        const playing = !!root.player && root.player.playing === true
        if (root.reviewShown) {
            if (!reviewView.step(delta)) return -1
            const id = reviewView.currentUtterance
            const startMs = id !== "" ? editorVm.lineInfo(id).startMs : undefined
            if (root.player && startMs !== undefined) {
                root.cursorSync = true
                root.player.seek(startMs)
                root.cursorSync = false
            }
            return 0
        }
        const r = editorVm.stepLine(delta, playing)
        if (r.row < 0) return -1
        list.positionViewAtIndex(r.row, ListView.Contain)
        if (root.player && r.startMs >= 0) {
            root.cursorSync = true
            root.followNext = playing
            root.player.seek(r.startMs)
            root.cursorSync = false
        }
        return r.row
    }
    function revealRow(row, mode) {
        if (row < 0 || row >= list.count) return
        if (list.height <= 0) { pendingRevealRow = row; return }    // no size yet: later
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
        // Sweep: the frozen window from the visible lines (+-2 lines, in the VM).
        editorVm.setSweepWindow(first, last)
    }
    // Jump to a point on the timeline (map dock click): the list scrolls there, the player seeks.
    // The line under the position becomes selected (playing and stopped).
    function seekToFraction(fraction) {
        const ms = editorVm.timeAtFraction(fraction)
        const row = editorVm.rowForTime(ms)
        root.revealRow(row, ListView.Beginning)
        if (root.player) {
            root.followNext = false
            root.cursorSync = true
            root.player.seek(ms)
            root.cursorSync = false
        }
        root.selectRowAt(ms)
    }
    // Map jump from another tab (Main.qml): select the line under the position.
    function selectRowAt(ms) {
        if (!editorVm.hasTranscript) return
        root.cursorSync = true
        if (editorVm.selectedCount <= 1) editorVm.selectOnly(editorVm.rowForTime(ms))
        root.cursorSync = false
    }
    // The panel goes below the anchor; if it does not fit there (e.g. the name of the map dock
    // above the player), above it.
    function popoverY(anchorItem, popupHeight) {
        const below = anchorItem.mapToItem(root, 0, anchorItem.height).y + 6
        if (below + popupHeight <= root.height) return Math.round(below)
        return Math.round(Math.max(0, anchorItem.mapToItem(root, 0, 0).y - popupHeight - 6))
    }
    // Panel of the WHOLE speaker (rail header avatar, map dock name).
    function openSpeakerPopover(speakerKey, anchorItem, focusTracks) {
        const p = anchorItem.mapToItem(root, 0, anchorItem.height)
        speakerWhy.speakerKey = speakerKey
        speakerWhy.focusTracks = focusTracks === true
        speakerWhy.x = Math.round(Math.max(8, Math.min(p.x - 7, root.width - speakerWhy.width - 8)))
        speakerWhy.y = root.popoverY(anchorItem, speakerWhy.implicitHeight)
        speakerPopoverRow = -1
        speakerWhy.open()
    }
    // "Why them?" from the line popover (opens in the popover's place).
    function openSpeakerWhyAt(speakerKey, x, y, focusTracks) {
        speakerWhy.speakerKey = speakerKey
        speakerWhy.focusTracks = focusTracks === true
        speakerWhy.x = Math.round(Math.max(8, Math.min(x, root.width - speakerWhy.width - 8)))
        speakerWhy.y = Math.round(Math.max(0, y))
        speakerWhy.open()
    }
    // The voiceprint's own panel (from the overview's fingerprint mark).
    function openVoiceprintPopover(speakerKey, anchorItem) {
        const p = anchorItem.mapToItem(root, 0, anchorItem.height)
        voiceprintPopover.speakerKey = speakerKey
        voiceprintPopover.x = Math.round(p.x - 12)
        voiceprintPopover.y = root.popoverY(anchorItem, voiceprintPopover.implicitHeight)
        voiceprintPopover.open()
    }
    // Panel of ONE LINE (the line's name, "Someone else said...", context menu): by default only this
    // line moves; if it is part of a multi-line selection, the selection moves.
    function openLinePopover(row, anchorItem, px, py) {
        const line = editorVm.rowInfo(row)
        if (line.utteranceId === undefined) return
        const p = px === undefined ? anchorItem.mapToItem(root, 0, anchorItem.height)
                                   : anchorItem.mapToItem(root, px, py)
        linePopover.selectionCount = editorVm.selectedCount > 1 && editorVm.isRowSelected(row)
                                     ? editorVm.selectedCount : 0
        showLinePopover(line, p.x - 7, p.y + 6)
        speakerPopoverRow = row
    }
    // A line's panel by line id (from the "Other..." button of To review).
    function openLinePopoverFor(utteranceId, anchorItem) {
        const line = editorVm.lineInfo(utteranceId)
        if (line.utteranceId === undefined) return
        const p = anchorItem.mapToItem(root, 0, anchorItem.height)
        linePopover.selectionCount = 0
        showLinePopover(line, p.x - 7, p.y + 6)
        speakerPopoverRow = -1
    }
    function showLinePopover(line, x, y) {
        linePopover.speakerKey = line.speakerKey
        linePopover.utteranceId = line.utteranceId
        linePopover.lineTime = line.timeLabel
        linePopover.lineStartMs = line.startMs
        linePopover.lineEndMs = line.endMs
        linePopover.x = Math.round(Math.max(8, Math.min(x, root.width - linePopover.width - 8)))
        linePopover.y = Math.round(Math.max(0, Math.min(y, root.height - linePopover.implicitHeight - 8)))
        linePopover.open()
    }
    // A panel opened from another panel opens only after the first one CLOSED (otherwise the closing
    // panel would give focus back to the editor and the new panel's keys would not work).
    property var afterPopupClose: null
    function runAfterPopupClose() {
        if (!afterPopupClose) return false
        const f = afterPopupClose
        afterPopupClose = null
        f()
        return true
    }
    function openVoiceprintFor(key) {
        const mark = toolbar.voiceprintMark(key)
        if (mark) root.openVoiceprintPopover(key, mark)
    }
    // "They were not there": the speaker's lines become anonymous and the "Who was there?" mark is
    // cleared too (ParticipantsViewModel.unbind). If there is no participant (or demo), the speaker
    // simply becomes anonymous -- both paths are one undo step.
    function markNotPresent(speakerKey, fixVoiceprints) {
        const person = editorVm.personOf(speakerKey)
        let participantId = ""
        let participantsVm = null
        if (person !== "" && !editorVm.demo) {
            participantsVm = participantsComponent.createObject(root, { meetingId: root.meetingId })
            const groups = participantsVm ? participantsVm.groups : []
            for (let g = 0; g < groups.length && participantId === ""; ++g)
                for (let r = 0; r < groups[g].rows.length; ++r)
                    if ((groups[g].rows[r].name || "").toLowerCase() === person.toLowerCase()) {
                        participantId = groups[g].rows[r].id
                        break
                    }
        }
        if (fixVoiceprints || participantId === "") editorVm.revertSpeakerToAnonymous(speakerKey, fixVoiceprints)
        if (participantId !== "") participantsVm.unbind(participantId)
        if (participantsVm) participantsVm.destroy()
    }
    // Confirmation before merging two speakers, with numbers. request: { kind: "merge" |
    // "reassign" | "rest", fromKey, intoKey, personName, fix }. Nothing to ask for an empty source.
    function requestMerge(request) {
        const from = editorVm.speakerInfo(request.fromKey)
        const into = editorVm.speakerInfo(request.intoKey)
        if (from.key === undefined || into.key === undefined) return
        if ((from.utteranceCount || 0) === 0) { performMerge(request); return }
        mergeDialog.request = request
        mergeDialog.fromName = from.name
        mergeDialog.fromCount = from.utteranceCount
        mergeDialog.intoName = into.name
        mergeDialog.intoCount = into.utteranceCount
        mergeDialog.open()
    }
    function performMerge(request) {
        if (request.kind === "reassign") editorVm.reassignSpeaker(request.fromKey, request.personName, request.fix === true)
        else if (request.kind === "rest") editorVm.moveRestOfSource()
        else editorVm.mergeSpeakers(request.fromKey, request.intoKey)
    }
    // Keep the just-fixed line visible when the notice bar appears below it.
    property int keepVisibleRow: -1
    function keepRowVisible() {
        if (keepVisibleRow >= 0 && keepVisibleRow < list.count)
            list.positionViewAtIndex(keepVisibleRow, ListView.Contain)
    }
    function openPicker(target, anchorItem, above) {
        pickerTarget = target
        const p = anchorItem.mapToItem(root, 0, 0)
        picker.x = Math.round(p.x)
        picker.y = Math.round(above ? p.y - picker.implicitHeight - 8 : p.y + anchorItem.height + 2)
        picker.open()
    }

    // QA / scripts: access to the view model (hook.findObject("transcriptTab").editor).
    readonly property alias editor: editorVm
    TranscriptEditorViewModel {
        id: editorVm
        meetingId: root.meetingId
        // The demo states run on the fictional "v3" meeting (built with the handoff names).
        demoVariant: root.demoVariant !== "" ? root.demoVariant : root.v3Demo ? "v3" : ""
        highlightColor: Theme.warnSoft
        highlightCurrentColor: Theme.warnLine
        timelineMs: (root.player && root.player.durationMs) || 0

        onRevealRequested: row => root.revealRow(row)
        // Sweep: at a stop the player pauses and the decision bar shows; a decision resumes playback;
        // the end of the section pauses (summary).
        onSweepStopReached: row => {
            if (root.player && root.player.playing === true) root.player.pause()
            Qt.callLater(() => { if (row >= 0 && row < list.count) list.positionViewAtIndex(row, ListView.Contain) })
        }
        onSweepResumeRequested: { if (root.player && root.player.playing !== true) root.player.play() }
        onSweepSectionEnded: { if (root.player && root.player.playing === true) root.player.pause() }
        // The To review view is part of Fix mode (the VM turns it on itself after a recheck); on exit
        // the previous mode returns.
        onUncertainOnlyChanged: {
            if (uncertainOnly && hasTranscript) {
                if (!root.reviewEntered) {
                    root.reviewEntered = true
                    root.reviewReturnFix = root.fixMode
                }
                root.fixMode = true
            } else if (!uncertainOnly && root.reviewEntered) {
                root.reviewEntered = false
                root.fixMode = root.reviewReturnFix
            }
        }
        onChangeChanged: {
            root.keepVisibleRow = changeActive ? changeRow : -1
            if (root.keepVisibleRow >= 0) Qt.callLater(root.keepRowVisible)
        }
        onNotice: text => { if (root.shell && root.shell.toast) root.shell.toast(text) }
        onOverviewChanged: Qt.callLater(root.updateViewport)
        onSessionChanged: {
            root.reviewEntered = false
            root.fixMode = editorVm.railVisible
            root.lastPlayingRow = -1
            root.speakerPopoverRow = -1
            linePopover.close()
            speakerWhy.close()
            voiceprintPopover.close()
            picker.close()
            mergeDialog.close()
            rebehindPopover.close()
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
                // While playing, the line under the player is the selected one (one cursor).
                if (root.player && root.player.playing === true) root.selectRowAtCursor(row)
            }
            root.lastPlayingRow = row
        }
    }

    // Request from the shell (e.g. the summary's timestamp link): scroll to that time.
    Connections {
        target: root.shell
        ignoreUnknownSignals: true
        function onTranscriptPositionRequested(ms) {
            if (editorVm.uncertainOnly) editorVm.uncertainOnly = false
            root.followNext = true
            root.revealRow(editorVm.rowForTime(ms))
        }
    }

    // ---- keys ----
    Shortcut {
        sequences: ["Ctrl+E"]
        enabled: root.visible && editorVm.hasTranscript
        onActivated: root.toggleFixMode()
    }
    Shortcut {
        sequences: ["Ctrl+L"]
        enabled: root.visible && editorVm.hasTranscript
        onActivated: editorVm.railVisible = !editorVm.railVisible
    }
    Shortcut {
        sequences: ["Ctrl+Z"]
        enabled: root.visible && root.undoAllowed && editorVm.canUndo && !root.textInputFocused
        onActivated: editorVm.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        enabled: root.visible && editorVm.canRedo && !root.textInputFocused
        onActivated: editorVm.redo()
    }
    // The selection / To review's selected line: toggle "not identifiable" (0).
    function toggleIgnored() {
        if (root.reviewShown) {
            const id = reviewView.currentUtterance
            if (id === "") return false
            return editorVm.setUtteranceIgnored(id, editorVm.lineInfo(id).ignored !== true)
        }
        if (editorVm.selectedCount === 0) return false
        // If all selected lines are already not identifiable: make them identifiable again; otherwise
        // the opposite.
        let all = true
        for (let r = 0; r < list.count && all; ++r)
            if (editorVm.isRowSelected(r) && editorVm.rowInfo(r).ignored !== true) all = false
        return editorVm.setSelectionIgnored(!all)
    }
    // "Someone else..." (M): the selected line's panel.
    function openFixForCurrent() {
        if (root.reviewShown) return reviewView.fixCurrent()
        const row = editorVm.currentRow
        if (row < 0) return false
        list.positionViewAtIndex(row, ListView.Contain)
        const item = list.itemAtIndex(row)
        if (!item) return false
        root.openLinePopover(row, item.head ? item.nameItem : item)
        return true
    }
    Keys.onReleased: event => {
        if (event.key === Qt.Key_Shift) root.shiftHeld = false
    }
    Keys.onPressed: event => {
        const k = TranscriptKeys.decode(event)
        root.shiftHeld = k.name === "Shift" || k.shift
        if (k.name === "Shift" || !editorVm.hasTranscript) return
        // To review (focus view): its own key map (ReviewFocusView.handleKey) goes first.
        if (root.reviewShown && reviewView.handleKey(k, event)) {
            event.accepted = true
            return
        }
        // Sweep mode: the target line's keys; [ / ]: speed (always).
        if (root.sweepKey(k)) {
            event.accepted = true
            return
        }
        if (k.plain && !k.shift && (k.name === "BracketLeft" || k.name === "BracketRight") && root.player) {
            root.changeTempo(k.name === "BracketRight" ? 1 : -1)
            event.accepted = true
            return
        }
        const ctrlOnly = k.ctrl && !k.alt && !k.meta && !k.shift
        if (ctrlOnly && k.name === "C") {
            root.copySelectionOrCurrent()
            event.accepted = true
        } else if (ctrlOnly && k.name === "A") {
            editorVm.selectAll()
            event.accepted = true
        } else if (k.plain && k.name === "B") {
            // B: next line to review -- in the To review view (switches there if not already).
            root.nextUncertain(k.shift ? -1 : 1)
            event.accepted = true
        } else if (k.plain && k.name === "digit" && k.digit === 0 && !k.shift) {
            // 0: not identifiable (Fix: the selection; To review: the selected line).
            root.toggleIgnored()
            event.accepted = true
        } else if (k.plain && k.name === "digit" && k.digit >= 1 && editorVm.selectedCount > 0 && !root.reviewShown) {
            editorVm.moveSelectionToLane(k.digit - 1, k.shift)
            event.accepted = true
        } else if (k.plain && k.name === "J") {
            // J: Looks right (Shift: not a sample).
            editorVm.confirmSelection(k.shift)
            event.accepted = true
        } else if (k.plain && !k.shift && k.name === "M") {
            root.openFixForCurrent()
            event.accepted = true
        } else if (k.plain && !k.shift && (k.name === "Down" || k.name === "Up" || k.name === "Left" || k.name === "Right")) {
            // Arrows: previous / next line (Read, Fix, To review); the list does not scroll.
            root.stepLine(k.name === "Down" || k.name === "Right" ? 1 : -1)
            event.accepted = true
        } else if (k.name === "Escape" && editorVm.selectedCount > 0) {
            editorVm.clearSelection()
            event.accepted = true
        } else if (k.name === "Escape" && editorVm.sampleFilterKey !== "") {
            editorVm.clearSampleFilter()
            event.accepted = true
        } else if (k.name === "Escape" && toolbar.searchOpen) {
            toolbar.closeSearch()
            event.accepted = true
        } else if (k.plain && k.name === "Space" && root.player) {
            root.player.toggle()
            event.accepted = true
        } else if (k.plain && k.name === "Enter" && editorVm.currentRow >= 0) {
            root.playFrom(editorVm.rowStartMs(editorVm.currentRow))
            event.accepted = true
        }
    }

    // ---- no transcript ----
    Column {
        visible: !editorVm.hasTranscript && !editorVm.legacyTranscript
        anchors.centerIn: parent
        spacing: 10
        TIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "file-text"; size: 28; color: Theme.borderStrong }
        TLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, root.width - 48)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Ehhez a megbeszéléshez még nincs átirat.")
            muted: true
        }
    }

    // ---- old-format transcript: readable (and copyable), but not editable here ----
    ColumnLayout {
        visible: !editorVm.hasTranscript && editorVm.legacyTranscript
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space5
            Layout.rightMargin: Theme.space5
            Layout.topMargin: 12
            Layout.bottomMargin: 10
            spacing: Theme.space3
            TIcon { name: "info"; size: 15; color: Theme.textMuted; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
            TLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                muted: true
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                text: qsTr("Ez az átirat régebbi formátumú: olvasható és másolható, de a beszélők itt nem javíthatók. Az újra-átírás új (a szolgáltatónál díjköteles) átírást indít, és lecseréli a mostani átiratot.")
            }
            TButton {
                objectName: "legacyRetranscribe"
                visible: !!root.shell && root.meetingId !== ""
                Layout.alignment: Qt.AlignTop
                size: "small"
                variant: "ghost"
                iconName: "rotate-ccw"
                text: qsTr("Újra-átírás…")
                onClicked: root.shell.retranscribe(root.meetingId)
            }
        }
        TDivider { Layout.fillWidth: true }
        Flickable {
            id: legacyFlick
            objectName: "legacyTranscript"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: legacyEdit.implicitHeight + 32
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}
            TextEdit {
                id: legacyEdit
                x: Theme.space5
                y: 14
                width: legacyFlick.width - 2 * Theme.space5
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                text: editorVm.legacyText !== "" ? editorVm.legacyText
                                                  : qsTr("Az átirat szövegfájlja (transcript.md) nem található.")
                color: Theme.text
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.text
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontBody
            }
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
            fixMode: root.fixMode
            viewMode: root.viewMode
            viewportStart: root.viewportStart
            viewportSize: root.viewportSize
            onModeRequested: mode => root.setViewMode(mode)
            onParticipantsRequested: root.participantsRequested()
            onSeekRequested: fraction => root.seekToFraction(fraction)
            onSearchStepRequested: direction => editorVm.searchStep(direction)
            onNextUncertainRequested: root.nextUncertain(1)
            onRecheckRequested: root.recheckSpeakers()
            onSpeakerClicked: (speakerKey, anchor) => root.openSpeakerPopover(speakerKey, anchor)
            onVoiceprintClicked: (speakerKey, anchor) => root.openVoiceprintPopover(speakerKey, anchor)
            // After the search closes, the keys belong to the editor again (not the hidden field).
            onSearchOpenChanged: if (!searchOpen) Qt.callLater(root.releaseHiddenFocus)
        }

        // Pinned header: the rail's column heads + the utterance counter + the voice analysis state.
        Rectangle {
            id: headRow
            // Only the rail's column heads (the counter and voice analysis belong to the toolbar); height 0
            // with the rail hidden (not `visible: false`: the layout would update late after the toggle).
            Layout.fillWidth: true
            // Also absent in the To review focus view (the speaker number row is on the card there).
            implicitHeight: root.railShown && !root.reviewShown ? railHeader.implicitHeight : 0
            clip: true
            color: Theme.surface

            TLabel {
                objectName: "railNote"
                visible: root.railShown
                x: root.textX + 12
                width: Math.max(0, filterRow.x - x - 12)
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -4
                text: root.railNote
                elide: Text.ElideRight
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            // List filters at the header's right edge: the sample meter's "+N now" filter (clearable) and
            // hiding / showing the "not identifiable" lines.
            Row {
                id: filterRow
                visible: root.railShown
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -4
                spacing: 12
                // Sweep mode: Fix view only.
                TSwitch {
                    id: sweepToggle
                    objectName: "sweepToggle"
                    visible: root.viewMode === "fix"
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Söprés")
                    font.pixelSize: Theme.fontCaption
                    checked: editorVm.sweepActive
                    onToggled: root.setSweep(checked)
                    TToolTip {
                        visible: sweepToggle.hovered
                        text: qsTr("Hallgatás közben javítani: a lejátszás a kétes sorok végén megáll, a billentyűk a futó sorra vonatkoznak")
                    }
                }
                Row {
                    objectName: "sampleFilterNote"
                    visible: editorVm.sampleFilterKey !== ""
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    TIcon { anchors.verticalCenter: parent.verticalCenter; name: "list-filter"; size: 13; color: Theme.accent }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Szűrő: %1 most lett mintái").arg(editorVm.speakerInfo(editorVm.sampleFilterKey).name || "")
                        color: Theme.accent
                        font.pixelSize: Theme.fontCaption
                        font.weight: Theme.weightMedium
                    }
                    TLabel {
                        objectName: "sampleFilterClear"
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Mind (Esc)")
                        color: Theme.accent
                        font.pixelSize: Theme.fontCaption
                        font.underline: clearHover.hovered
                        HoverHandler { id: clearHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: editorVm.clearSampleFilter() }
                    }
                }
                TLabel {
                    objectName: "ignoredToggle"
                    visible: editorVm.ignoredCount > 0 && editorVm.sampleFilterKey === ""
                    anchors.verticalCenter: parent.verticalCenter
                    text: editorVm.hideIgnored ? qsTr("%n nem azonosítható rejtve · mutasd", "", editorVm.ignoredCount)
                                               : qsTr("%n nem azonosítható · elrejtés", "", editorVm.ignoredCount)
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    font.underline: ignoredHover.hovered
                    HoverHandler { id: ignoredHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: editorVm.hideIgnored = !editorVm.hideIgnored }
                    TToolTip {
                        visible: ignoredHover.hovered
                        text: qsTr("A „nem azonosítható” sorok a hang-elemzésből kimaradnak; itt csak a lista nézetét állítod")
                    }
                }
            }
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
            TDivider { anchors.bottom: parent.bottom; width: parent.width }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                objectName: "transcriptList"
                anchors.fill: parent
                visible: !root.reviewShown
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
                    // When the bar appears the list gets shorter: the fixed line must not slide out.
                    if (height > 0 && root.keepVisibleRow >= 0) Qt.callLater(root.keepRowVisible)
                    root.updateViewport()
                }
                onCountChanged: Qt.callLater(root.updateViewport)

                // Rail mouse handling in one shared layer (a drag can span several lines):
                //  click on a line's own block   -> select (Ctrl / Shift: several lines)
                //  click on another column       -> the line (or the selection) moves there
                //  click on the "+" column       -> person picker for this line
                //  drag                          -> the affected lines go to the target column
                MouseArea {
                    id: railArea
                    visible: root.railShown
                    x: root.railX
                    width: root.railWidth
                    height: list.height
                    z: 2
                    preventStealing: true
                    hoverEnabled: true
                    cursorShape: root.hoverLane >= 0 || root.dragActive ? Qt.PointingHandCursor : Qt.ArrowCursor

                    property int pressRow: -1
                    property int pressLane: -9
                    property real pressX: 0
                    property real pressY: 0
                    property real lastY: 0

                    function rowAt(y) {
                        return list.indexAt(list.width / 2, list.contentY + Math.max(1, Math.min(list.height - 2, y)))
                    }
                    function laneAt(x) { return root.railLaneAt(x) }
                    function resetDrag() {
                        root.dragActive = false
                        root.dragFromRow = -1
                        root.dragToRow = -1
                        root.dragLane = -1
                        pressRow = -1
                    }
                    function placeChip(x, y) {
                        dragChip.x = Math.min(list.width - dragChip.width - 12, railArea.x + x + 14)
                        dragChip.y = Math.max(2, Math.min(list.height - dragChip.height - 2, y - 26))
                    }
                    function updateDrag(x, y) {
                        const row = railArea.rowAt(y)
                        if (row >= 0) root.dragToRow = row
                        const lane = railArea.laneAt(x)
                        root.dragLane = lane >= 0 ? lane : -1
                        railArea.placeChip(x, y)
                    }
                    // Hover: the line and the other column (no preview over its own column / a pause row).
                    function updateHover(x, y) {
                        const row = railArea.rowAt(y)
                        const item = row >= 0 && editorVm.rowStartMs(row) >= 0 ? list.itemAtIndex(row) : null
                        const lane = item ? railArea.laneAt(x) : -9
                        root.hoverRow = item ? row : -1
                        root.hoverLane = item && lane >= 0 && lane !== item.lane ? lane : -1
                        if (root.hoverLane >= 0) railArea.placeChip(x, y)
                    }
                    function clearHover() {
                        root.hoverRow = -1
                        root.hoverLane = -1
                    }

                    onPressed: mouse => {
                        root.forceActiveFocus()
                        pressRow = list.indexAt(list.width / 2, list.contentY + mouse.y)
                        pressLane = railArea.laneAt(mouse.x)
                        pressX = mouse.x
                        pressY = mouse.y
                        lastY = mouse.y
                        if (pressRow < 0 || editorVm.rowStartMs(pressRow) < 0) {
                            pressRow = -1
                            mouse.accepted = false
                        }
                    }
                    onPositionChanged: mouse => {
                        root.shiftHeld = (mouse.modifiers & Qt.ShiftModifier) !== 0
                        if (pressRow < 0) { railArea.updateHover(mouse.x, mouse.y); return }
                        lastY = mouse.y
                        if (!root.dragActive) {
                            if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) < 7) return
                            railArea.clearHover()
                            root.dragFromRow = pressRow
                            root.dragToRow = pressRow
                            root.dragActive = true
                        }
                        railArea.updateDrag(mouse.x, mouse.y)
                    }
                    onExited: railArea.clearHover()
                    onReleased: mouse => {
                        if (pressRow < 0) return
                        // Shift = "not a sample": the line(s) move, but their sound is not a sample.
                        const noisy = (mouse.modifiers & Qt.ShiftModifier) !== 0
                        if (root.dragActive) {
                            if (root.dragLane >= 0)
                                editorVm.moveRowsToLane(root.dragFromRow, root.dragToRow, root.dragLane, noisy)
                            railArea.resetDrag()
                            return
                        }
                        const row = pressRow
                        const lane = pressLane
                        railArea.resetDrag()
                        const item = list.itemAtIndex(row)
                        if (!item) return
                        if (lane === item.lane && lane !== -9) {
                            root.rowClicked(row, mouse.modifiers)
                        } else if (lane >= 0) {
                            editorVm.moveRowToLane(row, lane, noisy)
                        } else if (lane === -1) {
                            editorVm.lanesExpanded = !editorVm.lanesExpanded
                        } else if (lane === -3) {
                            if (!editorVm.isRowSelected(row)) editorVm.selectOnly(row)
                            root.openPicker("selection", railHeader.addItem)
                        }
                        // After a reassignment the line is elsewhere: the preview follows the current mouse position.
                        Qt.callLater(() => { if (railArea.containsMouse) railArea.updateHover(railArea.mouseX, railArea.mouseY) })
                    }
                    onCanceled: railArea.resetDrag()

                    // While dragging, the list scrolls by itself near its edges.
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

                // Floating label while dragging and hovering: [->] Name (move) or [check] Name · stays (the
                // line's own column) -- the same icons as the focus card's buttons.
                // Shift: "... · not a sample", dashed border.
                Rectangle {
                    id: dragChip
                    objectName: "dragChip"
                    visible: root.previewLane >= 0 && (root.dragActive || root.hoverRow >= 0)
                    readonly property bool nonSample: root.shiftHeld
                    readonly property bool keep: {
                        const row = root.dragActive ? root.dragFromRow : root.hoverRow
                        const it = row >= 0 ? list.itemAtIndex(row) : null
                        return !!it && it.lane === root.previewLane
                    }
                    readonly property color ink: nonSample ? Theme.accent : Theme.textOnAccent
                    z: 3
                    width: dragRow.implicitWidth + 16
                    height: dragLabel.implicitHeight + 6
                    radius: 10
                    color: nonSample ? Theme.raised : Theme.accent
                    TDashedRect {
                        anchors.fill: parent
                        visible: dragChip.nonSample
                        radius: parent.radius
                        color: Theme.accent
                        lineWidth: 1.5
                    }
                    Row {
                        id: dragRow
                        anchors.centerIn: parent
                        spacing: 4
                        TIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: dragChip.keep ? "check" : "arrow-right"
                            size: 12
                            strokeWidth: 2.4
                            color: dragChip.ink
                        }
                        TLabel {
                            id: dragLabel
                            objectName: "dragChipLabel"
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.previewLane >= 0 && root.previewLane < root.laneCount
                                  ? editorVm.lanes[root.previewLane].name + (dragChip.keep ? qsTr(" · marad") : "")
                                    + (dragChip.nonSample ? root.nonSampleSuffix : "") : ""
                            color: dragChip.ink
                            font.pixelSize: Theme.fontCaption
                            font.weight: Theme.weightSemiBold
                        }
                    }
                }
            }

            // To review (focus view): replaces the list.
            ReviewFocusView {
                id: reviewView
                objectName: "reviewView"
                anchors.fill: parent
                visible: root.reviewShown
                vm: editorVm
                tab: root
            }
        }

        // Sweep: the summary at the end of a section (above the "Just now" row).
        SweepSectionBar {
            id: sweepSummaryBar
            objectName: "sweepSummary"
            Layout.fillWidth: true
            summary: root.sweepOn ? editorVm.sweepSectionSummary : ({})
            onNextRequested: root.sweepSeekPlay(editorVm.sweepNextSection())
            onBehindRequested: root.sweepSeekPlay(editorVm.sweepReviewBehind())
            onLaterRequested: root.sweepSeekPlay(editorVm.sweepReviewLater())
            onDismissRequested: { editorVm.dismissSweepSummary(); root.forceActiveFocus() }
        }

        // The "Just now" row: below the list, at a FIXED place (always in the Fix / To review views; in
        // Read only after a reassignment). Does not disappear by itself.
        TranscriptChangeBar {
            id: changeBar
            objectName: "changeBar"
            visible: editorVm.hasTranscript && (root.viewMode !== "read" || editorVm.changeActive)
            Layout.fillWidth: true
            Layout.bottomMargin: selectionBar.visible ? 6 : 0
            vm: editorVm
            onRestRequested: root.requestMerge({ kind: "rest", fromKey: editorVm.changeSourceKey,
                                                 intoKey: editorVm.changeTargetKey })
            onVoiceprintPanelRequested: key => root.openVoiceprintFor(key)
        }

        // Sweep: the frozen window, "behind you: N lines doubtful again" (click opens the list), the
        // doubtful lines ahead, the section, the mode (stop / continuous) and the speed -- right next to
        // the player (map dock).
        Rectangle {
            id: sweepStrip
            objectName: "sweepStrip"
            visible: root.sweepOn
            Layout.fillWidth: true
            implicitHeight: visible ? 38 : 0
            color: Theme.surface
            readonly property var section: editorVm.sweepSections[editorVm.sweepSectionIndex] || ({})
            TDivider { anchors { left: parent.left; right: parent.right; top: parent.top } }
            RowLayout {
                anchors { fill: parent; leftMargin: Theme.space5; rightMargin: Theme.space5 }
                spacing: 10
                TIcon { name: "lock"; size: 13; color: Theme.textMuted }
                TLabel {
                    text: qsTr("Fagyasztott ablak")
                    font.pixelSize: Theme.fontCaption
                    font.weight: Theme.weightSemiBold
                }
                TLabel {
                    objectName: "sweepFrozenRange"
                    text: editorVm.sweepFrozenFrom !== "" ? editorVm.sweepFrozenFrom + "–" + editorVm.sweepFrozenTo : ""
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
                Rectangle { width: 1; height: 16; color: Theme.border }
                Rectangle {
                    id: behindChip
                    objectName: "sweepBehind"
                    readonly property int count: editorVm.rebehindCount
                    implicitWidth: behindRow.implicitWidth + 18
                    implicitHeight: 24
                    radius: 12
                    color: count > 0 ? Theme.warnSoft : "transparent"
                    border.width: 1
                    border.color: count > 0 ? Theme.warnLine : Theme.border
                    Row {
                        id: behindRow
                        anchors.centerIn: parent
                        spacing: 5
                        TIcon { anchors.verticalCenter: parent.verticalCenter; name: "rotate-ccw"; size: 12; color: behindChip.count > 0 ? Theme.warnInk : Theme.textMuted }
                        TLabel {
                            objectName: "sweepBehindLabel"
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("mögötted: %n sor újra kétes", "", behindChip.count)
                            color: behindChip.count > 0 ? Theme.warnInk : Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            font.weight: behindChip.count > 0 ? Theme.weightSemiBold : Theme.weightRegular
                        }
                    }
                    HoverHandler { id: behindHover; cursorShape: behindChip.count > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    TapHandler { enabled: behindChip.count > 0; onTapped: root.openRebehindList(behindChip) }
                    TToolTip {
                        visible: behindHover.hovered
                        text: behindChip.count > 0 ? qsTr("A háttér-elemzés ezeket a már elhagyott sorokat jelölte újra kétesnek — kattintásra a lista")
                                                   : qsTr("Ha a háttér-elemzés egy már elhagyott sort újra kétesnek jelöl, itt gyűlik")
                    }
                }
                TLabel {
                    objectName: "sweepAhead"
                    text: qsTr("előtted: %n kétes", "", editorVm.sweepAheadCount)
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
                Item { Layout.fillWidth: true }
                TLabel {
                    objectName: "sweepSectionLabel"
                    visible: sweepStrip.section.index !== undefined
                    text: sweepStrip.section.index === undefined ? ""
                          : qsTr("%1/%2. szakasz · %3 · %n kétes", "", sweepStrip.section.uncertain || 0)
                              .arg(sweepStrip.section.index + 1).arg(editorVm.sweepSections.length)
                              .arg(sweepStrip.section.label)
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
                Rectangle { width: 1; height: 16; color: Theme.border }
                TSwitch {
                    id: continuousToggle
                    objectName: "sweepContinuousToggle"
                    text: qsTr("Folyamatos")
                    font.pixelSize: Theme.fontCaption
                    checked: editorVm.sweepContinuous
                    onToggled: { editorVm.sweepContinuous = checked; root.forceActiveFocus() }
                    TToolTip {
                        visible: continuousToggle.hovered
                        text: qsTr("Kikapcsolva (alap): a kétes sor végén megáll. Bekapcsolva: nem áll meg; az imént hallott kétes sor még 3 mp-ig a billentyűk célpontja")
                    }
                }
                Rectangle { width: 1; height: 16; color: Theme.border }
                TIconButton {
                    objectName: "sweepTempoDown"
                    size: "small"
                    variant: "flat"
                    iconName: "minus"
                    enabled: !!root.player && (root.player.rate || 1) > 1.0
                    toolTipText: qsTr("Lassabban ([)")
                    onClicked: { root.changeTempo(-1); root.forceActiveFocus() }
                }
                TLabel {
                    objectName: "sweepTempo"
                    text: qsTr("tempó %1").arg(root.tempoLabel(root.player ? (root.player.rate || 1.0) : 1.0))
                    mono: true
                    font.pixelSize: Theme.fontCaption
                }
                TIconButton {
                    objectName: "sweepTempoUp"
                    size: "small"
                    variant: "flat"
                    iconName: "plus"
                    enabled: !!root.player && (root.player.rate || 1) < 2.0
                    toolTipText: qsTr("Gyorsabban (])")
                    onClicked: { root.changeTempo(1); root.forceActiveFocus() }
                }
            }
        }

        TranscriptSelectionBar {
            id: selectionBar
            objectName: "selectionBar"
            // In To review the selection is the focus line (the card's buttons decide): no bar.
            visible: editorVm.selectedCount > 0 && !root.reviewShown
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
        anonymousText: root.pickerTarget === "participant" ? qsTr("Névtelen résztvevő")
                     : root.pickerTarget === "speaker" ? qsTr("Névtelen beszélő") : qsTr("Új névtelen résztvevő")
        // "New person..." from the "Why them?" panel: the whole speaker goes to the chosen name.
        property string speakerKey: ""
        property bool fixVoiceprints: false
        onPersonChosen: name => {
            if (root.pickerTarget === "participant") {
                editorVm.addParticipant(name)
            } else if (root.pickerTarget === "speaker") {
                const existing = editorVm.speakerKeyForPerson(name)
                if (existing !== "" && existing !== picker.speakerKey)
                    root.requestMerge({ kind: "reassign", fromKey: picker.speakerKey, intoKey: existing,
                                        personName: name, fix: picker.fixVoiceprints })
                else
                    editorVm.reassignSpeaker(picker.speakerKey, name, picker.fixVoiceprints)
            } else {
                editorVm.moveSelectionToPerson(name)
            }
        }
        onAnonymousChosen: {
            if (root.pickerTarget === "participant") editorVm.addParticipant("")
            else if (root.pickerTarget === "speaker") editorVm.revertSpeakerToAnonymous(picker.speakerKey, picker.fixVoiceprints)
            else editorVm.moveSelectionToNewParticipant()
        }
        onClosed: root.forceActiveFocus()
    }

    // "Whose line is this?": one line / the selection / (on explicit choice) the whole speaker.
    LinePopover {
        id: linePopover
        objectName: "linePopover"
        editor: editorVm
        canListen: root.canPlay
        onListenRequested: (startMs, endMs) => root.playLine(startMs, endMs)
        onMergeRequested: request => root.requestMerge(request)
        onSpeakerWhyRequested: (key, tracks) => {
            const x = linePopover.x, y = linePopover.y
            root.afterPopupClose = () => root.openSpeakerWhyAt(key, x, y, tracks)
        }
        onVoiceprintRequested: key => root.afterPopupClose = () => root.openVoiceprintFor(key)
        onTagAdded: text => { if (root.shell && root.shell.toast) root.shell.toast(text, "tags") }
        onClosed: {
            root.speakerPopoverRow = -1
            if (root.runAfterPopupClose()) return
            if (!mergeDialog.visible) root.forceActiveFocus()
        }
    }

    // "Why them?": the whole speaker -- evidence, rail layout, candidates, "They were not there".
    SpeakerWhyPopover {
        id: speakerWhy
        objectName: "speakerWhyPopover"
        editor: editorVm
        canListen: root.canPlay
        onListenRequested: (startMs, endMs) => root.playLine(startMs, endMs)
        onMergeRequested: request => root.requestMerge(request)
        onVoiceprintRequested: key => root.afterPopupClose = () => root.openVoiceprintFor(key)
        // The "Tags" fix link: the People window scrolled to the Tags section.
        onPeopleRequested: person => { if (root.shell && root.shell.openPeople) root.shell.openPeople(person, "tags") }
        onNewPersonRequested: (key, fix) => {
            const x = speakerWhy.x, y = speakerWhy.y
            root.afterPopupClose = () => {
                picker.speakerKey = key
                picker.fixVoiceprints = fix
                root.pickerTarget = "speaker"
                picker.x = Math.round(Math.max(8, Math.min(x, root.width - picker.width - 8)))
                picker.y = Math.round(Math.max(0, y))
                picker.open()
            }
        }
        onNotPresentRequested: (key, fix) => root.markNotPresent(key, fix)
        onTagAdded: text => { if (root.shell && root.shell.toast) root.shell.toast(text, "tags") }
        onClosed: {
            if (root.runAfterPopupClose()) return
            if (!mergeDialog.visible) root.forceActiveFocus()
        }
    }

    // Sweep: the list of "behind you: N lines doubtful again" -- the line times, click jumps to the line.
    function openRebehindList(anchorItem) {
        const p = anchorItem.mapToItem(root, 0, 0)
        rebehindPopover.x = Math.round(Math.max(8, Math.min(p.x, root.width - rebehindPopover.width - 8)))
        rebehindPopover.y = Math.round(Math.max(0, p.y - rebehindPopover.implicitHeight - 6))
        rebehindPopover.open()
    }
    function jumpToRebehind(entry) {
        rebehindPopover.close()
        if (!entry || entry.row < 0) return
        root.revealRow(entry.row, ListView.Center)
        root.cursorSync = true
        editorVm.selectOnly(entry.row)
        root.cursorSync = false
        if (root.player) {
            root.cursorSync = true
            root.player.seek(entry.startMs)
            root.cursorSync = false
        }
    }
    TPopover {
        id: rebehindPopover
        objectName: "rebehindPopover"
        width: 300
        padding: 0
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
        onClosed: root.forceActiveFocus()
        contentItem: Column {
            topPadding: 10
            bottomPadding: 8
            TLabel {
                x: 14
                width: rebehindPopover.width - 28
                wrapMode: Text.Wrap
                text: qsTr("Újra kétes mögötted (a háttér-elemzés jelölte)")
                font.pixelSize: Theme.fontCaption
                font.weight: Theme.weightSemiBold
                bottomPadding: 6
            }
            Repeater {
                model: rebehindPopover.visible ? editorVm.rebehindRows : []
                Rectangle {
                    id: rebehindItem
                    required property var modelData
                    objectName: "rebehindRow"
                    width: rebehindPopover.width
                    height: 28
                    color: rebehindHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                    Row {
                        x: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        TLabel {
                            text: rebehindItem.modelData.time
                            mono: true
                            color: Theme.accent
                            font.pixelSize: Theme.fontCaption
                        }
                        TLabel {
                            text: rebehindItem.modelData.name
                            color: Theme.speakerInk(rebehindItem.modelData.colorIndex)
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightSemiBold
                        }
                    }
                    HoverHandler { id: rebehindHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.jumpToRebehind(rebehindItem.modelData) }
                }
            }
        }
    }

    // "They were not there": the "Who was there?" mark updates too (the participant binding ends).
    // Created only for the action (so it does not load participants on every meeting switch).
    Component {
        id: participantsComponent
        ParticipantsViewModel {}
    }

    // The voiceprint panel: state, usable material, creation (and undoing the one just made). A
    // voiceprint is created only by the button here.
    TPopover {
        id: voiceprintPopover
        objectName: "voiceprintPopover"
        property string speakerKey: ""
        width: 320
        padding: 0
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
        onAboutToShow: voiceprintPanel.reset()
        onClosed: root.forceActiveFocus()
        contentItem: VoiceprintPanel {
            id: voiceprintPanel
            editor: editorVm
            speakerKey: voiceprintPopover.speakerKey
        }
    }

    // Merging two speakers: confirmation with numbers. Moving a line / selection never asks; this
    // guards only the merge of a whole speaker.
    TDialog {
        id: mergeDialog
        objectName: "mergeDialog"
        property var request: ({})
        property string fromName: ""
        property int fromCount: 0
        property string intoName: ""
        property int intoCount: 0
        title: qsTr("Összevonod a két beszélőt?")
        TLabel {
            objectName: "mergeText"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.5
            text: qsTr("%1 %n sora összeolvad ezzel: %2 (%3). Visszavonható: Ctrl+Z.", "", mergeDialog.fromCount)
                      .arg(mergeDialog.fromName).arg(mergeDialog.intoName)
                      .arg(qsTr("%n sor", "", mergeDialog.intoCount))
        }
        actions: [
            TButton {
                objectName: "mergeCancel"
                text: qsTr("Mégse")
                onClicked: mergeDialog.reject()
            },
            TButton {
                objectName: "mergeAccept"
                text: qsTr("Összevonás")
                variant: "primary"
                onClicked: mergeDialog.accept()
            }
        ]
        onAccepted: root.performMerge(request)
        onClosed: root.forceActiveFocus()
    }

    // The line's context menu (right click): play, copy.
    TMenu {
        id: rowMenu
        objectName: "rowMenu"
        property int row: -1
        readonly property bool inSelection: editorVm.selectedCount > 1 && editorVm.isRowSelected(row)
        // "Someone else said...": the panel opens only AFTER the menu closes (otherwise the menu would
        // take focus back from the panel's search field on close).
        property bool fixPending: false
        onClosed: {
            if (fixPending) {
                fixPending = false
                root.openLinePopover(row, root, x, y - 6)
            } else {
                root.forceActiveFocus()
            }
        }
        TMenuItem {
            text: qsTr("Lejátszás innen")
            iconName: "play"
            enabled: root.canPlay
            onTriggered: root.playFrom(editorVm.rowStartMs(rowMenu.row))
        }
        TMenuItem {
            text: rowMenu.inSelection ? qsTr("Más mondta… (a kijelölt sorok)") : qsTr("Más mondta… (ez a sor)")
            iconName: "user"
            shortcutText: "M"
            onTriggered: rowMenu.fixPending = true
        }
        TMenuItem {
            objectName: "rowMenuConfirm"
            text: rowMenu.inSelection ? qsTr("Jó így (a kijelölt sorok)") : qsTr("Jó így")
            iconName: "check"
            shortcutText: "J"
            onTriggered: rowMenu.inSelection ? editorVm.confirmSelection(false) : editorVm.confirmRow(rowMenu.row)
        }
        // "Not identifiable" (0): the name stays, voice analysis ignores the line; again: reverts.
        TMenuItem {
            objectName: "rowMenuIgnore"
            readonly property bool ignored: rowMenu.visible && rowMenu.row >= 0
                                            && editorVm.rowInfo(rowMenu.row).ignored === true
            text: ignored ? qsTr("Újra azonosítható")
                          : rowMenu.inSelection ? qsTr("Nem azonosítható (a kijelölt sorok)") : qsTr("Nem azonosítható")
            reserveIconSpace: true
            shortcutText: "0"
            onTriggered: rowMenu.inSelection ? editorVm.setSelectionIgnored(!ignored)
                                             : editorVm.setRowIgnored(rowMenu.row, !ignored)
        }
        // Voiceprint sample from this line (also in the line popup; this is the right-click path).
        // For an uncertain line it also confirms it.
        TMenuItem {
            objectName: "rowMenuSample"
            readonly property var info: rowMenu.visible && rowMenu.row >= 0
                                        ? editorVm.lineSampleInfo(editorVm.rowUtteranceId(rowMenu.row)) : ({})
            text: info.confirmFirst === true
                  ? qsTr("Jó így + hanglenyomat-minta ebből a sorból (%1 mp)").arg(info.seconds || 0)
                  : (info.personName ? qsTr("Hanglenyomat-minta ebből a sorból (%1 mp)").arg(info.seconds || 0)
                                     : qsTr("Hanglenyomat-minta ebből a sorból"))
            iconName: "fingerprint"
            enabled: info.ok === true
            onTriggered: editorVm.createVoiceprintFromLine(editorVm.rowUtteranceId(rowMenu.row))
        }
        TMenuSeparator {}
        TMenuItem {
            text: qsTr("Sor másolása")
            iconName: "copy"
            onTriggered: editorVm.copyRow(rowMenu.row)
        }
        TMenuItem {
            text: qsTr("Kijelölt sorok másolása (%1)").arg(editorVm.selectedCount)
            iconName: "copy"
            shortcutText: "Ctrl+C"
            enabled: editorVm.selectedCount > 0
            onTriggered: editorVm.copySelection()
        }
        TMenuItem {
            text: qsTr("Teljes átirat másolása")
            reserveIconSpace: true
            onTriggered: editorVm.copyAll()
        }
        TMenuSeparator {}
        TMenuItem {
            text: qsTr("Minden sor kijelölése")
            reserveIconSpace: true
            shortcutText: "Ctrl+A"
            onTriggered: editorVm.selectAll()
        }
    }

    // ---- demo states (screenshots) ----
    Timer {
        id: demoTimer
        interval: 120
        property int tries: 0
        property int settled: 0
        // Demo speaker of the voiceprint panel: the first one that fits the state.
        function voiceprintDemoKey(s) {
            const list = editorVm.speakers
            for (let i = 0; i < list.length; ++i) {
                const sp = list[i]
                if (s === "voiceprintHas") {
                    if (sp.voiceprint === "has") return sp.key
                    continue
                }
                if (sp.voiceprint !== "none") continue
                const m = editorVm.voiceprintMaterial(sp.key)
                const enough = m.sufficient === true || m.supported !== true
                if ((s === "voiceprintShort") !== enough) return sp.key
            }
            return ""
        }
        onTriggered: {
            const s = root.demoState
            if (s.startsWith("voiceprint")) {
                // The state appears when voice analysis finishes: wait for it (at most ~6 s).
                const key = editorVm.embeddingRunning ? "" : voiceprintDemoKey(s)
                if (key === "") {
                    if (++tries < 50) demoTimer.start()
                    return
                }
                if (editorVm.speakerInfo(key).lane < 0) editorVm.lanesExpanded = true
                const mark = toolbar.voiceprintMark(key)
                if (!mark) {
                    if (++tries < 50) demoTimer.start()
                    return
                }
                root.openVoiceprintPopover(key, mark)
                if (s === "voiceprintDone") voiceprintPanel.create()
            } else if (root.v3Demo) {
                // The v3 states appear after voice analysis and the background analysis (groups) finish.
                if (editorVm.demoStatePending() || editorVm.embeddingRunning || editorVm.reviewRunning
                        || ++settled < 3 || (s === "newPersonSimilar" && !editorVm.newPersonSimilar.count)) {
                    if (++tries < 150) demoTimer.start()
                    return
                }
                root.applyV3DemoView(s)
            } else if (s === "speakerPopover" || s === "speakerPopoverPair") {
                // The whole speaker: from the rail header's first avatar (Pair: the pair chooser open).
                if (root.laneCount > 0) root.openSpeakerPopover(editorVm.lanes[0].key, railHeader)
                if (s === "speakerPopoverPair") speakerWhy.pairPicking = true
            } else if (s === "linePopover" || s === "lineToSpeakerPopover") {
                const item = list.itemAtIndex(0)
                if (item) root.openLinePopover(0, item.nameItem)
                if (s === "lineToSpeakerPopover") linePopover.setScope("speaker")
            } else if (s === "selectionPopover") {
                editorVm.selectRows(2, 4)
                const item = list.itemAtIndex(2)
                if (item) root.openLinePopover(2, item.nameItem)
            } else if (s === "mergeConfirm") {
                if (root.laneCount > 1)
                    root.requestMerge({ kind: "merge", fromKey: editorVm.lanes[root.laneCount - 1].key,
                                        intoKey: editorVm.lanes[1].key })
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
    // View part of the v3 demo states (after the VM prepared them): panel, opened group.
    function applyV3DemoView(s) {
        const keyOf = name => {
            const lanes = editorVm.lanes
            for (let i = 0; i < lanes.length; ++i) if (lanes[i].name === name) return i
            return -1
        }
        if (s === "markers" || s === "fixLinePopover") {
            list.positionViewAtBeginning()
            if (s === "fixLinePopover") {
                const item = list.itemAtIndex(2)
                if (item) root.openLinePopover(2, item.nameItem)
            }
        } else if (s === "reviewFocus") {
            reviewView.demoPlayPos = 0.42      // auto-play is playing right now (screenshot)
        } else if (s === "speakerWhy") {
            list.positionViewAtBeginning()
            const lane = Math.max(0, keyOf(DemoText.name("Fehér Gábor")))
            root.openSpeakerPopover(editorVm.lanes[lane].key, railHeader)
            speakerWhy.x = Math.round(root.railX + 4 + lane * Theme.laneWidth - 7)
        } else if (s === "ignoredRows") {
            list.positionViewAtBeginning()
        } else if (s === "imentLog") {
            changeBar.logOpen = true
        } else if (s === "shiftPreview") {
            // Shift held + hover on another column of the 3rd line: "-> Name · not a sample".
            list.positionViewAtBeginning()
            const item = list.itemAtIndex(2)
            if (item && root.laneCount > 1) {
                root.shiftHeld = true
                root.hoverRow = 2
                root.hoverLane = (item.lane + 1) % root.laneCount
                const p = item.mapToItem(list, 0, item.height / 2)
                dragChip.x = root.railX + 4 + root.hoverLane * Theme.laneWidth + 30
                dragChip.y = Math.max(2, p.y - 26)
            }
        } else if (s === "sweepStop" || s === "sweepContinuous" || s === "sweepSectionDone") {
            // Sweep: the VM sets the moment; the player position is given here (there is no player).
            const ms = editorVm.stageSweepDemo(s)
            if (ms >= 0) editorVm.setPlaybackPosition(ms, true)
            const target = s === "sweepSectionDone" ? editorVm.rowForTime(ms) : editorVm.sweepTargetRow
            if (target >= 0) list.positionViewAtIndex(target, s === "sweepSectionDone" ? ListView.End : ListView.Center)
            Qt.callLater(root.updateViewport)
        } else if (s === "contaminatedCore") {
            const key = editorVm.contaminated.speakerKey || ""
            if (key !== "") root.openSpeakerWhyAt(key, root.width - speakerWhy.width - 20, headRow.y + headRow.height + 58)
        }
    }

    Component.onCompleted: {
        Qt.callLater(root.updateViewport)
        const s = demoState
        if (s === "") return
        if (root.v3Demo) {
            root.setViewMode("fix")
            editorVm.applyDemoState(s)
            demoTimer.start()
            return
        }
        if (s === "playing") {
            editorVm.setPlaybackPosition(77000, true)
        } else if (s === "expanded") {
            editorVm.railVisible = true
            editorVm.lanesExpanded = true
        } else if (s === "speakerPopover" || s === "speakerPopoverPair" || s === "linePopover" || s === "lineToSpeakerPopover"
                   || s === "selectionPopover" || s === "mergeConfirm" || s === "personPicker" || s === "drag") {
            editorVm.railVisible = true
            demoTimer.start()
        } else {
            editorVm.applyDemoState(s)
            if (s.startsWith("voiceprint")) demoTimer.start()
        }
    }
}
