import QtQuick

// Temporary placeholder for screen parts not built yet (used by the shell scaffold).
// Agents building the slices replace the placeholder files' content entirely.
Item {
    id: root
    property string label: ""
    property string note: ""

    TDashedRect {
        anchors.fill: parent
        anchors.margins: 6
        color: Theme.borderStrong
    }
    Column {
        anchors.centerIn: parent
        width: parent.width - 24
        spacing: 2
        TLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.label
            mono: true
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
        TLabel {
            visible: root.note !== "" && root.height > 60
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.note
            muted: true
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }
    }
}
