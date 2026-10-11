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
    // A beszélő-sín (a Javítás nézetéé, tab.railX / railWidth) a sorok mellett: a kártyák, a
    // kijelölés-keret és a szöveg a sín jobb széle mellől indulnak; sín nélkül a régi margók.
    readonly property bool railShown: tab.railShown
    readonly property real contentX: railShown ? tab.textX + 8 : 24
    readonly property real contentRight: 20

    // A kulcs oszlopa a sínen (-1 = nincs látható oszlopa).
    function laneOfKey(key) {
        if (!key) return -1
        const lanes = vm.lanes
        for (let i = 0; i < lanes.length; ++i) if (lanes[i].key === key) return i
        return -1
    }
    // Kattintás a sín egy oszlopára egy sor mellett (a Javítás sín-kattintásának mintájára):
    //  saját oszlop → a sor kiválasztása; másik oszlop → a sor átkerül ahhoz a beszélőhöz;
    //  „+N" → az összecsukott oszlopok ki/be; „+" → a sor személyválasztója.
    function railClicked(utteranceId, ownLane, lane, anchor) {
        if (utteranceId === "") return false
        root.currentUtterance = utteranceId
        if (lane === -9) return false
        if (lane === ownLane) return true
        if (lane >= 0) {
            const key = lane < vm.lanes.length ? vm.lanes[lane].key : ""
            return key !== "" && vm.moveUtteranceToSpeaker(utteranceId, key)
        }
        if (lane === -1) {
            vm.lanesExpanded = !vm.lanesExpanded
            return true
        }
        if (lane === -3) {
            root.lineFixRequested(utteranceId, anchor)
            return true
        }
        return false
    }

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
    // A sor a javasolt beszélőhöz: kulccsal a meeting beszélőjéhez, kulcs nélkül (a csoport csak
    // nevet javasol, pl. a meetingen kívüli személyt) a személyhez.
    function moveToLikely(utteranceId, likelyKey, likelyName) {
        if (likelyKey !== "") return vm.moveUtteranceToSpeaker(utteranceId, likelyKey)
        if (likelyName !== "") return vm.moveUtteranceToPerson(utteranceId, likelyName)
        return false
    }
    // „1": a kiválasztott sor a javasolt beszélőhöz.
    function acceptCurrent() {
        const row = currentUtterance !== "" ? vm.reviewGroups.rowOfLine(currentUtterance) : -1
        const item = row >= 0 ? list.itemAtIndex(row) : null
        if (item && item.lineItem && item.lineItem.likelyName !== "" && !item.lineItem.decided)
            return moveToLikely(currentUtterance, item.lineItem.likelyKey, item.lineItem.likelyName)
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
        readonly property int lane: model.lane === undefined ? -2 : model.lane
        readonly property alias railCells: railLoader
        width: list.width
        height: lineColumn.implicitHeight + 16

        // A sín ezen sorra eső szelete (a kattintást a lista feletti railArea kezeli).
        Loader {
            id: railLoader
            objectName: "reviewRailCells"
            active: root.railShown
            x: root.tab.railX
            width: root.tab.railWidth
            height: line.height
            sourceComponent: SpeakerRailCells {
                laneCount: root.tab.laneCount
                hasGroup: root.tab.groupShown
                leftInset: 4
                laneWidth: Theme.laneWidth
                groupWidth: Theme.laneGroupWidth
                ownLane: line.lane
                lineColor: Theme.speakerLine(line.model.colorIndex)
                softColor: Theme.speakerSoft(line.model.colorIndex)
                uncertain: !line.decided
                selected: line.current
                suggestLane: line.decided ? -1 : root.laneOfKey(line.likelyKey)
                outlineColor: Theme.text
                dashColor: Theme.borderStrong
                accentColor: Theme.accent
            }
        }

        Rectangle {
            x: root.contentX
            width: parent.width - x - root.contentRight
            height: parent.height - 4
            y: 2
            radius: Theme.radiusControl
            visible: line.current
            color: Theme.raised
            border.width: 1.5
            border.color: Theme.accent
        }
        MouseArea {
            x: root.contentX
            width: parent.width - x
            height: parent.height
            onClicked: root.currentUtterance = line.utteranceId
            onDoubleClicked: root.tab.playFrom(line.model.startMs)
        }
        Column {
            id: lineColumn
            x: root.contentX + 42
            y: 8
            width: parent.width - x - root.contentRight - 12
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
                        root.moveToLikely(line.utteranceId, line.likelyKey, line.likelyName)
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
    // A késleltetett hívásra a nézet már meg is szűnhetett (demo-állapotváltás, teszt): őrzés.
    onVisibleChanged: if (visible) Qt.callLater(() => { if (list) list.positionViewAtBeginning() })

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
                x: root.contentX
                y: 10
                width: parent.width - x - root.contentRight
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
                        x: root.contentX
                        y: 10
                        width: parent.width - x - root.contentRight
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
                        x: root.contentX + 54 + 92
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

    // A sín kattintása egy rétegben a sorok felett (a Javítás railArea-jának mintájára; húzás nincs).
    // Csoport- és „további" sorokon semleges: a kattintás átmegy a kártyára.
    MouseArea {
        id: railArea
        objectName: "reviewRailArea"
        visible: root.railShown
        x: root.tab.railX
        width: root.tab.railWidth
        height: list.height
        z: 2
        onPressed: mouse => {
            const row = list.indexAt(list.width / 2, list.contentY + mouse.y)
            const item = row >= 0 ? list.itemAtIndex(row) : null
            if (!item || !item.lineItem) mouse.accepted = false
        }
        onClicked: mouse => {
            const row = list.indexAt(list.width / 2, list.contentY + mouse.y)
            const item = row >= 0 ? list.itemAtIndex(row) : null
            if (!item || !item.lineItem) return
            root.tab.forceActiveFocus()
            const li = item.lineItem
            root.railClicked(li.utteranceId, li.lane, root.tab.railLaneAt(mouse.x), li.railCells)
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
        objectName: "reviewEmpty"
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
