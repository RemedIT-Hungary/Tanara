import QtQuick

// Egysoros szöveg kiemelt találattal (könyvtár-keresés): előtte / találat (warnSoft háttér) /
// utána; a végén kipontoz. Találat nélkül csak az `after` látszik (sima, kipontozott sor).
//   LibraryHighlightText { before: "…a "; match: "demó"; after: " előtt még…" }
Item {
    id: root

    property string before: ""
    property string match: ""
    property string after: ""
    property int pixelSize: Theme.fontCaption
    property int weight: Theme.weightRegular
    property color color: Theme.textMuted
    property color matchColor: Theme.text

    // A Text a sorvégi szóközt nem számolja a szélességbe → a darabok közé kézzel tesszük.
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
