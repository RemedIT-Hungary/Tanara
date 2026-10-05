import QtQuick

// A címke-beviteli lista tartalma (C02) — a TagInputPopover ezt mutatja; a galéria helyben,
// felugró nélkül is kirajzolja. Fejléc: „LEGUTÓBB HASZNÁLT” (üres mező) vagy „HASONLÓ MÁR VAN”;
// sor: # ikon, név a találat kiemelésével (warnSoft), darabszám mono; „Új címke: „…”” /
// „Mégis új: „…”” + ikonnal; alul billentyű-tipp.
Column {
    id: list

    property var model: null
    property bool compact: false
    signal rowClicked(int index)

    readonly property var rows: model ? model.rows : []
    readonly property string mode: model ? model.mode : "recent"

    spacing: 0

    TLabel {
        visible: list.mode === "recent" || list.mode === "similar"
        width: list.width
        leftPadding: 10; topPadding: 8; bottomPadding: 4
        text: list.mode === "similar" ? qsTr("Hasonló már van") : qsTr("Legutóbb használt")
        muted: true
        font.pixelSize: Theme.fontMicro
        font.weight: Theme.weightSemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.66
    }

    Repeater {
        model: list.rows
        delegate: Rectangle {
            id: rowItem
            required property int index
            required property var modelData
            readonly property bool selected: list.model && list.model.selectedRow === index
            readonly property bool creates: modelData.kind === "new" || modelData.kind === "forceNew"
            width: list.width
            height: list.compact ? 28 : 32
            radius: 5
            color: selected ? Theme.sunken : rowHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: nameText.text
            Accessible.selected: selected

            HoverHandler { id: rowHover }
            TapHandler { onTapped: list.rowClicked(rowItem.index) }

            TIcon {
                id: rowIcon
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                name: rowItem.creates ? "plus" : "hash"
                size: 14
                color: rowItem.modelData.kind === "new" ? Theme.accent : Theme.textMuted
            }
            Row {
                anchors.left: rowIcon.right
                anchors.leftMargin: 8
                anchors.right: countText.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                TLabel {
                    id: nameText
                    visible: !rowItem.creates
                    text: rowItem.modelData.before
                    font.pixelSize: list.compact ? Theme.fontSmall : Theme.fontBody
                }
                Rectangle {
                    visible: rowItem.modelData.match !== ""
                    width: matchText.implicitWidth
                    height: matchText.implicitHeight
                    radius: 2
                    color: Theme.warnSoft
                    TLabel {
                        id: matchText
                        text: rowItem.modelData.match
                        font.pixelSize: list.compact ? Theme.fontSmall : Theme.fontBody
                    }
                }
                TLabel {
                    visible: rowItem.modelData.after !== ""
                    text: rowItem.modelData.after
                    font.pixelSize: list.compact ? Theme.fontSmall : Theme.fontBody
                }
                TLabel {
                    visible: rowItem.creates
                    text: rowItem.modelData.kind === "forceNew" ? qsTr("Mégis új: „%1”").arg(rowItem.modelData.name)
                                                                : qsTr("Új címke: „%1”").arg(rowItem.modelData.name)
                    font.pixelSize: list.compact ? Theme.fontSmall : Theme.fontBody
                }
            }
            TLabel {
                id: countText
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: rowItem.creates || rowItem.modelData.count <= 0 ? "" : rowItem.modelData.count
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    // Billentyű-tipp a lista alatt (felső elválasztóval).
    Item {
        width: list.width
        height: hint.implicitHeight + 4 + 12
        Rectangle { y: 4; width: parent.width; height: 1; color: Theme.border }
        TLabel {
            id: hint
            x: 10; y: 4 + 7
            width: parent.width - 20
            text: list.compact ? qsTr("Enter hozzáad · Esc bezár")
                                  : qsTr("↑↓ választás · Enter hozzáadás · Esc bezárás")
            muted: true
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideRight
        }
    }
}
