import QtQuick
import QtQuick.Templates as T

// „Miért ő?" — a TELJES beszélő panelje (handoff-v3 E3, 430 px). A sín-fejléc avatarjáról, a
// térkép-dokk nevéről és a sor-panel „Miért ő?" linkjéről nyílik.
//   MIÉRT Ő?: amire a gép támaszkodott (hang a mintákhoz, sáv, címke, hasonló hang), mindegyik
//     mellett a javítás helye: Minták (hanglenyomat), Címkéi (Személyek ablak), Módosítás (a
//     sáv-chipek lent), Kettejük átnézése (páronkénti átnézés).
//   MELYIK SÁVON BESZÉL?: a kézi sáv-beosztás (egyetlen igazság, ugyanaz, mint az Áttekintésben);
//     alatta, amit a gép tanult.
//   NEM Ő? VALÓJÁBAN…: jelöltek bizonyítékkal (a beszélő minden sora hozzájuk kerül).
//   Új személy… · Összevonás egy másik beszélővel… · Átnézés másik beszélővel… · Ő nem volt ott.
//   „Ha másé, a sorai kerüljenek ki X hanglenyomatából" (téves felismerés).
// Minden művelet egy visszavonási lépés; az összevonást a hívó erősítteti meg.
TPopover {
    id: control

    property var editor: null               // TranscriptEditorViewModel
    property string speakerKey: ""
    property var info: ({})
    property bool canListen: false
    // Nyitáskor a sáv-chipek kiemelése (a „Sáv-beosztás" / „Módosítás" javításról).
    property bool focusTracks: false
    property bool merging: false
    property bool pairPicking: false

    signal listenRequested(int startMs, int endMs)
    signal mergeRequested(var request)
    signal voiceprintRequested(string speakerKey)
    signal peopleRequested(string personName)
    // „Új személy…": a hívó személyválasztót nyit; a választott névhez kerül a teljes beszélő.
    signal newPersonRequested(string speakerKey, bool fixVoiceprints)
    // „Ő nem volt ott": a sorai névtelenek lesznek, a „Ki volt ott?" is frissül (a hívó intézi).
    signal notPresentRequested(string speakerKey, bool fixVoiceprints)

    width: 430
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    readonly property string speakerName: (info.name || "")
                                          + (info.nameDuplicate && info.rawLabel ? " (" + info.rawLabel + ")" : "")
    readonly property bool named: info.anonymous === false
    readonly property int lineCount: info.utteranceCount || 0
    property int revision: 0
    readonly property var reasons: editor && speakerKey !== "" && revision >= 0 ? editor.speakerEvidence(speakerKey) : []
    readonly property var tracks: editor && speakerKey !== "" && revision >= 0 ? editor.trackOptions(speakerKey) : []
    readonly property string sideBasis: editor && speakerKey !== "" && revision >= 0 ? editor.sideBasisText(speakerKey) : ""
    readonly property var others: editor && revision >= 0 ? editor.speakersMatching("", speakerKey) : []
    readonly property var pairCandidates: editor && named && revision >= 0 ? editor.pairCandidates(speakerKey) : []
    readonly property bool fix: fixBox.visible && fixBox.checked

    function refresh() {
        info = editor ? editor.speakerInfo(speakerKey) : ({})
        revision++
    }
    function chooseCandidate(row) {
        const c = candidates.get(row)
        if (c.speakerKey === undefined) return
        const key = speakerKey
        const fixPrints = fix
        close()
        if (c.speakerKey !== "")
            mergeRequested({ kind: "merge", fromKey: key, intoKey: c.speakerKey, personName: "", fix: false })
        else
            editor.reassignSpeaker(key, c.personName, fixPrints)
    }
    // Sáv-chip: kattintás = csak ez a sáv; a kijelölt újra = a megkötés törlése.
    function toggleTrack(trackId, checked) {
        editor.setSpeakerTracks(speakerKey, checked ? [] : [trackId])
        revision++
    }
    function applyFix(ev) {
        if (ev.fixTarget === "samples") {
            const key = speakerKey
            close()
            voiceprintRequested(key)
        } else if (ev.fixTarget === "tags") {
            const person = info.personName || ""
            close()
            peopleRequested(person)
        } else if (ev.fixTarget === "tracks") {
            focusTracks = true
            body.contentY = Math.max(0, Math.min(tracksBlock.y, body.contentHeight - body.height))
        } else if (ev.fixTarget === "split") {
            const key = speakerKey
            close()
            editor.splitSpeaker(key)
        } else if (ev.pairKey) {
            const a = speakerKey
            close()
            editor.recheckPair(a, ev.pairKey)
        }
    }

    onAboutToShow: {
        refresh()
        fixBox.checked = true
        merging = false
        pairPicking = false
        body.contentY = 0
    }
    onOpened: if (focusTracks) Qt.callLater(() => { body.contentY = Math.max(0, Math.min(tracksBlock.y, body.contentHeight - body.height)) })
    onClosed: focusTracks = false
    Connections {
        target: control.editor
        ignoreUnknownSignals: true
        function onReviewChanged() { if (control.opened) control.revision++ }
        function onSpeakersChanged() { if (control.opened) control.refresh() }
    }

    CandidateListModel {
        id: candidates
        editor: control.editor
        speakerKey: control.speakerKey
        limit: 2
    }

    component SectionHead: Row {
        property string label: ""
        property string hint: ""
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
    // Link-sor: ikon · felirat (accent vagy szöveg színű) · halvány magyarázat.
    component LinkRow: Item {
        id: link
        property string iconName: ""
        property string text: ""
        property string hint: ""
        property bool accent: false
        signal clicked()
        width: parent ? parent.width : 0
        height: 30
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: linkHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
            TFocusRing { visible: link.activeFocus; targetRadius: parent.radius }
        }
        Row {
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9
            TIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: link.iconName
                size: 14
                color: link.accent ? Theme.accent : Theme.text
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: link.text
                color: link.accent ? Theme.accent : Theme.text
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                visible: link.hint !== ""
                text: link.hint
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }
        HoverHandler { id: linkHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: link.clicked() }
    }

    contentItem: Column {
        // ---- fejléc ----
        Item {
            width: parent.width
            height: head.height + 22
            Rectangle {
                id: listenButton
                objectName: "listenButton"
                visible: control.lineCount > 0
                anchors.right: parent.right
                anchors.rightMargin: 14
                y: 12
                width: 28; height: 28; radius: 14
                color: listenHover.hovered && control.canListen ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                border.width: 1
                border.color: Theme.borderStrong
                opacity: control.canListen ? 1 : 0.5
                TIcon { anchors.centerIn: parent; name: "play"; size: 12; color: Theme.text }
                HoverHandler { id: listenHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    enabled: control.canListen
                    onTapped: {
                        const sample = control.editor.speakerSample(control.speakerKey)
                        if (sample.ok === true) control.listenRequested(sample.startMs, sample.endMs)
                    }
                }
                TToolTip { visible: listenHover.hovered; text: qsTr("Meghallgatás: egy jellemző, hosszabb megszólalás ettől a beszélőtől") }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Meghallgatás")
            }
            Column {
                id: head
                x: 14; y: 12
                width: parent.width - 28 - listenButton.width - 8
                spacing: 3
                TLabel {
                    objectName: "popoverTitle"
                    width: parent.width
                    text: control.speakerName
                    font.pixelSize: Theme.fontSmall + 2
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                TLabel {
                    width: parent.width
                    muted: true
                    font.pixelSize: Theme.fontSmall - 0.5
                    elide: Text.ElideRight
                    text: {
                        const lines = qsTr("%n sor ezen a megbeszélésen", "", control.lineCount)
                        const conf = control.info.voiceConfidence
                        if (conf !== undefined && conf >= 0) return qsTr("%1 · azonosítva").arg(lines)
                        if (control.named) return qsTr("%1 · kézzel elnevezve").arg(lines)
                        return qsTr("%1 · névtelen beszélő").arg(lines)
                    }
                }
            }
        }

        Flickable {
            id: body
            width: parent.width
            readonly property real maxHeight: T.Overlay.overlay
                                              ? T.Overlay.overlay.height - 2 * control.margins - 60 - 34 - 16 : 520
            height: Math.min(bodyColumn.implicitHeight, Math.max(160, maxHeight))
            contentHeight: bodyColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}
            Behavior on contentY { enabled: control.opened; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }

            Column {
                id: bodyColumn
                width: body.width

                // ---- MIÉRT Ő? ----
                Column {
                    objectName: "whyBlock"
                    visible: control.reasons.length > 0
                    x: 10
                    width: parent.width - 20
                    bottomPadding: 8
                    SectionHead { x: 4; label: qsTr("MIÉRT Ő?"); hint: qsTr("erre támaszkodtunk") }
                    Repeater {
                        model: control.reasons
                        EvidenceReason {
                            required property var modelData
                            objectName: "whyReason"
                            width: parent.width
                            evidence: modelData
                            onFixClicked: ev => control.applyFix(ev)
                        }
                    }
                }

                // ---- MELYIK SÁVON BESZÉL? ----
                Item {
                    id: tracksBlock
                    objectName: "tracksBlock"
                    visible: control.tracks.length > 0
                    width: parent.width
                    height: visible ? tracksColumn.implicitHeight + 18 : 0
                    TDivider { width: parent.width }
                    Column {
                        id: tracksColumn
                        x: 14; y: 8
                        width: parent.width - 28
                        spacing: 6
                        SectionHead { label: qsTr("MELYIK SÁVON BESZÉL?"); hint: qsTr("ezen a megbeszélésen") }
                        Flow {
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: control.tracks
                                Item {
                                    id: chip
                                    required property var modelData
                                    objectName: "trackChip"
                                    readonly property bool checked: modelData.checked === true
                                    width: chipRow.implicitWidth + 20
                                    height: 28
                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.CheckBox
                                    Accessible.name: modelData.name
                                    Accessible.checked: checked
                                    Keys.onSpacePressed: control.toggleTrack(modelData.id, checked)
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: Theme.radiusControl
                                        color: chip.checked ? Theme.accentSoft : Theme.raised
                                        border.width: control.focusTracks && !chip.checked ? 1.5 : 1
                                        border.color: chip.checked ? Theme.accent : control.focusTracks ? Theme.accentLine : Theme.borderStrong
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: parent.radius
                                            color: Theme.stateLayer
                                            opacity: chipHover.hovered ? Theme.hoverOpacity : 0
                                        }
                                        TFocusRing { visible: chip.activeFocus; targetRadius: parent.radius }
                                    }
                                    Row {
                                        id: chipRow
                                        anchors.centerIn: parent
                                        spacing: 7
                                        TIcon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: chip.modelData.kind === "mic" ? "mic" : chip.modelData.kind === "loopback" ? "phone" : "audio-lines"
                                            size: 14
                                            color: chip.checked ? Theme.accent : Theme.text
                                        }
                                        TLabel {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: chip.modelData.name
                                            color: chip.checked ? Theme.accent : Theme.text
                                            font.pixelSize: Theme.fontSmall
                                            font.weight: chip.checked ? Theme.weightSemiBold : Theme.weightMedium
                                        }
                                    }
                                    HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: control.toggleTrack(chip.modelData.id, chip.checked) }
                                    TToolTip {
                                        visible: chipHover.hovered
                                        text: chip.checked ? qsTr("A kézi beosztás törlése (a gép újra maga dönti el)")
                                                           : qsTr("%1 ezen a sávon beszél — a sáv-ellentmondás ehhez igazodik (Ctrl+Z)").arg(control.speakerName)
                                    }
                                }
                            }
                        }
                        TLabel {
                            visible: control.sideBasis !== ""
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: control.sideBasis
                            muted: true
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                }

                // ---- NEM Ő? VALÓJÁBAN… ----
                Item {
                    visible: candidates.count > 0
                    width: parent.width
                    height: visible ? notColumn.implicitHeight + 12 : 0
                    TDivider { width: parent.width }
                    Column {
                        id: notColumn
                        x: 8; y: 8
                        width: parent.width - 16
                        SectionHead { x: 6; label: qsTr("NEM Ő? VALÓJÁBAN…") }
                        Repeater {
                            model: candidates
                            CandidateRow {
                                required property int index
                                required property var model
                                objectName: "candidateChoice"
                                width: notColumn.width
                                personName: model.name
                                colorIndex: model.colorIndex
                                subText: model.subText
                                evidence: model.evidence
                                dimmed: model.otherSide
                                onClicked: control.chooseCandidate(index)
                            }
                        }
                    }
                }

                // ---- további teendők ----
                Column {
                    x: 10
                    width: parent.width - 20
                    topPadding: 4
                    bottomPadding: 8
                    LinkRow {
                        objectName: "newPersonLink"
                        iconName: "user-plus"
                        accent: true
                        text: qsTr("Új személy…")
                        hint: control.lineCount > 0 ? qsTr("a %n sor egy új névhez kerül", "", control.lineCount) : ""
                        onClicked: {
                            const key = control.speakerKey
                            const f = control.fix
                            control.close()
                            control.newPersonRequested(key, f)
                        }
                    }
                    LinkRow {
                        objectName: "mergeLink"
                        visible: control.others.length > 0
                        iconName: "merge"
                        text: qsTr("Összevonás egy másik beszélővel…")
                        onClicked: control.merging = !control.merging
                    }
                    Column {
                        objectName: "mergeList"
                        visible: control.merging
                        x: 18
                        width: parent.width - 18
                        Repeater {
                            model: control.merging ? control.others : []
                            PersonRow {
                                required property var modelData
                                objectName: "speakerChoice"
                                compact: true
                                width: parent.width
                                personName: modelData.name
                                speakerIndex: modelData.colorIndex
                                subText: qsTr("%n sor", "", modelData.utteranceCount)
                                highlighted: hovered
                                onClicked: {
                                    const key = control.speakerKey
                                    control.close()
                                    control.mergeRequested({ kind: "merge", fromKey: key, intoKey: modelData.key,
                                                             personName: "", fix: false })
                                }
                            }
                        }
                    }
                    LinkRow {
                        objectName: "pairRecheckButton"
                        visible: control.named && control.pairCandidates.length > 0 && control.editor && control.editor.voiceAvailable
                        iconName: "users"
                        text: qsTr("Átnézés másik beszélővel…")
                        hint: qsTr("ha két hang sorai összekeveredtek")
                        onClicked: control.pairPicking = !control.pairPicking
                    }
                    Column {
                        objectName: "pairRecheckList"
                        visible: control.pairPicking
                        x: 18
                        width: parent.width - 18
                        Repeater {
                            model: control.pairPicking ? control.pairCandidates : []
                            PersonRow {
                                required property var modelData
                                objectName: "pairChoice"
                                compact: true
                                width: parent.width
                                personName: modelData.name
                                speakerIndex: modelData.colorIndex
                                subText: qsTr("%n sor", "", modelData.utteranceCount)
                                highlighted: hovered
                                onClicked: {
                                    const a = control.speakerKey
                                    control.close()
                                    control.editor.recheckPair(a, modelData.key)
                                }
                            }
                        }
                    }
                    LinkRow {
                        objectName: "notPresentLink"
                        visible: control.named
                        iconName: "user-x"
                        text: qsTr("Ő nem volt ott")
                        hint: qsTr("a „Ki volt ott?” is frissül")
                        onClicked: {
                            const key = control.speakerKey
                            const f = control.fix
                            control.close()
                            control.notPresentRequested(key, f)
                        }
                    }
                    LinkRow {
                        objectName: "removeEmptyLink"
                        visible: control.info.added === true && control.lineCount === 0
                        iconName: "trash-2"
                        text: qsTr("Üres oszlop eltávolítása")
                        onClicked: {
                            const key = control.speakerKey
                            control.close()
                            control.editor.removeParticipant(key)
                        }
                    }
                }

                // ---- téves felismerés ----
                Item {
                    visible: control.named && control.info.hasVoiceprint === true
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
            }
        }

        // ---- lábléc ----
        Item {
            width: parent.width
            height: 34
            Rectangle { x: 1; width: parent.width - 2; height: parent.height - 1; radius: Theme.radiusPopup - 1; color: Theme.warnSoft }
            Rectangle { x: 1; width: parent.width - 2; height: parent.height / 2; color: Theme.warnSoft }
            TDivider { width: parent.width; color: Theme.warnLine }
            TLabel {
                objectName: "popoverFooter"
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 28
                elide: Text.ElideRight
                color: Theme.warnInk
                font.pixelSize: Theme.fontCaption
                text: control.lineCount === 0 ? qsTr("Ctrl+Z visszavonja")
                    : control.editor && control.editor.needsAz(control.lineCount)
                      ? qsTr("A teljes beszélőre vonatkozik: mind az %n sor · Ctrl+Z visszavonja", "", control.lineCount)
                      : qsTr("A teljes beszélőre vonatkozik: mind a %n sor · Ctrl+Z visszavonja", "", control.lineCount)
            }
        }
    }
}
