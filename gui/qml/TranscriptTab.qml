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
//  - három nézet az eszköz-sor szegmensén: Olvasás · Javítás · Átnézendő (N) — az utóbbi a
//    fókusz-nézet (ReviewFocusView, „A” koncepció): egy kétes sor egy rögzített kártyán, egy
//    billentyűs ítéletekkel, a fagyasztott munkalistán (reviewQueue); 0 sornál üres állapot +
//    újraellenőrzés;
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
//                "contaminatedCore" | "speakerWhy" | "newPersonSimilar" | "newPersonTag" (a sáv
//                címke-kérdése: „X új személy. Ráteszem a #Nordvik címkét?”) |
//                S2 (közös alap): "imentWarning" (az „Imént” sor eltérő-minta figyelmeztetéssel és
//                mini-naplóval; "imentLog": a napló nyitva) | "shiftPreview" (lenyomott Shift: „→ Név · nem minta” előnézet) |
//                "ignoredRows" (nem azonosítható sorok, minta-jelek) | "sampleFilter" (a minta-mérő
//                „+N most” szűrője) |
//                S6 (söprés-mód): "sweepStop" (megálló + döntés-sáv) | "sweepContinuous" (folyamatos
//                mód, utólagos ablak) | "sweepSectionDone" (szakasz-összesítő, „N sor újra kétes”)
//                S4 (fókusz-nézet): "reviewFocus" | "reviewFocusDecided" | "reviewFocusIncoming" |
//                "reviewFocusSummary" | "reviewFocusAlreadyDecided" (lásd TranscriptEditorViewModelQueue.cpp)
//                S5: "reviewFocusOpener" | "reviewFocusOpenerDoubt" (nyitó-javaslat) | "reviewFocusOffer" |
//                "reviewFocusOfferList" (mintázat-ajánlat; a lista kinyitva)
//
// Billentyűk (fizikai hely szerint, TranscriptKeys): 1–9 a kijelölés a sín n. beszélőjéhez,
// Shift+1–9 / Shift+sín-kattintás / Shift+húzás = ugyanez „nem minta”; 0 = nem azonosítható
// (újra 0: újra azonosítható); J = Jó így (Shift+J: nem minta); M = Más… (a sor panelje);
// B / Shift+B = következő / előző átnézendő; nyilak, Szóköz, Enter, Esc, Ctrl+C / A / Z; [ / ] =
// tempó − / + (1,0–2,0×).
// Söprés-módban (a Javítás nézet „Söprés” kapcsolója, S6) a billentyűk a CÉLSORRA vonatkoznak
// (megálló > utólagos ablak > futó sor; kijelölés nélkül, a kijelölés nem változik): Enter =
// alapértelmezés (javaslat, különben Jó így), J, 0, 1–9, M = Más…, Shift = nem minta; → = Később
// (megállóban) / a következő megálló; ← = a sor eleje; Shift+← / → = előző / következő sor; Szóköz
// a megállóban = tovább döntés nélkül.
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
    // A három nézet (eszköz-sor szegmens, Nézet menü): "read" | "fix" | "review". Az Átnézendő
    // a Javítás része (sín, jelölők, soronkénti műveletek; alatta editorVm.uncertainOnly);
    // kilépve a belépés előtti mód (Olvasás vagy Javítás) jön vissza.
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
            root.reviewReturnFix = fix          // az onUncertainOnlyChanged ezt állítja vissza
            editorVm.uncertainOnly = false
        }
        root.fixMode = fix
    }
    // A minta-mérő „+N most” kattintása (MapDock.samplesClicked): a Javítás ezekre a sorokra szűr.
    function showSessionSamples(speakerKey) {
        if (!editorVm.hasTranscript) return false
        if (root.viewMode !== "fix") root.setViewMode("fix")
        root.forceActiveFocus()
        return editorVm.showSessionSamples(speakerKey)
    }
    // Ctrl+E: Olvasás ↔ Javítás; az Átnézendőből (ami Javítás-jellegű) az Olvasásba.
    function toggleFixMode() { setViewMode(viewMode === "read" ? "fix" : "read") }
    // „Ki volt ott?” (eszköz-sor) — a héj nyitja a résztvevő-párbeszédet.
    signal participantsRequested()

    // ---- v3 Javítás mód (a térkép-dokk és az eszköz-sor ezeket olvassa) ----
    readonly property var v3DemoStates: ["markers", "fixLinePopover", "reviewGroups", "reviewExpanded",
                                         "contaminatedCore", "speakerWhy", "newPersonSimilar", "newPersonTag",
                                         "imentWarning", "imentLog", "shiftPreview", "ignoredRows", "sampleFilter",
                                         "sweepStop", "sweepContinuous", "sweepSectionDone",
                                         "reviewFocus", "reviewFocusDecided", "reviewFocusIncoming",
                                         "reviewFocusSummary", "reviewFocusAlreadyDecided",
                                         "reviewFocusOpener", "reviewFocusOpenerDoubt", "reviewFocusOffer",
                                         "reviewFocusOfferList"]
    readonly property bool v3Demo: v3DemoStates.indexOf(demoState) >= 0
    // Az Átnézendő fókusz-nézet látszik (a szegmens harmadik eleme = editorVm.uncertainOnly).
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
        : sweepOn ? qsTr("Söprés: hallgasd végig; a billentyűk a futó (megálló) sorra vonatkoznak · 1–%1 beszélő · J: Jó így · 0: nem azonosítható · Shift: nem minta").arg(Math.min(9, laneCount))
        : editorVm.newPersonKey !== "" && editorVm.similarShown && newPersonLane >= 0
          ? qsTr("új oszlop: %1 · kék keret: hozzá hasonló sorok").arg(editorVm.lanes[newPersonLane].name)
        : root.shiftHeld ? qsTr("Shift: nem minta — a sor átkerül, de a hangja nem tanítja a felismerést")
        : qsTr("másik oszlopra kattintás: áthelyezés · 1–%1: a kijelöltek áthelyezése · Shift: nem minta · 0: nem azonosítható").arg(Math.min(9, laneCount))

    // ---- a sorok (TranscriptRow) ezt olvassák ----
    readonly property bool railShown: editorVm.railVisible && editorVm.hasTranscript
    readonly property real railX: 12
    readonly property real railWidth: railHeader.implicitWidth
    readonly property real textX: railShown ? railX + railWidth + 1 : 0
    readonly property int laneCount: editorVm.lanes.length
    readonly property bool groupShown: railHeader.groupShown
    // A sín oszlopa a sín bal szélétől mért x alapján (a Javítás és az Átnézendő sín-kattintása):
    // >= 0 beszélő; -1 „+N" csoport; -3 „+"; -9 semmi.
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
    // Rámutatás a sínen (egy sor másik oszlopa): ugyanaz az előnézet, mint húzáskor (cél-cella a
    // beszélő színével, „→ Név" címke) — az Átnézendő sínjével azonos működés.
    property int hoverRow: -1
    property int hoverLane: -1
    readonly property int previewLane: dragActive ? dragLane : hoverLane
    readonly property int previewColorIndex: previewLane >= 0 && previewLane < laneCount ? editorVm.lanes[previewLane].colorIndex : 0
    // A Shift épp le van nyomva („nem minta” módosító): az előnézeti címke „→ Név · nem minta”,
    // szaggatott keretű cél-cella, a gombfeliratok „· nem minta” utótagot kapnak. A billentyű-
    // eseményekből és a sín egér-módosítóiból frissül; fókusz-vesztéskor nullázódik.
    property bool shiftHeld: false
    onActiveFocusChanged: if (!activeFocus && !reviewView.activeFocus) shiftHeld = false
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive) root.shiftHeld = false }
    }
    readonly property string nonSampleSuffix: qsTr(" · nem minta")

    // ---- S6: söprés-mód (a Javítás nézetben; a VM söprés-blokkja: TranscriptEditorViewModel) ----
    readonly property bool sweepOn: editorVm.sweepActive && root.viewMode === "fix"
    // A térkép-dokk söprés-rétege (Main.qml → MapDock.sweep); üres map: nincs söprés.
    readonly property var sweepDock: sweepOn ? editorVm.sweepDock : ({})
    onViewModeChanged: if (viewMode !== "fix" && editorVm.sweepActive) editorVm.sweepActive = false
    function setSweep(on) {
        if (on && root.viewMode !== "fix") return false
        editorVm.sweepActive = on
        root.forceActiveFocus()
        Qt.callLater(root.updateViewport)
        return editorVm.sweepActive === on
    }
    // A söprés-billentyűk célsora: megálló > utólagos ablak > futó sor; különben a kiválasztott sor.
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
    // Ugrás egy időpontra és lejátszás onnan (a söprés útjai: következő megálló, összesítő gombjai).
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
    // ←: a célsor eleje (a megálló a sor végén újra megáll).
    function sweepReplay() {
        const row = sweepTarget()
        if (row < 0) return
        editorVm.sweepDecide(row, "replay", -1, false)
        root.sweepSeekPlay(editorVm.rowStartMs(row))
    }
    // →: a következő megálló eleje (a lejátszás onnan szól).
    function sweepJumpNext() {
        const from = Math.max(sweepTarget(), editorVm.playingRow)
        const row = editorVm.sweepNextStopRow(from)
        if (row < 0) {
            if (root.shell && root.shell.toast) root.shell.toast(qsTr("Nincs több megálló."))
            return
        }
        root.sweepSeekPlay(editorVm.rowStartMs(row))
    }
    // [ / ]: tempó 1,0–2,0× negyedekben (a lejátszó-sor és a söprés-sáv mutatja).
    function changeTempo(direction) {
        if (!root.player) return
        const now = root.player.rate || 1.0
        const next = Math.round((now + 0.25 * direction) * 4) / 4
        root.player.rate = Math.max(1.0, Math.min(2.0, next))
    }
    function tempoLabel(r) {
        return String(Math.round(r * 100) / 100).replace(".", Qt.locale().decimalPoint) + "×"
    }
    // A söprés billentyűi (igaz: elfogyasztva). Söprés-módon kívül semmit nem csinál.
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

    // ---- lejátszó ----
    readonly property int playerPosition: (player && player.positionMs) || 0
    readonly property bool playerActive: !!player && !(player.previewPath) && (player.playing === true || playerPosition > 0)
    onPlayerPositionChanged: editorVm.setPlaybackPosition(playerPosition, playerActive)
    onPlayerActiveChanged: editorVm.setPlaybackPosition(playerPosition, playerActive)
    // A lejátszás követése: csak akkor görgetünk, ha az előző lejátszott sor látszott (a
    // felhasználó nem görgetett el), vagy ugrás történt (keresősáv, időbélyeg).
    property int lastPlayingRow: -1
    property bool followNext: false
    // Egy kurzor: a kijelölt sor és a lejátszó pozíciója kétirányban együtt mozog. Állva a sor
    // kiválasztása a sor elejére teker (lejátszás nélkül); lejátszás közben a lejátszó alatti sor
    // lesz a kijelölt (több soros kijelölést nem bont meg). A cursorSync őr a visszacsatolás ellen
    // (kijelölés → tekerés → pozíció → kijelölés).
    property bool cursorSync: false
    // A lejátszó a sor elejére (sima seek; lejátszás közben szól tovább). Ha a pozíció már a soron
    // belül van, nincs tekerés.
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
    // A pozíció alatti sor lesz a kijelölt (a lejátszó követése, térkép-ugrás).
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
    // Az Átnézendő nézeten kívül oda vált (0 sornál az üres állapot látszik, nincs jelzés).
    function nextUncertain(direction) {
        root.forceActiveFocus()
        if (!root.reviewShown) {
            root.setViewMode("review")      // a fókusz-nézet az első nyitott soron indul
            return
        }
        if (!reviewView.step(direction) && root.shell) root.shell.toast(qsTr("Nincs átnézendő sor."))
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
        // Egy sor kiválasztása = a kurzor oda (állva tekerés, lejátszás közben ugrás és tovább szól).
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
    // Egy sor meghallgatása kis elő-/utó-futással, a szomszéd sorok határáig (PlayerController).
    function playLine(startMs, endMs) {
        if (!player) return
        root.cursorSync = true
        if (editorVm.selectedCount <= 1) editorVm.selectOnly(editorVm.rowForTime(startMs))
        root.cursorSync = false
        const b = editorVm.playBounds(startMs, endMs)
        player.playRange(startMs, endMs, b.minStartMs, b.maxEndMs)
    }
    // Előző / következő sor (térkép-dokk gombjai, nyilak). Lejátszás közben a lejátszó alatti
    // sortól lép, a cél elejére teker és szól tovább (sima ugrás, nem playRange); állva kijelöli,
    // megmutatja és a lejátszót a sor elejére teszi (lejátszás nélkül; a Szóköz innen indít).
    // Az Átnézendő nézetben a csoportok sorain lép (mint a B).
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
        // Söprés: a fagyasztott ablak a látható sorokból (± 2 sor, a VM-ben).
        editorVm.setSweepWindow(first, last)
    }
    // Ugrás az idővonal egy pontjára (a térkép-dokk kattintása): a lista odagörget, a lejátszó teker.
    // A pozíció alatti sor lesz a kijelölt (lejátszás közben és állva is).
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
    // A térkép-ugrás más fülről (Main.qml): a pozíció alatti sor kijelölése.
    function selectRowAt(ms) {
        if (!editorVm.hasTranscript) return
        root.cursorSync = true
        if (editorVm.selectedCount <= 1) editorVm.selectOnly(editorVm.rowForTime(ms))
        root.cursorSync = false
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
        // Söprés (S6): megálló → a lejátszó megáll, a döntés-sáv látsszon; döntés → tovább szól;
        // a szakasz vége → megáll (összesítő).
        onSweepStopReached: row => {
            if (root.player && root.player.playing === true) root.player.pause()
            Qt.callLater(() => { if (row >= 0 && row < list.count) list.positionViewAtIndex(row, ListView.Contain) })
        }
        onSweepResumeRequested: { if (root.player && root.player.playing !== true) root.player.play() }
        onSweepSectionEnded: { if (root.player && root.player.playing === true) root.player.pause() }
        // Az Átnézendő nézet a Javítás mód része (újraellenőrzés után a VM maga is bekapcsolja).
        // Újraellenőrzés után a VM maga is bekapcsolja; kilépéskor a korábbi mód tér vissza.
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
                // Lejátszás közben a lejátszó alatti sor a kijelölt (egy kurzor).
                if (root.player && root.player.playing === true) root.selectRowAtCursor(row)
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
    // A kijelölés / az Átnézendő kiválasztott sora: „nem azonosítható” ki-be (0).
    function toggleIgnored() {
        if (root.reviewShown) {
            const id = reviewView.currentUtterance
            if (id === "") return false
            return editorVm.setUtteranceIgnored(id, editorVm.lineInfo(id).ignored !== true)
        }
        if (editorVm.selectedCount === 0) return false
        // Ha a kijelölés minden sora már nem azonosítható: újra azonosítható; különben az lesz.
        let all = true
        for (let r = 0; r < list.count && all; ++r)
            if (editorVm.isRowSelected(r) && editorVm.rowInfo(r).ignored !== true) all = false
        return editorVm.setSelectionIgnored(!all)
    }
    // „Más…” (M): a kiválasztott sor panelje.
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
        // Átnézendő (fókusz-nézet): a saját billentyű-térképe (ReviewFocusView.handleKey) előbb.
        if (root.reviewShown && reviewView.handleKey(k, event)) {
            event.accepted = true
            return
        }
        // Söprés-mód (S6): a célsor billentyűi; [ / ]: tempó (mindig).
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
            // B: a következő átnézendő sor — az Átnézendő nézetben (oda vált, ha még nem ott van).
            root.nextUncertain(k.shift ? -1 : 1)
            event.accepted = true
        } else if (k.plain && k.name === "digit" && k.digit === 0 && !k.shift) {
            // 0: nem azonosítható (Javítás: a kijelölés; Átnézendő: a kiválasztott sor).
            root.toggleIgnored()
            event.accepted = true
        } else if (k.plain && k.name === "digit" && k.digit >= 1 && editorVm.selectedCount > 0 && !root.reviewShown) {
            editorVm.moveSelectionToLane(k.digit - 1, k.shift)
            event.accepted = true
        } else if (k.plain && k.name === "J") {
            // J: Jó így (Shift: nem minta).
            editorVm.confirmSelection(k.shift)
            event.accepted = true
        } else if (k.plain && !k.shift && k.name === "M") {
            root.openFixForCurrent()
            event.accepted = true
        } else if (k.plain && !k.shift && (k.name === "Down" || k.name === "Up" || k.name === "Left" || k.name === "Right")) {
            // Nyilak: előző / következő sor (Olvasás, Javítás, Átnézendő); a lista nem görget.
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
            // A kereső bezárása után a billentyűk újra a szerkesztőéi (ne a rejtett mezőéi).
            onSearchOpenChanged: if (!searchOpen) Qt.callLater(root.releaseHiddenFocus)
        }

        // Rögzített fejléc: a sín oszlopfejei + a megszólalás-számláló + a hang-elemzés állapota.
        Rectangle {
            id: headRow
            // Csak a sín oszlopfejei (a számláló és a hangelemzés az eszköz-soré); rejtett sínnél 0
            // magas (nem `visible: false`: a láthatóság-váltás után az elrendezés késve frissülne).
            Layout.fillWidth: true
            // Az Átnézendő fókusz-nézetben sincs (ott a beszélő-számsor a kártyán).
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
            // A lista szűrői a fejléc jobb szélén: a minta-mérő „+N most” szűrője (törölhető) és a
            // „nem azonosítható” sorok elrejtése / mutatása.
            Row {
                id: filterRow
                visible: root.railShown
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -4
                spacing: 12
                // Söprés-mód (S6): csak a Javítás nézetben.
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
                    // Rámutatás: a sor és a másik oszlop (saját oszlop / szünet-sor felett nincs előnézet).
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
                        // Shift = „nem minta”: a sor (a sorok) átkerülnek, de a hangjuk nem minta.
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
                        // Átsorolás után a sor már máshol van: az előnézet a mostani egérhelyzet szerint.
                        Qt.callLater(() => { if (railArea.containsMouse) railArea.updateHover(railArea.mouseX, railArea.mouseY) })
                    }
                    onCanceled: railArea.resetDrag()

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

                // Lebegő címke húzás és rámutatás közben: „→ Név" (Shift: „→ Név · nem minta”,
                // szaggatott keretben).
                Rectangle {
                    id: dragChip
                    objectName: "dragChip"
                    visible: root.previewLane >= 0 && (root.dragActive || root.hoverRow >= 0)
                    readonly property bool nonSample: root.shiftHeld
                    z: 3
                    width: dragLabel.implicitWidth + 18
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
                    TLabel {
                        id: dragLabel
                        objectName: "dragChipLabel"
                        anchors.centerIn: parent
                        text: root.previewLane >= 0 && root.previewLane < root.laneCount
                              ? "→ " + editorVm.lanes[root.previewLane].name + (dragChip.nonSample ? root.nonSampleSuffix : "") : ""
                        color: dragChip.nonSample ? Theme.accent : Theme.textOnAccent
                        font.pixelSize: Theme.fontCaption
                        font.weight: Theme.weightSemiBold
                    }
                }
            }

            // Átnézendő (S4, „A” koncepció): a fókusz-nézet; a lista helyén.
            ReviewFocusView {
                id: reviewView
                objectName: "reviewView"
                anchors.fill: parent
                visible: root.reviewShown
                vm: editorVm
                tab: root
            }
        }

        // Söprés (S6): a szakasz végén az összesítő (az „Imént” sor fölött).
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

        // Az „Imént” sor: a lista alatt, FIX helyen (Javítás / Átnézendő nézetben mindig; az
        // Olvasásban csak egy átsorolás után). Nem tűnik el magától.
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

        // Söprés (S6): a fagyasztott ablak, „mögötted: N sor újra kétes” (kattintásra a lista), az
        // előtted álló kétes sorok, a szakasz, a mód (megálló / folyamatos) és a tempó — a lejátszó
        // (térkép-dokk) közvetlen szomszédjában.
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
            // Az Átnézendőben a kijelölés a fókusz-sor (a kártya gombjai döntenek): nincs sáv.
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
        onTagAdded: text => { if (root.shell && root.shell.toast) root.shell.toast(text, "tags") }
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
        // A „Címkéi” javító link: a Személyek ablak a Címkék szakaszra görgetve (C4).
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

    // Söprés (S6): „mögötted: N sor újra kétes” listája — a soridők, kattintásra a sorra ugrás.
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

    // „Ő nem volt ott": a „Ki volt ott?" jelölés is frissül (a résztvevő kötése megszűnik). Csak
    // a művelethez jön létre (ne töltse a résztvevőket minden megbeszélés-váltáskor).
    Component {
        id: participantsComponent
        ParticipantsViewModel {}
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
        // „Nem azonosítható” (0): a név marad, a hang-elemzés figyelmen kívül hagyja; újra: vissza.
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
        } else if (s === "reviewFocus") {
            reviewView.demoPlayPos = 0.42      // az auto-lejátszás épp szól (képernyőkép)
        } else if (s === "speakerWhy") {
            list.positionViewAtBeginning()
            const lane = Math.max(0, keyOf("Fehér Gábor"))
            root.openSpeakerPopover(editorVm.lanes[lane].key, railHeader)
            speakerWhy.x = Math.round(root.railX + 4 + lane * Theme.laneWidth - 7)
        } else if (s === "ignoredRows") {
            list.positionViewAtBeginning()
        } else if (s === "imentLog") {
            changeBar.logOpen = true
        } else if (s === "shiftPreview") {
            // Lenyomott Shift + rámutatás a 3. sor egy másik oszlopára: „→ Név · nem minta”.
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
            // Söprés (S6): a VM állítja be a pillanatot; a lejátszó-pozíciót itt adjuk (nincs lejátszó).
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
