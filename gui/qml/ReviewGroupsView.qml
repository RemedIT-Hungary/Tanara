import QtQuick
import QtQuick.Templates as T

// Az „Átnézendő" nézet (handoff-v3 E2, 15. döntés): a kétes sorok okok szerint csoportosítva,
// csoportonként egy döntéssel (és egy visszavonási lépéssel). Felül a szennyezett mag sávja
// (ContaminatedCoreBanner); alatta a csoport-kártyák (ReviewGroupCard); a kinyitott
// („Egyenként") csoport sorai a kártya alatt, soronként: Meghallgatom · „X mondta" (1) · Jó így ·
// Jó így, de nem minta · Más…; az aktív, összecsukott csoport alatt „··· N sor ebben a
// csoportban · Kinyitás". Az eldöntött sor a helyén marad, amíg a csoport nyitva van.
// B / Shift+B a csoportok sorain lépked (TranscriptTab). Lista-modell: editorVm.reviewGroups.
Item {
    id: root

    required property var vm                // TranscriptEditorViewModel
    required property var tab               // TranscriptTab (lejátszás, panelek)
    // A billentyűzettel / kattintással kiválasztott sor (sor-azonosító); "" = nincs.
    property string currentUtterance: ""
    readonly property alias list: list

    signal splitRequested(string speakerKey)
    signal speakerWhyRequested(string speakerKey, Item anchor)
    signal lineFixRequested(string utteranceId, Item anchor)

    // A sor kiválasztása + odagörgetés (B / Shift+B).
    function step(direction) {
        let from = currentUtterance !== "" ? vm.reviewGroups.rowOfLine(currentUtterance) : -1
        if (from < 0) from = list.indexAt(list.width / 2, list.contentY + 1)
        const row = vm.reviewGroups.stepLine(from, direction)
        if (row < 0) return false
        currentUtterance = vm.reviewGroups.utteranceAt(row)
        Qt.callLater(() => list.positionViewAtIndex(row, ListView.Contain))
        return true
    }
    // „1": a kiválasztott sor a javasolt beszélőhöz.
    function acceptCurrent() {
        const row = currentUtterance !== "" ? vm.reviewGroups.rowOfLine(currentUtterance) : -1
        const item = row >= 0 ? list.itemAtIndex(row) : null
        if (item && item.lineItem && item.lineItem.likelyKey !== "" && !item.lineItem.decided) {
            vm.moveUtteranceToSpeaker(currentUtterance, item.lineItem.likelyKey)
            return true
        }
        return false
    }

    component LineRow: Item {
        id: line
        required property var model
        readonly property string utteranceId: model.utteranceId
        readonly property string likelyKey: model.likelyKey || ""
        readonly property string likelyName: model.likelyName || ""
        readonly property bool decided: model.decided === true
        readonly property bool current: root.currentUtterance === utteranceId
        width: list.width
        height: lineColumn.implicitHeight + 16

        Rectangle {
            x: 24
            width: parent.width - 44
            height: parent.height - 4
            y: 2
            radius: Theme.radiusControl
            visible: line.current
            color: Theme.raised
            border.width: 1.5
            border.color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            onClicked: root.currentUtterance = line.utteranceId
            onDoubleClicked: root.tab.playFrom(line.model.startMs)
        }
        Column {
            id: lineColumn
            x: 66
            y: 8
            width: parent.width - x - 32
            spacing: 4
            opacity: line.decided ? 0.7 : 1
            Row {
                spacing: 8
                TLabel {
                    objectName: "reviewLineName"
                    anchors.verticalCenter: parent.verticalCenter
                    text: line.model.speakerName
                    color: Theme.speakerInk(line.model.colorIndex)
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightSemiBold
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: line.model.timeLabel
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontMicro
                }
                TranscriptMarker {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !line.decided && line.model.reason !== ""
                    kind: "uncertain"
                    reason: line.model.reason
                }
                TranscriptMarker {
                    objectName: "reviewDecided"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: line.decided
                    kind: line.model.decidedText === qsTr("javítva") ? "corrected" : "confirmed"
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: line.decided && line.model.decidedText !== qsTr("javítva")
                    text: line.model.decidedText
                    muted: true
                    font.pixelSize: Theme.fontMicro
                }
            }
            TLabel {
                width: parent.width
                text: line.model.lineText
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                cssLineHeight: 1.5
            }
            Row {
                visible: !line.decided
                topPadding: 2
                spacing: 6
                TButton {
                    focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
                    objectName: "lineListen"
                    size: "small"
                    iconName: "play"
                    iconSize: 12
                    text: qsTr("Meghallgatom")
                    enabled: root.tab.canPlay
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.tab.playLine(line.model.startMs, line.model.endMs)
                    }
                }
                TButton {
                    focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
                    objectName: "lineLikely"
                    visible: line.likelyName !== ""
                    size: "small"
                    variant: "primary"
                    text: qsTr("%1 mondta").arg(line.likelyName)
                    toolTipText: qsTr("A sor átkerül hozzá: %1 (1)").arg(line.likelyName)
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.vm.moveUtteranceToSpeaker(line.utteranceId, line.likelyKey)
                    }
                }
                TButton {
                    focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
                    objectName: "lineConfirm"
                    size: "small"
                    text: qsTr("Jó így")
                    toolTipText: qsTr("A beszélő rendben van: a sor többé nem kétes")
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.vm.confirmUtterance(line.utteranceId, false)
                    }
                }
                TButton {
                    focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
                    objectName: "lineConfirmNoisy"
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Jó így, de nem minta")
                    toolTipText: qsTr("Jó így, de ne használd mintának: a beszélő rendben van, de egymásra beszéltek, ezért a sor hangja nem lesz minta")
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.vm.confirmUtterance(line.utteranceId, true)
                    }
                }
                TButton {
                    focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
                    id: fixButton
                    objectName: "lineFix"
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Más…")
                    toolTipText: qsTr("Csak ez a sor kerül át ahhoz, akit választasz")
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.lineFixRequested(line.utteranceId, fixButton)
                    }
                }
            }
        }
    }

    // Bekapcsoláskor a nézet tetejéről (a szennyezett-mag sávjával) indul.
    onVisibleChanged: if (visible) Qt.callLater(() => list.positionViewAtBeginning())

    ListView {
        id: list
        objectName: "reviewList"
        anchors.fill: parent
        clip: true
        model: root.vm.reviewGroups
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 600
        T.ScrollBar.vertical: TScrollBar {}
        header: Item {
            width: list.width
            height: banner.visible ? banner.height + 12 : 4
            ContaminatedCoreBanner {
                id: banner
                objectName: "contaminatedBanner"
                visible: (root.vm.contaminated.speakerKey || "") !== ""
                x: 24
                y: 10
                width: parent.width - 44
                info: root.vm.contaminated
                onSplitRequested: key => root.splitRequested(key)
                onWhyRequested: (key, anchor) => root.speakerWhyRequested(key, anchor)
            }
        }
        footer: Item { width: 1; height: 16 }
        delegate: Item {
            id: delegate
            required property var model
            required property int index
            readonly property string kind: model.rowKind
            readonly property var lineItem: kind === "line" ? loader.item : null
            width: list.width
            height: loader.item ? loader.item.height : 0
            Loader {
                id: loader
                width: parent.width
                sourceComponent: delegate.kind === "group" ? groupComponent
                               : delegate.kind === "line" ? lineComponent : moreComponent
            }
            Component {
                id: groupComponent
                Item {
                    width: delegate.width
                    height: card.height + 10
                    ReviewGroupCard {
                        id: card
                        objectName: "reviewGroup"
                        x: 24
                        y: 10
                        width: parent.width - 44
                        groupId: delegate.model.groupId
                        iconName: delegate.model.iconName
                        title: delegate.model.title
                        subtitle: delegate.model.subtitle
                        lineCount: delegate.model.lineCount
                        evidence: delegate.model.evidence
                        proposedName: delegate.model.proposedName
                        actionable: delegate.model.actionable
                        canApply: delegate.model.canApply
                        expanded: delegate.model.expanded
                        active: delegate.model.active
                        needsAz: root.vm.needsAz(delegate.model.lineCount)
                        onApplyRequested: root.vm.applyReviewGroup(groupId)
                        onExpandToggled: root.vm.reviewGroups.setExpanded(groupId, !expanded)
                        onSkipRequested: root.vm.skipReviewGroup(groupId)
                    }
                }
            }
            Component {
                id: lineComponent
                LineRow { model: delegate.model }
            }
            Component {
                id: moreComponent
                Item {
                    width: delegate.width
                    height: 28
                    Row {
                        x: 24 + 54 + 92
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        TLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("··· %n sor ebben a csoportban", "", delegate.model.lineCount)
                            mono: true
                            muted: true
                            font.pixelSize: Theme.fontCaption
                        }
                        TLabel {
                            objectName: "reviewMore"
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Kinyitás")
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall - 0.5
                            font.weight: Theme.weightMedium
                            font.underline: moreHover.hovered
                            HoverHandler { id: moreHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.vm.reviewGroups.setExpanded(delegate.model.groupId, true) }
                        }
                    }
                }
            }
        }
    }

    // A háttér-elemzés fut: finom jelzés (a csoportok utána frissülnek).
    Row {
        objectName: "reviewRunning"
        visible: root.vm.reviewRunning
        anchors.right: parent.right
        anchors.rightMargin: 24
        y: 4
        spacing: 6
        TSpinner { anchors.verticalCenter: parent.verticalCenter; size: 12; color: Theme.textMuted }
        TLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Átnézendők frissítése…")
            muted: true
            font.pixelSize: Theme.fontCaption
        }
    }

    // Nincs átnézendő sor (innen is kérhető az újraellenőrzés).
    Column {
        visible: list.count === 0 && (root.vm.contaminated.speakerKey || "") === ""
        anchors.centerIn: parent
        spacing: Theme.space3
        TLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.vm.reviewRunning ? qsTr("Az átnézendő sorok keresése…")
                                        : qsTr("Nincs átnézendő sor — minden megszólalás beszélője rendben van.")
            muted: true
        }
        TButton {
            focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
            objectName: "emptyFilterRecheck"
            visible: root.vm.voiceAvailable && !root.vm.embeddingRunning && !root.vm.reviewRunning
            anchors.horizontalCenter: parent.horizontalCenter
            size: "small"
            variant: "ghost"
            iconName: "refresh-cw"
            text: qsTr("Beszélők újraellenőrzése…")
            toolTipText: root.vm.canRecheck ? qsTr("A megerősített és javított sorok hangjához mérem a többi sort")
                                            : root.vm.recheckBlocker
            onClicked: root.tab.recheckSpeakers()
        }
    }
}
