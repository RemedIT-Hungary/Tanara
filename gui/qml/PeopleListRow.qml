import QtQuick

// One row of the People window's name list: 28 px monogram, name (search match highlighted),
// "you" marker, below it an icon + meeting and sample counts. If the row starts a new section
// (`header`), the section label sits above it.
Item {
    id: root

    property string name: ""
    property string monogram: ""
    property bool isSelf: false
    property bool hasVoiceprint: false
    property string meta: ""
    property string header: ""
    property string before: ""
    property string match: ""
    property string after: ""
    // The search matched the person's tag: a tag icon before the meta ("tag: #Nordvik ...").
    property bool tagHit: false
    property bool selected: false
    signal clicked()

    readonly property int headerHeight: header !== "" ? 30 : 0

    implicitHeight: headerHeight + 48
    Accessible.role: Accessible.ListItem
    Accessible.name: name

    function esc(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }

    TLabel {
        visible: root.header !== ""
        x: 8; y: 12
        text: root.header
        muted: true
        font.pixelSize: Theme.fontMicro
        font.weight: Theme.weightSemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.66
    }

    Rectangle {
        id: row
        y: root.headerHeight
        width: parent.width
        height: 46
        radius: Theme.radiusControl
        color: root.selected ? Theme.accentSoft : "transparent"

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.stateLayer
            opacity: root.selected ? 0 : tap.pressed ? Theme.pressedOpacity : hover.hovered ? Theme.hoverOpacity : 0
        }
        HoverHandler { id: hover }
        TapHandler { id: tap; onTapped: root.clicked() }

        Rectangle {
            id: avatar
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 28; height: 28; radius: 14
            color: root.isSelf ? Theme.accent : Theme.sunken
            border.width: 1
            border.color: root.isSelf ? Theme.accent : Theme.border
            TLabel {
                anchors.centerIn: parent
                text: root.monogram
                color: root.isSelf ? Theme.textOnAccent : Theme.text
                font.pixelSize: 10
                font.weight: Theme.weightBold
                font.letterSpacing: 0.2
            }
        }

        Column {
            anchors.left: avatar.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Row {
                width: parent.width
                spacing: 6
                // The name is not shortened: the list pane is resizable (and clips only as a last resort).
                TLabel {
                    id: nameLabel
                    width: Math.min(implicitWidth, parent.width - (mePill.visible ? mePill.width + 6 : 0))
                    textFormat: root.match !== "" ? Text.RichText : Text.PlainText
                    text: root.match !== ""
                        ? root.esc(root.before) + "<span style=\"background-color:" + Theme.warnSoft + "\">"
                          + root.esc(root.match) + "</span>" + root.esc(root.after)
                        : root.name
                    font.weight: root.selected ? Theme.weightSemiBold : Theme.weightMedium
                    elide: root.match !== "" ? Text.ElideNone : Text.ElideRight
                    clip: root.match !== ""
                }
                Rectangle {
                    id: mePill
                    visible: root.isSelf
                    anchors.verticalCenter: nameLabel.verticalCenter
                    width: meLabel.implicitWidth + 10
                    height: 15
                    radius: 6
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.borderStrong
                    TLabel {
                        id: meLabel
                        anchors.centerIn: parent
                        text: qsTr("te")
                        muted: true
                        font.pixelSize: 10
                        font.weight: Theme.weightSemiBold
                    }
                }
            }
            Row {
                width: parent.width
                spacing: 5
                TIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: root.tagHit ? "tag" : root.hasVoiceprint ? "fingerprint" : "minus"
                    size: 12
                    color: root.hasVoiceprint && !root.tagHit ? Theme.successInk : Theme.textMuted
                }
                TLabel {
                    width: parent.width - 17
                    text: root.meta
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
            }
        }
    }
}
