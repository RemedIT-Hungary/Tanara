import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Tag field on the Overview: raised, borderStrong border, radius 7. Inside: the applied tags
// (x), the built-in input ("Tag..."), and - if there are any - after a divider the suggestions:
// the source name (e.g. "Based on the participants:", the People source), dashed accent chips
// (click: apply · x: does not fit here) and "Why?" (TagWhyPopover). The same MeetingTagsModel
// that used to back the header tag row (ShellMeetingModel.tags): shared undo stack, toast with "Undo".
Rectangle {
    id: root

    property var model: null                 // MeetingTagsModel
    readonly property var tags: model ? model.tags : []
    readonly property var suggestions: model && !model.computing ? model.suggestions : []
    readonly property bool hasSuggestions: suggestions.length > 0
    property int maxVisibleSuggestions: 3

    signal tagClicked(string id)

    implicitHeight: Math.max(38, flow.implicitHeight + 12)
    radius: 7
    color: Theme.raised
    border.width: 1
    border.color: input.stateFocused ? Theme.accent : Theme.borderStrong
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Címkék")

    TagInputModel {
        id: inputModel
        excludeIds: root.tags.map(t => t.id)
    }
    TapHandler { onTapped: input.open() }

    Flow {
        id: flow
        x: 6; y: 6
        width: parent.width - 12
        spacing: 6

        Repeater {
            model: root.tags
            TagChip {
                required property var modelData
                text: modelData.name
                removable: true
                removeAlwaysVisible: true
                onClicked: root.tagClicked(modelData.id)
                onRemoveRequested: if (root.model) root.model.remove(modelData.id)
            }
        }
        TagInput {
            id: input
            objectName: "overviewTagInput"
            bare: true
            width: root.hasSuggestions ? 96 : 180
            height: 24
            placeholderText: qsTr("Címke…")
            model: inputModel
            stateFocused: false
            onTagChosen: (name, isNew) => { if (root.model) root.model.add(name) }
            onBackspaceOnEmpty: if (root.model && root.tags.length > 0) root.model.remove(root.tags[root.tags.length - 1].id)
        }

        // ---- suggestions ----
        Row {
            visible: root.hasSuggestions
            height: 24
            spacing: 6
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 1; height: 16; color: Theme.border }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                // The People source: from the participants' manual tags.
                text: !root.model ? ""
                      : root.model.suggestionSource === "people" ? qsTr("A résztvevők alapján:")
                      : root.model.suggestionLabel + ":"
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }
        Repeater {
            model: root.suggestions.slice(0, root.maxVisibleSuggestions)
            TagChip {
                required property var modelData
                required property int index
                objectName: "overviewTagSuggestion"
                kind: modelData.isNew ? "llmNew" : "suggested"
                text: modelData.name
                toolTipText: qsTr("Kattintás: hozzáadás · ×: nem illik ide")
                onClicked: if (root.model) root.model.accept(index)
                onRemoveRequested: if (root.model) root.model.reject(index)
            }
        }
        T.AbstractButton {
            id: whyButton
            visible: root.hasSuggestions
            implicitWidth: whyRow.implicitWidth + 10
            implicitHeight: 24
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.name: qsTr("Miért ezek a javaslatok?")
            onClicked: why.opened ? why.close() : why.open()
            Keys.onReturnPressed: click()
            contentItem: Item {
                Row {
                    id: whyRow
                    anchors.centerIn: parent
                    spacing: 4
                    TIcon { anchors.verticalCenter: parent.verticalCenter; name: "info"; size: 13; color: Theme.textMuted }
                    TLabel { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Miért?"); muted: true; font.pixelSize: Theme.fontSmall }
                }
            }
            background: Rectangle {
                radius: Theme.radiusTag
                color: why.visible ? Theme.sunken : whyButton.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                TFocusRing { visible: whyButton.visualFocus; targetRadius: Theme.radiusTag }
            }
            TagWhyPopover {
                id: why
                x: -200
                y: whyButton.height + 6
                suggestions: root.model && root.model.suggestions.length >= 0 ? root.model.whyData() : []
                onAcceptRequested: (index) => { if (root.model) root.model.accept(index); if (root.suggestions.length === 0) why.close() }
                onRejectRequested: (index) => { if (root.model) root.model.reject(index); if (root.suggestions.length === 0) why.close() }
                onMeetingRequested: (meetingId) => { why.close(); root.meetingRequested(meetingId) }
            }
        }
    }

    signal meetingRequested(string meetingId)
}
