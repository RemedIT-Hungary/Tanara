import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Az Átnézendő MINTÁZAT-AJÁNLATA (S5; koncepció-dokumentum 2.2, 8. lépés): ha az utolsó 6
// döntésből ≥ 4 ugyanaz az (ok, jelenlegi, javasolt) hármas, a fókusz-kártya ALATT (az alsó
// kontextus-sorok helyén, a kártya nem mozdul) egy sáv: „A hátralévőből még N sor ugyanilyen
// (ok · X → Y). Mind az N → Y?” + [L Megmutatom] (a sorok kompakt, görgethető listája ▶-tel, a
// sáv alatt) + [I Igen] (egy visszavonási lépés; Shift = nem minta) + [N Nem] (ez a minta ebben a
// körben nem jön vissza). Az állapotot (kinyitva, épp szól) a ReviewFocusView tartja.
//   offer: a ReviewQueueModel.recentPattern() térképe
//   lines: a sameAhead sorai [{ utteranceId, timeLabel, lineText, startMs, endMs }]
Rectangle {
    id: root

    property var offer: ({})
    property var lines: []
    property bool expanded: false
    property bool shiftHeld: false
    property string playingId: ""

    signal showClicked()
    signal yesClicked()
    signal noClicked()
    signal playClicked(string utteranceId)

    readonly property int n: lines.length
    readonly property string reasonText: {
        switch (offer.reasonKind || "") {
        case "sideConflict": return qsTr("sáv-ellentmondás")
        case "coreMismatch": return qsTr("eltér a beszélő hangjától")
        case "similarToNewPerson": return qsTr("az új személyhez hasonló")
        case "uncertain": return qsTr("hangra kétes")
        }
        return ""
    }
    readonly property int rowHeight: 28
    readonly property int visibleRows: Math.min(5, n)

    objectName: "reviewFocusOffer"
    implicitHeight: col.implicitHeight + 20
    height: implicitHeight
    radius: Theme.radiusPopup
    color: Theme.accentSoft
    border.width: 1
    border.color: Theme.accentLine

    function allTo(count, name) {
        const az = count === 1 || (count >= 1000 && count < 2000) || String(count).charAt(0) === "5"
        return az ? qsTr("Mind az %1 → %2").arg(count).arg(name) : qsTr("Mind a %1 → %2").arg(count).arg(name)
    }
    function esc(s) {
        return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    ColumnLayout {
        id: col
        x: 14
        y: 10
        width: root.width - 28
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TIcon { name: "sparkles"; size: 16; color: Theme.accent }
            TLabel {
                objectName: "reviewOfferText"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSmall
                textFormat: Text.StyledText
                text: qsTr("A hátralévőből még <b>%n sor ugyanilyen</b>", "", root.n)
                      + " (" + (root.reasonText !== "" ? root.esc(root.reasonText) + " · " : "")
                      + root.esc(root.offer.currentName || "") + " → " + root.esc(root.offer.proposedName || "") + "). "
                      + root.esc(root.allTo(root.n, root.offer.proposedName || "")) + "?"
            }
            ReviewFocusKeyButton {
                objectName: "reviewOfferShow"
                keyText: "L"
                variant: "ghost"
                text: root.expanded ? qsTr("Elrejtem") : qsTr("Megmutatom")
                toolTipText: qsTr("A %n sor listája, egyenként meghallgatható", "", root.n)
                onClicked: root.showClicked()
            }
            ReviewFocusKeyButton {
                objectName: "reviewOfferYes"
                keyText: "I"
                variant: "primary"
                text: qsTr("Igen")
                nonSample: root.shiftHeld
                toolTipText: qsTr("Egy visszavonási lépés · Shift+I: egyik sem lesz minta")
                onClicked: root.yesClicked()
            }
            ReviewFocusKeyButton {
                objectName: "reviewOfferNo"
                keyText: "N"
                text: qsTr("Nem")
                toolTipText: qsTr("Ez a minta ebben a körben nem jön vissza")
                onClicked: root.noClicked()
            }
        }

        // A sorok listája (Megmutatom): legfeljebb 5 sor magas, a többi görgetve.
        ListView {
            id: list
            objectName: "reviewOfferList"
            visible: root.expanded
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? root.visibleRows * root.rowHeight : 0
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            model: root.lines
            T.ScrollBar.vertical: TScrollBar {}
            delegate: Rectangle {
                id: line
                required property var modelData
                readonly property bool playingNow: root.playingId !== "" && root.playingId === (modelData.utteranceId || "")
                objectName: "reviewOfferLine"
                width: ListView.view.width
                height: root.rowHeight
                radius: Theme.radiusControl
                color: playingNow ? Theme.raised : lineHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                HoverHandler { id: lineHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.playClicked(line.modelData.utteranceId || "") }
                RowLayout {
                    anchors { fill: parent; leftMargin: 8; rightMargin: 12 }
                    spacing: 10
                    TIcon { name: line.playingNow ? "volume-2" : "play"; size: 13; color: Theme.accent }
                    TLabel {
                        text: line.modelData.timeLabel || ""
                        mono: true
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        Layout.preferredWidth: 56
                    }
                    TLabel {
                        text: line.modelData.lineText || ""
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
