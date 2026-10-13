import QtQuick
import QtQuick.Templates as T

// The tag input field's popup list: 300 px (compact / recorder: 250 px, 28 px rows, at most 3),
// raised, radius 8, shadow. Header: "RECENTLY USED" (empty field) or "SIMILAR EXISTS" (a very
// similar name). Row: # icon, name with the match highlighted (warnSoft), count in mono; the
// "New tag: "..."" / "New anyway: "..."" row has a + icon. A keyboard hint at the bottom.
// Focus stays in the field (focus: false); TagInput handles the keys.
TPopover {
    id: popover

    property var model: null
    property bool compact: false
    signal rowClicked(int index)

    width: compact ? 250 : 300
    padding: 4
    focus: false
    closePolicy: T.Popup.CloseOnPressOutsideParent

    contentItem: TagInputList {
        model: popover.model
        compact: popover.compact
        onRowClicked: (index) => popover.rowClicked(index)
    }
}
