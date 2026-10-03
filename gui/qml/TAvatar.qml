import QtQuick

// Beszélő-avatar monogrammal. variant: "soft" (lágy kitöltés + 2 px gyűrű a beszélőszínben —
// listákban, fejlécekben) | "solid" (teli beszélőszín — a sáv-fejlécben, 20 px).
//   TAvatar { name: "Kovács Lilla"; speakerIndex: 0 }            // 24 px, soft
//   TAvatar { name: "Távoli 2"; speakerIndex: 5; variant: "solid"; size: 20 }
Rectangle {
    id: root
    property string name: ""
    property string monogram: Theme.monogram(name)
    property int speakerIndex: 0
    property string variant: "soft"
    property real size: Theme.avatarSize

    implicitWidth: size
    implicitHeight: size
    radius: size / 2
    color: variant === "solid" ? Theme.speakerLine(speakerIndex) : Theme.speakerSoft(speakerIndex)
    border.width: variant === "solid" ? 0 : 2
    border.color: Theme.speakerLine(speakerIndex)

    Text {
        anchors.centerIn: parent
        text: root.monogram
        color: root.variant === "solid" ? Theme.textOnSpeaker : Theme.speakerInk(root.speakerIndex)
        font.family: Theme.fontSans
        font.pixelSize: Math.max(8, Math.round(root.size * 0.375))
        font.weight: Theme.weightBold
    }
}
