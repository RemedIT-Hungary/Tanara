import QtQuick

// Többchipes címkemező (C05 / C07): 36 px magas (több címkénél sort tör), raised háttér,
// 1 px borderStrong keret (fókuszban accent + gyűrű). A felrakott címkék × jellel, utánuk a
// beépített beviteli mező („Címke hozzáadása…”) a C02 listával.
// Hol: az átirat előtti 1. lépés, az import párbeszéd, a tömeges címkézés panelje.
//   tags: [{ id, name, countText?, partial? }] — partial: tömeges kijelölésnél csak néhányon
//         van rajta (szaggatott keret, „1/3”); countText: a chip száma („3/3”)
//   TagField { tags: vm.tags; onAddRequested: (name, isNew) => vm.add(name); onRemoveRequested: (id) => vm.remove(id) }
Rectangle {
    id: root

    property var tags: []
    property string placeholderText: qsTr("Címke hozzáadása…")
    // Minden chipre (ha a sor maga nem ad countText-et).
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

    TagInputModel {
        id: ownInputModel
        excludeIds: root.tags.map(t => t.id)
    }

    // Kattintás az üres részre: a mezőbe.
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

            // A mező kitölti az utolsó sor maradékát.
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
