import QtQuick
import QtQuick.Templates as T

// "To review" view: doubtful utterances grouped by reason, one decision per group (and one
// undo step). On top the contaminated-core banner (ContaminatedCoreBanner); below it the
// group cards (ReviewGroupCard); the lines of an expanded ("One by one") group sit under the
// card, per line: Listen - "X said" (1) - Fine as is - Fine, but not a sample - Other...;
// under the active collapsed group "... N lines in this group - Expand". A decided line stays
// in place while its group is open. B / Shift+B step through the group lines (TranscriptTab).
// List model: editorVm.reviewGroups.
// Shift = "not a sample" everywhere (rail click / drag, "X said", "Fine as is", 1, J): while
// Shift is held the labels get a "- not a sample" suffix; "Not identifiable" (0) is a row
// button; a decided row shows a marker: fixed / fine / not identifiable + sample mark.
Item {
    id: root

    required property var vm                // TranscriptEditorViewModel
    required property var tab               // TranscriptTab (playback, panels)
    // The row selected by keyboard / click (row ID); "" = none.
    property string currentUtterance: ""
    readonly property alias list: list
    // The speaker rail (Fix view's, tab.railX / railWidth) beside the rows: cards, selection
    // frame and text start at the rail's right edge; without a rail the old margins apply.
    readonly property bool railShown: tab.railShown
    readonly property real contentX: railShown ? tab.textX + 8 : 24
    readonly property real contentRight: 20
    // Drag on the rail (one row): the grabbed row and target column; the cell and the floating
    // label draw from this.
    property string dragUtterance: ""
    property bool dragActive: false
    property int dragLane: -1
    // Hover on the rail (another column of a row): same preview as while dragging (target cell in
    // the speaker's color and a "-> Name" label), so it is clear where a click would put the row.
    property string hoverUtterance: ""
    property int hoverLane: -1
    readonly property int previewLane: dragActive ? dragLane : hoverLane
    readonly property int previewColorIndex: previewLane >= 0 && previewLane < tab.laneCount ? vm.lanes[previewLane].colorIndex : 0

    // The key's column on the rail (-1 = no visible column).
    function laneOfKey(key) {
        if (!key) return -1
        const lanes = vm.lanes
        for (let i = 0; i < lanes.length; ++i) if (lanes[i].key === key) return i
        return -1
    }
    // Click on a rail column next to a row (modeled on the Fix view's rail click):
    //  own column -> select the row; another column -> move the row to that speaker;
    //  "+N" -> toggle the collapsed columns; "+" -> the row's person picker.
    function railClicked(utteranceId, ownLane, lane, anchor, noisy) {
        if (utteranceId === "") return false
        root.currentUtterance = utteranceId
        if (lane === -9) return false
        if (lane === ownLane) return true
        if (lane >= 0) {
            const key = lane < vm.lanes.length ? vm.lanes[lane].key : ""
            return key !== "" && vm.moveUtteranceToSpeaker(utteranceId, key, noisy === true)
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

    // Select the row and scroll to it (B / Shift+B).
    function step(direction) {
        let from = currentUtterance !== "" ? vm.reviewGroups.rowOfLine(currentUtterance) : -1
        if (from < 0) from = list.indexAt(list.width / 2, list.contentY + 1)
        const row = vm.reviewGroups.stepLine(from, direction)
        if (row < 0) return false
        currentUtterance = vm.reviewGroups.utteranceAt(row)
        Qt.callLater(() => list.positionViewAtIndex(row, ListView.Contain))
        return true
    }
    // Assign the row to the suggested speaker: by key to the meeting's speaker; without a key (the group
    // only suggests a name, e.g. a person outside the meeting) to the person.
    function moveToLikely(utteranceId, likelyKey, likelyName, noisy) {
        if (likelyKey !== "") return vm.moveUtteranceToSpeaker(utteranceId, likelyKey, noisy === true)
        if (likelyName !== "") return vm.moveUtteranceToPerson(utteranceId, likelyName, noisy === true)
        return false
    }
    function currentLine() {
        const row = currentUtterance !== "" ? vm.reviewGroups.rowOfLine(currentUtterance) : -1
        const item = row >= 0 ? list.itemAtIndex(row) : null
        return item && item.lineItem ? item.lineItem : null
    }
    // "1" (Shift+1: not a sample): the selected row goes to the suggested speaker.
    function acceptCurrent(noisy) {
        const li = currentLine()
        if (li && li.likelyName !== "" && !li.decided)
            return moveToLikely(currentUtterance, li.likelyKey, li.likelyName, noisy)
        return false
    }
    // "J" (Shift+J: not a sample): the selected row is "Fine as is".
    function confirmCurrent(noisy) {
        const li = currentLine()
        return !!li && !li.decided && vm.confirmUtterance(currentUtterance, noisy === true)
    }
    // "M": the selected row's panel ("Other...").
    function fixCurrent() {
        const li = currentLine()
        if (!li) return false
        root.lineFixRequested(currentUtterance, li.fixAnchor)
        return true
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
        readonly property bool dragging: root.dragActive && root.dragUtterance === utteranceId
        readonly property bool previewing: dragging || (!root.dragActive && root.hoverUtterance === utteranceId)
        readonly property alias railCells: railLoader
        readonly property alias fixAnchor: fixButton
        readonly property bool ignored: model.ignored === true
        readonly property bool nonSample: root.tab.shiftHeld
        width: list.width
        height: lineColumn.implicitHeight + 16

        // The slice of the rail for this row (the railArea above the list handles the click).
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
                dimmed: line.previewing && root.previewLane >= 0 && root.previewLane !== line.lane
                targetLane: line.previewing && root.previewLane !== line.lane ? root.previewLane : -1
                targetDashed: root.tab.shiftHeld
                targetLineColor: Theme.speakerLine(root.previewColorIndex)
                targetSoftColor: Theme.speakerSoft(root.previewColorIndex)
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
                    kind: line.model.decidedKind === "ignored" ? "ignored"
                        : line.model.decidedKind === "corrected" ? "corrected" : "confirmed"
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: line.decided && line.model.decidedKind === "confirmed"
                    text: line.model.decidedText
                    muted: true
                    font.pixelSize: Theme.fontMicro
                }
                TranscriptMarker {
                    objectName: "reviewSample"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: line.decided && (line.model.sampleState || "") !== ""
                    kind: line.model.sampleState === "sample" ? "sample" : "noSample"
                    toolTipText: line.model.sampleState === "sample" ? qsTr("Hangminta lett")
                                                                     : qsTr("Nem minta: a hangját nem használom")
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
                    focusPolicy: Qt.TabFocus     // clicks leave focus with the editor (B, 1, Ctrl+Z)
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
                    focusPolicy: Qt.TabFocus     // clicks leave focus with the editor (B, 1, Ctrl+Z)
                    objectName: "lineLikely"
                    visible: line.likelyName !== ""
                    size: "small"
                    variant: "primary"
                    text: qsTr("%1 mondta").arg(line.likelyName) + (line.nonSample ? root.tab.nonSampleSuffix : "")
                    toolTipText: qsTr("A sor átkerül hozzá: %1 (1; Shift: nem minta)").arg(line.likelyName)
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.moveToLikely(line.utteranceId, line.likelyKey, line.likelyName, root.tab.shiftHeld)
                    }
                }
                TButton {
                    focusPolicy: Qt.TabFocus     // clicks leave focus with the editor (B, 1, Ctrl+Z)
                    objectName: "lineConfirm"
                    size: "small"
                    text: qsTr("Jó így") + (line.nonSample ? root.tab.nonSampleSuffix : "")
                    toolTipText: qsTr("A beszélő rendben van: a sor többé nem kétes (J; Shift: nem minta)")
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.vm.confirmUtterance(line.utteranceId, root.tab.shiftHeld)
                    }
                }
                TButton {
                    focusPolicy: Qt.TabFocus     // clicks leave focus with the editor (B, 1, Ctrl+Z)
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
                    focusPolicy: Qt.TabFocus     // clicks leave focus with the editor (B, 1, Ctrl+Z)
                    objectName: "lineIgnore"
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Nem azonosítható")
                    toolTipText: qsTr("Nem tudni, ki mondta: a név marad, de a hangját semmire nem használom (0)")
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.vm.setUtteranceIgnored(line.utteranceId, true)
                    }
                }
                TButton {
                    focusPolicy: Qt.TabFocus     // clicks leave focus with the editor (B, 1, Ctrl+Z)
                    id: fixButton
                    objectName: "lineFix"
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Más…")
                    toolTipText: qsTr("Csak ez a sor kerül át ahhoz, akit választasz (M)")
                    onClicked: {
                        root.currentUtterance = line.utteranceId
                        root.lineFixRequested(line.utteranceId, fixButton)
                    }
                }
            }
        }
    }

    // When turned on, the view starts at the top (with the contaminated-core banner).
    // The delayed call may outlive the view (demo state change, test): guard.
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
                        newSpeakerSide: delegate.model.newSpeakerSide || ""
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

    // Rail click and drag in one layer above the rows (modeled on the Fix view's railArea).
    // Click: railClicked. Drag (one row): the grabbed row's cell shows the target column with a
    // floating "-> Name" label; on release the row moves there. Neutral on group and "more"
    // rows: the click passes through to the card.
    MouseArea {
        id: railArea
        objectName: "reviewRailArea"
        visible: root.railShown
        x: root.tab.railX
        width: root.tab.railWidth
        height: list.height
        z: 2
        preventStealing: true
        hoverEnabled: true
        cursorShape: root.hoverLane >= 0 || root.dragActive ? Qt.PointingHandCursor : Qt.ArrowCursor

        property var pressItem: null
        property real pressX: 0
        property real pressY: 0

        function lineAt(y) {
            const row = list.indexAt(list.width / 2, list.contentY + Math.max(1, Math.min(list.height - 2, y)))
            const item = row >= 0 ? list.itemAtIndex(row) : null
            return item && item.lineItem ? item.lineItem : null
        }
        function resetDrag() {
            root.dragActive = false
            root.dragUtterance = ""
            root.dragLane = -1
            pressItem = null
        }
        function placeChip(x, y) {
            dragChip.x = Math.min(list.width - dragChip.width - 12, railArea.x + x + 14)
            dragChip.y = Math.max(2, Math.min(list.height - dragChip.height - 2, y - 26))
        }
        function updateDrag(x, y) {
            const lane = root.tab.railLaneAt(x)
            root.dragLane = lane >= 0 ? lane : -1
            railArea.placeChip(x, y)
        }
        // Hover: the row and the other column (no preview over its own column / the card).
        function updateHover(x, y) {
            const li = railArea.lineAt(y)
            const lane = li ? root.tab.railLaneAt(x) : -9
            root.hoverUtterance = li ? li.utteranceId : ""
            root.hoverLane = li && lane >= 0 && lane !== li.lane ? lane : -1
            if (root.hoverLane >= 0) railArea.placeChip(x, y)
        }
        function clearHover() {
            root.hoverUtterance = ""
            root.hoverLane = -1
        }

        onPressed: mouse => {
            pressItem = railArea.lineAt(mouse.y)
            if (!pressItem) { mouse.accepted = false; return }
            pressX = mouse.x
            pressY = mouse.y
        }
        onPositionChanged: mouse => {
            root.tab.shiftHeld = (mouse.modifiers & Qt.ShiftModifier) !== 0
            if (!pressItem) { railArea.updateHover(mouse.x, mouse.y); return }
            if (!root.dragActive) {
                if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) < 7) return
                railArea.clearHover()
                root.dragUtterance = pressItem.utteranceId
                root.dragActive = true
            }
            railArea.updateDrag(mouse.x, mouse.y)
        }
        onExited: railArea.clearHover()
        onReleased: mouse => {
            if (!pressItem) return
            const li = pressItem
            const noisy = (mouse.modifiers & Qt.ShiftModifier) !== 0     // Shift = not a sample
            root.tab.forceActiveFocus()
            if (root.dragActive) {
                const lane = root.dragLane
                railArea.resetDrag()
                if (lane >= 0 && lane !== li.lane) root.railClicked(li.utteranceId, li.lane, lane, li.railCells, noisy)
            } else {
                railArea.resetDrag()
                root.railClicked(li.utteranceId, li.lane, root.tab.railLaneAt(mouse.x), li.railCells, noisy)
            }
            // After reassignment the row is elsewhere: the preview follows the current mouse position.
            Qt.callLater(() => { if (railArea.containsMouse) railArea.updateHover(railArea.mouseX, railArea.mouseY) })
        }
        onCanceled: railArea.resetDrag()
    }

    // Floating label while dragging and hovering: "-> Name" (modeled on the Fix view's dragChip).
    Rectangle {
        id: dragChip
        objectName: "reviewDragChip"
        visible: root.previewLane >= 0 && (root.dragActive || root.hoverUtterance !== "")
        readonly property bool nonSample: root.tab.shiftHeld
        z: 3
        width: dragLabel.implicitWidth + 18
        height: dragLabel.implicitHeight + 6
        radius: 10
        color: nonSample ? Theme.raised : Theme.accent
        TDashedRect {
            anchors.fill: parent
            visible: dragChip.nonSample
            radius: parent.radius
            color: Theme.accent
            lineWidth: 1.5
        }
        TLabel {
            id: dragLabel
            anchors.centerIn: parent
            text: root.previewLane >= 0 && root.previewLane < root.tab.laneCount
                  ? "→ " + root.vm.lanes[root.previewLane].name + (dragChip.nonSample ? root.tab.nonSampleSuffix : "") : ""
            color: dragChip.nonSample ? Theme.accent : Theme.textOnAccent
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
        }
    }

    // Background analysis is running: a subtle indicator (the groups refresh afterward).
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

    // Nothing to review (re-check can be requested from here too).
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
            focusPolicy: Qt.TabFocus     // clicks leave focus with the editor (B, 1, Ctrl+Z)
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
