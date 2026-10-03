import QtQuick
import QtQuick.Templates as T

// Személyválasztó panel (K5, 300 px): keresőmező (gépelésre szűr, ékezet-függetlenül), az
// ismert személyek (monogram, név, „12 megbeszélés" / „nincs hanglenyomat", ujjlenyomat-ikon),
// „Új személy: „…"" sor, alul súgó. Enter az első / kiemelt találatot választja, fel / le léptet.
//   PersonPicker { id: picker; editor: vm; onPersonChosen: name => … ; onAnonymousChosen: … }
TPopover {
    id: control

    property var editor: null               // TranscriptEditorViewModel
    property string initialQuery: ""
    // A névtelen lehetőség felirata (üres → nincs ilyen sor).
    property string anonymousText: qsTr("Névtelen résztvevő")

    signal personChosen(string name)
    signal anonymousChosen()

    width: 300
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    readonly property int rowHeight: 44
    // A kiemelt sor: 0…count-1 = személy; count = az alsó („Új személy" / névtelen) sor.
    property int currentIndex: 0
    readonly property bool bottomRowShown: people.canCreate || (search.text.trim() === "" && anonymousText !== "")

    function choose(index) {
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
        currentIndex = 0
        search.forceActiveFocus()
    }

    PersonListModel {
        id: people
        editor: control.editor
        query: search.text
        onCountChanged: control.currentIndex = count > 0 ? 0 : count
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
                placeholderText: qsTr("Név keresése vagy új személy")
                Keys.onDownPressed: control.currentIndex = Math.min(control.currentIndex + 1,
                                        people.count - (control.bottomRowShown ? 0 : 1))
                Keys.onUpPressed: control.currentIndex = Math.max(0, control.currentIndex - 1)
                Keys.onReturnPressed: control.choose(control.currentIndex)
                Keys.onEnterPressed: control.choose(control.currentIndex)
                Keys.onEscapePressed: control.close()
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
                height: Math.min(count, 5) * control.rowHeight
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
                    width: list.width
                    height: control.rowHeight
                    personName: name
                    subText: inMeeting ? qsTr("már résztvevő")
                           : hasVoiceprint ? qsTr("%n megbeszélés", "", meetingCount)
                           : qsTr("nincs hanglenyomat")
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

        // Lábléc (surface): az alsó sarkok a panel lekerekítését követik.
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
                text: qsTr("A beszélő névtelenül is maradhat; később is elnevezheted.")
            }
        }
    }
}
