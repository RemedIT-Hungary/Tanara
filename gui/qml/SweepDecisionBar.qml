import QtQuick
import QtQuick.Layouts

// Sweep mode, stopped: the decision bar below the doubtful line where playback stopped (loaded
// by TranscriptRow). Every button shows its key too; the keys live in TranscriptTab (by physical
// position, TranscriptKeys), the buttons call the same actions:
//   Enter  the default: the speaker suggested for the voice ("Fehér Gábor said it"), else "Fine as is"
//   J      Fine as is      0  Unidentifiable        1-9  the n-th speaker of the rail
//   M      Other... (the line's panel)              Right  Later (until the next stop)
//   Space  continue without deciding   Left  line start   Shift  not a sample (labels switch too)
// After a decision playback resumes by itself (TranscriptEditorViewModel.sweepResumeRequested).
Item {
    id: root

    required property var tab              // TranscriptTab
    required property var vm               // TranscriptEditorViewModel
    required property int rowIndex
    required property string speakerKey
    required property int ownLane
    required property string likelyKey
    required property string likelyName
    // Tests / screenshots also rely on the buttons (and the objectNames).
    readonly property bool hasSuggestion: likelyKey !== "" && likelyKey !== speakerKey
    readonly property bool shift: tab.shiftHeld
    readonly property string suffix: shift ? tab.nonSampleSuffix : ""
    readonly property int likelyColor: hasSuggestion ? (vm.speakerInfo(likelyKey).colorIndex || 0) : -1

    implicitHeight: column.implicitHeight + 12
    implicitWidth: column.implicitWidth

    // Key-cap button (TButton + the key label in front).
    component KeyButton: TButton {
        id: kb
        property string keyText: ""
        property int speakerIndex: -1
        size: "small"
        leftPadding: (keyText !== "" ? cap.width + 8 : 0) + (speakerIndex >= 0 ? 14 : 0) + 8
        rightPadding: 10
        Rectangle {
            id: cap
            visible: kb.keyText !== ""
            x: 7
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(18, capLabel.implicitWidth + 8)
            height: 18
            radius: 4
            color: kb.variant === "primary" ? Theme.alpha(Theme.textOnAccent, 0.18) : Theme.sunken
            border.width: 1
            border.color: kb.variant === "primary" ? Theme.alpha(Theme.textOnAccent, 0.35) : Theme.border
            TLabel {
                id: capLabel
                anchors.centerIn: parent
                text: kb.keyText
                mono: true
                color: kb.variant === "primary" ? Theme.textOnAccent : Theme.textMuted
                font.pixelSize: Theme.fontMicro
                font.weight: Theme.weightMedium
            }
        }
        Rectangle {
            visible: kb.speakerIndex >= 0
            x: (kb.keyText !== "" ? cap.x + cap.width + 7 : 9)
            anchors.verticalCenter: parent.verticalCenter
            width: 8; height: 8; radius: 4
            color: kb.speakerIndex >= 0 ? Theme.speakerLine(kb.speakerIndex) : "transparent"
        }
    }

    Column {
        id: column
        y: 6
        width: root.width
        spacing: 6

        Flow {
            width: parent.width
            spacing: 6

            KeyButton {
                objectName: "sweepDefault"
                keyText: "Enter"
                variant: "primary"
                speakerIndex: root.likelyColor
                text: (root.hasSuggestion ? qsTr("%1 mondta").arg(root.likelyName) : qsTr("Jó így")) + root.suffix
                toolTipText: qsTr("Az alapértelmezés (Enter). Shift+Enter: ugyanez, de a hangja nem lesz minta")
                onClicked: root.tab.sweepDecideAt(root.rowIndex, "default", -1)
            }
            KeyButton {
                objectName: "sweepConfirm"
                visible: root.hasSuggestion
                keyText: "J"
                text: qsTr("Jó így") + root.suffix
                onClicked: root.tab.sweepDecideAt(root.rowIndex, "confirm", -1)
            }
            KeyButton {
                objectName: "sweepIgnore"
                keyText: "0"
                text: qsTr("Nem azonosítható")
                toolTipText: qsTr("A név marad, de a sor hangját semmire nem használom")
                onClicked: root.tab.sweepDecideAt(root.rowIndex, "ignore", -1)
            }
            Rectangle { width: 1; height: 28; color: Theme.border }
            Repeater {
                model: Math.min(9, root.tab.laneCount)
                KeyButton {
                    id: laneButton
                    required property int index
                    readonly property var lane: root.vm.lanes[index] || ({})
                    objectName: "sweepLane"
                    visible: index !== root.ownLane
                    keyText: String(index + 1)
                    speakerIndex: lane.colorIndex !== undefined ? lane.colorIndex : 0
                    text: (lane.name || "") + root.suffix
                    width: Math.min(implicitWidth, 190)
                    onClicked: root.tab.sweepDecideAt(root.rowIndex, "lane", index)
                }
            }
            Rectangle { width: 1; height: 28; color: Theme.border }
            KeyButton {
                objectName: "sweepOther"
                keyText: "M"
                variant: "ghost"
                text: qsTr("Más…")
                onClicked: root.tab.sweepOpenOther(root.rowIndex)
            }
            KeyButton {
                objectName: "sweepLater"
                keyText: "→"
                variant: "ghost"
                text: qsTr("Később")
                toolTipText: qsTr("Most kihagyom; a szakasz végén visszanézhető")
                onClicked: root.tab.sweepDecideAt(root.rowIndex, "later", -1)
            }
        }
        TLabel {
            width: parent.width
            text: qsTr("A lejátszás itt megállt · Space: tovább döntés nélkül · ←: a sor eleje · Shift: nem minta")
            muted: true
            elide: Text.ElideRight
            font.pixelSize: Theme.fontCaption
        }
    }
}
