import QtQuick
import QtQuick.Layouts

// Round end in the To review focus view: what was decided, what became a pattern per speaker,
// and what background analysis found meanwhile. Enter = next round (the newly arrived + the
// left-over), Right = only the left-over, K = done (back to Correct).
//   summary: ReviewQueueModel.roundSummary()
Rectangle {
    id: root

    property var summary: ({})
    signal nextClicked()
    signal laterClicked()
    signal doneClicked()
    signal confirmSettledClicked()

    readonly property int incoming: summary.incoming || 0
    readonly property int later: summary.later || 0
    readonly property int caused: summary.incomingCausedByDecisions || 0
    readonly property int settled: summary.alreadyDecided || 0
    readonly property int confirmable: summary.confirmable || 0
    readonly property var samples: (summary.sampleDelta || []).filter(s => (s.delta || 0) > 0)

    objectName: "reviewFocusSummary"
    implicitHeight: col.implicitHeight + 44
    height: implicitHeight
    radius: Theme.radiusDialog
    color: Theme.raised
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        id: col
        x: 24
        y: 22
        width: root.width - 48
        spacing: 14
        RowLayout {
            spacing: 10
            TIcon { name: "circle-check"; size: 20; color: Theme.success }
            TLabel {
                text: qsTr("%1. kör kész").arg(root.summary.round || 1)
                font.pixelSize: Theme.fontTitle
                font.weight: Theme.weightSemiBold
            }
        }
        TLabel {
            objectName: "reviewSummaryCounts"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontHeading
            textFormat: Text.StyledText
            text: qsTr("Kész: <b>%1 eldöntve</b>, %2 nem azonosítható, %3 később.")
                      .arg(root.summary.decided || 0).arg(root.summary.ignored || 0).arg(root.later)
        }
        // The auto-decided lines: the same "Confirm all" as in the header.
        RowLayout {
            objectName: "reviewSummarySettled"
            Layout.fillWidth: true
            visible: root.settled > 0
            spacing: 10
            TIcon { name: "check"; size: 16; strokeWidth: 2.2; color: Theme.success }
            TLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSmall
                text: root.confirmable > 0
                      ? qsTr("%n sor magától eldőlt (a háttér-elemzés szerint már nem kétes).", "", root.settled)
                        + " " + qsTr("Megerősítve %n lesz minta.", "", root.confirmable)
                      : qsTr("%n sor magától eldőlt; mind rövid, nem lenne minta.", "", root.settled)
            }
            ReviewFocusKeyButton {
                objectName: "reviewSummaryConfirmSettled"
                keyText: "Ctrl+Enter"
                text: qsTr("Mind megerősítem")
                enabled: root.confirmable > 0
                toolTipText: qsTr("Egy lépésben, visszavonható (Ctrl+Z)")
                onClicked: root.confirmSettledClicked()
            }
        }
        // Sample contribution per speaker (since the round start).
        Flow {
            objectName: "reviewSummarySamples"
            Layout.fillWidth: true
            visible: root.samples.length > 0
            spacing: 14
            Repeater {
                model: root.samples
                delegate: Row {
                    required property var modelData
                    spacing: 6
                    TAvatar {
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.name || ""
                        speakerIndex: modelData.colorIndex || 0
                        size: 22
                    }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("%1 +%2 minta").arg(modelData.name || "").arg(modelData.delta || 0)
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightSemiBold
                        color: Theme.speakerInk(modelData.colorIndex || 0)
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: incRow.implicitHeight + 18
            radius: Theme.radiusControl
            color: root.incoming > 0 ? Theme.warnSoft : Theme.sunken
            border.width: 1
            border.color: root.incoming > 0 ? Theme.warnLine : Theme.border
            RowLayout {
                id: incRow
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 12 }
                spacing: 10
                TIcon { name: "inbox"; size: 16; color: root.incoming > 0 ? Theme.warnInk : Theme.textMuted }
                TLabel {
                    objectName: "reviewSummaryIncoming"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSmall
                    color: root.incoming > 0 ? Theme.warnInk : Theme.textMuted
                    text: root.incoming === 0 ? qsTr("A háttér-elemzés nem talált új kétes sort.")
                        : root.caused > 0 ? qsTr("Az elemzés %n új kétes sort talált (ebből %1 a mostani döntéseid miatt). Kör közben egyik sem került eléd.", "", root.incoming).arg(root.caused)
                        : qsTr("Az elemzés %n új kétes sort talált. Kör közben egyik sem került eléd.", "", root.incoming)
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ReviewFocusKeyButton {
                objectName: "reviewNextRound"
                keyText: "Enter"
                variant: "primary"
                text: qsTr("Következő kör (%1)").arg(root.incoming + root.later)
                enabled: root.incoming + root.later > 0
                toolTipText: qsTr("Az új kétes sorok és a később hagyottak, időrendben")
                onClicked: root.nextClicked()
            }
            ReviewFocusKeyButton {
                objectName: "reviewLaterRound"
                keyText: "→"
                text: qsTr("Később hagyottak (%1)").arg(root.later)
                enabled: root.later > 0
                toolTipText: qsTr("Csak a később hagyott sorok (az új kétesek várnak)")
                onClicked: root.laterClicked()
            }
            ReviewFocusKeyButton {
                objectName: "reviewDone"
                keyText: "K"
                variant: "ghost"
                text: qsTr("Kész")
                toolTipText: qsTr("Vissza a Javítás nézetbe")
                onClicked: root.doneClicked()
            }
        }
    }
}
