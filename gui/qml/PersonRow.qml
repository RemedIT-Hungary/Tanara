import QtQuick

// Egy személy sora a választó panelekben. Két méret:
//   magas (44 px, PersonPicker): 28 px monogram · név 14/500 + alsor 12 · ujjlenyomat-ikon
//   alacsony (38 px, LinePopover / SpeakerWhyPopover): 24 px monogram · név 14 · jobb oldalt az alsor
// speakerIndex >= 0: a monogram a beszélő színében (a meeting egy másik beszélője).
Item {
    id: root

    property string personName: ""
    property string monogram: Theme.monogram(personName)
    property string subText: ""
    property bool voiceprint: false
    property bool highlighted: false
    property int speakerIndex: -1
    property bool compact: false
    readonly property alias hovered: hover.hovered

    signal clicked()

    implicitHeight: compact ? 38 : 44
    Accessible.role: Accessible.ListItem
    Accessible.name: personName

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.highlighted ? Theme.sunken : "transparent"
    }

    Rectangle {
        id: avatar
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        width: root.compact ? 24 : 28
        height: width
        radius: width / 2
        color: root.speakerIndex >= 0 ? Theme.speakerSoft(root.speakerIndex) : Theme.sunken
        border.width: root.speakerIndex >= 0 ? 2 : 1
        border.color: root.speakerIndex >= 0 ? Theme.speakerLine(root.speakerIndex) : Theme.border
        TLabel {
            anchors.centerIn: parent
            text: root.monogram
            color: root.speakerIndex >= 0 ? Theme.speakerInk(root.speakerIndex) : Theme.textMuted
            font.pixelSize: root.compact ? 9 : 10
            font.weight: Theme.weightBold
        }
    }

    // Magas sor: név + alatta az alsor.
    Column {
        visible: !root.compact
        anchors.left: avatar.right
        anchors.leftMargin: 10
        anchors.right: fingerprint.visible ? fingerprint.left : parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        TLabel {
            width: parent.width
            text: root.personName
            font.weight: Theme.weightMedium
            elide: Text.ElideRight
        }
        TLabel {
            width: parent.width
            visible: root.subText !== ""
            text: root.subText
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }
    TIcon {
        id: fingerprint
        visible: !root.compact && root.voiceprint
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        name: "fingerprint"
        size: 15
        color: Theme.textMuted
    }

    // Alacsony sor: név balra, alsor jobbra.
    TLabel {
        visible: root.compact
        anchors.left: avatar.right
        anchors.leftMargin: 10
        anchors.right: sub.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.personName
        elide: Text.ElideRight
    }
    TLabel {
        id: sub
        visible: root.compact
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.subText
        muted: true
        font.pixelSize: Theme.fontCaption
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: root.clicked() }
}
