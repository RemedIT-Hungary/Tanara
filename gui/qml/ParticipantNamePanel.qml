import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// The naming panel of a "Who was there?" row (the card's "Name..." / ⋯; before and after the
// transcript alike). Top: the voice sample (listen BEFORE naming). Then:
//   "This is me — <own name>"  first when the own voice is not on the list yet, otherwise at the
//                              bottom of the people (ParticipantService::setSelfParticipant; when it
//                              fails, its reason shows here)
//   search + known people      PersonListModel's order (already a participant -> shared tag ->
//                              the rest), as in the person picker
//   "New person: <typed>"
//   "Same voice as..."         MEETING-LOCAL merge (mergeParticipantInto): opens the other rows;
//                              differs from the People window's global "Merge people"
//   "Not a participant" / "Was not there"   removal (one undo step)
// Each choice is one undo step of the meeting's shared stack (the change bar).
//   ParticipantNamePanel { vm: overviewVm; player: player; editor: editor }  ->  openFor(row, rows, anchor)
TPopover {
    id: control
    objectName: "participantNamePanel"

    property var vm: null                       // OverviewViewModel
    property var player: null                   // PlayerController
    property var editor: null                   // TranscriptEditorViewModel (the people list)
    property var row: ({})
    property var rows: []                       // the card's rows (merge targets)
    property bool mergeOpen: false
    property string errorText: ""

    readonly property bool anonymous: row.anonymous === true
    readonly property bool isSelf: row.self === true
    // The own voice is already a (voiced) row: "This is me" goes to the bottom.
    readonly property bool ownVoiceListed: rows.some(r => r.self === true && r.voiceless !== true && r.suggested !== true)
    readonly property var mergeTargets: rows.filter(r => r.id !== row.id && r.suggested !== true && (r.id || "") !== "")
    readonly property bool hasSample: (row.sampleTrackId || "") !== "" && row.sampleStartMs >= 0
                                      && row.sampleEndMs > row.sampleStartMs

    width: 340
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    function openFor(r, all, anchor, merge) {
        row = r
        rows = all
        mergeOpen = merge === true
        errorText = ""
        const host = parent
        const p = anchor.mapToItem(host, 0, anchor.height)
        x = Math.max(8, Math.min(host.width - width - 8, p.x + anchor.width - width))
        y = p.y + 4
        open()
    }
    function chooseName(name) {
        if (!vm || name.trim() === "") return
        if (vm.nameParticipant(row.id, name)) close()
        else errorText = qsTr("Nem sikerült elnevezni ezt a sort.")
    }
    function chooseSelf() {
        if (!vm) return
        const res = vm.setSelfParticipant(row.id)
        if (res.ok) close()
        else errorText = res.reason !== "" ? res.reason : qsTr("Nem sikerült: add meg a saját neved a Beállításokban.")
    }

    onAboutToShow: {
        people.refresh()
        search.text = ""
        if (!mergeOpen) search.forceActiveFocus()
    }

    PersonListModel {
        id: people
        editor: control.editor
        query: search.text
        hideMeetingPeople: false
        excludeName: control.anonymous ? "" : (control.row.name || "").replace(/ \(.*\)$/, "")
    }

    // A pickable line (icon / avatar, title, detail on the right).
    component PickRow: Item {
        id: pr
        property string title: ""
        property string detail: ""
        property string icon: ""
        property int avatar: -1
        property string monogram: ""
        property bool accentRow: false
        signal picked()
        width: parent ? parent.width : 0
        implicitHeight: 34
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Theme.stateLayer
            opacity: prHover.hovered ? Theme.hoverOpacity : 0
        }
        RowLayout {
            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
            spacing: 8
            Item {
                implicitWidth: 22; implicitHeight: 22
                TAvatar {
                    anchors.fill: parent
                    visible: pr.avatar >= 0
                    size: 22
                    speakerIndex: Math.max(0, pr.avatar)
                    monogram: pr.monogram
                }
                TIcon {
                    anchors.centerIn: parent
                    visible: pr.avatar < 0
                    name: pr.icon
                    size: 15
                    color: pr.accentRow ? Theme.accent : Theme.textMuted
                }
            }
            TLabel {
                Layout.fillWidth: true
                text: pr.title
                color: pr.accentRow ? Theme.accent : Theme.text
                font.weight: pr.accentRow ? Theme.weightSemiBold : Theme.weightMedium
                elide: Text.ElideRight
            }
            TLabel {
                visible: pr.detail !== ""
                text: pr.detail
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }
        HoverHandler { id: prHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: pr.picked() }
        Accessible.role: Accessible.Button
        Accessible.name: pr.title
    }

    contentItem: Column {
        // ---- the sample: listen first ----
        Item {
            visible: control.hasSample
            width: parent.width
            height: visible ? sampleBox.height + 20 : 0
            Rectangle {
                id: sampleBox
                x: 10; y: 10
                width: parent.width - 20
                height: 48
                radius: Theme.radiusControl
                color: Theme.sunken
                Row {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    SampleButton {
                        id: panelSample
                        objectName: "namePanelSample"
                        anchors.verticalCenter: parent.verticalCenter
                        player: control.player
                        row: control.row
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        TLabel {
                            text: panelSample.playingNow ? qsTr("Szól a minta…") : qsTr("Hallgasd meg, mielőtt nevet adsz")
                            color: panelSample.playingNow ? Theme.accent : Theme.text
                            font.pixelSize: 12
                            font.weight: Theme.weightMedium
                        }
                        TLabel {
                            readonly property real len: Math.round((control.row.sampleEndMs - control.row.sampleStartMs) / 1000)
                            text: (control.row.name || "") + " · " + qsTr("%1 mp").arg(len)
                            muted: true
                            mono: true
                            font.pixelSize: Theme.fontMicro
                        }
                    }
                }
            }
        }

        // ---- who is it ----
        Column {
            visible: !control.mergeOpen
            width: parent.width
            topPadding: control.hasSample ? 0 : 8
            PickRow {
                objectName: "namePanelSelfTop"
                visible: !control.isSelf && !control.ownVoiceListed
                x: 4; width: parent.width - 8
                avatar: 0
                monogram: Theme.monogram(control.vm ? control.vm.selfName : "")
                title: control.vm && control.vm.selfName !== "" ? qsTr("Ez én vagyok — %1").arg(control.vm.selfName)
                                                               : qsTr("Ez én vagyok")
                onPicked: control.chooseSelf()
            }
            Item {
                width: parent.width
                height: 44
                TSearchField {
                    id: search
                    objectName: "namePanelSearch"
                    x: 10; y: 6
                    width: parent.width - 20
                    placeholderText: control.anonymous ? qsTr("Kié ez a hang? Név keresése vagy új név…")
                                                       : qsTr("Másik név keresése vagy új név…")
                    Keys.onReturnPressed: control.chooseName(people.count > 0 ? people.nameAt(0) : search.text.trim())
                    Keys.onEnterPressed: control.chooseName(people.count > 0 ? people.nameAt(0) : search.text.trim())
                }
            }
            ListView {
                id: list
                objectName: "namePanelPeople"
                x: 4
                width: parent.width - 8
                height: Math.min(contentHeight, 4 * 44)
                clip: true
                model: people
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: TScrollBar {}
                delegate: PersonRow {
                    required property int index
                    required property string name
                    required property bool hasVoiceprint
                    required property int meetingCount
                    required property bool inMeeting
                    required property string matchedAlias
                    required property string tagHint
                    width: list.width
                    height: 44
                    personName: name
                    subText: (matchedAlias !== "" ? qsTr("„%1”").arg(matchedAlias) + " · " : "")
                           + (inMeeting ? qsTr("már a listán")
                           : tagHint !== "" ? tagHint
                           : hasVoiceprint ? qsTr("%n megbeszélés", "", meetingCount)
                           : qsTr("nincs hanglenyomat"))
                    voiceprint: hasVoiceprint
                    onClicked: control.chooseName(name)
                }
            }
            PickRow {
                objectName: "namePanelNewPerson"
                visible: people.canCreate
                x: 4; width: parent.width - 8
                icon: "user-plus"
                accentRow: true
                title: qsTr("Új személy: „%1”").arg(search.text.trim())
                onPicked: control.chooseName(search.text.trim())
            }
            PickRow {
                objectName: "namePanelSelfBottom"
                visible: !control.isSelf && control.ownVoiceListed && search.text.trim() === ""
                x: 4; width: parent.width - 8
                avatar: 0
                monogram: Theme.monogram(control.vm ? control.vm.selfName : "")
                title: control.vm && control.vm.selfName !== "" ? qsTr("Ez én vagyok — %1").arg(control.vm.selfName)
                                                               : qsTr("Ez én vagyok")
                onPicked: control.chooseSelf()
            }
        }

        TLabel {
            objectName: "namePanelError"
            visible: control.errorText !== ""
            x: 14
            width: parent.width - 28
            topPadding: 4; bottomPadding: 4
            text: control.errorText
            color: Theme.dangerInk
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }

        Item { width: parent.width; height: 9; TDivider { anchors.verticalCenter: parent.verticalCenter; width: parent.width } }

        // ---- the row's own operations ----
        Column {
            x: 4
            width: parent.width - 8
            PickRow {
                objectName: "namePanelSameVoice"
                visible: control.mergeTargets.length > 0
                icon: control.mergeOpen ? "chevron-up" : "merge"
                title: qsTr("Ugyanaz a hang, mint…")
                detail: qsTr("csak itt")
                onPicked: control.mergeOpen = !control.mergeOpen
            }
            Repeater {
                model: control.mergeOpen ? control.mergeTargets : []
                PickRow {
                    required property var modelData
                    objectName: "namePanelMergeTarget"
                    avatar: modelData.anonymous === true ? -1 : Math.max(0, modelData.colorIndex || 0)
                    monogram: modelData.monogram || "?"
                    icon: "audio-lines"
                    title: modelData.name || ""
                    detail: modelData.talkShare >= 0 ? Math.round(modelData.talkShare * 100) + "%" : ""
                    onPicked: {
                        if (control.vm && control.vm.mergeParticipantInto(control.row.id, modelData.id)) control.close()
                        else control.errorText = qsTr("Ezt a két sort nem lehet összevonni.")
                    }
                }
            }
            PickRow {
                objectName: "namePanelRemove"
                visible: control.row.removable === true
                icon: "user-x"
                title: control.anonymous ? qsTr("Nem résztvevő") : qsTr("Nem volt ott")
                detail: control.anonymous ? qsTr("a beszéde névtelen marad") : qsTr("a sorai névtelenek lesznek")
                onPicked: {
                    if (control.vm) control.vm.removeParticipant(control.row.id)
                    control.close()
                }
            }
        }

        // Footer: what a choice does; the merge view names the global one.
        Item {
            width: parent.width
            height: hint.height + 18
            TLabel {
                id: hint
                x: 14; y: 9
                width: parent.width - 28
                wrapMode: Text.Wrap
                muted: true
                font.pixelSize: Theme.fontCaption
                cssLineHeight: 1.35
                text: control.mergeOpen
                      ? qsTr("Csak ebben a megbeszélésben: a két hang egy résztvevő lesz. Két személy végleges egyesítése a Személyek ablakban van („Személyek egyesítése”).")
                      : qsTr("A választott névre kerül a hang, és legközelebb felismerjük. Ctrl+Z visszavonja.")
            }
        }
    }
}
