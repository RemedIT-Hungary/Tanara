import QtQuick
import QtQuick.Templates as T

// „Kinek a sora ez?" (handoff-v3 E1, 390 px): egy sor (vagy a kijelölés) beszélőjének javítása.
// A sor nevéről, a „Más…" gombról (Átnézendő) és a helyi menüből nyílik. A hatókör mindig ki
// van írva: „Csak ez a sor" (alap) · „Kijelölt N sor" · „<Utónév> mind a N sora" (a teljes
// beszélő — itt kifejezett választás, előtte megerősítés, ha összevonás lesz belőle).
//   JAVASOLT: a jelöltek bizonyítékkal (CandidateListModel), 1–3 billentyűvel; a másik sávon
//   beszélő és a mostani beszélő halvány, de választható.
//   MIÉRT NEM X?: a mostani beszélő ellen szóló okok, mindegyik mellett a javítás helye
//   (Sáv-beosztás → a beszélő „Miért ő?" panelje a sáv-chipekkel; Minták → hanglenyomat).
//   „Új személy ebből a sorból…": név megadása (üresen: új névtelen résztvevő).
// Minden választás egy visszavonási lépés; a hívó (TranscriptTab) kéri a megerősítést.
TPopover {
    id: control

    property var editor: null               // TranscriptEditorViewModel
    property string speakerKey: ""
    property string utteranceId: ""
    property string lineTime: ""
    property int lineStartMs: -1
    property int lineEndMs: -1
    // > 1: a sor egy több soros kijelölés része (ekkor az az alapértelmezett hatókör).
    property int selectionCount: 0
    // "line" | "selection" | "speaker" — megnyitáskor mindig a legszűkebb.
    property string scope: "line"
    property var info: ({})
    property string initialQuery: ""
    property bool canListen: false
    // „Új személy ebből a sorból…": a kereső az új nevet várja.
    property bool naming: false

    signal listenRequested(int startMs, int endMs)
    // { kind: "merge" | "reassign", fromKey, intoKey, personName, fix } — a hívó erősítteti meg.
    signal mergeRequested(var request)
    // A teljes beszélő „Miért ő?" panelje (a „Miért ő?" linkről, a Sáv-beosztás javításról).
    signal speakerWhyRequested(string speakerKey, bool tracks)
    signal voiceprintRequested(string speakerKey)

    width: 390
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    readonly property bool wholeSpeaker: scope === "speaker"
    readonly property string speakerName: (info.name || "")
                                          + (info.nameDuplicate && info.rawLabel ? " (" + info.rawLabel + ")" : "")
    // „Lilla mind a 368 sora": elnevezett személynél az utónév (a magyar névsorrend szerint az utolsó szó).
    readonly property string shortName: info.anonymous === false && (info.name || "").indexOf(" ") > 0
                                        ? info.name.substring(info.name.lastIndexOf(" ") + 1) : (info.name || "")
    readonly property bool named: info.anonymous === false
    readonly property int lineCount: info.utteranceCount || 0
    readonly property int moveCount: scope === "line" ? 1 : scope === "selection" ? selectionCount : lineCount
    readonly property bool typed: search.text.trim() !== ""
    readonly property var whyItems: scope === "line" && editor && utteranceId !== "" && whyRevision >= 0
                                    ? editor.whyNot(utteranceId) : []
    property int whyRevision: 0
    // A billentyűzettel kiemelt sor: 0…candidates.count-1 a jelöltek, utána a „Másik személy" lista.
    property int currentIndex: -1

    function refresh() {
        info = editor ? editor.speakerInfo(speakerKey) : ({})
        whyRevision++
    }
    function setScope(value) {
        scope = value
        currentIndex = -1
        search.forceActiveFocus()
    }

    // ---- választás: a hatókör dönti el, mi történik ----
    function choosePerson(name) {
        if (name === "") return
        if (scope === "line") {
            close()
            editor.moveUtteranceToPerson(utteranceId, name)
        } else if (scope === "selection") {
            close()
            editor.moveSelectionToPerson(name)
        } else {
            const fix = fixBox.visible && fixBox.checked
            const existing = editor.speakerKeyForPerson(name)
            close()
            if (existing !== "" && existing !== speakerKey)
                mergeRequested({ kind: "reassign", fromKey: speakerKey, intoKey: existing, personName: name, fix: fix })
            else
                editor.reassignSpeaker(speakerKey, name, fix)
        }
    }
    function chooseSpeaker(key) {
        if (key === speakerKey && scope !== "selection") { close(); return }
        close()
        if (scope === "line") editor.moveUtteranceToSpeaker(utteranceId, key)
        else if (scope === "selection") editor.moveSelectionToSpeaker(key)
        else mergeRequested({ kind: "merge", fromKey: speakerKey, intoKey: key, personName: "", fix: false })
    }
    function chooseAnonymous() {
        close()
        if (scope === "line") editor.moveUtteranceToNewParticipant(utteranceId)
        else if (scope === "selection") editor.moveSelectionToNewParticipant()
        else editor.revertSpeakerToAnonymous(speakerKey, fixBox.visible && fixBox.checked)
    }
    function chooseCandidate(row) {
        const c = candidates.get(row)
        if (c.speakerKey === undefined) return
        if (c.speakerKey !== "") chooseSpeaker(c.speakerKey)
        else choosePerson(c.personName)
    }
    function chooseIndex(index) {
        if (index < candidates.count) chooseCandidate(index)
        else choosePerson(people.nameAt(index - candidates.count))
    }
    // Enter: a kiemelt sor; beírt szövegnél a legjobb találat vagy az új személy. Üres keresővel,
    // kiemelés nélkül SEMMI (egy véletlen Enter ne rendeljen át semmit).
    function chooseFirst() {
        const total = (naming ? 0 : candidates.count) + people.count
        if (naming) {
            if (typed) choosePerson(search.text.trim())
            else chooseAnonymous()
        } else if (currentIndex >= 0 && currentIndex < total) chooseIndex(currentIndex)
        else if (typed && total > 0) chooseIndex(0)
        else if (typed && people.canCreate) choosePerson(search.text.trim())
    }

    onAboutToShow: {
        scope = selectionCount > 1 ? "selection" : utteranceId !== "" ? "line" : "speaker"
        naming = false
        refresh()
        people.refresh()
        search.text = initialQuery
        currentIndex = -1
        fixBox.checked = true
        voiceprintPanel.reset()
        search.forceActiveFocus()
    }
    Connections {
        target: control.editor
        ignoreUnknownSignals: true
        function onReviewChanged() { if (control.opened) control.whyRevision++ }
    }

    CandidateListModel {
        id: candidates
        editor: control.editor
        utteranceId: control.wholeSpeaker ? "" : control.utteranceId
        speakerKey: control.wholeSpeaker ? control.speakerKey : ""
        limit: control.wholeSpeaker ? 4 : 3
        query: control.naming ? "" : search.text
    }
    PersonListModel {
        id: people
        editor: control.editor
        query: control.naming ? "" : search.text
        excludeName: control.wholeSpeaker ? (control.info.personName || "") : ""
        excludeMeetingPeople: true
    }

    component SectionHead: Row {
        property string label: ""
        property string hint: ""
        x: 18
        spacing: 8
        height: 24
        TLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            muted: true
            font.pixelSize: Theme.fontMicro
            font.weight: Theme.weightSemiBold
            font.letterSpacing: 0.66
        }
        TLabel {
            anchors.verticalCenter: parent.verticalCenter
            visible: parent.hint !== ""
            text: parent.hint
            muted: true
            font.pixelSize: Theme.fontMicro + 0.5
        }
    }
    component ScopeOption: Item {
        id: option
        property string value: ""
        property string text: ""
        readonly property bool selected: control.scope === value
        implicitWidth: optionRow.implicitWidth
        implicitHeight: 22
        visible: true
        Accessible.role: Accessible.RadioButton
        Accessible.name: text
        Accessible.checked: selected
        Row {
            id: optionRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 15; height: 15; radius: 7.5
                color: "transparent"
                border.width: option.selected ? 5 : 1.5
                border.color: option.selected ? Theme.accent : Theme.borderStrong
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: option.text
                font.pixelSize: Theme.fontSmall
                font.weight: option.selected ? Theme.weightMedium : Theme.weightRegular
            }
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: control.setScope(option.value) }
    }

    contentItem: Column {
        id: column

        // ---- fejléc ----
        Item {
            width: parent.width
            height: headColumn.height + 22
            Rectangle {
                id: listenButton
                objectName: "listenButton"
                visible: control.scope === "line" ? control.lineStartMs >= 0 : control.lineCount > 0
                readonly property bool canPlay: control.canListen
                anchors.right: parent.right
                anchors.rightMargin: 14
                y: 12
                width: 28; height: 28; radius: 14
                color: listenHover.hovered && canPlay ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                border.width: 1
                border.color: Theme.borderStrong
                opacity: canPlay ? 1 : 0.5
                TIcon { anchors.centerIn: parent; name: "play"; size: 12; color: Theme.text }
                HoverHandler { id: listenHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    enabled: listenButton.canPlay
                    onTapped: {
                        if (control.scope === "line") {
                            control.listenRequested(control.lineStartMs, control.lineEndMs)
                            return
                        }
                        const sample = control.editor.speakerSample(control.speakerKey)
                        if (sample.ok === true) control.listenRequested(sample.startMs, sample.endMs)
                    }
                }
                TToolTip {
                    visible: listenHover.hovered
                    text: control.scope === "line" ? qsTr("Meghallgatás: ez a sor")
                                                   : qsTr("Meghallgatás: egy jellemző, hosszabb megszólalás ettől a beszélőtől")
                }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Meghallgatás")
            }
            Column {
                id: headColumn
                x: 14; y: 12
                width: parent.width - 28 - (listenButton.visible ? listenButton.width + 8 : 0)
                spacing: 3
                TLabel {
                    objectName: "popoverTitle"
                    width: parent.width
                    text: control.scope === "line" ? qsTr("Kinek a sora ez?")
                        : control.scope === "selection" ? qsTr("Kié a kijelölt %n sor?", "", control.selectionCount)
                        : qsTr("%1 valójában…").arg(control.speakerName)
                    font.pixelSize: Theme.fontSmall + 2
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                Row {
                    spacing: 10
                    TLabel {
                        muted: true
                        font.pixelSize: Theme.fontSmall - 0.5
                        text: control.scope === "selection" ? qsTr("A kattintott sor most: %1").arg(control.speakerName)
                                                            : qsTr("Most: %1 · %2").arg(control.speakerName).arg(control.lineTime)
                    }
                    TLabel {
                        objectName: "whyLink"
                        visible: control.speakerKey !== ""
                        text: qsTr("Miért ő?")
                        color: Theme.accent
                        font.pixelSize: Theme.fontSmall - 0.5
                        font.weight: Theme.weightMedium
                        font.underline: whyHover.hovered
                        HoverHandler { id: whyHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                const key = control.speakerKey
                                control.close()
                                control.speakerWhyRequested(key, false)
                            }
                        }
                    }
                }
            }
        }

        // ---- hatókör ----
        Item {
            width: parent.width
            height: 32
            Flow {
                x: 18
                width: parent.width - 32
                spacing: 16
                ScopeOption { objectName: "scopeLine"; value: "line"; text: qsTr("Csak ez a sor") }
                ScopeOption {
                    objectName: "scopeSelection"
                    visible: control.selectionCount > 1
                    value: "selection"
                    text: qsTr("Kijelölt %n sor", "", control.selectionCount)
                }
                ScopeOption {
                    objectName: "scopeSpeaker"
                    value: "speaker"
                    text: control.needsAz(control.lineCount)
                          ? qsTr("%1 mind az %2 sora").arg(control.shortName).arg(control.lineCount)
                          : qsTr("%1 mind a %2 sora").arg(control.shortName).arg(control.lineCount)
                }
            }
        }
        TDivider { width: parent.width }

        // ---- kereső ----
        Item {
            width: parent.width
            height: 50
            TSearchField {
                id: search
                objectName: "linePopoverSearch"
                x: 14; y: 10
                width: parent.width - 28
                height: 30
                font.pixelSize: Theme.fontSmall
                placeholderText: control.naming ? qsTr("Az új személy neve (üresen: névtelen)")
                                                : qsTr("Név keresése vagy új személy")
                onTextChanged: control.currentIndex = control.typed && !control.naming ? 0 : -1
                Keys.onDownPressed: control.currentIndex = Math.min(control.currentIndex + 1,
                                        candidates.count + people.count - 1)
                Keys.onUpPressed: control.currentIndex = Math.max(0, control.currentIndex - 1)
                Keys.onReturnPressed: control.chooseFirst()
                Keys.onEnterPressed: control.chooseFirst()
                Keys.onEscapePressed: {
                    if (control.naming) { control.naming = false; search.text = "" }
                    else control.close()
                }
                // 1–3: a javasolt jelölt (üres keresővel).
                Keys.onPressed: event => {
                    if (control.typed || control.naming) return
                    if (event.key >= Qt.Key_1 && event.key <= Qt.Key_3) {
                        const row = candidates.rowForKey(event.key - Qt.Key_0)
                        if (row >= 0) control.chooseCandidate(row)
                        event.accepted = true
                    }
                }
            }
        }

        // ---- a lista (görgethető, ha nem fér ki) ----
        Flickable {
            id: body
            width: parent.width
            // A fej, a kereső, az „Új személy" sor és a lábléc mellett a lista a többi helyet kapja
            // (a hívó a panelt feljebb tolja, ha a sor alatt nem fér el).
            readonly property real maxHeight: T.Overlay.overlay ? T.Overlay.overlay.height - 2 * control.margins - 236 : 420
            height: Math.min(bodyColumn.implicitHeight, Math.max(160, maxHeight))
            contentHeight: bodyColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}

            Column {
                id: bodyColumn
                width: body.width

                // JAVASOLT / (keresés:) EBBEN A MEGBESZÉLÉSBEN
                SectionHead {
                    visible: !control.naming && candidates.count > 0
                    label: control.typed ? qsTr("EBBEN A MEGBESZÉLÉSBEN")
                         : control.wholeSpeaker ? qsTr("ÖSSZEVONÁS VAGY ÁTNEVEZÉS") : qsTr("JAVASOLT")
                    hint: control.typed ? "" : qsTr("a bizonyítékok sorrendjében")
                }
                Column {
                    objectName: control.wholeSpeaker ? "mergeList" : "meetingSpeakers"
                    visible: !control.naming && candidates.count > 0
                    x: 8
                    width: parent.width - 16
                    spacing: 1
                    Repeater {
                        model: candidates
                        CandidateRow {
                            required property int index
                            required property var model
                            objectName: model.current ? "currentChoice" : "speakerChoice"
                            width: parent.width
                            personName: model.name
                            colorIndex: model.colorIndex
                            subText: model.subText
                            evidence: model.evidence
                            keyHint: model.keyHint
                            dimmed: model.dimmed
                            strongEvidence: index === 0 && !model.current
                            highlighted: control.currentIndex === index
                            onClicked: control.chooseCandidate(index)
                        }
                    }
                }

                // ---- keresés: másik (ismert) személy, új személy ----
                SectionHead {
                    visible: people.count > 0 && control.typed && !control.naming
                    label: qsTr("MÁSIK SZEMÉLY")
                }
                Repeater {
                    model: control.typed && !control.naming ? people : null
                    PersonRow {
                        required property int index
                        required property string name
                        required property int meetingCount
                        required property string matchedAlias
                        objectName: "personChoice"
                        compact: true
                        x: 8
                        width: bodyColumn.width - 16
                        personName: name
                        subText: (matchedAlias !== "" ? qsTr("„%1”").arg(matchedAlias) + " · " : "")
                               + (meetingCount > 0 ? qsTr("%n megbeszélés", "", meetingCount) : qsTr("nincs hanglenyomat"))
                        highlighted: control.currentIndex - candidates.count === index || hovered
                        onClicked: control.choosePerson(name)
                    }
                }
                PersonRow {
                    id: newPersonRow
                    objectName: "newPersonChoice"
                    visible: control.typed && (control.naming || people.canCreate)
                    x: 8
                    width: parent.width - 16
                    height: visible ? 38 : 0
                    compact: true
                    monogram: "+"
                    personName: qsTr("Új személy: „%1”").arg(search.text.trim())
                    highlighted: hovered || (control.naming && control.typed)
                    onClicked: control.choosePerson(search.text.trim())
                }
                PersonRow {
                    id: anonymousRow
                    objectName: "anonymousRow"
                    visible: control.naming && !control.typed
                    x: 8
                    width: parent.width - 16
                    height: visible ? 38 : 0
                    compact: true
                    monogram: "?"
                    personName: control.wholeSpeaker ? qsTr("Névtelen beszélő") : qsTr("Új névtelen résztvevő")
                    highlighted: hovered
                    onClicked: control.chooseAnonymous()
                }
                Item { width: 1; height: 6 }

                // ---- MIÉRT NEM X? ----
                Rectangle {
                    objectName: "whyNotBlock"
                    visible: control.scope === "line" && control.whyItems.length > 0 && !control.typed && !control.naming
                    width: parent.width
                    height: visible ? whyColumn.implicitHeight + 16 : 0
                    color: Theme.surface
                    TDivider { width: parent.width }
                    Column {
                        id: whyColumn
                        x: 10; y: 8
                        width: parent.width - 20
                        SectionHead {
                            x: 4
                            label: qsTr("MIÉRT NEM %1?").arg(control.speakerName.toUpperCase())
                        }
                        Repeater {
                            model: control.whyItems
                            EvidenceReason {
                                required property var modelData
                                objectName: "whyNotReason"
                                width: whyColumn.width
                                evidence: modelData
                                onFixClicked: ev => {
                                    const key = control.speakerKey
                                    control.close()
                                    if (ev.fixTarget === "samples") control.voiceprintRequested(key)
                                    else control.speakerWhyRequested(key, ev.fixTarget === "tracks")
                                }
                            }
                        }
                    }
                }

                // ---- teljes beszélő: téves felismerés + hanglenyomat ----
                Item {
                    visible: control.wholeSpeaker && control.named && control.info.hasVoiceprint === true
                    width: parent.width
                    height: visible ? fixBox.height + 20 : 0
                    TDivider { width: parent.width }
                    TCheckBox {
                        id: fixBox
                        objectName: "fixVoiceprints"
                        x: 14; y: 10
                        width: parent.width - 28
                        checked: true
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("Ha másé, a sorai kerüljenek ki %1 hanglenyomatából (téves felismerés)").arg(control.speakerName)
                    }
                }
                VoiceprintPanel {
                    id: voiceprintPanel
                    objectName: "voiceprintBlock"
                    visible: control.wholeSpeaker
                    width: parent.width
                    compact: true
                    editor: control.editor
                    speakerKey: control.speakerKey
                    onChanged: control.refresh()
                }
            }
        }

        // ---- „Új személy ebből a sorból…" ----
        Item {
            visible: !control.wholeSpeaker && !control.naming
            width: parent.width
            height: visible ? 38 : 0
            TDivider { width: parent.width }
            Row {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 9
                TIcon { anchors.verticalCenter: parent.verticalCenter; name: "user-plus"; size: 14; color: Theme.accent }
                TLabel {
                    objectName: "newPersonLink"
                    anchors.verticalCenter: parent.verticalCenter
                    text: control.scope === "selection" ? qsTr("Új személy a kijelölt sorokból…") : qsTr("Új személy ebből a sorból…")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightMedium
                    font.underline: newHover.hovered
                    HoverHandler { id: newHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (control.typed) {
                                control.choosePerson(search.text.trim())
                                return
                            }
                            control.naming = true
                            search.forceActiveFocus()
                        }
                    }
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("ha senki sem ő")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
            }
        }

        // ---- lábléc: hány sor mozdul ----
        Item {
            width: parent.width
            height: 34
            readonly property bool bulk: control.wholeSpeaker && control.lineCount > 1
            Rectangle {
                x: 1
                width: parent.width - 2
                height: parent.height - 1
                radius: Theme.radiusPopup - 1
                color: parent.bulk ? Theme.warnSoft : Theme.surface
            }
            Rectangle { x: 1; width: parent.width - 2; height: parent.height / 2; color: parent.bulk ? Theme.warnSoft : Theme.surface }
            TDivider { width: parent.width; color: parent.bulk ? Theme.warnLine : Theme.border }
            TLabel {
                objectName: "popoverFooter"
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 28
                elide: Text.ElideRight
                color: parent.bulk ? Theme.warnInk : Theme.textMuted
                font.pixelSize: Theme.fontCaption
                text: !control.wholeSpeaker
                      ? qsTr("%n sor kerül át · Ctrl+Z visszavonja · a gép tanul belőle", "", control.moveCount)
                      : control.lineCount === 0 ? qsTr("Ctrl+Z visszavonja")
                      : control.needsAz(control.lineCount)
                        ? qsTr("A teljes beszélőre vonatkozik: mind az %n sor · Ctrl+Z visszavonja", "", control.lineCount)
                        : qsTr("A teljes beszélőre vonatkozik: mind a %n sor · Ctrl+Z visszavonja", "", control.lineCount)
            }
        }
    }

    function needsAz(n) { return editor ? editor.needsAz(n) : false }
}
