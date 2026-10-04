import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Összefoglaló fül — M08: témánkénti elemzés. Fejléc (számlálók, „Téma hozzáadása”,
// „Hiányzók elemzése”), alatta a téma-kártyák: hozzáadás / szerkesztés / törlés / húzásos
// átrendezés (mind azonnal mentve), témánként külön futtatás, megszakítás és újrapróbálás.
// Az állapot a lemezről jön, ezért újraindítás után onnan folytatható, ahol abbamaradt.
Item {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null
    property bool adding: false
    // A mező elrejtése után a fókusz ne maradjon a (láthatatlan) szövegmezőben: különben a
    // billentyűk (Szóköz, nyilak, gyorsbillentyűk) oda mennének.
    onAddingChanged: if (!adding) Qt.callLater(root.releaseHiddenFocus)
    function releaseHiddenFocus() {
        const it = root.Window.activeFocusItem
        if (it && !it.visible) root.forceActiveFocus()
    }

    readonly property var topics: vm.topics

    function analyzeMissing() { if (root.shell) root.shell.startTopicAnalysis(root.meetingId) }
    function runTopic(topicId) { if (root.shell) root.shell.analyzeTopic(root.meetingId, topicId) }
    function removeTopic(row, title, hasResult) {
        const meeting = root.meetingId
        const topicId = root.topics.topicIdAt(row)
        // Kész elemzésű témánál megerősítést kérünk (az elemzés kikerül az összegzésből).
        if (hasResult && root.shell
                && !root.shell.confirm(qsTr("Törlöd a témát?"),
                                       qsTr("„%1” elemzése kikerül az összegzésből. A többi téma eredménye megmarad.").arg(title),
                                       qsTr("Téma törlése"), true))
            return
        // A megerősítés alatt a kijelölés (vagy a téma-lista) megváltozhatott: csak ugyanabban
        // a megbeszélésben, azonosító szerint törlünk.
        if (root.meetingId === meeting) root.topics.removeTopicById(topicId)
    }
    function beginAdd() {
        root.adding = true
        list.positionViewAtEnd()
        newTitle.forceActiveFocus()
    }
    function commitAdd() {
        if (!root.topics.addTopic(newTitle.text, newSummary.text)) { newTitle.forceActiveFocus(); return }
        newTitle.text = ""
        newSummary.text = ""
        root.adding = false
        list.positionViewAtEnd()
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: 24; rightMargin: 24; topMargin: 18 }
        spacing: 12

        // ---- fejléc ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TButton {
                text: root.vm.hasSummary ? qsTr("Összefoglaló") : qsTr("Vissza")
                variant: "ghost"; size: "small"
                implicitHeight: 32
                leftPadding: 6; rightPadding: 8
                iconName: "chevron-left"
                spacing: 4
                toolTipText: root.vm.hasSummary ? qsTr("Vissza az összefoglalóhoz") : qsTr("Vissza a választáshoz")
                onClicked: root.vm.topicsOpen = false
            }
            TLabel {
                text: qsTr("Témák")
                font.pixelSize: Theme.fontHeading
                font.weight: Theme.weightSemiBold
                Layout.alignment: Qt.AlignBaseline
            }
            TLabel {
                Layout.fillWidth: true
                text: root.topics.countsText
                muted: true
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignBaseline
            }
            TButton {
                text: qsTr("Téma hozzáadása")
                size: "small"
                implicitHeight: 32
                leftPadding: 12; rightPadding: 12
                iconName: "plus"
                iconSize: 14
                spacing: 6
                onClicked: root.beginAdd()
            }
            TButton {
                // Ha minden téma kész, de még nincs (friss) összegzés, ugyanez a művelet az
                // összegzést készíti el.
                text: root.topics.missingCount > 0 || root.topics.count === 0 || root.topics.busy
                      ? qsTr("Hiányzók elemzése") : qsTr("Összegzés készítése")
                variant: "primary"; size: "small"
                implicitHeight: 32
                leftPadding: 12; rightPadding: 12
                font.weight: Theme.weightSemiBold
                enabled: root.vm.canRun && root.topics.count > 0 && !root.vm.jobRunning
                         && (root.topics.missingCount > 0 || !root.topics.busy)
                toolTipText: !root.vm.canRun ? (root.vm.blocker.reason || "") : ""
                onClicked: root.analyzeMissing()
            }
        }

        SummaryStatusBanners {
            Layout.fillWidth: true
            vm: root.vm
            meetingId: root.meetingId
            shell: root.shell
            onRetryRequested: root.analyzeMissing()
        }

        // ---- kártyák ----
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.rightMargin: -14                  // a görgetősáv a jobb margóba kerül
            model: root.topics
            spacing: 12
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            bottomMargin: 24
            T.ScrollBar.vertical: TScrollBar {}

            moveDisplaced: Transition {
                NumberAnimation { properties: "y"; duration: Theme.durationNormal; easing.type: Theme.easing }
            }
            move: Transition {
                NumberAnimation { properties: "y"; duration: Theme.durationFast; easing.type: Theme.easing }
            }

            delegate: TopicCard {
                id: card
                required property int index
                required property var model

                width: ListView.view.width - 14
                z: dragging ? 2 : 1
                topicId: model.topicId
                title: model.title
                summary: model.summary
                topicState: model.topicState
                error: model.error
                errorDetail: model.errorDetail
                hasResult: model.hasResult
                resultText: model.resultText
                resultDecisions: model.resultDecisions
                resultOpenQuestions: model.resultOpenQuestions
                resultActions: model.resultActions
                canRun: root.vm.canRun
                blockedReason: root.vm.blocker.reason || ""

                onRunRequested: root.runTopic(topicId)
                onCancelRequested: root.topics.cancelTopic(index)
                onRemoveRequested: root.removeTopic(index, title, hasResult)
                onSaveRequested: (newTitle, newSummary) => root.topics.updateTopic(index, newTitle, newSummary)
                onMoveRequested: (delta) => root.topics.moveTopic(index, index + delta)
                onDragMoved: (y) => {
                    // A mutató alatti kártya helyére lép, ha a közepén túlhaladt.
                    const p = card.mapToItem(list.contentItem, 10, y)
                    const to = list.indexAt(p.x, p.y)
                    if (to < 0 || to === index) return
                    const target = list.itemAtIndex(to)
                    if (!target) return
                    const mid = target.y + target.height / 2
                    if ((to > index && p.y > mid) || (to < index && p.y < mid))
                        root.topics.moveTopic(index, to)
                }
            }

            footer: Item {
                width: list.width - 14
                height: root.adding ? addCard.implicitHeight + 12 : (list.count === 0 ? emptyHint.implicitHeight + 24 : 0)

                TLabel {
                    id: emptyHint
                    visible: list.count === 0 && !root.adding
                    y: 12
                    width: parent.width
                    text: qsTr("Még nincs téma. Vegyél fel egyet kézzel, vagy térj vissza, és kérj javaslatot a modelltől.")
                    muted: true
                    wrapMode: Text.Wrap
                }

                // ---- új téma felvétele ----
                Rectangle {
                    id: addCard
                    visible: root.adding
                    y: list.count > 0 ? 12 : 0
                    width: parent.width
                    implicitHeight: addCol.implicitHeight + 24
                    height: implicitHeight
                    radius: Theme.radiusPopup
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.accentLine

                    ColumnLayout {
                        id: addCol
                        x: 14; y: 12
                        width: parent.width - 28
                        spacing: 8
                        TTextField {
                            id: newTitle
                            Layout.fillWidth: true
                            placeholderText: qsTr("Az új téma címe")
                            font.weight: Theme.weightSemiBold
                            Keys.onReturnPressed: root.commitAdd()
                            Keys.onEscapePressed: root.adding = false
                            Accessible.name: qsTr("Az új téma címe")
                        }
                        TTextArea {
                            id: newSummary
                            Layout.fillWidth: true
                            placeholderText: qsTr("Rövid leírás: mire figyeljen az elemzés (elhagyható)")
                            font.pixelSize: Theme.fontSmall
                            implicitHeight: Math.max(52, contentHeight + topPadding + bottomPadding)
                            Keys.onEscapePressed: root.adding = false
                            Accessible.name: qsTr("Az új téma leírása")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            TButton {
                                text: qsTr("Mégse")
                                variant: "ghost"; size: "small"
                                onClicked: root.adding = false
                            }
                            TButton {
                                text: qsTr("Hozzáadás")
                                variant: "primary"; size: "small"
                                enabled: newTitle.text.trim() !== ""
                                onClicked: root.commitAdd()
                            }
                        }
                    }
                }
            }
        }
    }
}
