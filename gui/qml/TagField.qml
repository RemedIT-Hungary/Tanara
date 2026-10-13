import QtQuick

// Multi-chip tag field: 36 px high (wraps to more lines with many tags), raised background,
// 1 px borderStrong border (focused: accent + ring). Applied tags with an x, followed by the
// built-in input ("Add tag...") with the suggestion list.
// Used in: step 1 before the transcript, the import dialog, the bulk-tagging panel.
//   tags: [{ id, name, countText?, partial? }] - partial: in a bulk selection only some
//         have it (dashed border, "1/3"); countText: the chip's count ("3/3")
//   TagField { tags: vm.tags; onAddRequested: (name, isNew) => vm.add(name); onRemoveRequested: (id) => vm.remove(id) }
Rectangle {
    id: root

    property var tags: []
    property string placeholderText: qsTr("Címke hozzáadása…")
    // For every chip (if the row itself gives no countText).
    property string countText: ""
    property var inputModel: null
    property bool stateFocused: input.field.activeFocus
    property alias input: input
    signal addRequested(string name, bool isNew)
    signal removeRequested(string id)
    signal chipClicked(string id)

    function open() { input.open() }

    implicitWidth: 360
    implicitHeight: Math.max(36, flow.implicitHeight + 12)
    radius: Theme.radiusControl
    color: Theme.raised
    border.width: 1
    border.color: stateFocused ? Theme.accent : Theme.borderStrong
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Címkék")

    TFocusRing { visible: root.stateFocused; border.color: Theme.accentSoft }

    // Own input model if the host gives none: with the real controller the full tag set
    // (recent, matches), without it the demo set - so autocomplete is the same everywhere.
    TagInputModel {
        id: ownInputModel
        controller: App.controller
        excludeIds: root.tags.map(t => t.id)
    }

    // Click on the empty area: into the field.
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
                kind: modelData.partial ? "partial" : "applied"
                text: modelData.name
                countText: modelData.countText !== undefined ? modelData.countText : root.countText
                removable: true
                removeAlwaysVisible: true
                onClicked: root.chipClicked(modelData.id)
                onRemoveRequested: root.removeRequested(modelData.id)
            }
        }
        TagInput {
            id: input
            bare: true
            width: Math.max(140, flow.width - lastRowWidth())
            height: 24
            placeholderText: root.placeholderText
            model: root.inputModel ? root.inputModel : ownInputModel
            stateFocused: false
            onTagChosen: (name, isNew) => root.addRequested(name, isNew)
            onBackspaceOnEmpty: if (root.tags.length > 0) root.removeRequested(root.tags[root.tags.length - 1].id)

            // The field fills the rest of the last line.
            function lastRowWidth() {
                let rowY = -1, used = 0
                for (let i = 0; i < flow.children.length; ++i) {
                    const c = flow.children[i]
                    if (c === input || !c.visible || c.width === 0) continue
                    if (c.y !== rowY) { rowY = c.y; used = 0 }
                    used += c.width + flow.spacing
                }
                return used
            }
        }
    }
}
