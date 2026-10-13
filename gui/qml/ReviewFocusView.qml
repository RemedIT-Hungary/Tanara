import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Az „Átnézendő” nézet, „A” koncepció (Fókusz-sor; koncepció-dokumentum 2. fejezet): egyszerre
// egy kétes sor a RÖGZÍTETT helyű fókusz-kártyán (ReviewFocusCard), automatikusan megszólal, és
// egy billentyűvel eldől. Fölötte és alatta 3–3 halvány kontextus-sor (az átirat sorai idő szerint,
// címke nélkül, soronként az időugrással), alul az időrendi munkalista-csík (a kurzor), felül a
// haladás („N / M”), a kör száma, a magától eldőlt sorok számlálója („Mind megerősítem”), a
// „+K új vár” számláló és az auto-lejátszás kapcsolója. A munkalista a ReviewQueueModel (vm.reviewQueue) fagyasztott
// pillanatképe: a háttér-elemzés sosem rendezi át, csak a „+K új vár” számláló nő.
//
// A kártya helye (y) és mérete sosem változik: ami alá kerül (szennyezett mag sávja, az S5
// ajánlat-sávja a `offerSlot` Loaderben), az az alsó kontextus-sorok helyét veszi el. 760 px alatt
// a nézet saját görgetéssel fér el.
//
// Billentyűk (fizikai hely szerint, TranscriptKeys; a TranscriptTab hívja a handleKey()-t):
//   Enter alapértelmezés (a javasolt név; ha nincs javaslat: a név marad) · J a név marad ·
//   1–9 a sín n. beszélője · + új beszélő · Shift+… nem minta · 0 nem azonosítható · M Más… (a
//   sor panelje) · → Később · ← / ↑ előző tétel · ↓ következő tétel (bármilyen állapotú) ·
//   B / Shift+B (a Tab) következő / előző NYITOTT · Ctrl+Enter Mind megerősítem (a magától
//   eldőlt sorok, egy lépés) · Szóköz lejátszás / szünet ·
//   R újra az elejéről · , / . az előző / következő kontextus-sor · Esc a Javítás nézet ugyanazon
//   a soron · Ctrl+Z (a Tab Shortcutja) visszavonás: a fókusz a visszavont sorra ugrik.
//   Kör-zárásnál: Enter következő kör · → később hagyottak · K kész · Ctrl+Enter Mind megerősítem.
// A magától eldőlt (alreadyDecided) sorokat a kurzor átugorja (S7): nem nyitottak; ↑ / ← / ↓ a
// csíkon rájuk is lép, ott a kártya normálisan mutatja (Enter = a név marad).
//   Nyitó-javaslatnál (S5): Enter Mind az N → Y (✗ szúrópróba után: Egyenként) · Shift+Enter nem
//   minta · E Egyenként · → Később · 1–3 szúrópróba lejátszása · Shift+1–3 ✓ / ✗ váltás · Szóköz
//   szünet · Esc Javítás. Mintázat-ajánlatnál (a fókusz-kártya billentyűi közben is élnek):
//   I Igen · Shift+I Igen, nem minta · N Nem · L Megmutatom / Elrejtem.
// Az S5 helyei: `openerComponent` + `openerShown` (a kör eleji „nyitó” állapot-ág, a kártya
// helyén; ReviewFocusOpener) és `offerComponent` (az `offerSlot` Loader, a kártya ALATT;
// ReviewFocusOffer — csak akkor nem null, ha van élő ajánlat).
Item {
    id: root

    required property var vm                // TranscriptEditorViewModel
    required property var tab               // TranscriptTab (lejátszó, panelek, nézet-váltás)

    readonly property var queue: vm.reviewQueue
    readonly property var player: tab.player
    readonly property string currentUtterance: queue.currentUtterance
    readonly property var current: queue.current
    readonly property alias card: card
    readonly property alias flick: flick
    readonly property alias offerSlot: offerSlot

    // ---- S5 helyek ----
    // A kör eleji nyitó-javaslat: ha nem null és `openerShown`, a kártya helyén ez látszik.
    property Component openerComponent: openerDefault
    property bool openerShown: false
    // Az ajánlat-sáv (mintázat-ajánlat) a kártya ALATT: ha nem null, az `offerSlot` ezt tölti be,
    // és az alsó kontextus-sorok helyét veszi el.
    property Component offerComponent: offerActive ? offerDefault : null

    // ---- S5: nyitó-javaslatok (a kör elején, egymás után) ----
    property var openerIds: []              // a kör nyitóinak groupId-jei (a kör elején)
    property int openerIndex: -1
    property var opener: ({})               // a mostani nyitó (queue.openers() friss eleme)
    property var openerSpots: []            // [{ utteranceId, timeLabel, lineText, startMs, endMs }]
    property var spotHeard: [false, false, false]
    property var spotOk: [true, true, true]
    readonly property bool openerDoubt: spotHeard.some((h, i) => h && !spotOk[i])
    readonly property int playingSpot: {
        if (!playerPlaying) return -1
        for (let i = 0; i < openerSpots.length; ++i) {
            const sp = openerSpots[i]
            if (positionMs >= sp.startMs - 200 && positionMs <= sp.endMs + 300) return i
        }
        return -1
    }
    // ---- S5: mintázat-ajánlat (a kártya alatt) ----
    property var offerData: ({})
    property var offerLines: []
    property bool offerExpanded: false
    readonly property bool offerActive: phase === "focus" && (offerData.key || "") !== "" && offerLines.length > 0
    readonly property string playingOfferLine: {
        if (!playerPlaying || !offerExpanded) return ""
        for (let i = 0; i < offerLines.length; ++i)
            if (positionMs >= offerLines[i].startMs && positionMs <= offerLines[i].endMs) return offerLines[i].utteranceId
        return ""
    }

    // Állapot-ág: "empty" | "summary" | "opener" | "focus"
    readonly property bool emptyState: queue.active && queue.count === 0 && queue.incomingCount === 0
    readonly property bool summaryShown: queue.active && queue.roundComplete && !emptyState
    readonly property string phase: !queue.active ? "" : emptyState ? "empty" : summaryShown ? "summary"
                                    : openerShown && openerComponent ? "opener" : "focus"
    // Képernyőkép (demó): a kártya „szól” ennél a helyzetnél (0..1; < 0: a valódi lejátszó).
    property real demoPlayPos: -1
    property var summary: ({})
    property var contextLines: []

    // Rögzített geometria (a flick tartalmában): a kártya y-ja minden fókusz-állapotban ugyanaz.
    readonly property int topPad: 12
    readonly property int ctxHeight: 30
    readonly property int ctxGap: 4
    readonly property int ctxCount: 3                // kontextus-sor a kártya fölött és alatt
    readonly property int cardY: topPad + ctxCount * ctxHeight + ctxCount * ctxGap + 4
    readonly property real cardWidth: Math.max(320, Math.min(flick.width - 48, 1040))

    // A haladás: a kör sorai közül a már nem nyitottak (a „később” sorok nem számítanak késznek).
    readonly property int doneCount: queue.count - queue.openCount - queue.laterCount
    readonly property int totalCount: queue.count

    // ---- lejátszás ----
    readonly property int playTailMs: 200      // + a lejátszó 100 ms utó-futása ≈ 0,3 s ráhagyás
    readonly property int positionMs: (player && player.positionMs) || 0
    readonly property bool playerPlaying: !!player && player.playing === true
    readonly property int cardStartMs: current.startMs || 0
    readonly property int cardEndMs: current.endMs || 0
    readonly property bool cardPlaying: playerPlaying && currentUtterance !== ""
                                        && positionMs >= cardStartMs - 400 && positionMs <= cardEndMs + 600
    readonly property real cardPlayPos: cardEndMs > cardStartMs
                                        ? Math.max(0, Math.min(1, (positionMs - cardStartMs) / (cardEndMs - cardStartMs))) : 0

    // ---- munkalista ----
    // A pillanatkép a háttér-elemzés VÉGÉN készül (különben a még futó elemzés eredménye rögtön
    // „+N új vár”-ként jönne); addig a „keresése…” felirat látszik.
    function ensureStarted() {
        if (!root.visible || !vm.hasTranscript) return
        if (!vm.queueActive) {
            if (vm.reviewRunning || vm.embeddingRunning) return     // a startWait időzítő újrapróbálja
            queue.start()
        }
        refreshContext()
        refreshSummary()
    }
    Timer {
        id: startWait
        interval: 250
        repeat: true
        running: root.visible && !root.queue.active && root.vm.hasTranscript
        onTriggered: root.ensureStarted()
    }
    function refreshContext() {
        root.contextLines = currentUtterance !== "" ? vm.contextLines(currentUtterance, ctxCount, ctxCount) : []
    }
    // Időugrás (ms) a sor és a kártya felőli szomszédja között (fölötte: a következő sorig, alatta:
    // az előző sortól); -1: nincs sor.
    function gapFor(offset) {
        const line = contextAt(offset)
        if (line.utteranceId === undefined) return -1
        const near = offset < 0 ? (offset === -1 ? { startMs: cardStartMs } : contextAt(offset + 1))
                                : (offset === 1 ? { endMs: cardEndMs } : contextAt(offset - 1))
        if (offset < 0) return near.startMs === undefined ? -1 : Math.max(0, near.startMs - line.endMs)
        return near.endMs === undefined ? -1 : Math.max(0, line.startMs - near.endMs)
    }
    function refreshSummary() {
        root.summary = root.summaryShown ? queue.roundSummary() : ({})
    }
    function contextAt(offset) {
        for (let i = 0; i < contextLines.length; ++i) if (contextLines[i].offset === offset) return contextLines[i]
        return ({})
    }
    // A fókusz-sor a VM kijelölt sora is (a dokk és a lejátszó követi, mint a Javításban).
    function syncRow() {
        const id = currentUtterance
        if (id === "") return
        const info = vm.lineInfo(id)
        if (info.startMs === undefined) return
        const row = vm.rowForTime(info.startMs)
        if (row < 0 || tab.cursorSync) return
        tab.cursorSync = true
        vm.selectOnly(row)
        tab.cursorSync = false
    }
    // A sor lejátszása a határaira (+ ráhagyás); a kártya sora lesz a kijelölt.
    function playUtterance(id, tailMs) {
        if (!player || id === "") return false
        const info = vm.lineInfo(id)
        if (info.startMs === undefined) return false
        const b = vm.playBounds(info.startMs, info.endMs)
        player.playRange(info.startMs, info.endMs + tailMs, b.minStartMs, tailMs > 0 ? -1 : b.maxEndMs)
        return true
    }
    function playCard() {
        syncRow()
        return playUtterance(currentUtterance, playTailMs)
    }
    function togglePlay() {
        if (!player) return false
        if (playerPlaying) {
            player.pause()
            return true
        }
        return playCard()
    }
    function playContext(offset) {
        const line = contextAt(offset)
        return playUtterance(line.utteranceId || "", 0)
    }
    property string lastAutoPlayed: ""
    function autoPlay() {
        if (!root.visible || root.phase !== "focus" || !vm.reviewAutoPlay || !tab.canPlay) return
        if (currentUtterance === "" || currentUtterance === lastAutoPlayed) return
        lastAutoPlayed = currentUtterance
        playCard()
    }

    // ---- ítéletek (a kurzor sorára) ----
    // A döntés után a modell maga lép a következő nyitott sorra; ha nem lépett (a döntés nem
    // változtatott, pl. egy már megerősített sor „Jó így”-e), továbblépünk.
    function decide(action) {
        const before = currentUtterance
        if (before === "") return false
        const ok = action(before)
        if (queue.currentUtterance === before && (queue.current.state || "") !== "pending") queue.advance()
        tab.forceActiveFocus()
        return ok
    }
    function hasSuggestion() {
        const c = queue.current
        return (c.state || "") !== "alreadyDecided" && ((c.likelyName || "") !== "" || (c.likelyNewSide || "") !== "")
               && (c.likelyKey || "") !== (c.speakerKey || "")
    }
    function acceptDefault(noisy) {
        const c = queue.current
        if (!hasSuggestion()) return confirmCurrent(noisy)
        return decide(id => (c.likelyKey || "") !== "" ? vm.moveUtteranceToSpeaker(id, c.likelyKey, noisy === true)
                          : (c.likelyName || "") !== "" ? vm.moveUtteranceToPerson(id, c.likelyName, noisy === true)
                          : vm.moveUtteranceToNewParticipant(id, noisy === true))
    }
    function confirmCurrent(noisy) {
        return decide(id => vm.confirmUtterance(id, noisy === true))
    }
    function toggleIgnored() {
        return decide(id => vm.setUtteranceIgnored(id, vm.lineInfo(id).ignored !== true))
    }
    function moveToLane(lane, noisy) {
        const lanes = vm.lanes
        if (lane < 0 || lane >= lanes.length || currentUtterance === "") return false
        const key = lanes[lane].key
        if (key === (queue.current.speakerKey || "")) return confirmCurrent(noisy)
        return decide(id => vm.moveUtteranceToSpeaker(id, key, noisy === true))
    }
    // „+”: a sor egy új, névtelen beszélőhöz.
    function moveToNew(noisy) {
        if (currentUtterance === "") return false
        return decide(id => vm.moveUtteranceToNewParticipant(id, noisy === true))
    }
    // „Mind megerősítem” (Ctrl+Enter): a magától eldőlt sorok (≥ 1,5 s) egy visszavonási lépésben.
    function confirmSettled() {
        const n = vm.confirmSettled()
        tab.forceActiveFocus()
        return n > 0
    }
    function later() {
        const ok = queue.later()
        tab.forceActiveFocus()
        return ok
    }
    // „M”: a sor panelje (a Tab LinePopovere) a „Más…” gombról.
    function fixCurrent() {
        if (currentUtterance === "" || root.phase !== "focus") return false
        tab.openLinePopoverFor(currentUtterance, card.otherAnchor)
        return true
    }
    // B / Shift+B, a térkép-dokk sor-léptetője: következő / előző NYITOTT sor.
    // A nézetbe lépéskor (még nincs munkalista) a kurzor az első nyitott soron marad.
    function step(direction) {
        if (!queue.active) {
            ensureStarted()
            return queue.currentIndex >= 0
        }
        return direction < 0 ? queue.retreat() : queue.advance()
    }
    // Esc / „Kész”: vissza a Javítás nézetbe, ugyanazon a soron.
    function openFix() {
        const id = currentUtterance
        const info = id !== "" ? vm.lineInfo(id) : ({})
        tab.setViewMode("fix")
        if (info.startMs === undefined) return true
        Qt.callLater(() => {
            const row = vm.rowForTime(info.startMs)
            if (row < 0) return
            tab.cursorSync = true
            vm.selectOnly(row)
            tab.cursorSync = false
            tab.revealRow(row)
        })
        return true
    }
    function nextRound() {
        queue.startNextRound(true, true)
        tab.forceActiveFocus()
    }
    function laterRound() {
        queue.startNextRound(false, true)
        tab.forceActiveFocus()
    }

    // ---- S5: nyitó-javaslatok ----
    function lineOf(id) {
        const m = queue.get(queue.indexOf(id))
        return { utteranceId: id, timeLabel: m.timeLabel || "", lineText: m.lineText || "",
                 startMs: m.startMs || 0, endMs: m.endMs || 0 }
    }
    function openerById(groupId) {
        const list = queue.openers()
        for (let i = 0; i < list.length; ++i) if (list[i].groupId === groupId) return list[i]
        return null
    }
    // A kör elején (start / új kör): a nyitók, amelyeknek van még nyitott sora.
    function startOpeners() {
        const ids = []
        for (const o of queue.openers()) if ((o.pendingCount || 0) > 0) ids.push(o.groupId)
        root.openerIds = ids
        root.openerIndex = -1
        showOpener(0)
    }
    // Az i. (vagy az utána következő, még nyitott sorú) nyitó; ha nincs több: a fókusz.
    function showOpener(i) {
        for (; i < openerIds.length; ++i) {
            const o = openerById(openerIds[i])
            if (!o || (o.pendingCount || 0) === 0) continue
            root.openerIndex = i
            root.opener = o
            root.openerSpots = (o.spotIds || []).map(id => lineOf(id))
            root.spotHeard = [false, false, false]
            root.spotOk = [true, true, true]
            root.openerShown = true
            applyOpenerDemo()
            return true
        }
        root.openerIndex = -1
        root.opener = ({})
        root.openerSpots = []
        root.spotHeard = [false, false, false]
        root.spotOk = [true, true, true]
        root.openerShown = false
        return false
    }
    function nextOpener() {
        showOpener(openerIndex + 1)
        tab.forceActiveFocus()
    }
    function pendingOpenerIds() {
        const out = []
        for (const id of (opener.ids || [])) {
            const i = queue.indexOf(id)
            if (i >= 0 && (queue.get(i).state || "") === "pending") out.push(id)
        }
        return out
    }
    // Enter: Mind az N → Y (egy visszavonási lépés); ✗ szúrópróba után az alapértelmezés az Egyenként.
    function openerDefault_(noisy) {
        return openerDoubt ? openerSingle() : openerAccept(noisy)
    }
    function openerAccept(noisy) {
        if (phase !== "opener" || !opener.groupId) return false
        const ok = vm.applyReviewGroup(opener.groupId, noisy === true)
        nextOpener()
        return ok
    }
    // E: a nyitó sorai a munkalista elejére (a kurzor helyére), és egyenként jönnek.
    function openerSingle() {
        if (phase !== "opener") return false
        queue.bringToFront(pendingOpenerIds())
        nextOpener()
        return true
    }
    // →: a nyitó kimarad, a sorok a helyükön maradnak.
    function openerLater() {
        if (phase !== "opener") return false
        nextOpener()
        return true
    }
    function playSpot(k) {
        if (k < 0 || k >= openerSpots.length) return false
        const sp = openerSpots[k]
        tab.playLine(sp.startMs, sp.endMs)
        if (!spotHeard[k]) {
            const h = spotHeard.slice()
            h[k] = true
            root.spotHeard = h
        }
        tab.forceActiveFocus()
        return true
    }
    function toggleSpot(k) {
        if (k < 0 || k >= openerSpots.length || !spotHeard[k]) return false
        const o = spotOk.slice()
        o[k] = !o[k]
        root.spotOk = o
        tab.forceActiveFocus()
        return true
    }
    // Demó (képernyőkép): a szúrópróba állapota.
    function applyOpenerDemo() {
        const s = tab.demoState || ""
        if (s === "reviewFocusOpener") {
            root.spotHeard = [true, false, false]
        } else if (s === "reviewFocusOpenerDoubt") {
            root.spotHeard = [true, true, false]
            root.spotOk = [true, false, true]
        }
    }

    // ---- S5: mintázat-ajánlat ----
    function refreshOffer() {
        const p = queue.active ? queue.recentPattern() : ({})
        const key = p.key || ""
        if (key !== (offerData.key || "")) root.offerExpanded = (tab.demoState || "") === "reviewFocusOfferList"
        root.offerData = p
        root.offerLines = key === "" ? [] : (p.sameAhead || []).map(id => lineOf(id))
    }
    function offerYes(noisy) {
        if (!offerActive) return false
        const ids = offerLines.map(l => l.utteranceId)
        const ok = vm.moveUtterancesToSpeaker(ids, offerData.proposedKey, noisy === true)
        root.offerExpanded = false
        refreshOffer()
        tab.forceActiveFocus()
        return ok
    }
    function offerNo() {
        if (!offerActive) return false
        queue.dismissPattern(offerData.key)
        tab.forceActiveFocus()
        return true
    }
    function toggleOfferList() {
        if (!offerActive) return false
        root.offerExpanded = !offerExpanded
        tab.forceActiveFocus()
        return true
    }
    function playOfferLine(id) {
        const l = lineOf(id)
        if (l.endMs > l.startMs) tab.playLine(l.startMs, l.endMs)
        tab.forceActiveFocus()
    }

    // A TranscriptTab Keys.onPressed-je hívja (k = TranscriptKeys.decode(event)); igaz: kezelve.
    function handleKey(k, event) {
        if (!queue.active) return false
        // Ctrl+Enter: Mind megerősítem (fókuszban és a kör-zárásnál).
        if (k.name === "Enter" && k.ctrl && !k.alt && !k.meta && !k.shift
                && (root.phase === "focus" || root.phase === "summary")) {
            confirmSettled()
            return true
        }
        if (root.phase === "summary") {
            if (k.plain && !k.shift && k.name === "Enter") { if ((summary.incoming || 0) + (summary.later || 0) > 0) nextRound(); return true }
            if (k.plain && !k.shift && k.name === "Right") { if ((summary.later || 0) > 0) laterRound(); return true }
            if (k.plain && !k.shift && event.key === Qt.Key_K) return openFix()
            if (k.name === "Escape") return openFix()
            return false
        }
        if (root.phase === "opener") {
            if (k.name === "Escape") return openFix()
            if (!k.plain) return false
            if (k.name === "Enter") { openerDefault_(k.shift); return true }
            if (event.key === Qt.Key_E) { if (!k.shift) openerSingle(); return true }
            if (k.name === "Right") { if (!k.shift) openerLater(); return true }
            if (k.name === "digit" && k.digit >= 1 && k.digit <= 3) {
                if (k.shift) toggleSpot(k.digit - 1)
                else playSpot(k.digit - 1)
                return true
            }
            if (k.name === "Space") { if (player && playerPlaying) player.pause(); return true }
            return false
        }
        if (root.phase !== "focus") return k.name === "Escape" ? openFix() : false
        if (k.name === "Escape") return openFix()
        if (!k.plain) return false
        // A mintázat-ajánlat billentyűi (a kártya többi billentyűje közben is él).
        if (root.offerActive) {
            if (event.key === Qt.Key_I) { offerYes(k.shift); return true }
            if (event.key === Qt.Key_N && !k.shift) { offerNo(); return true }
            if (event.key === Qt.Key_L && !k.shift) { toggleOfferList(); return true }
        }
        // „+” (a számsor „Új beszélő…” chipje; a magyar kiosztáson a Shift+3 a számsoré marad).
        if (k.name === "" && event.key === Qt.Key_Plus) { moveToNew(false); return true }
        switch (k.name) {
        case "Enter": acceptDefault(k.shift); return true
        case "J": confirmCurrent(k.shift); return true
        case "digit":
            if (k.digit === 0) { if (!k.shift) toggleIgnored(); return true }
            moveToLane(k.digit - 1, k.shift)
            return true
        case "M": if (!k.shift) fixCurrent(); return true
        case "Right": if (!k.shift) later(); return true
        case "Left": case "Up": if (!k.shift) queue.previous(); return true
        case "Down": if (!k.shift) queue.next(); return true
        case "Space": togglePlay(); return true
        case "R": playCard(); return true
        case "Comma": playContext(-1); return true
        case "Period": playContext(1); return true
        }
        return false
    }

    onVisibleChanged: if (visible) Qt.callLater(root.ensureStarted)
    Component.onCompleted: Qt.callLater(root.ensureStarted)
    onCurrentUtteranceChanged: {
        refreshContext()
        if (!root.visible) return
        syncRow()
        Qt.callLater(root.autoPlay)
    }
    onPhaseChanged: {
        refreshSummary()
        if (phase === "focus") Qt.callLater(root.autoPlay)
    }
    Connections {
        target: root.queue
        // Kör eleje (start / új kör): a nyitó-javaslatok sorra.
        function onRoundChanged() { root.startOpeners() }
        function onActiveChanged() { if (!root.queue.active) root.showOpener(root.openerIds.length) }
        function onPatternChanged() { root.refreshOffer() }
        function onCountsChanged() { root.refreshSummary() }
        function onIncomingChanged() {
            // Üres munkalistánál (pl. újraellenőrzés után) az új kétes sorokkal új kör indul.
            if (root.visible && root.queue.active && root.queue.count === 0 && root.queue.incomingCount > 0)
                Qt.callLater(() => { if (root.queue.count === 0 && root.queue.incomingCount > 0) root.queue.start() })
            root.refreshSummary()
        }
        function onCurrentChanged() {
            root.refreshContext()
            root.refreshOffer()         // a sameAhead a kurzor sorát kihagyja
        }
    }
    Connections {
        target: root.vm
        // Új megbeszélés: a VM leállította a munkalistát; ha látszunk, újraindul.
        function onSessionChanged() {
            root.lastAutoPlayed = ""
            Qt.callLater(root.ensureStarted)
        }
        function onSpeakersChanged() { root.refreshContext() }
        // Visszavonás után a fókusz a visszavont sorra (ha a modell nem oda ugrott).
        function onDecisionsChanged() {
            root.refreshContext()
            const d = root.vm.lastDecision
            if (d.undone !== true || !d.utteranceId) return
            const i = root.queue.indexOf(d.utteranceId)
            if (i >= 0 && root.queue.currentIndex !== i) root.queue.jumpTo(i)
        }
    }

    // ---- fejléc: kör, haladás, „+K új vár”, auto-lejátszás ----
    Rectangle {
        id: header
        objectName: "reviewFocusHeader"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 44
        color: Theme.surface
        RowLayout {
            anchors { fill: parent; leftMargin: 24; rightMargin: 16 }
            spacing: 12
            TLabel {
                objectName: "reviewFocusRound"
                text: qsTr("%1. kör").arg(root.queue.round)
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightSemiBold
            }
            Row {
                objectName: "reviewRunning"
                visible: root.vm.reviewRunning
                spacing: 6
                TSpinner { anchors.verticalCenter: parent.verticalCenter; size: 12; color: Theme.textMuted }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("elemzés fut…")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
            }
            Item { Layout.fillWidth: true }
            // A magától eldőlt sorok (a kurzor átugorja): „N magától eldőlt · Mind megerősítem”.
            Row {
                objectName: "reviewFocusSettledBar"
                visible: root.queue.alreadyDecidedCount > 0
                spacing: 8
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5
                    TIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "check"
                        size: 14
                        strokeWidth: 2.2
                        color: Theme.success
                    }
                    TLabel {
                        objectName: "reviewFocusSettledCount"
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("%n magától eldőlt", "", root.queue.alreadyDecidedCount)
                        font.pixelSize: Theme.fontSmall
                    }
                }
                ReviewFocusKeyButton {
                    objectName: "reviewFocusConfirmSettled"
                    anchors.verticalCenter: parent.verticalCenter
                    implicitHeight: 28
                    keyText: "Ctrl+Enter"
                    text: qsTr("Mind megerősítem")
                    font.pixelSize: Theme.fontSmall
                    enabled: root.queue.confirmableCount > 0
                    toolTipText: root.queue.confirmableCount > 0
                                 ? qsTr("%n sor megerősítése egy lépésben (minta lesz); a rövid (1,5 mp alatti) sorokhoz nem nyúl", "", root.queue.confirmableCount)
                                 : qsTr("A magától eldőlt sorok mind rövidek (1,5 mp alatt): nem lennének minta")
                    onClicked: root.confirmSettled()
                }
                TDivider { vertical: true; height: 20; anchors.verticalCenter: parent.verticalCenter }
            }
            TLabel { text: qsTr("haladás"); muted: true; font.pixelSize: Theme.fontCaption }
            TProgressBar {
                objectName: "reviewFocusProgressBar"
                Layout.preferredWidth: Math.min(150, Math.max(60, root.width * 0.12))
                from: 0
                to: Math.max(1, root.totalCount)
                value: root.doneCount
                thickness: 6
            }
            TLabel {
                objectName: "reviewFocusProgress"
                text: qsTr("%1 / %2").arg(root.doneCount).arg(root.totalCount)
                mono: true
                font.pixelSize: Theme.fontSmall
            }
            // „+K új vár”: fix helyen; a szám nő, a lista nem mozdul. Kattintás: a lista végére.
            Rectangle {
                id: incoming
                objectName: "reviewFocusIncoming"
                readonly property int count: root.queue.incomingCount
                Layout.preferredHeight: 26
                Layout.preferredWidth: incomingRow.implicitWidth + 16
                radius: 13
                color: count > 0 ? Theme.warnSoft : "transparent"
                border.width: 1
                border.color: count > 0 ? Theme.warnLine : Theme.border
                Row {
                    id: incomingRow
                    anchors.centerIn: parent
                    spacing: 5
                    TIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "inbox"
                        size: 13
                        color: incoming.count > 0 ? Theme.warnInk : Theme.textMuted
                    }
                    TLabel {
                        objectName: "reviewFocusIncomingText"
                        anchors.verticalCenter: parent.verticalCenter
                        text: incoming.count > 0 ? qsTr("+%1 új vár").arg(incoming.count) : qsTr("nincs új")
                        font.pixelSize: Theme.fontCaption
                        font.weight: Theme.weightSemiBold
                        color: incoming.count > 0 ? Theme.warnInk : Theme.textMuted
                    }
                }
                HoverHandler { id: incomingHover; cursorShape: incoming.count > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                TapHandler {
                    onTapped: {
                        root.queue.admitIncoming()
                        root.tab.forceActiveFocus()
                    }
                }
                TToolTip {
                    visible: incomingHover.hovered
                    text: incoming.count > 0
                          ? qsTr("A háttér-elemzés %1 új kétes sort talált. Kattintás: a kör végére kerülnek (előtted semmi nem mozdul).").arg(incoming.count)
                          : qsTr("A háttér-elemzés új találatai itt gyűlnek; a munkalista közben nem változik.")
                }
            }
            TDivider { vertical: true; Layout.preferredHeight: 20 }
            TSwitch {
                objectName: "reviewAutoPlay"
                text: qsTr("Auto-lejátszás")
                checked: root.vm.reviewAutoPlay
                focusPolicy: Qt.NoFocus
                font.pixelSize: Theme.fontSmall
                onToggled: {
                    root.vm.reviewAutoPlay = checked
                    root.tab.forceActiveFocus()
                }
                TToolTip {
                    visible: parent.hovered
                    text: qsTr("A kártyára lépő sor magától megszólal (a sor + 0,3 mp)")
                }
            }
        }
        TDivider { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } }
    }

    // ---- fókusz-terület: kontextus + kártya + az alá kerülő elemek; szűk helyen saját görgetés ----
    Flickable {
        id: flick
        objectName: "reviewFocusFlick"
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: worklist.top }
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, content.height + 16)
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        T.ScrollBar.vertical: TScrollBar {}

        Item {
            id: content
            width: flick.width
            height: root.phase === "summary" ? summaryCard.y + summaryCard.height
                  : root.phase === "opener" ? openerSlot.y + openerSlot.height
                  : lower.y + lower.height
            visible: root.phase === "focus" || root.phase === "opener" || root.phase === "summary"

            // Felső kontextus-sorok (−2, −1).
            Column {
                id: upper
                objectName: "reviewFocusUpper"
                visible: root.phase === "focus"
                x: (content.width - root.cardWidth) / 2
                y: root.topPad
                width: root.cardWidth
                spacing: root.ctxGap
                Repeater {
                    model: [-3, -2, -1]
                    delegate: ReviewFocusContextRow {
                        required property int modelData
                        objectName: "reviewFocusContext"
                        width: upper.width
                        line: root.contextAt(modelData)
                        gapMs: { void root.contextLines; return root.gapFor(modelData) }
                        playingNow: root.playerPlaying && line.startMs !== undefined
                                    && root.positionMs >= line.startMs && root.positionMs < line.endMs
                        onPlayRequested: root.playContext(modelData)
                    }
                }
            }

            ReviewFocusCard {
                id: card
                visible: root.phase === "focus"
                x: (content.width - root.cardWidth) / 2
                y: root.cardY
                width: root.cardWidth
                item: root.current
                lanes: root.vm.lanes
                shiftHeld: root.tab.shiftHeld
                playing: root.demoPlayPos >= 0 || root.cardPlaying
                playPos: root.demoPlayPos >= 0 ? root.demoPlayPos : root.cardPlayPos
                durationMs: Math.max(0, root.cardEndMs - root.cardStartMs)
                onEnterClicked: root.acceptDefault(root.tab.shiftHeld)
                onConfirmClicked: root.confirmCurrent(root.tab.shiftHeld)
                onIgnoreClicked: root.toggleIgnored()
                onOtherClicked: root.fixCurrent()
                onLaterClicked: root.later()
                onLaneClicked: lane => root.moveToLane(lane, root.tab.shiftHeld)
                onNewSpeakerClicked: root.moveToNew(root.tab.shiftHeld)
                onPlayClicked: {
                    root.togglePlay()
                    root.tab.forceActiveFocus()
                }
            }

            // A kör eleji nyitó-javaslat (S5) a kártya helyén.
            Loader {
                id: openerSlot
                objectName: "reviewFocusOpenerSlot"
                active: root.phase === "opener"
                x: card.x
                y: root.topPad
                width: root.cardWidth
                sourceComponent: root.openerComponent
            }

            // A kártya ALATT: szennyezett mag sávja, S5 ajánlat-sáv, alsó kontextus-sorok (+1, +2).
            // Ami ide kerül, az a kontextus-sorok helyét veszi el — a kártyáét sosem.
            Column {
                id: lower
                objectName: "reviewFocusLower"
                visible: root.phase === "focus"
                x: card.x
                y: root.cardY + card.height + root.ctxGap + 4
                width: root.cardWidth
                spacing: root.ctxGap
                ContaminatedCoreBanner {
                    objectName: "contaminatedBanner"
                    visible: (root.vm.contaminated.speakerKey || "") !== ""
                    width: lower.width
                    info: root.vm.contaminated
                    onSplitRequested: key => root.vm.splitSpeaker(key)
                    onWhyRequested: (key, anchor) => root.tab.openSpeakerPopover(key, anchor)
                }
                Loader {
                    id: offerSlot
                    objectName: "reviewFocusOfferSlot"
                    width: lower.width
                    active: root.offerComponent !== null
                    sourceComponent: root.offerComponent
                    visible: active && !!item
                }
                Repeater {
                    model: offerSlot.visible ? [] : [1, 2, 3]
                    delegate: ReviewFocusContextRow {
                        required property int modelData
                        objectName: "reviewFocusContext"
                        width: lower.width
                        line: root.contextAt(modelData)
                        gapMs: { void root.contextLines; return root.gapFor(modelData) }
                        playingNow: root.playerPlaying && line.startMs !== undefined
                                    && root.positionMs >= line.startMs && root.positionMs < line.endMs
                        onPlayRequested: root.playContext(modelData)
                    }
                }
            }

            ReviewFocusSummary {
                id: summaryCard
                visible: root.phase === "summary"
                x: card.x
                y: root.topPad
                width: root.cardWidth
                summary: root.summary
                onNextClicked: root.nextRound()
                onLaterClicked: root.laterRound()
                onDoneClicked: root.openFix()
                onConfirmSettledClicked: root.confirmSettled()
            }
        }
    }

    // ---- munkalista-csík ----
    ReviewFocusWorklist {
        id: worklist
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        visible: root.phase === "focus" || root.phase === "opener" || root.phase === "summary"
        height: visible ? implicitHeight : 0
        queue: root.queue
        onJump: index => {
            root.queue.jumpTo(index)
            root.tab.forceActiveFocus()
        }
    }

    // ---- S5: a nyitó-javaslat és az ajánlat-sáv alapértelmezett komponensei ----
    Component {
        id: openerDefault
        ReviewFocusOpener {
            opener: root.opener
            spots: root.openerSpots
            lanes: root.vm.lanes
            position: root.openerIndex
            total: root.openerIds.length
            remainAfter: Math.max(0, root.queue.openCount - (root.opener.pendingCount || 0))
            heard: root.spotHeard
            judged: root.spotOk
            playingSpot: root.playingSpot
            shiftHeld: root.tab.shiftHeld
            onAcceptClicked: root.openerAccept(root.tab.shiftHeld)
            onSingleClicked: root.openerSingle()
            onLaterClicked: root.openerLater()
            onSpotClicked: index => root.playSpot(index)
            onVerdictClicked: index => root.toggleSpot(index)
        }
    }
    Component {
        id: offerDefault
        ReviewFocusOffer {
            offer: root.offerData
            lines: root.offerLines
            expanded: root.offerExpanded
            shiftHeld: root.tab.shiftHeld
            playingId: root.playingOfferLine
            onShowClicked: root.toggleOfferList()
            onYesClicked: root.offerYes(root.tab.shiftHeld)
            onNoClicked: root.offerNo()
            onPlayClicked: id => root.playOfferLine(id)
        }
    }

    // ---- nincs átnézendő sor (innen is kérhető az újraellenőrzés) ----
    Column {
        objectName: "reviewEmpty"
        visible: root.phase === "empty" || !root.queue.active
        anchors.centerIn: parent
        spacing: Theme.space3
        TLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.vm.reviewRunning || !root.queue.active ? qsTr("Az átnézendő sorok keresése…")
                                        : qsTr("Nincs átnézendő sor — minden megszólalás beszélője rendben van.")
            muted: true
        }
        TButton {
            focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad
            objectName: "emptyFilterRecheck"
            visible: root.phase === "empty" && root.vm.voiceAvailable && !root.vm.embeddingRunning && !root.vm.reviewRunning
            anchors.horizontalCenter: parent.horizontalCenter
            size: "small"
            variant: "ghost"
            iconName: "refresh-cw"
            text: qsTr("Beszélők újraellenőrzése…")
            toolTipText: root.vm.canRecheck ? qsTr("A megerősített és javított sorok hangjához mérem a többi sort")
                                            : root.vm.recheckBlocker
            onClicked: root.tab.recheckSpeakers()
        }
    }
}
