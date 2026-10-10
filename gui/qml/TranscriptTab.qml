import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// „Átirat" fül: az átirat-szerkesztő (3a). Felül eszköztár + áttekintő, alatta a
// megszólalások listája; bal oldalt a bekapcsolható beszélő-sín (soronkénti átsorolás:
// kattintás másik oszlopba, húzás, több sor kijelölése + alsó sáv / 1–9 billentyű).
//
// A javítás hatóköre (a tulajdonossal egyeztetve, a tervezői spectől szándékosan eltérve):
//  - egy sor NEVÉRE kattintva (vagy „Más mondta…") alapból CSAK AZ A SOR kerül át (több soros
//    kijelölés részeként: a kijelölt sorok); a teljes beszélő ott kifejezett választás;
//  - a TELJES beszélő a beszélő-szintű helyekről javítható: sín-fejléc avatar, térkép-dokk név,
//    a sor-panel „Miért ő?" linkje → „Miért ő?" panel (SpeakerWhyPopover, handoff-v3 E3);
//  - a sor-panel (LinePopover, E1): jelöltek bizonyítékkal, „Miért nem X?" + a javítás helye;
//  - Javítás módban az „Átnézendő" chip a csoport-nézetet kapcsolja (ReviewGroupsView, E2): a
//    kétes sorok okok szerint, csoportonként egy döntés / egy visszavonási lépés;
//  - új személy után a hozzá hasonló sorok a sínen (accent keret) és a térképen (E4);
//  - hanglenyomat CSAK kifejezett műveletre készül: a térkép ujjlenyomat-jele (saját panel), a
//    „Miért ő?" Minták-linkje, a sor-panel teljes-beszélő hatóköre, és — egy teljes beszélő
//    elnevezése után, ha a személynek még nincs — az értesítő sáv „Hanglenyomat készítése" gombja;
//  - minden átsorolás után alul értesítő sáv: mi történt + Visszavonás / „Hasonló N sor is" /
//    „<Forrás> mind a N sora";
//  - két beszélő összevonása előtt számokkal megerősítő ablak (egy sor / kijelölés sosem kérdez).
// Az állapotot a TranscriptEditorViewModel adja.
//
// Képernyőképhez (kitalált mintaadat, lásd TranscriptDemoSession):
//   demoVariant: "" | "two" | "many" | "long" | "novoice" | "none"
//   demoState:   "" | "rail" | "playing" | "selection" | "suggestion" | "suggestionShown" |
//                "filter" | "search" | "searchEmpty" | "linePopover" | "selectionPopover" |
//                "lineToSpeakerPopover" | "speakerPopover" | "personPicker" | "drag" |
//                "expanded" | "changeLine" | "changeSelection" | "changeSpeaker" |
//                "changeFilter" | "mergeConfirm" | "changeVoiceprint" | "changeVoiceprintDone" |
//                "voiceprintHas" | "voiceprintNone" | "voiceprintDone" | "voiceprintShort" |
//                "changePairOffer" | "speakerPopoverPair" |
//                v3 Javítás mód (a „v3" kitalált meetingen, ha demoVariant üres):
//                "markers" | "fixLinePopover" | "reviewGroups" | "reviewExpanded" |
//                "contaminatedCore" | "speakerWhy" | "newPersonSimilar"
Item {
    id: root

    // ---- a szelet-szerződés bemenetei (CONTRACT.md) ----
    property string meetingId: ""      // selected meeting; "" = none
    property var player: null          // PlayerController or null
    property var shell: null           // ShellActions or null

    property string demoVariant: ""
    property string demoState: ""
    // A héj kikapcsolhatja a Ctrl+Z-t, amíg a fókusz nem a szerkesztőben van (ott a címke-lépés
    // visszavonása él; lásd Main.qml tagUndoActive).
    property bool undoAllowed: true
    // Olvasás (tiszta lista) / Javítás (sín, jelölők, átnézendők) — Ctrl+E, eszköz-sor szegmens.
    // Belépéskor a megbeszélés megjegyzett sín-állapota dönt; a Javítás bekapcsolja a sínt.
    property bool fixMode: false
    onFixModeChanged: if (editorVm.hasTranscript && editorVm.railVisible !== fixMode) editorVm.railVisible = fixMode
    // „Ki volt ott?” (eszköz-sor) — a héj nyitja a résztvevő-párbeszédet.
    signal participantsRequested()

    // ---- v3 Javítás mód (a térkép-dokk és az eszköz-sor ezeket olvassa) ----
    readonly property var v3DemoStates: ["markers", "fixLinePopover", "reviewGroups", "reviewExpanded",
                                         "contaminatedCore", "speakerWhy", "newPersonSimilar"]
    readonly property bool v3Demo: v3DemoStates.indexOf(demoState) >= 0
    // Az Átnézendő csoport-nézet látszik (az „Átnézendő" chip = editorVm.uncertainOnly).
    readonly property bool reviewShown: editorVm.uncertainOnly && editorVm.hasTranscript
    // Az új személyhez hasonló sorok a térképnek: [x, w, …] (0..1); ugyanezek az
    // editorVm.overview `marks` listáiban is (a MapDock accent jelei).
    readonly property var similarMarks: editorVm.similarMarks
    // Az imént felvett személy oszlopa a sínen (-1 = nincs / összecsukva).
    readonly property int newPersonLane: {
        const key = editorVm.newPersonKey
        if (key === "") return -1
        const lanes = editorVm.lanes
        for (let i = 0; i < lanes.length; ++i) if (lanes[i].key === key) return i
        return -1
    }
    // A sín-fejléc mellett: mit csinál itt a kattintás / mi a kiemelés.
    readonly property string railNote: reviewShown ? qsTr("Átnézendő: csak a kétes sorok, okok szerint")
        : editorVm.newPersonKey !== "" && editorVm.similarShown && newPersonLane >= 0
          ? qsTr("új oszlop: %1 · kék keret: hozzá hasonló sorok").arg(editorVm.lanes[newPersonLane].name)
        : qsTr("másik oszlopra kattintás: áthelyezés · 1–%1: a kijelöltek áthelyezése · húzás").arg(Math.min(9, laneCount))

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

    // Szövegmezőben (kereső, személyválasztó) a szerkesztő billentyűi nem élnek.
    readonly property bool textInputFocused: {
        const it = root.Window.activeFocusItem
        return !!it && (it instanceof TextInput || it instanceof TextEdit)
    }

    function openSearch() { toolbar.openSearch() }
    function releaseHiddenFocus() {
        const it = root.Window.activeFocusItem
        if (it && !it.visible) root.forceActiveFocus()
    }
    // „Következő bizonytalan": a kijelölt (különben az első látható) sortól lép tovább.
    function nextUncertain(direction) {
        if (root.reviewShown) {
            root.forceActiveFocus()
            if (!reviewView.step(direction) && root.shell) root.shell.toast(qsTr("Nincs átnézendő sor."))
            return
        }
        let from = editorVm.currentRow
        if (from < 0) from = list.indexAt(list.width / 2, list.contentY + 1) - (direction < 0 ? 0 : 1)
        root.forceActiveFocus()
        if (editorVm.stepUncertain(from, direction) < 0 && root.shell)
            root.shell.toast(qsTr("Nincs bizonytalan sor."))
    }
    // Újraellenőrzés a megerősített / javított sorok hangja alapján. A héjon át (megerősítő
    // ablak + visszajelzés); héj nélkül (demó, képernyőkép) közvetlenül.
    function recheckSpeakers() {
        if (root.shell && root.shell.recheckSpeakers && root.meetingId !== "") {
            root.shell.recheckSpeakers(root.meetingId)
            return
        }
        const r = editorVm.recheckSpeakers()
        if (!r.ran && r.blocker && root.shell) root.shell.toast(r.blocker)
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
    // Ugrás az idővonal egy pontjára (a térkép-dokk kattintása): a lista odagörget, a lejátszó teker.
    function seekToFraction(fraction) {
        const ms = editorVm.timeAtFraction(fraction)
        root.revealRow(editorVm.rowForTime(ms), ListView.Beginning)
        if (root.player) { root.followNext = false; root.player.seek(ms) }
    }
    // A panel a horgony alá kerül; ha ott nem fér el (pl. a lejátszó fölötti térkép-dokk
    // neve), fölé.
    function popoverY(anchorItem, popupHeight) {
        const below = anchorItem.mapToItem(root, 0, anchorItem.height).y + 6
        if (below + popupHeight <= root.height) return Math.round(below)
        return Math.round(Math.max(0, anchorItem.mapToItem(root, 0, 0).y - popupHeight - 6))
    }
    // A TELJES beszélő panelje (sáv-fejléc avatar, térkép-dokk név).
    function openSpeakerPopover(speakerKey, anchorItem, focusTracks) {
        const p = anchorItem.mapToItem(root, 0, anchorItem.height)
        speakerWhy.speakerKey = speakerKey
        speakerWhy.focusTracks = focusTracks === true
        speakerWhy.x = Math.round(Math.max(8, Math.min(p.x - 7, root.width - speakerWhy.width - 8)))
        speakerWhy.y = root.popoverY(anchorItem, speakerWhy.implicitHeight)
        speakerPopoverRow = -1
        speakerWhy.open()
    }
    // „Miért ő?" a sor-panelről (a panel helyén nyílik).
    function openSpeakerWhyAt(speakerKey, x, y, focusTracks) {
        speakerWhy.speakerKey = speakerKey
        speakerWhy.focusTracks = focusTracks === true
        speakerWhy.x = Math.round(Math.max(8, Math.min(x, root.width - speakerWhy.width - 8)))
        speakerWhy.y = Math.round(Math.max(0, y))
        speakerWhy.open()
    }
    // A hanglenyomat saját panelje (az áttekintő ujjlenyomat-jeléről).
    function openVoiceprintPopover(speakerKey, anchorItem) {
        const p = anchorItem.mapToItem(root, 0, anchorItem.height)
        voiceprintPopover.speakerKey = speakerKey
        voiceprintPopover.x = Math.round(p.x - 12)
        voiceprintPopover.y = root.popoverY(anchorItem, voiceprintPopover.implicitHeight)
        voiceprintPopover.open()
    }
    // EGY SOR panelje (a sor neve, „Más mondta…", helyi menü): alapból csak ez a sor megy;
    // ha a sor egy több soros kijelölés része, a kijelölés.
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
    // Egy sor panelje a sor-azonosítóval (az Átnézendő nézet „Más…" gombjáról).
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
    // Egy panelről nyitott másik panel csak az első BEZÁRULTA után nyílik (különben a bezáródó
    // panel visszaadná a fókuszt a szerkesztőnek, és az új panel billentyűi nem élnének).
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
    // „Ő nem volt ott": a beszélő sorai névtelenek lesznek, és a „Ki volt ott?" jelölése is
    // törlődik (ParticipantsViewModel.unbind). Ha nincs hozzá résztvevő (vagy demó), a beszélő
    // egyszerűen névtelenné válik — mindkét út egy visszavonási lépés.
    function markNotPresent(speakerKey, fixVoiceprints) {
        const person = editorVm.personOf(speakerKey)
        let participantId = ""
        if (person !== "" && !editorVm.demo) {
            participantsVm.reload()
            const groups = participantsVm.groups
            for (let g = 0; g < groups.length && participantId === ""; ++g)
                for (let r = 0; r < groups[g].rows.length; ++r)
                    if ((groups[g].rows[r].name || "").toLowerCase() === person.toLowerCase()) {
                        participantId = groups[g].rows[r].id
                        break
                    }
        }
        if (fixVoiceprints || participantId === "") editorVm.revertSpeakerToAnonymous(speakerKey, fixVoiceprints)
        if (participantId !== "") participantsVm.unbind(participantId)
    }
    // Két beszélő összevonása előtt megerősítés, számokkal. request: { kind: "merge" |
    // "reassign" | "rest", fromKey, intoKey, personName, fix }. Üres forrásnál nincs mit kérdezni.
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
    // A most javított sor maradjon látható, amikor az értesítő sáv megjelenik alatta.
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

    // QA / szkriptek: a nézetmodell elérése (hook.findObject("transcriptTab").editor).
    readonly property alias editor: editorVm
    TranscriptEditorViewModel {
        id: editorVm
        meetingId: root.meetingId
        // A v3 demó-állapotok a handoff neveivel épített „v3" kitalált meetingen futnak.
        demoVariant: root.demoVariant !== "" ? root.demoVariant : root.v3Demo ? "v3" : ""
        highlightColor: Theme.warnSoft
        highlightCurrentColor: Theme.warnLine
        timelineMs: (root.player && root.player.durationMs) || 0

        onRevealRequested: row => root.revealRow(row)
        onChangeChanged: {
            root.keepVisibleRow = changeActive ? changeRow : -1
            if (root.keepVisibleRow >= 0) Qt.callLater(root.keepRowVisible)
        }
        onNotice: text => { if (root.shell && root.shell.toast) root.shell.toast(text) }
        onOverviewChanged: Qt.callLater(root.updateViewport)
        onSessionChanged: {
            root.fixMode = editorVm.railVisible
            root.lastPlayingRow = -1
            root.speakerPopoverRow = -1
            linePopover.close()
            speakerWhy.close()
            voiceprintPopover.close()
            picker.close()
            mergeDialog.close()
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
        sequences: ["Ctrl+E"]
        enabled: root.visible && editorVm.hasTranscript
        onActivated: root.fixMode = !root.fixMode
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
    Keys.onPressed: event => {
        if (!editorVm.hasTranscript) return
        const plain = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
        const ctrlOnly = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier | Qt.ShiftModifier)) === Qt.ControlModifier
        if (ctrlOnly && event.key === Qt.Key_C) {
            root.copySelectionOrCurrent()
            event.accepted = true
        } else if (ctrlOnly && event.key === Qt.Key_A) {
            editorVm.selectAll()
            event.accepted = true
        } else if (plain && event.key === Qt.Key_B && root.reviewShown) {
            root.nextUncertain((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
            event.accepted = true
        } else if (plain && event.key === Qt.Key_B && editorVm.uncertainCount > 0) {
            root.nextUncertain((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
            event.accepted = true
        } else if (plain && event.key === Qt.Key_1 && root.reviewShown && reviewView.currentUtterance !== "") {
            // Az Átnézendő nézet kiválasztott sora a javasolt beszélőhöz („X mondta").
            reviewView.acceptCurrent()
            event.accepted = true
        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
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

    // ---- régi formátumú átirat: olvasható (és másolható), de itt nem szerkeszthető ----
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
            viewportStart: root.viewportStart
            viewportSize: root.viewportSize
            onFixModeToggled: on => root.fixMode = on
            onParticipantsRequested: root.participantsRequested()
            onSeekRequested: fraction => root.seekToFraction(fraction)
            onSearchStepRequested: direction => editorVm.searchStep(direction)
            onNextUncertainRequested: root.nextUncertain(1)
            onRecheckRequested: root.recheckSpeakers()
            onSpeakerClicked: (speakerKey, anchor) => root.openSpeakerPopover(speakerKey, anchor)
            onVoiceprintClicked: (speakerKey, anchor) => root.openVoiceprintPopover(speakerKey, anchor)
            // A kereső bezárása után a billentyűk újra a szerkesztőéi (ne a rejtett mezőéi).
            onSearchOpenChanged: if (!searchOpen) Qt.callLater(root.releaseHiddenFocus)
        }

        // Rögzített fejléc: a sín oszlopfejei + a megszólalás-számláló + a hang-elemzés állapota.
        Rectangle {
            id: headRow
            // Csak a sín oszlopfejei (a számláló és a hangelemzés az eszköz-soré); rejtett sínnél 0
            // magas (nem `visible: false`: a láthatóság-váltás után az elrendezés késve frissülne).
            Layout.fillWidth: true
            implicitHeight: root.railShown ? railHeader.implicitHeight : 0
            clip: true
            color: Theme.surface

            TLabel {
                objectName: "railNote"
                visible: root.railShown
                x: root.textX + 12
                width: Math.max(0, parent.width - x - 20)
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -4
                text: root.railNote
                elide: Text.ElideRight
                muted: true
                font.pixelSize: Theme.fontCaption
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
                    // A sáv megjelenésekor a lista alacsonyabb lesz: a javított sor ne csússzon ki.
                    if (height > 0 && root.keepVisibleRow >= 0) Qt.callLater(root.keepRowVisible)
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

            // Átnézendő (E2): a kétes sorok csoportjai; a lista helyén.
            ReviewGroupsView {
                id: reviewView
                objectName: "reviewView"
                anchors.fill: parent
                visible: root.reviewShown
                vm: editorVm
                tab: root
                onSplitRequested: key => editorVm.splitSpeaker(key)
                onSpeakerWhyRequested: (key, anchor) => root.openSpeakerPopover(key, anchor)
                onLineFixRequested: (id, anchor) => root.openLinePopoverFor(id, anchor)
            }
        }

        // A legutóbbi átsorolás értesítése: a lista alatt (nem takarja a javított sort).
        TranscriptChangeBar {
            id: changeBar
            objectName: "changeBar"
            visible: editorVm.changeActive
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: selectionBar.visible ? 6 : 10
            vm: editorVm
            onRestRequested: root.requestMerge({ kind: "rest", fromKey: editorVm.changeSourceKey,
                                                 intoKey: editorVm.changeTargetKey })
            onVoiceprintPanelRequested: key => root.openVoiceprintFor(key)
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
        anonymousText: root.pickerTarget === "participant" ? qsTr("Névtelen résztvevő")
                     : root.pickerTarget === "speaker" ? qsTr("Névtelen beszélő") : qsTr("Új névtelen résztvevő")
        // „Új személy…" a „Miért ő?" panelről: a teljes beszélő a választott névhez kerül.
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

    // „Kinek a sora ez?" (E1): egy sor / a kijelölés / (kifejezett választásra) a teljes beszélő.
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
        onClosed: {
            root.speakerPopoverRow = -1
            if (root.runAfterPopupClose()) return
            if (!mergeDialog.visible) root.forceActiveFocus()
        }
    }

    // „Miért ő?" (E3): a teljes beszélő — bizonyíték, sáv-beosztás, jelöltek, „Ő nem volt ott".
    SpeakerWhyPopover {
        id: speakerWhy
        objectName: "speakerWhyPopover"
        editor: editorVm
        canListen: root.canPlay
        onListenRequested: (startMs, endMs) => root.playLine(startMs, endMs)
        onMergeRequested: request => root.requestMerge(request)
        onVoiceprintRequested: key => root.afterPopupClose = () => root.openVoiceprintFor(key)
        onPeopleRequested: person => { if (root.shell && root.shell.openPeople) root.shell.openPeople(person) }
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
        onClosed: {
            if (root.runAfterPopupClose()) return
            if (!mergeDialog.visible) root.forceActiveFocus()
        }
    }

    // „Ő nem volt ott": a „Ki volt ott?" jelölés is frissül (a résztvevő kötése megszűnik).
    ParticipantsViewModel {
        id: participantsVm
        meetingId: root.meetingId
    }

    // A hanglenyomat panelje: állapot, használható anyag, készítés (és a most készült
    // visszavonása). Lenyomat csak az itteni gombra készül.
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

    // Két beszélő összevonása: megerősítés számokkal (M10 minta). Egy sor / kijelölés
    // áthelyezése sosem kérdez — ez csak a teljes beszélőt érintő összeolvasztás előtt áll.
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

    // A sor helyi menüje (jobb gomb): lejátszás, másolás.
    TMenu {
        id: rowMenu
        objectName: "rowMenu"
        property int row: -1
        readonly property bool inSelection: editorVm.selectedCount > 1 && editorVm.isRowSelected(row)
        // „Más mondta…": a panel csak a menü bezárulta UTÁN nyílik (különben a menü záráskor
        // visszavenné a fókuszt a panel keresőmezőjétől).
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
            onTriggered: rowMenu.fixPending = true
        }
        // Hanglenyomat-minta ebből a sorból (a sor felugrójában is ott van; itt a jobb-klikk útja).
        // Bizonytalan sornál egyben megerősítés is.
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

    // ---- demó-állapotok (képernyőkép) ----
    Timer {
        id: demoTimer
        interval: 120
        property int tries: 0
        property int settled: 0
        // A hanglenyomat-panel demó-beszélője: az első, amelyik az állapotnak megfelel.
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
                // A hang-elemzés végén áll elő az állapot: addig várunk (legfeljebb ~6 mp).
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
                // A v3 állapotok a hang-elemzés és a háttér-elemzés (csoportok) végén állnak elő.
                if (editorVm.demoStatePending() || editorVm.embeddingRunning || editorVm.reviewRunning
                        || ++settled < 3 || (s === "newPersonSimilar" && !editorVm.newPersonSimilar.count)) {
                    if (++tries < 150) demoTimer.start()
                    return
                }
                root.applyV3DemoView(s)
            } else if (s === "speakerPopover" || s === "speakerPopoverPair") {
                // A teljes beszélő: a sáv-fejléc első avatarjáról (Pair: a pár-választó nyitva).
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
    // A v3 demó-állapotok nézet-része (a VM előkészítése után): panel, kinyitott csoport.
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
        } else if (s === "reviewGroups") {
            reviewView.list.positionViewAtBeginning()
        } else if (s === "reviewExpanded") {
            const groupId = editorVm.reviewGroups.groupAt(0)
            if (groupId !== "") editorVm.reviewGroups.setExpanded(groupId, true)
            reviewView.step(1)
            reviewView.list.positionViewAtBeginning()
        } else if (s === "speakerWhy") {
            list.positionViewAtBeginning()
            const lane = Math.max(0, keyOf("Fehér Gábor"))
            root.openSpeakerPopover(editorVm.lanes[lane].key, railHeader)
            speakerWhy.x = Math.round(root.railX + 4 + lane * Theme.laneWidth - 7)
        } else if (s === "contaminatedCore") {
            reviewView.list.positionViewAtBeginning()
            const key = editorVm.contaminated.speakerKey || ""
            if (key !== "") root.openSpeakerWhyAt(key, root.width - speakerWhy.width - 20, headRow.y + headRow.height + 58)
        }
    }

    Component.onCompleted: {
        Qt.callLater(root.updateViewport)
        const s = demoState
        if (s === "") return
        if (root.v3Demo) {
            root.fixMode = true
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
