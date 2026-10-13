import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// The meeting note editor (MeetingNoteModel) -- step 1 of the pre-transcript view, and a shared
// part of the Summary tab. Top to bottom: template suggestions from earlier meetings with similar
// titles (a click puts them into the field; if the field already has text it asks: replace or
// append; the card shows the source meeting's tags as mini chips), the field, below it the
// explanation and the detected call. Saving: delayed (900 ms) and on focus loss; the model keeps
// the draft together with the meeting id, so it can never end up in another meeting.
ColumnLayout {
    id: root

    required property var model            // MeetingNoteModel
    property string helperText: ""
    property string placeholderText: qsTr("Például: Résztvevők: Kovács Anna, Szabó Bence. A „Nordwig” helyesen: Nordvik. A „kvarc modul” helyesen: Qvarko-modul.")
    property string accessibleName: qsTr("Megjegyzés a megbeszéléshez")
    // In a narrow column (right of the Summary) the suggestion cards are more compact.
    property bool compact: false
    property int fieldHeight: 96
    // The detected call row (in the pre-transcript view it sits in the tag suggestion row, if visible).
    property bool showDetectedCall: true

    spacing: 8

    function commit() { saveTimer.stop(); root.model.commitDraft() }
    // We work with the suggestion's TEXT (not an index): saving the draft can recompute the list.
    function pick(text) {
        saveTimer.stop()
        if (area.text.trim() === "") root.apply(text, "replace")
        else { choice.suggestion = text; choice.open() }
    }
    function apply(text, mode) {
        root.model.applySuggestion(text, mode)   // also saves the pending draft
        area.text = root.model.note
    }

    // ---- template suggestions ----
    ColumnLayout {
        visible: root.model.suggestions.length > 0
        Layout.fillWidth: true
        Layout.bottomMargin: 4
        spacing: 6

        TLabel {
            Layout.fillWidth: true
            text: qsTr("Korábbi, hasonló megbeszélések megjegyzései — kattintásra a mezőbe kerül:")
            muted: true
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }
        Repeater {
            model: root.model.suggestions
            T.AbstractButton {
                id: sug
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: sugCol.implicitHeight + 16
                hoverEnabled: true
                Accessible.name: qsTr("Javaslat: %1, %2").arg(modelData.title).arg(modelData.dateText)
                focusPolicy: Qt.TabFocus      // on click the field keeps focus
                onClicked: root.pick(modelData.note)

                background: Rectangle {
                    radius: Theme.radiusControl
                    color: Theme.raised
                    border.width: 1
                    border.color: sug.hovered ? Theme.borderStrong : Theme.border
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Theme.stateLayer
                        opacity: sug.down ? Theme.pressedOpacity : sug.hovered ? Theme.hoverOpacity : 0
                    }
                    TFocusRing { visible: sug.visualFocus }
                }
                contentItem: Item {
                    ColumnLayout {
                        id: sugCol
                        x: 10; y: 8
                        width: sug.width - 20
                        spacing: 2
                        RowLayout {
                            id: sugRow
                            Layout.fillWidth: true
                            spacing: 8
                            TIcon { name: "copy"; size: 13; color: Theme.textMuted }
                            TLabel {
                                id: sugTitle
                                // The title gets its full width if it fits next to the chips and the date.
                                Layout.preferredWidth: Math.max(40, Math.min(implicitWidth,
                                    sugCol.width - 13 - 3 * sugRow.spacing
                                    - (sugTagRow.visible ? sugTagRow.implicitWidth + sugRow.spacing : 0)
                                    - (sugDate.visible ? sugDate.implicitWidth : 0)))
                                text: sug.modelData.title
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightMedium
                                elide: Text.ElideRight
                            }
                            // The source meeting's tags (mini chip, 18 px).
                            Row {
                                id: sugTagRow
                                visible: !root.compact && sugTags.count > 0
                                spacing: 4
                                Repeater {
                                    id: sugTags
                                    model: sug.modelData.tags !== undefined ? sug.modelData.tags.slice(0, 2) : []
                                    TagChip {
                                        required property var modelData
                                        mini: true
                                        focusPolicy: Qt.NoFocus
                                        text: modelData
                                        maxWidth: 140
                                        onClicked: root.pick(sug.modelData.note)   // part of the card
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                            TLabel {
                                id: sugDate
                                visible: !root.compact
                                text: sug.modelData.dateText
                                muted: true
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                        TLabel {
                            visible: root.compact
                            Layout.fillWidth: true
                            text: sug.modelData.dateText
                            muted: true
                            font.pixelSize: Theme.fontCaption
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: sug.modelData.preview
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // ---- the field ----
    TTextArea {
        id: area
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(root.fieldHeight, implicitHeight)
        placeholderText: root.placeholderText
        Component.onCompleted: text = root.model.note
        onEditingFinished: root.commit()
        onActiveFocusChanged: if (!activeFocus) root.commit()
        // The model keeps the draft together with the meeting id: the delayed save can never end up in
        // another meeting.
        onTextChanged: if (activeFocus) { root.model.draft(text); saveTimer.restart() }
        Accessible.name: root.accessibleName
        Timer { id: saveTimer; interval: 900; onTriggered: root.commit() }
        Connections {
            target: root.model
            function onNoteChanged() {
                if (!area.activeFocus && area.text !== root.model.note)
                    area.text = root.model.note
            }
            function onMeetingIdChanged() { saveTimer.stop(); area.text = root.model.note }
        }
    }

    TLabel {
        visible: root.helperText !== ""
        Layout.fillWidth: true
        text: root.helperText
        muted: true
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
    }

    // ---- detected call (from the watcher; informational only) ----
    RowLayout {
        visible: root.showDetectedCall && root.model.detectedCallApp !== ""
        Layout.fillWidth: true
        spacing: 6
        TIcon { name: "radar"; size: 13; color: Theme.textMuted }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Észlelt hívás: %1").arg(root.model.detectedCallApp)
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }

    TDialog {
        id: choice
        property string suggestion: ""
        title: qsTr("Már van megjegyzés")
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Lecseréled a mostanit a korábbi megbeszélés megjegyzésére, vagy hozzáfűzöd a végéhez?")
        }
        actions: [
            TButton { text: qsTr("Mégse"); variant: "ghost"; onClicked: choice.reject() },
            TButton { text: qsTr("Hozzáfűzés"); onClicked: { choice.accept(); root.apply(choice.suggestion, "append") } },
            TButton { text: qsTr("Csere"); variant: "primary"; onClicked: { choice.accept(); root.apply(choice.suggestion, "replace") } }
        ]
    }
}
