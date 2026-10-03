import QtQuick

// Felelős-chip a teendők mellett (nem kattintható): a meeting beszélőjénél a beszélő színében
// (pötty + soft háttér + ink szöveg), más névnél semleges.
//   SummaryOwnerChip { name: "Kovács Lilla"; speakerIndex: 0 }
Rectangle {
    id: root
    property string name: ""
    property int speakerIndex: -1          // -1: nem a meeting beszélője

    property real maximumWidth: 240        // hosszabb név kipontozva

    readonly property bool person: speakerIndex >= 0
    readonly property real chrome: person ? 3 + 18 + 6 + 9 : 18

    implicitHeight: 24
    implicitWidth: Math.min(maximumWidth, label.implicitWidth + chrome)
    radius: 12
    color: person ? Theme.speakerSoft(speakerIndex) : Theme.sunken

    Rectangle {
        visible: root.person
        x: 3
        anchors.verticalCenter: parent.verticalCenter
        width: 18; height: 18; radius: 9
        color: root.person ? Theme.speakerLine(root.speakerIndex) : "transparent"
    }
    Text {
        id: label
        x: root.person ? 27 : 9
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, root.maximumWidth - root.chrome)
        elide: Text.ElideRight
        text: root.name
        color: root.person ? Theme.speakerInk(root.speakerIndex) : Theme.textMuted
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontCaption
        font.weight: Theme.weightSemiBold
    }
}
