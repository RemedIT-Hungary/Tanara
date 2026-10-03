import QtQuick
import QtQuick.Templates as T

// „Ki mondta?" panel (340 px). Két helyről nyílik, és a HATÓKÖRE mindig ki van írva:
//   - egy sor nevére kattintva (vagy „Más mondta…"): alapból CSAK AZ A SOR kerül át (ha a sor
//     egy több soros kijelölés része: a kijelölt sorok); a teljes beszélő itt csak kifejezett
//     választásra jön szóba (hatókör-választó a fejléc alatt);
//   - a sáv-fejléc avatarjáról / az áttekintő nevéről: a TELJES beszélő (átnevezés,
//     visszaállítás névtelenre, összevonás, téves hang-felismerés javítása, hanglenyomat).
// A meeting egy másik beszélőjével való összevonást a panel nem hajtja végre: a hívó
// megerősítést kér (mergeRequested). Minden művelet egy visszavonási lépés.
TPopover {
    id: control

    property var editor: null               // TranscriptEditorViewModel
    property string speakerKey: ""
    // A sor, amelyről a panel nyílt ("" = beszélő-szintű helyről nyílt: nincs hatókör-választó).
    property string utteranceId: ""
    property string lineTime: ""
    property int lineStartMs: -1
    property int lineEndMs: -1
    // > 1: a sor egy több soros kijelölés része (ekkor az az alapértelmezett hatókör).
    property int selectionCount: 0
    // "line" | "selection" | "speaker" — megnyitáskor mindig a legszűkebb.
    property string scope: "speaker"
    // A megnyitáskor frissül; a műveletek után a panel bezárul.
    property var info: ({})
    property var material: ({})
    property string voiceprintMessage: ""
    property bool voiceprintOk: false
    property string initialQuery: ""
    // „Meghallgatás": a sor maga, vagy (teljes beszélőnél) egy jellemző minta. A lejátszó a hívóé.
    property bool canListen: false
    signal listenRequested(int startMs, int endMs)
    // Összevonás a meeting egy másik beszélőjével: { kind: "merge" | "reassign", fromKey,
    // intoKey, personName, fix } — a hívó számokkal megerősítteti, és ő hajtja végre.
    signal mergeRequested(var request)

    width: 340
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    readonly property bool fromLine: utteranceId !== ""
    readonly property bool wholeSpeaker: scope === "speaker"
    readonly property string speakerName: info.name || ""
    readonly property bool named: info.anonymous === false
    readonly property int lineCount: info.utteranceCount || 0
    readonly property int moveCount: scope === "line" ? 1 : scope === "selection" ? selectionCount : lineCount
    // A meeting többi beszélője a keresőre szűrve (ékezet-függetlenül, mint a személylista —
    // különben a „feher" nem találná meg Fehér Ádámot, és új személy jönne létre helyette).
    function matchingOthers() {
        return editor ? editor.speakersMatching(search.text, speakerKey) : []
    }
    readonly property int speakersRevision: editor ? editor.speakerCount + editor.lanes.length : 0
    readonly property var others: { speakersRevision; return matchingOthers() }
    // Soronkénti hatókörben a meeting többi beszélője is a billentyűzettel bejárható lista
    // része (ők állnak elöl); a teljes beszélőnél csak az ismert személyek.
    readonly property int otherCount: wholeSpeaker ? 0 : others.length
    // A billentyűzettel kiemelt sor (-1 = nincs): 0…otherCount-1 a meeting beszélői, utána
    // a „Másik személy" lista.
    property int currentIndex: -1

    function refresh() {
        info = editor ? editor.speakerInfo(speakerKey) : ({})
        material = editor ? editor.voiceprintMaterial(speakerKey) : ({})
    }
    function resetCurrent() {
        const total = (wholeSpeaker ? 0 : matchingOthers().length) + people.count
        currentIndex = search.text.trim() !== "" && total > 0 ? 0 : -1
    }
    function setScope(value) {
        scope = value
        resetCurrent()
        search.forceActiveFocus()
    }

    // ---- választás: a hatókör dönti el, mi történik ----
    function choosePerson(name) {
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
            // A személy már beszélője a meetingnek: ez összevonás → előbb megerősítés.
            if (existing !== "" && existing !== speakerKey)
                mergeRequested({ kind: "reassign", fromKey: speakerKey, intoKey: existing, personName: name, fix: fix })
            else
                editor.reassignSpeaker(speakerKey, name, fix)
        }
    }
    function chooseSpeaker(key) {
        close()
        if (scope === "line") editor.moveUtteranceToSpeaker(utteranceId, key)
        else if (scope === "selection") editor.moveSelectionToSpeaker(key)
        else mergeRequested({ kind: "merge", fromKey: speakerKey, intoKey: key, personName: "", fix: false })
    }
    function chooseAnonymous() {
        const fix = fixBox.visible && fixBox.checked
        close()
        if (scope === "line") editor.moveUtteranceToNewParticipant(utteranceId)
        else if (scope === "selection") editor.moveSelectionToNewParticipant()
        else editor.revertSpeakerToAnonymous(speakerKey, fix)
    }
    function chooseIndex(index) {
        if (index < otherCount) chooseSpeaker(others[index].key)
        else choosePerson(people.nameAt(index - otherCount))
    }
    // Enter a keresőben: a (nyíllal) kiemelt sor; beírt szövegnél a legjobb találat vagy az új
    // személy. Üres keresővel, kiemelés nélkül SEMMI — egy véletlen Enter ne rendeljen át
    // semmit a lista első emberéhez.
    function chooseFirst() {
        const typed = search.text.trim() !== ""
        const total = otherCount + people.count
        if (currentIndex >= 0 && currentIndex < total) chooseIndex(currentIndex)
        else if (typed && total > 0) chooseIndex(0)
        else if (typed && people.canCreate) choosePerson(search.text.trim())
    }

    onAboutToShow: {
        voiceprintMessage = ""
        voiceprintOk = false
        scope = !fromLine ? "speaker" : selectionCount > 1 ? "selection" : "line"
        refresh()
        people.refresh()
        search.text = initialQuery
        resetCurrent()
        fixBox.checked = true
        search.forceActiveFocus()
    }

    PersonListModel {
        id: people
        editor: control.editor
        query: search.text
        excludeName: control.wholeSpeaker ? (control.info.personName || "") : ""
        excludeMeetingPeople: !control.wholeSpeaker
        onCountChanged: control.resetCurrent()
    }

    component SectionLabel: TLabel {
        x: 14
        muted: true
        font.pixelSize: Theme.fontMicro
        font.weight: Theme.weightSemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.66
    }

    // A hatókör-választó egy sora (rádiógomb + felirat).
    component ScopeOption: Item {
        id: option
        property string value: ""
        property string text: ""
        readonly property bool selected: control.scope === value
        width: parent ? parent.width : 0
        height: visible ? 28 : 0
        Accessible.role: Accessible.RadioButton
        Accessible.name: text
        Accessible.checked: selected
        Rectangle {
            anchors.fill: parent
            radius: 5
            color: option.selected ? Theme.raised : "transparent"
            border.width: option.selected ? 1 : 0
            border.color: Theme.border
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Theme.stateLayer
                opacity: !option.selected && optionHover.hovered ? Theme.hoverOpacity : 0
            }
        }
        Rectangle {
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 14; height: 14; radius: 7
            color: "transparent"
            border.width: option.selected ? 4 : 1.5
            border.color: option.selected ? Theme.accent : Theme.borderStrong
        }
        TLabel {
            x: 30
            width: parent.width - 38
            anchors.verticalCenter: parent.verticalCenter
            text: option.text
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSmall
            font.weight: option.selected ? Theme.weightSemiBold : Theme.weightRegular
        }
        HoverHandler { id: optionHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: control.setScope(option.value) }
    }

    // A meeting többi beszélője: soronkénti hatókörben áthelyezési cél (elöl), a teljes
    // beszélőnél összevonás (hátul).
    component OthersBlock: Column {
        id: block
        property bool merge: false
        width: parent ? parent.width : 0
        visible: control.others.length > 0 && control.wholeSpeaker === merge
        Item {
            width: parent.width
            height: block.merge ? 24 : 20
            SectionLabel {
                y: block.merge ? 8 : 4
                text: block.merge ? qsTr("Összevonás ebben a megbeszélésben") : qsTr("Ebben a megbeszélésben")
            }
        }
        ListView {
            id: blockList
            objectName: block.merge ? "mergeList" : "meetingSpeakers"
            x: 4
            width: parent.width - 8
            height: Math.min(count, block.merge ? 4 : 3) * 38
            clip: true
            model: block.visible ? control.others : []
            currentIndex: block.merge ? -1 : control.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}
            delegate: PersonRow {
                required property var modelData
                required property int index
                objectName: "speakerChoice"
                compact: true
                width: blockList.width
                personName: modelData.name
                speakerIndex: modelData.colorIndex
                subText: block.merge ? qsTr("összevonás") : qsTr("%n sor", "", modelData.utteranceCount)
                highlighted: hovered || (!block.merge && control.currentIndex === index)
                onClicked: control.chooseSpeaker(modelData.key)
            }
        }
    }

    contentItem: Column {
        // Fejléc: a cím mindig kimondja, mire vonatkozik a választás.
        Item {
            width: parent.width
            height: headColumn.height + 22
            TIconButton {
                id: listenButton
                objectName: "listenButton"
                visible: control.scope === "line" ? control.lineStartMs >= 0
                       : control.wholeSpeaker && control.lineCount > 0
                enabled: control.canListen
                anchors.right: parent.right
                anchors.rightMargin: 10
                y: 9
                size: "small"
                iconName: "play"
                iconSize: 14
                toolTipText: control.wholeSpeaker
                             ? qsTr("Meghallgatás: egy jellemző, hosszabb megszólalás ettől a beszélőtől")
                             : qsTr("Meghallgatás: ez a sor")
                onClicked: {
                    if (!control.wholeSpeaker) {
                        control.listenRequested(control.lineStartMs, control.lineEndMs)
                        return
                    }
                    const sample = control.editor.speakerSample(control.speakerKey)
                    if (sample.ok === true) control.listenRequested(sample.startMs, sample.endMs)
                }
            }
            Column {
                id: headColumn
                x: 14; y: 12
                width: parent.width - 28 - (listenButton.visible ? listenButton.width + 6 : 0)
                spacing: 3
                TLabel {
                    objectName: "popoverTitle"
                    width: parent.width
                    text: control.scope === "line" ? qsTr("Kinek a sora ez?")
                        : control.scope === "selection" ? qsTr("Kié a kijelölt %n sor?", "", control.selectionCount)
                        : qsTr("%1 valójában…").arg(control.speakerName)
                    muted: control.wholeSpeaker
                    font.pixelSize: control.wholeSpeaker ? Theme.fontCaption : Theme.fontSmall
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                TLabel {
                    width: parent.width
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                    text: {
                        if (control.scope === "line")
                            return qsTr("Most: %1 · %2").arg(control.speakerName).arg(control.lineTime)
                        if (control.scope === "selection")
                            return qsTr("A kattintott sor most: %1").arg(control.speakerName)
                        const lines = qsTr("%n megszólalás", "", control.lineCount)
                        const conf = control.info.voiceConfidence
                        if (conf !== undefined && conf >= 0)
                            return qsTr("%1 · hang alapján felismerve (%2%)").arg(lines).arg(Math.round(conf * 100))
                        if (control.named) return qsTr("%1 · kézzel elnevezve").arg(lines)
                        return qsTr("%1 · névtelen beszélő").arg(lines)
                    }
                }
            }
        }

        // ---- Hatókör (csak sorról nyitva): alapból a legszűkebb ----
        Item {
            visible: control.fromLine
            width: parent.width
            height: visible ? scopeColumn.height + 14 : 0
            Rectangle {
                x: 10
                width: parent.width - 20
                height: scopeColumn.height + 6
                radius: Theme.radiusControl
                color: Theme.sunken
            }
            Column {
                id: scopeColumn
                x: 13; y: 3
                width: parent.width - 26
                ScopeOption {
                    objectName: "scopeLine"
                    value: "line"
                    text: qsTr("Csak ez a sor")
                }
                ScopeOption {
                    objectName: "scopeSelection"
                    visible: control.selectionCount > 1
                    value: "selection"
                    text: qsTr("Kijelölt %n sor", "", control.selectionCount)
                }
                ScopeOption {
                    objectName: "scopeSpeaker"
                    value: "speaker"
                    text: qsTr("%1 minden sora (%2)").arg(control.speakerName).arg(control.lineCount)
                }
            }
        }

        Item {
            width: parent.width
            height: 40
            TSearchField {
                id: search
                x: 10
                width: parent.width - 20
                font.pixelSize: Theme.fontBody
                placeholderText: qsTr("Név keresése vagy új személy")
                Keys.onDownPressed: control.currentIndex = Math.min(control.currentIndex + 1,
                                        control.otherCount + people.count - 1)
                Keys.onUpPressed: control.currentIndex = Math.max(0, control.currentIndex - 1)
                Keys.onReturnPressed: control.chooseFirst()
                Keys.onEnterPressed: control.chooseFirst()
                Keys.onEscapePressed: control.close()
            }
        }

        // ---- Soronkénti hatókör: elöl a meeting többi beszélője ----
        OthersBlock { merge: false }

        // ---- Másik személy ----
        Item {
            visible: peopleList.count > 0 || newPersonRow.visible || anonymousRow.visible
            width: parent.width
            height: visible ? 20 : 0
            SectionLabel { y: 4; text: qsTr("Másik személy") }
        }
        ListView {
            id: peopleList
            x: 4
            width: parent.width - 8
            height: Math.min(count, 3) * 38
            clip: true
            model: people
            currentIndex: control.currentIndex - control.otherCount
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}
            delegate: PersonRow {
                required property int index
                required property string name
                required property bool hasVoiceprint
                required property int meetingCount
                required property bool inMeeting
                objectName: "personChoice"
                compact: true
                width: peopleList.width
                personName: name
                subText: inMeeting ? qsTr("összevonás") : meetingCount > 0 ? qsTr("%n megbeszélés", "", meetingCount)
                       : qsTr("nincs hanglenyomat")
                highlighted: control.currentIndex - control.otherCount === index || hovered
                onClicked: control.choosePerson(name)
            }
        }
        PersonRow {
            id: newPersonRow
            visible: people.canCreate
            x: 4
            width: parent.width - 8
            height: visible ? 38 : 0
            compact: true
            monogram: "+"
            personName: qsTr("Új személy: „%1”").arg(search.text.trim())
            highlighted: hovered || (control.otherCount + people.count === 0 && people.canCreate)
            onClicked: control.choosePerson(search.text.trim())
        }
        PersonRow {
            id: anonymousRow
            objectName: "anonymousRow"
            visible: search.text.trim() === "" && (control.wholeSpeaker ? control.named : true)
            x: 4
            width: parent.width - 8
            height: visible ? 38 : 0
            compact: true
            monogram: "?"
            personName: control.wholeSpeaker ? qsTr("Névtelen beszélő") : qsTr("Új névtelen résztvevő")
            subText: control.wholeSpeaker && (control.info.rawLabel || "") !== ""
                     ? qsTr("%1 néven").arg(control.info.rawLabel) : ""
            highlighted: hovered
            onClicked: control.chooseAnonymous()
        }

        // ---- Teljes beszélő: összevonás ebben a megbeszélésben ----
        OthersBlock { merge: true }
        Item { width: 1; height: 4 }

        // ---- Téves felismerés: a sorok kerüljenek ki a hanglenyomatból (csak teljes beszélő) ----
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
                text: qsTr("A sorok kerüljenek ki %1 hanglenyomatából (téves felismerés)").arg(control.speakerName)
            }
        }

        // ---- Hanglenyomat (csak kifejezett műveletre készül; beszélő-szintű helyről nyitva) ----
        Item {
            visible: !control.fromLine
            width: parent.width
            height: visible ? Math.max(vpText.height, vpButton.visible ? vpButton.height : 0) + 20 : 0
            TDivider { width: parent.width }
            TIcon { x: 14; y: 11; name: "fingerprint"; size: 16; color: Theme.textMuted }
            TLabel {
                id: vpText
                x: 40; y: 10
                width: parent.width - 54 - (vpButton.visible ? vpButton.width + 10 : 0)
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.4
                color: control.voiceprintMessage !== "" && !control.voiceprintOk ? Theme.dangerInk : Theme.text
                text: {
                    if (control.voiceprintMessage !== "") return control.voiceprintMessage
                    if (!control.named)
                        return qsTr("Hanglenyomat csak elnevezett beszélőhöz készíthető.")
                    const has = control.info.hasVoiceprint === true ? qsTr("Van hanglenyomata.")
                                                                    : qsTr("Még nincs hanglenyomata.")
                    if (control.material.supported !== true)
                        return has + " " + qsTr("Újat most nem lehet készíteni, mert a hang-elemzés nem érhető el.")
                    if (control.material.sufficient === true)
                        return has + " " + qsTr("Itt %1 mp jól használható beszéde van.").arg(control.material.usableSec)
                    return has + " " + qsTr("Ebből a megbeszélésből nem készíthető: még kb. %1 mp tiszta beszéd "
                                            + "kellene (legalább 3 másodperces sorokból).")
                                          .arg(control.material.missingSec)
                }
            }
            TButton {
                id: vpButton
                visible: control.named && control.material.supported === true
                         && control.material.sufficient === true && !control.voiceprintOk
                anchors.right: parent.right
                anchors.rightMargin: 12
                y: 10
                size: "small"
                text: control.info.hasVoiceprint === true ? qsTr("Új készítése") : qsTr("Készítés")
                toolTipText: qsTr("Hanglenyomat készítése a beszélő itteni, hosszabb soraiból")
                onClicked: {
                    const result = control.editor.createVoiceprint(control.speakerKey)
                    control.voiceprintOk = result.ok === true
                    control.voiceprintMessage = result.message || ""
                    control.refresh()
                }
            }
        }

        // ---- Üres, kézzel felvett oszlop eltávolítása ----
        Item {
            visible: control.wholeSpeaker && control.info.added === true && control.lineCount === 0
            width: parent.width
            height: visible ? 44 : 0
            TDivider { width: parent.width }
            TButton {
                x: 10; y: 8
                size: "small"
                variant: "dangerGhost"
                iconName: "trash-2"
                text: qsTr("Üres oszlop eltávolítása")
                onClicked: {
                    control.close()
                    control.editor.removeParticipant(control.speakerKey)
                }
            }
        }

        // ---- Lábléc: hány sor mozdul ----
        Item {
            width: parent.width
            height: 34
            // Sorról nyitva a teljes beszélő kifejezett (tömeges) választás: a lábléc kiemeli.
            readonly property bool bulk: control.fromLine && control.wholeSpeaker && control.lineCount > 1
            Rectangle { x: 1; width: parent.width - 2; height: parent.height - 1; radius: Theme.radiusPopup - 1; color: parent.bulk ? Theme.warnSoft : Theme.surface }
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
                font.weight: parent.bulk ? Theme.weightSemiBold : Theme.weightRegular
                text: !control.wholeSpeaker ? qsTr("%n sor kerül át · visszavonható: Ctrl+Z", "", control.moveCount)
                    : control.lineCount === 0 ? qsTr("Visszavonható: Ctrl+Z")
                    : control.editor && control.editor.needsAz(control.lineCount)
                      ? qsTr("Mind az %n sor átkerül · visszavonható: Ctrl+Z", "", control.lineCount)
                      : qsTr("Mind a %n sor átkerül · visszavonható: Ctrl+Z", "", control.lineCount)
            }
        }
    }
}
