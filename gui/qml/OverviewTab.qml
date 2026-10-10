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
// A „Ki volt ott?” párbeszéd (U3) a participantsRequested jelre nyílik (Main: shell.openParticipants).
// Képernyőképhez: --qml-prop 'demoState="overviewProcessing|overviewDone|overviewEmpty|overviewNobody"'.
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

    signal participantsRequested()

    // „Később”: a banner ebben a munkamenetben, ennél a megbeszélésnél nem jön elő.
    property var laterFor: ({})
    readonly property bool bannerShown: vm.approvalPending && !laterFor[meetingId]
    // Kész állapotban az összecsukott sorok nyitása.
    property bool stepsOpen: false
    property bool tracksOpen: false
    onMeetingIdChanged: { stepsOpen = false; tracksOpen = false }

    OverviewViewModel {
        id: vm
        meetingId: root.meetingId
        demoState: root.demoState
    }
    TrackListModel {
        id: tracks
        meetingId: root.meetingId
        demoState: root.demoState === "overviewEmpty" || root.demoState === "overviewNobody" ? "overviewTwo" : "overview"
    }
    // Saját címke-modell, ha a gazda nem ad (önálló képernyőkép): kitalált készlet.
    MeetingTagsModel {
        id: ownTags
        demoState: root.demoState === "overviewEmpty" || root.demoState === "overviewNobody" ? "none"
                 : root.demoState === "overviewDone" ? "few" : "similar"
    }
    readonly property var effectiveTags: tagsModel ? tagsModel : ownTags

    function openPicker(anchor) {
        const p = anchor.mapToItem(root, 0, anchor.height)
        picker.x = Math.max(8, Math.min(root.width - picker.width - 8, p.x + anchor.width - picker.width))
        picker.y = p.y + 6
        picker.open()
    }
    // A „Sávok részletei” panel (régi Sávok fül; a Main demó-állapota is ezt nyitja).
    function openTracks() { tracksDialog.open() }
    function solo() {
        if (!vm.setSolo() && root.shell)
            root.shell.toast(qsTr("Ehhez add meg a saját neved a Beállításokban (Általános › Te)."))
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

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ================= bal oszlop =================
        Flickable {
            id: leftFlick
            objectName: "overviewLeft"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: left.implicitHeight + 36
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
                    onAddRequested: (name) => vm.addParticipant(name)
                    onRemoveRequested: (id) => vm.removeParticipant(id)
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

    // ---- „+ Résztvevő” ----
    PersonPicker {
        id: picker
        objectName: "overviewPersonPicker"
        editor: root.editor
        anonymousText: ""
        onPersonChosen: name => vm.addParticipant(name)
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
