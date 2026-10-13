import QtQuick
import QtQuick.Layouts

// One step of the pre-transcript step list: a 28 px numbered circle, a 2 px connector line
// below it, the title and content on the right. tone: "current" (filled accent) | "next"
// (accent border) | "idle" (neutral border). Children go below the title.
//   PreTranscriptStep { number: 1; tone: "current"; title: qsTr("What was the meeting about?") ... }
Item {
    id: root

    property int number: 1
    property string tone: "idle"
    property string title: ""
    property string tag: ""            // e.g. "optional" next to the title
    property bool last: false          // no line or bottom spacing below the last step
    property real contentSpacing: 8
    default property alias content: body.data

    implicitWidth: 480
    implicitHeight: Math.max(28, body.implicitHeight + (last ? 0 : 22))

    Rectangle {
        id: circle
        width: 28; height: 28; radius: 14
        color: root.tone === "current" ? Theme.accent : Theme.raised
        border.width: root.tone === "current" ? 0 : 1.5
        border.color: root.tone === "next" ? Theme.accent : Theme.borderStrong
        TLabel {
            anchors.centerIn: parent
            text: root.number
            mono: true
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
            color: root.tone === "current" ? Theme.textOnAccent
                 : root.tone === "next" ? Theme.accent : Theme.text
        }
    }
    Rectangle {
        visible: !root.last
        x: 13; y: 32
        width: 2
        height: Math.max(0, root.height - 36)
        color: Theme.border
    }

    ColumnLayout {
        id: body
        x: 44
        width: root.width - 44
        spacing: root.contentSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 3
            spacing: 8
            TLabel {
                text: root.title
                font.pixelSize: Theme.fontHeading
                font.weight: Theme.weightSemiBold
                Layout.alignment: Qt.AlignBaseline
            }
            TLabel {
                visible: root.tag !== ""
                text: root.tag
                muted: true
                font.pixelSize: Theme.fontCaption
                Layout.alignment: Qt.AlignBaseline
            }
            Item { Layout.fillWidth: true }
        }
    }
}
