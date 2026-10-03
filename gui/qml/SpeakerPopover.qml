import QtQuick
import QtQuick.Templates as T

// A teljes beszélő javítása (340 px; a névre vagy a sáv-fejléc avatarjára kattintva):
// másik személyhez rendelés (minden sora megy), visszaállítás névtelenre, összevonás a
// meeting egy másik beszélőjével, a téves hang-felismerés javítása (jelölőnégyzet), és a
// KIFEJEZETT hanglenyomat-készítés. Minden művelet egy visszavonási lépés.
TPopover {
    id: control

    property var editor: null               // TranscriptEditorViewModel
    property string speakerKey: ""
    // A megnyitáskor frissül; a műveletek után a panel bezárul.
    property var info: ({})
    property var material: ({})
    property string voiceprintMessage: ""
    property bool voiceprintOk: false
    property string initialQuery: ""
    // „Meghallgatás": reprezentatív minta a beszélőtől (a lejátszó a hívóé).
    property bool canListen: false
    signal listenRequested(int startMs, int endMs)

    width: 340
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    readonly property string speakerName: info.name || ""
    readonly property bool named: info.anonymous === false
    readonly property int lineCount: info.utteranceCount || 0
    readonly property var others: {
        const all = editor ? editor.speakers : []
        const q = search.text.trim().toLowerCase()
        return all.filter(s => s.key !== control.speakerKey && (q === "" || s.name.toLowerCase().indexOf(q) >= 0))
    }
    // A billentyűzettel kiemelt sor a „Másik személy" listában (-1 = nincs).
    property int currentIndex: -1

    function refresh() {
        info = editor ? editor.speakerInfo(speakerKey) : ({})
        material = editor ? editor.voiceprintMaterial(speakerKey) : ({})
    }

    function reassign(name) {
        const fix = fixBox.visible && fixBox.checked
        close()
        editor.reassignSpeaker(speakerKey, name, fix)
    }
    // Enter a keresőben: a (nyíllal) kiemelt sor; beírt szövegnél a legjobb találat vagy az új
    // személy. Üres keresővel, kiemelés nélkül SEMMI — egy véletlen Enter ne rendelje át a
    // teljes beszélőt (és a hanglenyomatát) a lista első emberéhez.
    function chooseFirst() {
        const typed = search.text.trim() !== ""
        if (currentIndex >= 0 && currentIndex < people.count) reassign(people.nameAt(currentIndex))
        else if (typed && people.count > 0) reassign(people.nameAt(0))
        else if (typed && people.canCreate) reassign(search.text.trim())
    }

    onAboutToShow: {
        voiceprintMessage = ""
        voiceprintOk = false
        refresh()
        people.refresh()
        search.text = initialQuery
        currentIndex = -1
        fixBox.checked = true
        search.forceActiveFocus()
    }

    PersonListModel {
        id: people
        editor: control.editor
        query: search.text
        excludeName: control.info.personName || ""
        onCountChanged: control.currentIndex = search.text.trim() !== "" && count > 0 ? 0 : -1
    }

    component SectionLabel: TLabel {
        x: 14
        muted: true
        font.pixelSize: Theme.fontMicro
        font.weight: Theme.weightSemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.66
    }

    contentItem: Column {
        // Fejléc
        Item {
            width: parent.width
            height: headColumn.height + 22
            TIconButton {
                id: listenButton
                objectName: "listenButton"
                visible: control.lineCount > 0
                enabled: control.canListen
                anchors.right: parent.right
                anchors.rightMargin: 10
                y: 9
                size: "small"
                iconName: "play"
                iconSize: 14
                toolTipText: qsTr("Meghallgatás: egy jellemző, hosszabb megszólalás ettől a beszélőtől")
                onClicked: {
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
                    width: parent.width
                    text: qsTr("%1 valójában…").arg(control.speakerName)
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                TLabel {
                    width: parent.width
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                    text: {
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

        Item {
            width: parent.width
            height: 40
            TSearchField {
                id: search
                x: 10
                width: parent.width - 20
                font.pixelSize: Theme.fontBody
                placeholderText: qsTr("Név keresése vagy új személy")
                Keys.onDownPressed: control.currentIndex = Math.min(control.currentIndex + 1, people.count - 1)
                Keys.onUpPressed: control.currentIndex = Math.max(0, control.currentIndex - 1)
                Keys.onReturnPressed: control.chooseFirst()
                Keys.onEnterPressed: control.chooseFirst()
                Keys.onEscapePressed: control.close()
            }
        }

        // ---- Másik személy ----
        Item { width: parent.width; height: 20; SectionLabel { y: 4; text: qsTr("Másik személy") } }
        ListView {
            id: peopleList
            x: 4
            width: parent.width - 8
            height: Math.min(count, 3) * 38
            clip: true
            model: people
            currentIndex: control.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}
            delegate: PersonRow {
                required property int index
                required property string name
                required property bool hasVoiceprint
                required property int meetingCount
                required property bool inMeeting
                compact: true
                width: peopleList.width
                personName: name
                subText: inMeeting ? qsTr("összevonás") : meetingCount > 0 ? qsTr("%n megbeszélés", "", meetingCount)
                       : qsTr("nincs hanglenyomat")
                highlighted: control.currentIndex === index || hovered
                onClicked: control.reassign(name)
            }
        }
        PersonRow {
            visible: people.canCreate
            x: 4
            width: parent.width - 8
            height: visible ? 38 : 0
            compact: true
            monogram: "+"
            personName: qsTr("Új személy: „%1”").arg(search.text.trim())
            highlighted: hovered || (people.count === 0 && people.canCreate)
            onClicked: control.reassign(search.text.trim())
        }
        PersonRow {
            visible: control.named && search.text.trim() === ""
            x: 4
            width: parent.width - 8
            height: visible ? 38 : 0
            compact: true
            monogram: "?"
            personName: qsTr("Névtelen beszélő")
            subText: (control.info.rawLabel || "") !== "" ? qsTr("%1 néven").arg(control.info.rawLabel) : ""
            highlighted: hovered
            onClicked: {
                const fix = fixBox.visible && fixBox.checked
                control.close()
                control.editor.revertSpeakerToAnonymous(control.speakerKey, fix)
            }
        }

        // ---- Összevonás ebben a megbeszélésben ----
        Item {
            visible: control.others.length > 0
            width: parent.width
            height: visible ? 24 : 0
            SectionLabel { y: 8; text: qsTr("Összevonás ebben a megbeszélésben") }
        }
        ListView {
            id: othersList
            visible: control.others.length > 0
            x: 4
            width: parent.width - 8
            height: Math.min(count, 4) * 38
            clip: true
            model: control.others
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}
            delegate: PersonRow {
                required property var modelData
                compact: true
                width: othersList.width
                personName: modelData.name
                speakerIndex: modelData.colorIndex
                subText: qsTr("összevonás")
                highlighted: hovered
                onClicked: {
                    control.close()
                    control.editor.mergeSpeakers(control.speakerKey, modelData.key)
                }
            }
        }
        Item { width: 1; height: 4 }

        // ---- Téves felismerés: a sorok kerüljenek ki a hanglenyomatból ----
        Item {
            visible: control.named && control.info.hasVoiceprint === true
            width: parent.width
            height: visible ? fixBox.height + 20 : 0
            TDivider { width: parent.width }
            TCheckBox {
                id: fixBox
                x: 14; y: 10
                width: parent.width - 28
                checked: true
                font.pixelSize: Theme.fontSmall
                text: qsTr("A sorok kerüljenek ki %1 hanglenyomatából (téves felismerés)").arg(control.speakerName)
            }
        }

        // ---- Hanglenyomat (csak kifejezett műveletre készül) ----
        Item {
            width: parent.width
            height: Math.max(vpText.height, vpButton.visible ? vpButton.height : 0) + 20
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
            visible: control.info.added === true && control.lineCount === 0
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

        // ---- Lábléc ----
        Item {
            width: parent.width
            height: 34
            Rectangle { x: 1; width: parent.width - 2; height: parent.height - 1; radius: Theme.radiusPopup - 1; color: Theme.surface }
            Rectangle { x: 1; width: parent.width - 2; height: parent.height / 2; color: Theme.surface }
            TDivider { width: parent.width }
            TLabel {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 28
                elide: Text.ElideRight
                muted: true
                font.pixelSize: Theme.fontCaption
                text: control.lineCount === 0 ? qsTr("Visszavonható: Ctrl+Z")
                    : qsTr("Minden sora (%n) átkerül · visszavonható: Ctrl+Z", "", control.lineCount)
            }
        }
    }
}
