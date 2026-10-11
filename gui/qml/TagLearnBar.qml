import QtQuick

// Tanulási kérdés a személy-címkékhez (vault UX-terv 5.), a változás-sáv sötét formanyelvén:
//   „Fehér Gábor és Varga Árpád másodszor van #Nordvik megbeszélésen. Rájuk teszem a címkét?”
//   Igen · Nem · Egyenként… · ×
// Az „Egyenként…” egy kis listát nyit (személy · #címke, soronként Igen / Nem). Az „Igen” és a
// „Nem” egy-egy visszavonási lépés (a hívó toastja „Visszavonás”-sal). A bezárás nem elutasítás.
// Kétféle hely: önálló sáv (a „Ki volt ott?” utáni TagLearnPopup) és beágyazott sor a
// szerkesztő változás-sávjában (embedded: nincs saját háttér, árnyék és ×).
//   question: { text, pairs: [{ index, name, tagName, text }] } (ParticipantsViewModel.learnQuestion)
Rectangle {
    id: root

    property var question: ({})
    property bool embedded: false
    property bool expanded: false
    readonly property var pairs: question && question.pairs ? question.pairs : []
    readonly property bool hovered: hover.hovered

    signal answered(bool yes)
    signal pairAnswered(int index, bool yes)
    signal dismissed()

    implicitWidth: 640
    implicitHeight: mainRow.height + (expanded && pairs.length > 0 ? list.height + 6 : 0)
    radius: Theme.radiusPopup
    color: embedded ? "transparent" : Theme.text

    TShadow { visible: !root.embedded; radius: root.radius }
    HoverHandler { id: hover }

    // Sötét sávra írt művelet (a TranscriptChangeBar BarAction-jének mása).
    component LearnAction: Item {
        id: action
        property string text: ""
        property string iconName: ""
        property bool strong: false
        property string toolTipText: ""
        signal clicked()

        implicitWidth: content.implicitWidth + 20
        width: implicitWidth
        height: 28
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text !== "" ? text : toolTipText
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Theme.alpha(Theme.bg, tap.pressed ? 0.24 : actionHover.hovered ? 0.16 : action.strong ? 0.08 : 0)
            border.width: action.strong ? 1 : 0
            border.color: Theme.alpha(Theme.bg, 0.4)
            TFocusRing { visible: action.activeFocus; targetRadius: parent.radius }
        }
        Row {
            id: content
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            TIcon {
                visible: action.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: action.iconName
                size: 14
                color: Theme.bg
            }
            TLabel {
                visible: action.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: action.text
                color: Theme.bg
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
            }
        }
        HoverHandler { id: actionHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: tap; onTapped: action.clicked() }
        TToolTip { visible: action.toolTipText !== "" && actionHover.hovered; text: action.toolTipText }
    }

    Item {
        id: mainRow
        width: parent.width
        height: Math.max(44, questionText.implicitHeight + 20)

        TIcon {
            id: tagIcon
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            name: "tag"
            size: 15
            color: Theme.accentLine
        }
        TLabel {
            id: questionText
            objectName: "learnText"
            anchors.left: tagIcon.right
            anchors.leftMargin: 8
            anchors.right: actions.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.question && root.question.text ? root.question.text : ""
            color: Theme.bg
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightMedium
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            LearnAction {
                objectName: "learnYes"
                text: qsTr("Igen")
                strong: true
                toolTipText: qsTr("A címke rájuk kerül (Ctrl+Z: visszavonás)")
                onClicked: root.answered(true)
            }
            LearnAction {
                objectName: "learnNo"
                text: qsTr("Nem")
                toolTipText: qsTr("Nem tartoznak ide: nem javasoljuk újra")
                onClicked: root.answered(false)
            }
            LearnAction {
                objectName: "learnEach"
                visible: root.pairs.length > 0
                text: root.expanded ? qsTr("Egyenként ▴") : qsTr("Egyenként…")
                toolTipText: qsTr("Személyenként és címkénként külön döntés")
                onClicked: root.expanded = !root.expanded
            }
            LearnAction {
                objectName: "learnClose"
                visible: !root.embedded
                iconName: "x"
                toolTipText: qsTr("Most nem (a javaslat a Személyek ablakban megmarad)")
                onClicked: root.dismissed()
            }
        }
    }

    Column {
        id: list
        objectName: "learnPairs"
        visible: root.expanded && root.pairs.length > 0
        y: mainRow.height
        x: 12
        width: parent.width - 24
        Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.bg, 0.18) }
        Repeater {
            model: root.pairs
            Item {
                id: pairRow
                required property var modelData
                required property int index
                objectName: "learnPair_" + index
                width: list.width
                height: 36
                TLabel {
                    anchors.left: parent.left
                    anchors.leftMargin: 26
                    anchors.right: pairActions.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.StyledText
                    elide: Text.ElideRight
                    color: Theme.bg
                    font.pixelSize: Theme.fontSmall
                    text: pairRow.modelData.name + " · <b>#" + pairRow.modelData.tagName + "</b>"
                          + " <font color=\"" + Theme.borderStrong + "\">" + pairRow.modelData.text + "</font>"
                }
                Row {
                    id: pairActions
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    LearnAction {
                        objectName: "learnPairYes_" + pairRow.index
                        text: qsTr("Igen")
                        strong: true
                        onClicked: root.pairAnswered(pairRow.modelData.index, true)
                    }
                    LearnAction {
                        objectName: "learnPairNo_" + pairRow.index
                        text: qsTr("Nem")
                        onClicked: root.pairAnswered(pairRow.modelData.index, false)
                    }
                }
            }
        }
    }
}
