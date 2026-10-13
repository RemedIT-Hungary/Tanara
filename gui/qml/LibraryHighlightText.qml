import QtQuick

// Single-line text with a highlighted hit (library search): before / hit (warnSoft background) /
// after; elided at the end. Without a hit only `after` shows (a plain, elided line).
//   LibraryHighlightText { before: "...a "; match: "demo"; after: " before..." }
Item {
    id: root

    property string before: ""
    property string match: ""
    property string after: ""
    property int pixelSize: Theme.fontCaption
    property int weight: Theme.weightRegular
    property color color: Theme.textMuted
    property color matchColor: Theme.text

    // Text does not count a trailing space in its width -> we put spaces between the pieces by hand.
    readonly property real spaceWidth: Math.round(pixelSize * 0.28)

    implicitHeight: afterLabel.implicitHeight
    implicitWidth: row.implicitWidth
    clip: true

    Row {
        id: row
        height: parent.height
        TLabel {
            visible: root.before !== ""
            text: root.before.replace(/\s+$/, "")
            color: root.color
            font.pixelSize: root.pixelSize
            font.weight: root.weight
        }
        Item { width: /\s$/.test(root.before) ? root.spaceWidth : 0; height: 1 }
        Rectangle {
            visible: root.match !== ""
            width: matchLabel.implicitWidth + 6
            height: matchLabel.implicitHeight
            radius: 3
            color: Theme.warnSoft
            TLabel {
                id: matchLabel
                x: 3
                text: root.match
                color: root.matchColor
                font.pixelSize: root.pixelSize
                font.weight: Math.max(root.weight, Theme.weightMedium)
            }
        }
        Item { width: root.match !== "" && /^\s/.test(root.after) ? root.spaceWidth : 0; height: 1 }
        TLabel {
            id: afterLabel
            text: root.match !== "" ? root.after.replace(/^\s+/, "") : root.after
            width: Math.max(0, root.width - x)
            elide: Text.ElideRight
            color: root.color
            font.pixelSize: root.pixelSize
            font.weight: root.weight
        }
    }
}
