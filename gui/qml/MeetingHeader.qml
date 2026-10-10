import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Megbeszélés-fejléc, EGY sor (design/handoff-v3 „Header”, 1–3. döntés): 56 px, alsó vonal.
// Cím 19/600 + ceruza (✎ / F2 / dupla kattintás: helyben átnevezés) · függőleges elválasztó ·
// fülek „Áttekintés · Átirat · Vezetői összefoglaló · Memó” · … „…” (a Megbeszélés menü).
// A futó lépés a fülén látszik („Átirat 62%” pirula); a még nem elérhető fül halvány, és
// rámutatásra / kattintásra egy panel mondja meg, mi hiányzik, és felajánlja a műveletet.
// A régi meta-sor, a címkesor és a „Résztvevők azonosítása” gomb innen kikerült (Áttekintés,
// Megbeszélés menü).
// Önállóan (képernyőkép) kitalált minta: demoState = "" | "tabTooltip" (a Vezetői
// összefoglaló fül panelje nyitva, V4).
Item {
    id: root

    property var shell: null                 // ShellActions vagy null
    property var meeting: null               // ShellMeetingModel vagy null (önálló képernyőkép)
    property string meetingId: meeting ? meeting.meetingId : ""
    // 0 Áttekintés · 1 Átirat · 2 Vezetői összefoglaló · 3 Memó
    property int currentTab: 0
    property string demoState: ""

    // Az adatok a ShellMeetingModel-ből; demóban / képernyőképhez felülírhatók.
    property string title: meeting ? meeting.title : "Negyedéves partnertalálkozó"
    property bool hasTranscript: meeting ? meeting.hasTranscript : true
    property bool hasSummary: meeting ? meeting.hasSummary : false
    property bool summaryStale: meeting ? meeting.summaryStale : false
    property int transcribePercent: meeting ? meeting.transcribePercent : -1
    property int summarizePercent: meeting ? meeting.summarizePercent : -1
    property string providerLabel: meeting ? meeting.summaryProviderLabel : "LM Studio · saját kulcs"
    property bool canIdentify: meeting ? meeting.canIdentify && !meeting.identifyRunning : true
    readonly property alias tabRow: tabRow
    readonly property alias tabTip: tabTip

    signal tabRequested(int index)
    signal participantsRequested()

    function tabAvailable(index) {
        // Az Átirat az átírás közben is megnyitható (a fülön a haladás).
        if (index === 1) return root.hasTranscript || root.transcribePercent >= 0
        if (index >= 2) return root.hasSummary
        return true
    }
    function startRename() {
        titleEdit.text = root.title
        titleEdit.visible = true
        titleEdit.forceActiveFocus()
        titleEdit.selectAll()
    }
    function finishRename(commit) {
        if (!titleEdit.visible)
            return
        titleEdit.visible = false
        // A fókusz ne maradjon a (láthatatlan) mezőben: a Szóköz / nyilak oda mennének.
        if (titleEdit.activeFocus) root.forceActiveFocus()
        if (commit && root.shell && titleEdit.text.trim() !== "" && titleEdit.text.trim() !== root.title)
            root.shell.renameMeeting(root.meetingId, titleEdit.text)
    }
    // A nem elérhető fül panelje (kattintás / rámutatás).
    function openTabTip(index, tabItem, pinned) {
        if (!tabItem) return
        tabTip.tabIndex = index
        tabTip.pinned = pinned
        tabTip.open()
    }
    onMeetingIdChanged: { finishRename(false); tabTip.close() }
    Component.onCompleted: if (demoState === "tabTooltip") Qt.callLater(() => openTabTip(2, tabRepeater.itemAt(2), true))
    readonly property real tipAnchorX: {
        const tab = tabRepeater.count > tabTip.tabIndex ? tabRepeater.itemAt(tabTip.tabIndex) : null
        return tab ? headerRow.x + tabRow.x + tab.x : 0
    }

    implicitHeight: 56

    RowLayout {
        id: headerRow
        anchors { fill: parent; leftMargin: Theme.space5; rightMargin: Theme.space4 }
        spacing: 14

        // ---- cím + ceruza ----
        Item {
            id: titleBox
            Layout.fillHeight: true
            Layout.minimumWidth: 120
            Layout.maximumWidth: Math.max(160, root.width * 0.42)
            Layout.preferredWidth: titleEdit.visible ? Layout.maximumWidth : titleRow.implicitWidth

            Row {
                id: titleRow
                visible: !titleEdit.visible
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                TLabel {
                    id: titleLabel
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, titleBox.Layout.maximumWidth - 23)
                    text: root.title
                    font.pixelSize: 19
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                    TapHandler { onDoubleTapped: root.startRename() }
                    HoverHandler { id: titleHover }
                    TToolTip { visible: titleHover.hovered && titleLabel.truncated; text: root.title }
                }
                TIcon {
                    objectName: "renamePencil"
                    anchors.verticalCenter: parent.verticalCenter
                    name: "pencil"
                    size: 15
                    color: pencilHover.hovered ? Theme.text : Theme.textMuted
                    HoverHandler { id: pencilHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.startRename() }
                    TToolTip { visible: pencilHover.hovered; text: qsTr("Átnevezés (F2)") }
                }
            }
            TTextField {
                id: titleEdit
                objectName: "meetingTitleEdit"
                visible: false
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: -8 }
                leftPadding: 7
                implicitHeight: 32
                font.pixelSize: 19
                font.weight: Theme.weightSemiBold
                Accessible.name: qsTr("A megbeszélés címe")
                onAccepted: root.finishRename(true)
                Keys.onEscapePressed: root.finishRename(false)
                onActiveFocusChanged: if (!activeFocus) root.finishRename(true)
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.topMargin: 17
            Layout.bottomMargin: 17
            implicitWidth: 1
            color: Theme.border
        }

        // ---- fülek ----
        Row {
            id: tabRow
            objectName: "meetingTabs"
            Layout.fillHeight: true
            spacing: 2
            Repeater {
                id: tabRepeater
                model: [
                    { label: qsTr("Áttekintés"), name: "overview" },
                    { label: qsTr("Átirat"), name: "transcript" },
                    { label: qsTr("Vezetői összefoglaló"), name: "summary" },
                    { label: qsTr("Memó"), name: "memo" }
                ]
                T.AbstractButton {
                    id: tab
                    required property var modelData
                    required property int index
                    objectName: "tab_" + modelData.name
                    readonly property bool active: root.currentTab === index
                    readonly property bool available: root.tabAvailable(index)
                    readonly property string pill: index === 1 && root.transcribePercent >= 0 ? root.transcribePercent + "%"
                                                 : index === 2 && root.summarizePercent >= 0 ? root.summarizePercent + "%"
                                                 : ""
                    readonly property bool stalePill: index === 2 && root.summaryStale && pill === ""
                    height: tabRow.height
                    implicitWidth: tabContent.implicitWidth + leftPadding + rightPadding
                    leftPadding: 11; rightPadding: 11
                    hoverEnabled: true
                    activeFocusOnTab: true
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData.label
                    onHoveredChanged: {
                        if (hovered && !available) hoverOpen.restart()
                        else hoverOpen.stop()
                    }
                    onClicked: {
                        if (available) {
                            tabTip.close()
                            root.tabRequested(index)
                        } else {
                            root.openTabTip(index, tab, true)
                        }
                    }
                    Keys.onReturnPressed: clicked()
                    Timer {
                        id: hoverOpen
                        interval: 350
                        onTriggered: if (tab.hovered && !tabTip.opened) root.openTabTip(tab.index, tab, false)
                    }
                    contentItem: Item {
                        implicitWidth: tabContent.implicitWidth
                        Row {
                            id: tabContent
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 7
                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: tab.modelData.label
                                font.pixelSize: Theme.fontBody
                                font.weight: tab.active ? Theme.weightSemiBold : Theme.weightRegular
                                color: !tab.available ? Theme.borderStrong
                                     : tab.active || tab.hovered ? Theme.text : Theme.textMuted
                            }
                            Rectangle {
                                visible: tab.pill !== ""
                                anchors.verticalCenter: parent.verticalCenter
                                implicitWidth: pillText.implicitWidth + 10
                                implicitHeight: 18
                                radius: Theme.radiusTag
                                color: Theme.accentSoft
                                TLabel {
                                    id: pillText
                                    anchors.centerIn: parent
                                    text: tab.pill
                                    mono: true
                                    color: Theme.accent
                                    font.pixelSize: Theme.fontMicro
                                    font.weight: Theme.weightMedium
                                }
                            }
                            TPill {
                                visible: tab.stalePill
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("elavult")
                                tone: "warn"
                            }
                        }
                    }
                    background: Item {
                        Rectangle {
                            anchors.fill: parent
                            visible: tabTip.opened && tabTip.tabIndex === tab.index
                            color: Theme.alpha(Theme.stateLayer, Theme.hoverOpacity)
                        }
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 2
                            color: Theme.accent
                            visible: tab.active
                        }
                        TFocusRing { visible: tab.visualFocus; targetRadius: Theme.radiusControl }
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        TIconButton {
            id: moreButton
            objectName: "meetingMoreButton"
            iconName: "ellipsis"
            iconSize: 15
            toolTipText: qsTr("Megbeszélés: átnevezés, azonosítás, újra-átírás, export, törlés")
            down: pressed || moreMenu.visible
            onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height + 4)

            MeetingMenu {
                id: moreMenu
                shell: root.shell
                meetingId: root.meetingId
                hasTranscript: root.hasTranscript
                canIdentify: root.canIdentify
                onRenameRequested: root.startRename()
                onParticipantsRequested: root.participantsRequested()
            }
        }
    }

    TDivider { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } }

    // ---- a nem elérhető fül panelje (V4 jobb felső) ----
    TPopover {
        id: tabTip
        objectName: "tabTooltip"
        property int tabIndex: 2
        property bool pinned: false
        // A fül bal széle alatt, a fejléc alsó vonalára 8 px-re ráfutva (V4).
        x: Math.max(0, Math.min(root.width - width, root.tipAnchorX))
        y: root.height - 8
        width: 320
        padding: 14
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

        readonly property bool transcribing: root.transcribePercent >= 0
        readonly property bool summarizing: root.summarizePercent >= 0
        readonly property string tipTitle: tabIndex === 1 ? qsTr("Még nincs átirat")
                                         : tabIndex === 3 ? qsTr("Még nincs memó")
                                         : qsTr("Még nincs vezetői összefoglaló")
        readonly property string tipText: {
            if (tabIndex === 1)
                return transcribing ? qsTr("Az átírás fut (%1%). Ha elkészül, itt olvashatod és javíthatod.").arg(root.transcribePercent)
                                    : qsTr("A felvételt még nem írtuk át. Az átírás után itt olvashatod és javíthatod a beszélőket.")
            if (!root.hasTranscript)
                return qsTr("Az összefoglaló az átiratból készül: előbb az átírásnak kell elkészülnie.")
            if (summarizing)
                return qsTr("Készül az összefoglaló (%1%).").arg(root.summarizePercent)
            return qsTr("Az átirat kész. A gyors összefoglaló kb. 1 perc, a Memó ugyanabból készül.")
        }
        readonly property string actionText: tabIndex === 1 ? (transcribing ? "" : qsTr("Átírás indítása"))
                                           : root.hasTranscript && !summarizing ? qsTr("Összefoglaló készítése") : ""

        // Rámutatással nyitva: bezárul, ha sem a fül, sem a panel fölött nincs az egér.
        Timer {
            interval: 300
            repeat: true
            running: tabTip.opened && !tabTip.pinned
            onTriggered: {
                const tab = tabRepeater.itemAt(tabTip.tabIndex)
                if (!tipHover.hovered && !(tab && tab.hovered)) tabTip.close()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            HoverHandler { id: tipHover }
            TLabel {
                Layout.fillWidth: true
                text: tabTip.tipTitle
                font.pixelSize: Theme.fontBody
                font.weight: Theme.weightSemiBold
                wrapMode: Text.Wrap
            }
            TLabel {
                Layout.fillWidth: true
                text: tabTip.tipText
                muted: true
                font.pixelSize: 12
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
            RowLayout {
                visible: tabTip.actionText !== ""
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                TButton {
                    objectName: "tabTipAction"
                    text: tabTip.actionText
                    variant: "primary"
                    size: "small"
                    iconName: tabTip.tabIndex === 1 ? "file-text" : "sparkles"
                    onClicked: {
                        tabTip.close()
                        if (!root.shell) return
                        if (tabTip.tabIndex === 1) root.shell.startTranscription(root.meetingId)
                        else root.shell.startQuickSummary(root.meetingId)
                    }
                }
                TLabel {
                    visible: tabTip.tabIndex >= 2 && root.providerLabel !== ""
                    Layout.fillWidth: true
                    text: root.providerLabel
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                }
            }
            // A fül tartalma (témánkénti elemzés, megjegyzés) így is elérhető marad.
            TButton {
                objectName: "tabTipOpen"
                visible: tabTip.tabIndex >= 2 && root.hasTranscript
                Layout.leftMargin: -8
                text: qsTr("Más lehetőségek…")
                variant: "ghost"
                size: "small"
                onClicked: { tabTip.close(); root.tabRequested(tabTip.tabIndex) }
            }
        }
    }
}
