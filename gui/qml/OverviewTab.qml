import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Áttekintés fül (design/handoff-v3 V1/V3/V5/V6, 6–10. döntés): az első fül, két oszlop.
//   bal (miről és kikről): MIRŐL SZÓL (forrás-kártya: kézi leírás; naptár később), címkemező a
//       javaslatokkal, KIK VOLTAK OTT (résztvevők forrás-jelvényekkel és beszédaránnyal; üresen
//       „+ Résztvevő” / „Csak én beszéltem”; ha mindenki ismeretlen: a nyers beszélők +
//       „Elnevezem őket”), és a jóváhagyás-banner („A hanglenyomat-elemzés kész: N jelölt”).
//   jobb 340 px (technikai): FELDOLGOZÁS lépései, SÁVOK (lejátszás, lekeverés-kapcsoló,
//       „Beemelem”); kész megbeszélésnél ADATOK 2×3 csempe és a lépések / sávok egy-egy sorba
//       csukva. A régi Sávok fül teljes nézete (átnevezés, megkeresés, visszaállítás, végleges
//       törlés, lekeverés) a „Részletek” panelen.
// Átirat előtt (U6) is ez a fül látszik: a sávok hallgathatók / kapcsolhatók, résztvevő és címke
//       adható, és az „Átírás indítása” (szolgáltató-állapot, hiányzó beállítás, hiba „Újra”-val)
//       a FELDOLGOZÁS „Átirat” lépésében él (TranscribeStepPanel); kézi lekeverésnél a „Lekeverés”
//       lépésben a „Lekeverés indítása”, a hanglenyomat-lépésben az azonosítás kapcsolója.
// Az első átírás futása közben (A1) ugyanebben a lépésben a szakaszok, a „Közben nyugodtan
//       dolgozz tovább” sor és a „Megszakítás”. Átírás előtt a forrás-kártya alatt a jegyzet-
//       sablonok hasonló megbeszélésekből és az észlelt hívás (A3); a címkesor a cím alapján
//       javasol, ha nincs más javaslat (A2, MeetingTagsModel.draftTitle).
// A résztvevő-sor jelvényei (A4): tooltip a bizonyítékkal; átirat után kattintásra a szerkesztő
//       „Miért ő?” panelje (shell.openSpeakerWhy). A névtelen sor (A5) kattintásra / „Elnevezés…”
//       gombbal elnevezhető: átirat után személyválasztó → editor.reassignSpeaker; átirat előtt a
//       „Ki volt ott?” párbeszéd (ott a név szerkeszthető). A hanglenyomat-lépésen „Újraelemzés” (D4).
// A „Ki volt ott?” párbeszéd (U3) a participantsRequested jelre nyílik (Main: shell.openParticipants).
// Változás-sáv (G7b, v3 4. döntés): az Áttekintés minden művelete (jóváhagyás, „Csak én beszéltem”,
//       sáv be / ki, leírás, résztvevő felvétele / törlése, javaslat elutasítása, címke, elnevezés) a
//       megbeszélés KÖZÖS visszavonási vermébe kerül (MeetingUndoService); a Main a héj
//       meetingChangePosted jelére showChange(text)-et hív → sötét sáv a bal oszlop alján:
//       „<mi történt> · Visszavonás · Ctrl+Z · ×”; visszavonás után „Visszavonva: …” (showUndone).
// Képernyőképhez: --qml-prop 'demoState="overviewProcessing|overviewDone|overviewEmpty|overviewNobody|
//   overviewRecorded|overviewRecordedError|overviewTranscribing"'.
Item {
    id: root

    property string meetingId: ""
    property var player: null                // PlayerController vagy null
    property var shell: null                 // ShellActions vagy null
    property var tagsModel: null             // MeetingTagsModel (ShellMeetingModel.tags) vagy null
    property var editor: null                // TranscriptEditorViewModel (a személyválasztóhoz) vagy null
    property string demoState: ""

    readonly property alias vm: vm
    readonly property alias tracks: tracks
    readonly property alias changeBar: changeBar

    signal participantsRequested()

    // „Később”: a banner ebben a munkamenetben, ennél a megbeszélésnél nem jön elő.
    property var laterFor: ({})
    readonly property bool bannerShown: vm.approvalPending && !laterFor[meetingId]
    // Kész állapotban az összecsukott sorok nyitása.
    property bool stepsOpen: false
    property bool tracksOpen: false
    onMeetingIdChanged: { stepsOpen = false; tracksOpen = false; changeBar.dismiss() }

    // A V5-szerű (két sávos, üres) demó-állapotok.
    readonly property bool demoTwoTracks: ["overviewEmpty", "overviewNobody", "overviewRecorded",
                                           "overviewRecordedError", "overviewTranscribing"].indexOf(root.demoState) >= 0
    // Átirat előtt, az átírás indítása előtt: a lépésekben az indító vezérlők.
    readonly property bool beforeTranscription: !vm.hasTranscript && !vm.transcribing
    // A hanglenyomat-lépés állapota (az „Újraelemzés” csak lefutott elemzés után látszik).
    readonly property string voiceState: {
        for (const s of vm.steps) if (s.key === "voice") return s.state
        return ""
    }
    readonly property bool identifyShown: root.beforeTranscription && pre.identifyAvailable
    readonly property bool reanalyzeShown: vm.canReanalyze && (root.voiceState === "done" || root.voiceState === "attention")

    OverviewViewModel {
        id: vm
        meetingId: root.meetingId
        demoState: root.demoState
    }
    // Az átírás kapuzása, hibája, a lekeverés állapota és az azonosítás kapcsolója (a régi
    // átirat előtti nézet modellje) — csak amíg nincs átirat.
    PreTranscriptViewModel {
        id: pre
        meetingId: vm.hasTranscript ? "" : root.meetingId
        demoState: root.demoState === "overviewRecordedError" ? "failed"
                 : root.demoState === "overviewRecorded" ? "readyNote"
                 : root.demoState === "overviewTranscribing" ? "uploading"
                 : root.demoState === "overviewProcessing" || root.demoState === "overviewEmpty" ? "running" : ""
    }
    TrackListModel {
        id: tracks
        meetingId: root.meetingId
        demoState: root.demoTwoTracks ? "overviewTwo" : "overview"
    }
    // Beszélő → sáv kötés a KIK VOLTAK OTT soraiban (G6, sáv-chipek).
    ParticipantTrackBinder {
        id: trackBinder
        objectName: "participantTrackBinder"
        meetingId: root.meetingId
    }
    // Saját címke-modell, ha a gazda nem ad (önálló képernyőkép): kitalált készlet.
    MeetingTagsModel {
        id: ownTags
        demoState: root.demoTwoTracks ? "none"
                 : root.demoState === "overviewDone" ? "few" : "similar"
    }
    // A Beállítások bezárása után a kapuzás újraolvasása (kulcs került be / ki).
    onVisibleChanged: if (visible && root.beforeTranscription) pre.refresh()
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive && root.visible && root.beforeTranscription)
                pre.refresh()
        }
    }
    readonly property var effectiveTags: tagsModel ? tagsModel : ownTags
    // A2: átírás előtt a cím alapú tartalék-javaslat (ha nincs rendes javaslat).
    Binding {
        target: root.effectiveTags
        property: "draftTitle"
        value: vm.hasTranscript ? "" : vm.title
    }

    function openPicker(anchor) {
        const p = anchor.mapToItem(root, 0, anchor.height)
        picker.x = Math.max(8, Math.min(root.width - picker.width - 8, p.x + anchor.width - picker.width))
        picker.y = p.y + 6
        picker.open()
    }
    // A „Sávok részletei” panel (régi Sávok fül; a Main demó-állapota is ezt nyitja).
    function openTracks() { tracksDialog.open() }
    // Átírás indítása előtt (a lépés gombja, a hiba „Újra”-ja) a félbehagyott leírás mentődik.
    function commitSource() { if (sourceCard.editing) sourceCard.finish(true) }
    function solo() {
        if (vm.setSolo() || !root.shell) return
        root.shell.toast(qsTr("Ehhez add meg a saját neved: Beállítások › Beszélők › Te."))
        root.shell.openSettings("speakers")
    }
    // A4: a jelvény → a szerkesztő „Miért ő?” panelje arra a beszélőre (Átirat fül).
    function explainSpeaker(speakerKey) {
        if (root.shell && typeof root.shell.openSpeakerWhy === "function")
            root.shell.openSpeakerWhy(root.meetingId, speakerKey)
    }
    // A5: a névtelen sor elnevezése. Átirat után személyválasztó (→ editor.reassignSpeaker);
    // átirat előtt a „Ki volt ott?” párbeszéd (a core ott engedi a név megadását).
    function nameRow(row, anchor) {
        if (!vm.hasTranscript || (row.speakerKey || "") === "") {
            root.participantsRequested()
            return
        }
        namePicker.speakerKey = row.speakerKey
        const p = anchor.mapToItem(root, 0, anchor.height)
        namePicker.x = Math.max(8, Math.min(root.width - namePicker.width - 8, p.x + anchor.width - namePicker.width))
        namePicker.y = p.y + 6
        namePicker.open()
    }
    // A változás-sáv (a Main hívja a közös verem jeleire; demóban a képernyőképhez).
    function showChange(text) { changeBar.post(text, false) }
    function showUndone(text) { changeBar.post(qsTr("Visszavonva: %1").arg(text), true) }
    function hideChange() { changeBar.dismiss() }

    // „Újraelemzés” (D4): átirat után a menü „Hanglenyomat-elemzés újra”-jával azonos (héj),
    // átirat előtt a hangelemzés újra (analyzeParticipants).
    function reanalyze() {
        if (vm.hasTranscript) { if (root.shell) root.shell.identifyParticipants(root.meetingId) }
        else vm.reanalyze()
    }

    // Szakasz-fejléc: 12/600 nagybetűs címke · halvány tipp · jobbra accent hivatkozás.
    component SectionHeader: RowLayout {
        id: sh
        property string title: ""
        property string hint: ""
        property string actionText: ""
        property string actionIcon: ""
        property string actionName: ""
        signal actionClicked(Item anchor)
        spacing: 10
        TLabel {
            text: sh.title
            muted: true
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
            font.letterSpacing: 0.7
        }
        TLabel {
            Layout.fillWidth: true
            text: sh.hint
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
        T.AbstractButton {
            id: link
            objectName: sh.actionName
            visible: sh.actionText !== ""
            hoverEnabled: true
            implicitWidth: linkRow.implicitWidth + 6
            implicitHeight: 22
            onClicked: sh.actionClicked(link)
            contentItem: Item {
                Row {
                    id: linkRow
                    anchors.centerIn: parent
                    spacing: 4
                    TIcon { visible: sh.actionIcon !== ""; anchors.verticalCenter: parent.verticalCenter; name: sh.actionIcon; size: 13; color: Theme.accent }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: sh.actionText
                        color: Theme.accent
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightMedium
                        font.underline: link.hovered
                    }
                }
            }
            background: Item { TFocusRing { visible: link.visualFocus; targetRadius: Theme.radiusTag } }
        }
    }
    // Összecsukott sor (kész állapot): ikon-kör · cím + alszöveg · ›
    component CollapsedRow: T.AbstractButton {
        id: cr
        property string iconName: ""
        property string title: ""
        property string sub: ""
        property bool open: false
        property bool first: false
        hoverEnabled: true
        implicitHeight: 52
        contentItem: RowLayout {
            spacing: 10
            Item {
                implicitWidth: 26; implicitHeight: 26
                Rectangle { anchors.fill: parent; radius: 13; color: "transparent"; border.width: 1; border.color: Theme.borderStrong }
                TIcon { anchors.centerIn: parent; name: "check"; size: 12; color: Theme.text }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    spacing: 5
                    TIcon { name: cr.iconName; size: 12; color: Theme.textMuted }
                    TLabel { text: cr.title; font.pixelSize: Theme.fontSmall; font.weight: Theme.weightSemiBold }
                }
                TLabel { Layout.fillWidth: true; text: cr.sub; muted: true; font.pixelSize: 12; elide: Text.ElideRight }
            }
            TIcon { name: cr.open ? "chevron-down" : "chevron-right"; size: 14; color: Theme.textMuted }
        }
        leftPadding: 10; rightPadding: 10
        background: Item {
            Rectangle { visible: !cr.first; width: parent.width; height: 1; color: Theme.border }
            Rectangle { anchors.fill: parent; color: Theme.stateLayer; opacity: cr.hovered ? Theme.hoverOpacity : 0 }
        }
    }

    // Sötét változás-sáv (a TranscriptChangeBar formanyelvén): üzenet · Visszavonás · Ctrl+Z · ×.
    // Rámutatás alatt nem jár le; az új üzenet a régit váltja.
    component ChangeBar: Rectangle {
        id: bar
        property string text: ""
        property bool undone: false     // „Visszavonva: …” — nincs több teendő rajta
        property bool shown: false
        function post(message, isUndone) {
            bar.text = message
            bar.undone = isUndone
            bar.shown = message !== ""
            expiry.restart()
        }
        function dismiss() { bar.shown = false; expiry.stop() }
        objectName: "overviewChangeBar"
        visible: shown
        height: 44
        radius: Theme.radiusPopup
        color: Theme.text
        TShadow { radius: bar.radius }
        HoverHandler { id: barHover }
        Timer {
            id: expiry
            interval: bar.undone ? 5000 : 12000
            running: bar.shown && !barHover.hovered
            onTriggered: bar.shown = false
        }
        TIcon {
            id: barIcon
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            name: bar.undone ? "undo-2" : "circle-check"
            size: 16
            color: Theme.bg
        }
        TLabel {
            id: barText
            objectName: "overviewChangeText"
            anchors { left: barIcon.right; leftMargin: 8; right: barActions.left; rightMargin: 12
                      verticalCenter: parent.verticalCenter }
            text: bar.text
            color: Theme.bg
            elide: Text.ElideRight
            font.weight: Theme.weightSemiBold
        }
        Row {
            id: barActions
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 6
            BarAction {
                objectName: "overviewChangeUndo"
                visible: !bar.undone
                strong: true
                iconName: "undo-2"
                text: qsTr("Visszavonás · Ctrl+Z")
                onClicked: {
                    const hasUndo = root.shell && root.shell.hasMeetingUndo === true
                    if (hasUndo) root.shell.undoMeeting()
                    else bar.post(qsTr("Visszavonva: %1").arg(bar.text), true)   // demó
                }
            }
            BarAction {
                objectName: "overviewChangeClose"
                iconName: "x"
                toolTipText: qsTr("Bezárás")
                onClicked: bar.dismiss()
            }
        }
    }
    // Sötét sávra írt művelet (felirat + opcionális ikon). `strong`: keretes, hangsúlyos.
    component BarAction: Item {
        id: action
        property string text: ""
        property string iconName: ""
        property bool strong: false
        property string toolTipText: ""
        signal clicked()
        implicitWidth: actionRow.implicitWidth + 20
        width: implicitWidth
        height: 28
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text !== "" ? text : toolTipText
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Theme.alpha(Theme.bg, actionTap.pressed ? 0.24 : actionHover.hovered ? 0.16 : action.strong ? 0.08 : 0)
            border.width: action.strong ? 1 : 0
            border.color: Theme.alpha(Theme.bg, 0.4)
            TFocusRing { visible: action.activeFocus; targetRadius: parent.radius }
        }
        Row {
            id: actionRow
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            TIcon {
                visible: action.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: action.iconName
                size: 14
                color: Theme.bg
            }
            TLabel {
                visible: action.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: action.text
                color: Theme.bg
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
            }
        }
        HoverHandler { id: actionHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: actionTap; onTapped: action.clicked() }
        TToolTip { visible: action.toolTipText !== "" && actionHover.hovered; text: action.toolTipText }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ================= bal oszlop =================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Flickable {
                id: leftFlick
                objectName: "overviewLeft"
                anchors.fill: parent
                // A lebegő változás-sáv ne takarja az utolsó sort.
                contentHeight: left.implicitHeight + 36 + (changeBar.visible ? changeBar.height + 12 : 0)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: TScrollBar {}

                ColumnLayout {
                    id: left
                    x: Theme.space5
                    y: 18
                    width: leftFlick.width - 2 * Theme.space5
                    spacing: 10

                    SectionHeader {
                        Layout.fillWidth: true
                        title: vm.hasTranscript ? qsTr("MIRŐL SZÓLT") : qsTr("MIRŐL SZÓL")
                        actionText: qsTr("Forrás")
                        actionIcon: "plus"
                        actionName: "addSourceLink"
                        onActionClicked: (anchor) => sourceMenu.popup(anchor, anchor.width - sourceMenu.width, anchor.height + 4)
                        TMenu {
                            id: sourceMenu
                            TMenuItem { text: qsTr("Leírás"); iconName: "file-text"; onTriggered: sourceCard.startEdit() }
                            TMenuItem { text: qsTr("Naptár-esemény (hamarosan)"); iconName: "calendar"; enabled: false }
                        }
                    }
                    SourceCard {
                        id: sourceCard
                        Layout.fillWidth: true
                        note: vm.contextNote
                        suggestions: vm.hasTranscript ? [] : pre.note.suggestions
                        detectedCallApp: vm.hasTranscript ? "" : pre.note.detectedCallApp
                        onSaveRequested: (note) => vm.setContextNote(note)
                    }
                    TagRowOverview {
                        objectName: "overviewTags"
                        Layout.fillWidth: true
                        model: root.effectiveTags
                        onTagClicked: (id) => { if (root.shell) root.shell.filterByTag(id) }
                        onMeetingRequested: (id) => { if (root.shell) root.shell.showMeeting(id) }
                    }

                    SectionHeader {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        title: qsTr("KIK VOLTAK OTT")
                        hint: vm.peopleHint
                        actionText: vm.approved ? qsTr("Módosítás") : vm.peopleState === "list" ? qsTr("+ Résztvevő") : ""
                        actionName: "participantsAction"
                        onActionClicked: (anchor) => vm.approved ? root.participantsRequested() : root.openPicker(anchor)
                    }
                    ParticipantList {
                        objectName: "participantList"
                        visible: vm.peopleState === "list"
                        Layout.fillWidth: true
                        people: vm.participants
                        canExplain: vm.hasTranscript
                        trackBinder: trackBinder
                        onAddRequested: (name) => vm.addParticipant(name)
                        onRemoveRequested: (id) => vm.removeParticipant(id)
                        onWhyRequested: (key) => root.explainSpeaker(key)
                        onNameRequested: (row, anchor) => root.nameRow(row, anchor)
                    }
                    EmptyCard {
                        objectName: "peopleEmpty"
                        visible: vm.peopleState === "empty"
                        Layout.fillWidth: true
                        iconName: "users"
                        title: qsTr("Még nem tudjuk, kik voltak ott")
                        text: vm.hasTranscript ? qsTr("Add meg, kik voltak ott: a beszélők így a nevükre kerülnek, és legközelebb a hangjukról is felismerjük őket.")
                                               : qsTr("A hanglenyomat-elemzés után ide jönnek a jelöltek. Ha tudod, add meg most: az azonosítás így pontosabb lesz.")
                        TButton {
                            id: addPersonButton
                            objectName: "emptyAddParticipant"
                            text: qsTr("+ Résztvevő")
                            size: "small"
                            onClicked: root.openPicker(addPersonButton)
                        }
                        TButton {
                            objectName: "soloButton"
                            text: qsTr("Csak én beszéltem")
                            size: "small"
                            toolTipText: qsTr("Minden beszéd a te nevedre kerül, az azonosítás kimarad")
                            onClicked: root.solo()
                        }
                    }
                    EmptyCard {
                        objectName: "peopleNobody"
                        visible: vm.peopleState === "nobody"
                        Layout.fillWidth: true
                        iconName: "fingerprint"
                        title: qsTr("%n beszélő, ismeretlen hanggal", "", vm.anonymousSpeakers.length)
                        text: qsTr("Egyikük hangja sem hasonlít egy ismert emberére sem. Elnevezheted őket; legközelebb már felismerjük a hangjukat.")
                        Repeater {
                            model: vm.anonymousSpeakers
                            Rectangle {
                                required property var modelData
                                width: chipRow.implicitWidth + 12
                                height: 26
                                radius: 13
                                color: Theme.speakerSoft(modelData.colorIndex)
                                border.width: 1
                                border.color: Theme.speakerLine(modelData.colorIndex)
                                Row {
                                    id: chipRow
                                    x: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6
                                    TAvatar {
                                        anchors.verticalCenter: parent.verticalCenter
                                        size: 20
                                        variant: "solid"
                                        speakerIndex: modelData.colorIndex
                                        monogram: "B" + (modelData.colorIndex + 1)
                                    }
                                    TLabel {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.name
                                        color: Theme.speakerInk(modelData.colorIndex)
                                        font.pixelSize: 12
                                        font.weight: Theme.weightSemiBold
                                    }
                                    TLabel {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: Math.round(modelData.share * 100) + "%"
                                        mono: true
                                        color: Theme.speakerInk(modelData.colorIndex)
                                        opacity: 0.75
                                        font.pixelSize: Theme.fontMicro
                                    }
                                }
                            }
                        }
                        TButton {
                            objectName: "nameThemButton"
                            text: qsTr("Elnevezem őket")
                            variant: "primary"
                            size: "small"
                            onClicked: root.participantsRequested()
                        }
                    }

                    // Jóváhagyás-banner (a hangelemzés kész, még nincs döntés).
                    Rectangle {
                        objectName: "approvalBanner"
                        visible: root.bannerShown
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        implicitHeight: bannerRow.implicitHeight + 22
                        radius: Theme.radiusPopup
                        color: Theme.accentSoft
                        border.width: 1
                        border.color: Theme.accentLine
                        RowLayout {
                            id: bannerRow
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 28
                            spacing: 12
                            TIcon { name: "fingerprint"; size: 18; color: Theme.accent }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                TLabel {
                                    Layout.fillWidth: true
                                    text: qsTr("A hanglenyomat-elemzés kész: %n jelölt", "", vm.candidateCount)
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Theme.weightSemiBold
                                    wrapMode: Text.Wrap
                                }
                                TLabel {
                                    Layout.fillWidth: true
                                    text: vm.hasTranscript ? qsTr("Nézd át, kik voltak ott. Ebből kötöm az átirat beszélőit a nevekhez.")
                                                           : qsTr("Nézd át, kik voltak ott. Ebből köti az átirat a beszélőket, amikor megérkezik.")
                                    muted: true
                                    font.pixelSize: 12
                                    cssLineHeight: 1.4
                                    wrapMode: Text.Wrap
                                }
                            }
                            TButton {
                                objectName: "reviewButton"
                                text: qsTr("Átnézem")
                                variant: "primary"
                                onClicked: root.participantsRequested()
                            }
                            TButton {
                                objectName: "laterButton"
                                text: qsTr("Később")
                                variant: "ghost"
                                muted: true
                                onClicked: {
                                    const later = Object.assign({}, root.laterFor)
                                    later[root.meetingId] = true
                                    root.laterFor = later
                                }
                            }
                        }
                    }
                }
            }
            ChangeBar {
                id: changeBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                          leftMargin: Theme.space5; rightMargin: Theme.space5; bottomMargin: 10 }
            }
        }

        // ================= jobb oszlop (340) =================
        Rectangle {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            color: Theme.surface
            Rectangle { width: 1; height: parent.height; color: Theme.border }

            Flickable {
                id: rightFlick
                objectName: "overviewRight"
                anchors { fill: parent; leftMargin: 1 }
                contentHeight: right.implicitHeight + 36
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: TScrollBar {}

                ColumnLayout {
                    id: right
                    x: 18; y: 18
                    width: rightFlick.width - 36
                    spacing: 10

                    // ---- kész: ADATOK + összecsukott sorok ----
                    SectionHeader { visible: vm.allDone && vm.stats.length > 0; Layout.fillWidth: true; title: qsTr("ADATOK") }
                    StatTiles {
                        objectName: "statTiles"
                        visible: vm.allDone && vm.stats.length > 0
                        Layout.fillWidth: true
                        stats: vm.stats
                    }
                    SectionHeader {
                        visible: vm.allDone
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        title: qsTr("FELDOLGOZÁS ÉS SÁVOK")
                    }
                    Rectangle {
                        visible: vm.allDone
                        Layout.fillWidth: true
                        implicitHeight: collapsedColumn.implicitHeight
                        radius: Theme.radiusPopup
                        color: Theme.raised
                        border.width: 1
                        border.color: Theme.border
                        clip: true
                        Column {
                            id: collapsedColumn
                            width: parent.width
                            CollapsedRow {
                                objectName: "stepsCollapsed"
                                width: parent.width
                                first: true
                                iconName: "circle-check"
                                title: qsTr("Minden lépés kész")
                                sub: vm.stepsSummary
                                open: root.stepsOpen
                                onClicked: root.stepsOpen = !root.stepsOpen
                            }
                            CollapsedRow {
                                objectName: "tracksCollapsed"
                                width: parent.width
                                iconName: "audio-lines"
                                title: qsTr("Sávok")
                                sub: tracks.excludedCount > 0
                                     ? qsTr("%n aktív", "", tracks.includedCount) + " · " + qsTr("%n kimaradt", "", tracks.excludedCount)
                                     : qsTr("%n aktív", "", tracks.includedCount)
                                open: root.tracksOpen
                                onClicked: root.tracksOpen = !root.tracksOpen
                            }
                        }
                    }

                    // ---- lépések ----
                    SectionHeader {
                        visible: !vm.allDone
                        Layout.fillWidth: true
                        title: qsTr("FELDOLGOZÁS")
                    }
                    PipelineSteps {
                        objectName: "pipelineSteps"
                        visible: !vm.allDone || root.stepsOpen
                        Layout.fillWidth: true
                        steps: vm.steps
                        // Átírás előtt az indító, az első átírás közben a szakaszok + „Megszakítás” (A1).
                        transcriptContent: !vm.hasTranscript && (root.beforeTranscription || vm.transcribing)
                                           ? transcribeStep : null
                        mixdownContent: root.beforeTranscription && (pre.mixdownState === "missing" || pre.mixdownState === "stale")
                                        ? mixdownStep : null
                        voiceContent: root.identifyShown || root.reanalyzeShown ? voiceStep : null
                    }

                    // ---- sávok ----
                    SectionHeader {
                        visible: !vm.allDone || root.tracksOpen
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        title: qsTr("SÁVOK")
                        actionText: qsTr("Részletek")
                        actionName: "tracksDetailsLink"
                        onActionClicked: tracksDialog.open()
                    }
                    TrackList {
                        objectName: "trackList"
                        visible: !vm.allDone || root.tracksOpen
                        Layout.fillWidth: true
                        tracks: tracks
                        player: root.player
                        sideWho: vm.sideWho
                    }
                }
            }
        }
    }

    // ---- a lépésekbe ágyazott vezérlők (átirat előtt) ----
    Component {
        id: transcribeStep
        TranscribeStepPanel {
            vm: pre
            shell: root.shell
            meetingId: root.meetingId
            onStartRequested: root.commitSource()
        }
    }
    Component {
        id: mixdownStep
        RowLayout {
            spacing: 8
            TButton {
                objectName: "startMixdownButton"
                text: pre.mixdownState === "stale" ? qsTr("Lekeverés frissítése") : qsTr("Lekeverés indítása")
                size: "small"
                iconName: "audio-lines"
                iconSize: 14
                onClicked: pre.startMixdown()
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: voiceStep
        ColumnLayout {
            spacing: 6
            RowLayout {
                visible: root.identifyShown
                Layout.fillWidth: true
                spacing: 8
                TSwitch {
                    objectName: "identifySwitch"
                    checked: pre.identifyEnabled
                    onToggled: pre.identifyEnabled = checked
                    Accessible.name: qsTr("Résztvevők azonosítása hang alapján")
                }
                TLabel {
                    Layout.fillWidth: true
                    text: pre.identifyEnabled ? qsTr("Azonosítás hang alapján") : qsTr("Kihagyva: a beszélők névtelenek maradnak")
                    muted: true
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
            TButton {
                objectName: "reanalyzeButton"
                visible: root.reanalyzeShown
                text: qsTr("Újraelemzés")
                size: "small"
                iconName: "refresh-cw"
                iconSize: 14
                toolTipText: vm.hasTranscript ? qsTr("A hanglenyomat-elemzés újra: a névtelen beszélők a mentett hangmintákhoz")
                                              : qsTr("A hanglenyomat-elemzés újra a sávokon")
                onClicked: root.reanalyze()
            }
        }
    }

    // ---- „+ Résztvevő” ----
    PersonPicker {
        id: picker
        objectName: "overviewPersonPicker"
        editor: root.editor
        anonymousText: ""
        onPersonChosen: name => vm.addParticipant(name)
    }
    // ---- „Elnevezés…” (névtelen beszélő, átirat után) ----
    PersonPicker {
        id: namePicker
        objectName: "overviewNamePicker"
        property string speakerKey: ""
        editor: root.editor
        anonymousText: ""
        hintText: qsTr("A beszélő minden sora a választott névre kerül; Ctrl+Z visszavonja.")
        onPersonChosen: name => {
            if (vm.nameSpeakerInDemo(namePicker.speakerKey, name)) return
            if (root.editor) root.editor.reassignSpeaker(namePicker.speakerKey, name, false)
        }
    }

    // ---- a régi Sávok fül teljes nézete ----
    TDialog {
        id: tracksDialog
        objectName: "tracksDialog"
        title: qsTr("Sávok részletei")
        width: Math.min(900, (parent ? parent.width : 900) - 32)
        TracksTab {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(520, root.height - 120)
            meetingId: root.meetingId
            player: root.player
            shell: root.shell
        }
        actions: [ TButton { text: qsTr("Bezárás"); onClicked: tracksDialog.close() } ]
    }
}
