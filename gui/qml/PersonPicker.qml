import QtQuick
import QtQuick.Templates as T

// Person picker panel (300 px): search field (filters as you type, accent-insensitive), known
// people (monogram, name, "12 meetings" / "no voiceprint", fingerprint icon), a "New person: <name>"
// row, help at the bottom. Enter picks the first / highlighted hit, up / down moves.
// Order: already a participant -> shared tag ("#Nordvik - 11 / 14 - 12 meetings"; with >= 6 rows
// under a "By #Nordvik" section title) -> the rest (PersonListModel). tagId: the Tags window's
// "+ Person" mode (the tag is the reference; people who already have it are left out).
//   PersonPicker { id: picker; editor: vm; onPersonChosen: name => … ; onAnonymousChosen: … }
TPopover {
    id: control

    property var editor: null               // TranscriptEditorViewModel
    property string tagId: ""               // Tags window: we pick for this tag
    property alias peopleModel: people
    property string initialQuery: ""
    // Label of the anonymous option (empty -> no such row).
    property string anonymousText: qsTr("Névtelen résztvevő")
    // The bottom help row.
    // Optional voice sample to listen to before naming: the Overview row (sampleTrackId /
    // sampleStartMs / sampleEndMs) + the PlayerController; no sample -> no row.
    property var sampleRow: null
    property var player: null
    property string hintText: qsTr("A beszélő névtelenül is maradhat; később is elnevezheted.")

    signal personChosen(string name)
    signal anonymousChosen()

    width: 300
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    readonly property int rowHeight: 44
    // The highlighted row: 0..count-1 = person; count = the bottom ("New person" / anonymous) row.
    // -1 = no highlight (opens like this with an empty search: a stray Enter picks nobody).
    property int currentIndex: -1
    readonly property bool bottomRowShown: people.canCreate || (search.text.trim() === "" && anonymousText !== "")

    function choose(index) {
        if (index < 0) return
        if (index >= 0 && index < people.count) {
            const name = people.nameAt(index)
            close()
            personChosen(name)
        } else if (people.canCreate) {
            const name = search.text.trim()
            close()
            personChosen(name)
        } else if (search.text.trim() === "" && anonymousText !== "") {
            close()
            anonymousChosen()
        }
    }

    onAboutToShow: {
        people.refresh()
        search.text = initialQuery
        currentIndex = search.text.trim() !== "" ? (people.count > 0 ? 0 : people.count) : -1
        search.forceActiveFocus()
    }

    PersonListModel {
        id: people
        editor: control.editor
        tagId: control.tagId
        query: search.text
        onCountChanged: control.currentIndex = search.text.trim() === "" ? -1 : count > 0 ? 0 : count
    }

    contentItem: Column {
        Item {
            width: parent.width
            height: 52
            TSearchField {
                id: search
                x: 10; y: 10
                width: parent.width - 20
                font.pixelSize: Theme.fontBody
                placeholderText: control.tagId !== "" ? qsTr("Név keresése") : qsTr("Név keresése vagy új személy")
                Keys.onDownPressed: control.currentIndex = Math.min(control.currentIndex + 1,
                                        people.count - (control.bottomRowShown ? 0 : 1))
                Keys.onUpPressed: control.currentIndex = Math.max(0, control.currentIndex - 1)
                Keys.onReturnPressed: control.choose(control.currentIndex)
                Keys.onEnterPressed: control.choose(control.currentIndex)
                Keys.onEscapePressed: control.close()
            }
            TDivider { anchors.bottom: parent.bottom; width: parent.width }
        }

        // The voice sample (naming an unknown voice): listen first.
        Item {
            visible: sampleButton.visible
            width: parent.width
            height: visible ? 40 : 0
            Row {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                SampleButton {
                    id: sampleButton
                    anchors.verticalCenter: parent.verticalCenter
                    player: control.player
                    row: control.sampleRow
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Hallgasd meg a hangot")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
            }
            TDivider { anchors.bottom: parent.bottom; width: parent.width }
        }

        Item {
            width: parent.width
            height: list.height + (list.count > 0 ? 8 : 0)
            ListView {
                id: list
                x: 4; y: 4
                width: parent.width - 8
                readonly property int sectionHeight: people.tagSectionTitle !== "" ? 26 : 0
                height: Math.min(contentHeight, 5 * control.rowHeight + sectionHeight)
                clip: true
                model: people
                section.property: "group"
                section.delegate: Item {
                    required property string section
                    width: list.width
                    height: section === "tag" ? list.sectionHeight : 0
                    TLabel {
                        visible: parent.height > 0
                        x: 10
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        text: people.tagSectionTitle
                        muted: true
                        font.pixelSize: Theme.fontMicro
                        font.weight: Theme.weightSemiBold
                        font.letterSpacing: 0.66
                    }
                }
                currentIndex: control.currentIndex
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
                    height: control.rowHeight
                    personName: name
                    // Found by nickname: the nickname is shown too, so it is clear why the person
                    // is listed.
                    subText: (matchedAlias !== "" ? qsTr("„%1”").arg(matchedAlias) + " · " : "")
                           + (inMeeting ? qsTr("már résztvevő")
                           : tagHint !== "" ? tagHint
                           : hasVoiceprint ? qsTr("%n megbeszélés", "", meetingCount)
                           : qsTr("nincs hanglenyomat"))
                    voiceprint: hasVoiceprint
                    highlighted: control.currentIndex === index
                    onHoveredChanged: if (hovered) control.currentIndex = index
                    onClicked: control.choose(index)
                }
            }
        }

        Item {
            visible: control.bottomRowShown
            width: parent.width
            height: visible ? 44 : 0
            TDivider { width: parent.width; visible: list.count > 0 }
            Rectangle {
                x: 4; y: 4
                width: parent.width - 8
                height: 36
                radius: 5
                color: control.currentIndex === people.count ? Theme.sunken : "transparent"
                Row {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    TIcon { anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: 15; color: Theme.accent }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        width: control.width - 60
                        elide: Text.ElideRight
                        text: people.canCreate ? qsTr("Új személy: „%1”").arg(search.text.trim()) : control.anonymousText
                        color: Theme.accent
                        font.weight: Theme.weightMedium
                    }
                }
                HoverHandler { onHoveredChanged: if (hovered) control.currentIndex = people.count }
                TapHandler { onTapped: control.choose(people.count) }
            }
        }

        // Footer (surface): the bottom corners follow the panel's rounding.
        Item {
            width: parent.width
            height: hint.height + 18
            Rectangle { x: 1; width: parent.width - 2; height: parent.height - 1; radius: Theme.radiusPopup - 1; color: Theme.surface }
            Rectangle { x: 1; width: parent.width - 2; height: parent.height / 2; color: Theme.surface }
            TDivider { width: parent.width }
            TLabel {
                id: hint
                x: 14; y: 9
                width: parent.width - 28
                wrapMode: Text.Wrap
                muted: true
                font.pixelSize: Theme.fontCaption
                cssLineHeight: 1.4
                text: control.hintText
            }
        }
    }
}
