import QtQuick
import QtQuick.Templates as T

// A felvevő teljes tartalma (design/handoff-recorder, R01–R10) EGY elemben: saját címsor,
// cím-mező, indítás / felvétel-állapot, forrás-sorok vagy a kinyitott eszköz-lista, a
// „Vége a megbeszélésnek?” doboz, a bezárás-lap és a kész / nincs-eszköz állapot.
// Az ablakot (keret nélküli, mindig felül) a RecorderWindow adja; ez az elem képernyőképhez
// és beágyazáshoz önállóan is használható. Szélessége 380 px, a magassága a tartalmat követi.
//   RecorderView { vm: RecorderViewModel {} ; onHideRequested: … }
Item {
    id: root

    property var vm: null                    // RecorderViewModel
    property bool expanded: false            // kinyitott eszköz-lista (R02 / R04)
    property bool pinned: true               // „Mindig felül”
    property bool pinSupported: true         // hamis: a platform nem engedi (Wayland)
    property bool sheetOpen: false           // bezárás felvétel közben (R07)
    property bool editingTitle: false
    property bool closeAfterStop: false      // „Leállítás és bezárás” után a kész állapot zár

    signal moveRequested()                   // a címsor húzása
    signal pillRequested()
    signal hideRequested()                   // háttérbe (tálcára)
    signal closeRequested()                  // bezárás (csak ha nem fut felvétel)

    readonly property string st: vm ? vm.state : "idle"
    readonly property bool recording: st === "recording" || st === "stopping"

    implicitWidth: Theme.recorderWidth
    implicitHeight: Math.max(titleBar.height + body.implicitHeight + 24,
                             sheetOpen ? titleBar.height + sheet.implicitHeight + 48 : 0)
    width: implicitWidth
    height: implicitHeight

    // A ✕ gomb / ablak-bezárás: felvétel közben SOHA nem állít le — a lapot nyitja.
    function requestClose() {
        if (root.recording) root.sheetOpen = true
        else root.closeRequested()
    }
    function beginTitleEdit() {
        root.editingTitle = true
        titleInput.text = root.vm && !root.vm.titleAutomatic ? root.vm.title : ""
        titleInput.forceActiveFocus()
        titleInput.selectAll()
    }
    function commitTitleEdit() {
        if (!root.editingTitle) return
        root.editingTitle = false
        if (root.vm && titleInput.text.trim() !== "") root.vm.title = titleInput.text
    }

    // Címke-mező (Ctrl+T / „+ Címke”): a fókusz bezáráskor oda tér vissza, ahol volt.
    function beginTagInput(text) {
        if (!root.vm || !root.vm.tagsEditable) return
        if (!tagRow.adding) tagRow.returnFocus = root.Window.activeFocusItem
        tagRow.adding = true
        tagInput.open()
        if (text) tagInput.text = text          // képernyőkép (T08c): gépelés közben
    }
    function endTagInput() {
        if (!tagRow.adding) return
        tagRow.adding = false
        tagInput.close()
        tagInput.text = ""
        const back = tagRow.returnFocus
        tagRow.returnFocus = null
        if (back && back !== tagInput.field) back.forceActiveFocus()
    }

    TagInputModel {
        id: tagInputModel
        controller: root.vm ? root.vm.controller : null
        excludeIds: root.vm ? root.vm.tagIds : []
    }
    Connections {
        target: root.vm
        function onTagInputRequested() { root.beginTagInput() }
    }

    onStChanged: {
        if (root.vm && !root.vm.tagsEditable) root.endTagInput()
        if (!root.recording) root.sheetOpen = false
        if (st === "done" && root.closeAfterStop) {
            root.closeAfterStop = false
            root.closeRequested()
        }
    }

    Shortcut { sequence: "Ctrl+R"; enabled: root.st === "idle"; onActivated: root.vm.start() }
    Shortcut { sequence: "Ctrl+."; enabled: root.st === "recording"; onActivated: root.vm.stop() }
    Shortcut { sequence: "Escape"; enabled: root.sheetOpen; onActivated: root.sheetOpen = false }
    Shortcut { sequence: "Ctrl+T"; enabled: root.vm !== null && root.vm.tagsEditable && !root.sheetOpen; onActivated: root.vm.openTagInput() }

    component BarButton: T.Button {
        id: bb
        property string iconName: ""
        property string toolTipText: ""
        property bool active: false
        implicitWidth: 26
        implicitHeight: 24
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: toolTipText
        contentItem: Item {
            TIcon {
                anchors.centerIn: parent
                name: bb.iconName
                size: 14
                color: !bb.enabled ? Theme.borderStrong : bb.active ? Theme.accent : Theme.textMuted
            }
        }
        background: Rectangle {
            radius: 4
            color: bb.active ? Theme.accentSoft : "transparent"
            Rectangle {
                anchors.fill: parent
                radius: 4
                color: Theme.stateLayer
                opacity: !bb.enabled ? 0 : bb.down ? Theme.pressedOpacity * 1.5
                       : bb.hovered ? Theme.hoverOpacity * 1.5 : 0
            }
            TFocusRing { visible: bb.visualFocus; targetRadius: 4 }
        }
        TToolTip {
            y: bb.height + 6
            visible: bb.toolTipText !== "" && bb.hovered
            text: bb.toolTipText
        }
    }

    // ---- kártya ----
    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusDialog
        color: Theme.bg
        border.width: 1
        border.color: Theme.borderStrong
    }

    // ---- címsor (30 px) ----
    Item {
        id: titleBar
        x: 1; y: 1
        width: parent.width - 2
        height: 30

        Rectangle {                       // surface háttér, felül lekerekítve
            anchors.fill: parent
            radius: Theme.radiusDialog - 1
            color: Theme.surface
            Rectangle { x: 0; y: parent.height - parent.radius; width: parent.width; height: parent.radius; color: Theme.surface }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

        DragHandler {
            target: null
            onActiveChanged: if (active) root.moveRequested()
        }
        TapHandler { onDoubleTapped: root.expanded = !root.expanded }

        Rectangle {                       // állapot-pötty (felvételnél 3 px recLine gyűrűvel)
            id: dotRing
            x: 9 - (root.recording ? 3 : 0)
            anchors.verticalCenter: parent.verticalCenter
            width: 8 + (root.recording ? 6 : 0); height: width; radius: width / 2
            color: root.recording ? Theme.recLine : "transparent"
            Rectangle {
                anchors.centerIn: parent
                width: 8; height: 8; radius: 4
                color: root.recording ? Theme.rec : root.st === "done" ? Theme.accent : Theme.borderStrong
            }
        }
        TLabel {
            x: 26
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Tanara felvevő")
            muted: true
            font.pixelSize: 12
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 3
            anchors.verticalCenter: parent.verticalCenter
            BarButton {
                iconName: "pin"
                active: root.pinned && root.pinSupported
                enabled: root.pinSupported
                toolTipText: root.pinSupported ? qsTr("Mindig felül")
                    : qsTr("Mindig felül: ezen a rendszeren az ablakkezelő dönt (Wayland)")
                onClicked: root.pinned = !root.pinned
            }
            BarButton {
                iconName: root.expanded ? "chevrons-up" : "chevrons-down"
                enabled: root.st !== "done" && root.st !== "noDevice"
                toolTipText: root.expanded ? qsTr("Összecsukás") : qsTr("Kinyitás")
                onClicked: root.expanded = !root.expanded
            }
            BarButton {
                iconName: "minimize-2"
                toolTipText: qsTr("Pirula méret")
                onClicked: root.pillRequested()
            }
            BarButton {
                iconName: "arrow-down-to-line"
                toolTipText: qsTr("Háttérbe (tálcára)")
                onClicked: root.hideRequested()
            }
            BarButton {
                iconName: "x"
                toolTipText: qsTr("Bezárás")
                onClicked: root.requestClose()
            }
        }
    }

    // ---- törzs: 12 px belső margó, 10 px köz ----
    Column {
        id: body
        x: 12
        y: titleBar.y + titleBar.height + 12
        width: parent.width - 24
        spacing: 10

        // Hiba (indítás / sáv-hozzáadás nem sikerült)
        Rectangle {
            visible: root.vm && root.vm.errorText !== ""
            width: parent.width
            height: errRow.implicitHeight + 16
            radius: 6
            color: Theme.dangerSoft
            border.width: 1
            border.color: Theme.dangerLine
            Row {
                id: errRow
                x: 10; y: 8
                width: parent.width - 20
                spacing: 8
                TIcon { name: "circle-alert"; size: 15; color: Theme.dangerInk; y: 1 }
                TLabel {
                    width: parent.width - 15 - 8 - 24
                    text: root.vm ? root.vm.errorText : ""
                    color: Theme.dangerInk
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                BarButton { iconName: "x"; toolTipText: qsTr("Bezárás"); onClicked: root.vm.clearError(); y: -4 }
            }
        }

        // R06 — „Vége a megbeszélésnek?” (soha nem állít le magától)
        Rectangle {
            visible: root.vm && root.vm.askVisible
            width: parent.width
            height: askCol.implicitHeight + 20
            radius: 6
            color: Theme.warnSoft
            border.width: 1
            border.color: Theme.warnLine
            Column {
                id: askCol
                x: 12; y: 10
                width: parent.width - 24
                spacing: 8
                Row {
                    width: parent.width
                    spacing: 8
                    TIcon { name: "triangle-alert"; size: 15; color: Theme.warnInk; y: 2 }
                    Column {
                        width: parent.width - 23
                        spacing: 2
                        TLabel { text: qsTr("Vége a megbeszélésnek?"); font.weight: Theme.weightSemiBold }
                        TLabel {
                            width: parent.width
                            text: root.vm ? root.vm.askText : ""
                            font.pixelSize: 12
                            cssLineHeight: 1.4
                            wrapMode: Text.Wrap
                        }
                    }
                }
                Row {
                    x: 23
                    spacing: 6
                    RecorderButton {
                        implicitHeight: 28; leftPadding: 10; rightPadding: 10
                        text: qsTr("Folytatom")
                        onClicked: root.vm.continueRecording()
                    }
                    RecorderButton {
                        implicitHeight: 28; leftPadding: 10; rightPadding: 10
                        kind: "inverted"; mark: "square"
                        text: qsTr("Leállítom")
                        onClicked: root.vm.stop()
                    }
                }
            }
        }

        // Cím-mező (32 px) — minden állapotban szerkeszthető
        Column {
            width: parent.width
            spacing: 4
            Rectangle {
                id: titleField
                width: parent.width
                height: 32
                radius: 6
                color: root.editingTitle ? Theme.raised : Theme.surface
                border.width: 1
                border.color: root.editingTitle ? Theme.accent : Theme.border

                Rectangle {               // 3 px accentSoft gyűrű szerkesztéskor
                    visible: root.editingTitle
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: 9
                    color: "transparent"
                    border.width: 3
                    border.color: Theme.accentSoft
                    z: -1
                }
                TLabel {
                    id: titleText
                    visible: !root.editingTitle
                    x: 10
                    width: parent.width - 10 - 8 - 14 - 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.vm ? root.vm.title : ""
                    color: root.vm && root.vm.titleAutomatic ? Theme.textMuted : Theme.text
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                TextInput {
                    id: titleInput
                    visible: root.editingTitle
                    x: 10
                    width: titleText.width
                    anchors.verticalCenter: parent.verticalCenter
                    clip: true
                    color: Theme.text
                    selectionColor: Theme.accentSoft
                    selectedTextColor: Theme.text
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.fontBody
                    font.weight: Theme.weightSemiBold
                    selectByMouse: true
                    onAccepted: root.commitTitleEdit()
                    onActiveFocusChanged: if (!activeFocus && root.editingTitle && Window.active) root.commitTitleEdit()
                    Keys.onEscapePressed: root.editingTitle = false
                }
                TIcon {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    name: "pencil"; size: 14; color: Theme.textMuted
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !root.editingTitle
                    cursorShape: Qt.IBeamCursor
                    onClicked: root.beginTitleEdit()
                }
            }
            TLabel {
                visible: root.vm && root.vm.titleAutomatic && root.st === "idle" && !root.editingTitle
                x: 2
                text: qsTr("Automatikus név · kattints az átnevezéshez")
                muted: true
                font.pixelSize: 11
            }
        }

        // Címkék (C06, T08a–c): kompakt chipek (× mindig látszik) + „+ Címke  Ctrl+T”. Indítás
        // előtt és felvétel közben (összecsukva és kinyitva is) szerkeszthető; a pirulában nincs.
        Flow {
            id: tagRow
            objectName: "recorderTagRow"
            property bool adding: false
            property Item returnFocus: null
            visible: root.vm !== null && root.vm.tagsEditable && root.st !== "noDevice"
            width: parent.width
            spacing: 6

            Repeater {
                model: root.vm ? root.vm.tags : []
                TagChip {
                    required property var modelData
                    compact: true
                    text: modelData.name
                    removable: true
                    removeAlwaysVisible: true
                    onRemoveRequested: root.vm.removeTag(modelData.id)
                }
            }
            T.AbstractButton {
                id: tagAddBtn
                objectName: "recorderTagAdd"
                visible: !tagRow.adding
                implicitWidth: tagAddRow.implicitWidth + 12
                implicitHeight: 22
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Címke hozzáadása")
                onClicked: root.vm.openTagInput()
                Keys.onReturnPressed: click()
                Keys.onEnterPressed: click()
                contentItem: Item {
                    Row {
                        id: tagAddRow
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5
                        TIcon { name: "plus"; size: 13; color: Theme.textMuted; anchors.verticalCenter: parent.verticalCenter }
                        TLabel {
                            text: qsTr("Címke")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            font.weight: Theme.weightMedium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        TLabel {
                            leftPadding: 3
                            text: "Ctrl+T"
                            mono: true; muted: true
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                background: Rectangle {
                    radius: Theme.radiusTag
                    color: Theme.stateLayer
                    opacity: tagAddBtn.down ? Theme.pressedOpacity : tagAddBtn.hovered ? Theme.hoverOpacity : 0
                    TFocusRing { visible: tagAddBtn.visualFocus; targetRadius: Theme.radiusTag }
                }
            }
            TagInput {
                id: tagInput
                objectName: "recorderTagInput"
                visible: tagRow.adding
                compact: true
                width: 150
                height: 22
                model: tagInputModel
                placeholderText: qsTr("Címke…")
                onTagChosen: (name, isNew) => { root.vm.addTag(name); root.endTagInput() }
                onBackspaceOnEmpty: {
                    const t = root.vm ? root.vm.tags : []
                    if (t.length > 0) root.vm.removeTag(t[t.length - 1].id)
                }
                onClosed: root.endTagInput()
            }
            Connections {
                target: tagInput.field
                // Máshová kattintva (üres mezővel) a gomb tér vissza.
                function onActiveFocusChanged() {
                    if (!tagInput.field.activeFocus && tagRow.adding && tagInput.text === "") root.endTagInput()
                }
            }
        }

        // Indítás (R01/R02)
        RecorderButton {
            visible: root.st === "idle"
            width: parent.width
            implicitHeight: 42
            radius: 7
            kind: "record"; mark: "dot"; markSize: 12; markColor: "#ffffff"
            fontSize: 15
            text: qsTr("Felvétel indítása")
            hint: "Ctrl+R"
            enabled: root.vm ? root.vm.canStart : false
            toolTipText: enabled ? "" : qsTr("Kapcsolj be legalább egy forrást")
            onClicked: root.vm.start()
        }

        // Felvétel-állapot + Leállítás (R03/R04). A piros rész jelző, nem gomb.
        Item {
            visible: root.recording
            width: parent.width
            height: 42
            Rectangle {
                width: parent.width - stopBtn.width - 12
                height: 42
                radius: 7
                color: Theme.recSoft
                border.width: 1
                border.color: Theme.recLine
                Rectangle {               // 12 px pötty 4 px recLine gyűrűvel (1,2 mp lüktetés)
                    id: recRing
                    x: 12 - 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20; radius: 10
                    color: Theme.recLine
                    SequentialAnimation on opacity {
                        running: root.st === "recording" && !App.demo && root.visible
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { from: 1; to: 0.25; duration: 600 }
                        NumberAnimation { from: 0.25; to: 1; duration: 600 }
                    }
                }
                Rectangle {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12; height: 12; radius: 6
                    color: Theme.rec
                }
                TLabel {
                    x: 34
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.st === "stopping" ? qsTr("LEZÁRÁS…") : qsTr("FELVÉTEL")
                    color: Theme.recInk
                    font.pixelSize: 12
                    font.weight: Theme.weightBold
                    font.letterSpacing: 0.96
                }
                TLabel {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.vm ? root.vm.elapsedText : "00:00:00"
                    mono: true
                    font.pixelSize: 22
                    font.weight: Theme.weightMedium
                    font.letterSpacing: -0.44
                }
            }
            RecorderButton {
                id: stopBtn
                anchors.right: parent.right
                implicitHeight: 42
                leftPadding: 14; rightPadding: 14
                radius: 7
                kind: "inverted"; mark: "square"; markSize: 11
                fontSize: 14
                text: qsTr("Leállítás")
                toolTipText: "Ctrl+."
                enabled: root.st === "recording"     // a sávfájlok lezárása alatt rövid időre tiltva
                onClicked: root.vm.stop()
            }
        }

        // R09 — kész
        Rectangle {
            visible: root.st === "done"
            width: parent.width
            height: doneCol.implicitHeight + 20
            radius: 7
            readonly property bool problem: root.vm && root.vm.doneProblem !== ""
            color: problem ? Theme.warnSoft : Theme.successSoft
            border.width: 1
            border.color: problem ? Theme.warnLine : Theme.successLine
            Column {
                id: doneCol
                x: 12; y: 10
                width: parent.width - 24
                spacing: 10
                Item {
                    width: parent.width
                    height: 18
                    TIcon {
                        id: doneIcon
                        anchors.verticalCenter: parent.verticalCenter
                        name: parent.parent.parent.problem ? "triangle-alert" : "check"
                        size: 16
                        color: parent.parent.parent.problem ? Theme.warnInk : Theme.successInk
                    }
                    TLabel {
                        x: 24
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.parent.parent.problem ? qsTr("Elmentve, de hiányosan") : qsTr("Elmentve")
                        font.weight: Theme.weightSemiBold
                    }
                    TLabel {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.vm ? root.vm.doneSummary : ""
                        mono: true; muted: true
                        font.pixelSize: 12
                    }
                }
                TLabel {
                    visible: parent.parent.problem
                    width: parent.width
                    text: root.vm ? root.vm.doneProblem : ""
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                Item {
                    width: parent.width
                    height: 32
                    RecorderButton {
                        width: parent.width - newBtn.width - 6
                        kind: "primary"
                        text: qsTr("Megnyitás az elemzőben")
                        onClicked: root.vm.openInAnalyzer()
                    }
                    RecorderButton {
                        id: newBtn
                        anchors.right: parent.right
                        mark: "dot"; markSize: 9; markColor: Theme.rec
                        fontWeight: Theme.weightMedium
                        text: qsTr("Új felvétel")
                        onClicked: root.vm.newRecording()
                    }
                }
            }
        }

        // R10 — nincs hangeszköz
        Column {
            visible: root.st === "noDevice"
            width: parent.width
            spacing: 0
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Item { width: 1; height: 18 }
            TIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "mic-off"; size: 20; color: Theme.textMuted }
            Item { width: 1; height: 8 }
            TLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Nem találok hangeszközt")
                font.weight: Theme.weightSemiBold
            }
            Item { width: 1; height: 8 }
            TLabel {
                x: 12
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                text: Qt.platform.os === "linux"
                    ? qsTr("Csatlakoztass mikrofont vagy fejhallgatót. Linuxon ellenőrizd, hogy fut-e a PipeWire vagy a PulseAudio.")
                    : qsTr("Csatlakoztass mikrofont vagy fejhallgatót, és ellenőrizd a rendszer hangbeállításait.")
                muted: true
                font.pixelSize: 12
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
            Item { width: 1; height: 12 }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6
                RecorderButton {
                    implicitHeight: 30
                    iconName: "rotate-ccw"; iconSize: 13
                    fontWeight: Theme.weightMedium
                    text: qsTr("Újrakeresés")
                    onClicked: root.vm.rescan()
                }
                RecorderButton {
                    implicitHeight: 30
                    kind: "ghost"
                    fontWeight: Theme.weightMedium
                    text: qsTr("Rögzítés beállításai")
                    onClicked: root.vm.openSettings()
                }
            }
            Item { width: 1; height: 0 }
        }

        // Forrás-sorok (összecsukott nézet): csak a kiválasztott / rögzített eszközök
        Column {
            visible: !root.expanded && (root.st === "idle" || root.recording)
            width: parent.width
            spacing: 2
            Repeater {
                model: root.vm ? root.vm.devices : null
                Item {
                    id: line
                    required property string name
                    required property string iconName
                    required property bool selected
                    required property int level
                    required property int peak
                    required property string status
                    required property string statusText
                    visible: selected
                    width: parent.width
                    height: visible ? 26 : 0
                    TIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: line.iconName; size: 14; color: Theme.textMuted
                    }
                    TLabel {
                        x: 22
                        width: parent.width - 22 - 8 - lineMeter.width - (linePill.visible ? linePill.width + 8 : 0)
                        anchors.verticalCenter: parent.verticalCenter
                        text: line.name
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: linePill
                        visible: line.status === "silentWarn" || line.status === "disconnected"
                        anchors.right: lineMeter.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: pillText.implicitWidth + 12
                        height: pillText.implicitHeight + 2
                        radius: 8
                        color: line.status === "disconnected" ? Theme.dangerSoft : Theme.warnSoft
                        TLabel {
                            id: pillText
                            anchors.centerIn: parent
                            text: line.statusText
                            color: line.status === "disconnected" ? Theme.dangerInk : Theme.warnInk
                            font.pixelSize: 11
                            font.weight: Theme.weightSemiBold
                        }
                    }
                    VuMeter {
                        id: lineMeter
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        level: line.level; peak: line.peak
                    }
                }
            }
        }

        // Lábléc (összecsukva): „3 / 6 forrás rögzül” · „Források ▾”
        Item {
            visible: !root.expanded && (root.st === "idle" || root.recording)
            width: parent.width
            height: 30 - 10 + 8          // a törzs alsó 12 px-es margójába lóg, mint a designban
            Rectangle { x: -4; width: parent.width + 8; height: 1; color: Theme.border }
            TLabel {
                x: 4
                y: 1 + (30 - height) / 2
                text: root.recording ? qsTr("%n sáv", "", root.vm ? root.vm.trackCount : 0)
                    : qsTr("%1 / %2 forrás rögzül").arg(root.vm ? root.vm.selectedCount : 0).arg(root.vm ? root.vm.deviceCount : 0)
                muted: true
                font.pixelSize: 12
            }
            T.Button {
                id: srcBtn
                anchors.right: parent.right
                y: 1 + (30 - height) / 2
                implicitWidth: srcRow.implicitWidth + 8
                implicitHeight: 24
                Accessible.name: qsTr("Források")
                onClicked: root.expanded = true
                contentItem: Item {
                    Row {
                        id: srcRow
                        anchors.centerIn: parent
                        spacing: 4
                        TLabel { text: qsTr("Források"); font.pixelSize: 12; font.weight: Theme.weightMedium }
                        TIcon { name: "chevron-down"; size: 13; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                background: Rectangle {
                    radius: 4
                    color: Theme.stateLayer
                    opacity: srcBtn.down ? Theme.pressedOpacity : srcBtn.hovered ? Theme.hoverOpacity : 0
                    TFocusRing { visible: srcBtn.visualFocus; targetRadius: 4 }
                }
                hoverEnabled: true
            }
        }

        // Kinyitott eszköz-lista (R02 / R04)
        Column {
            visible: root.expanded && (root.st === "idle" || root.recording)
            width: parent.width
            spacing: 2
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Item { width: 1; height: 5 }
            Item {
                width: parent.width
                height: Math.max(hint.implicitHeight, 20) + 4
                TLabel {
                    id: hint
                    width: parent.width - collapseBtn.width - 8
                    text: root.recording ? qsTr("A kikapcsolt eszközök jelzője tájékoztató.")
                        : qsTr("Beszélj vagy játssz le hangot: a mozgó jelző mutatja, melyik eszközön jön hang.")
                    muted: true
                    font.pixelSize: 12
                    cssLineHeight: 1.4
                    wrapMode: Text.Wrap
                }
                T.Button {
                    id: collapseBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: hint.verticalCenter
                    implicitWidth: colRow.implicitWidth + 8
                    implicitHeight: 24
                    hoverEnabled: true
                    Accessible.name: qsTr("Összecsukás")
                    onClicked: root.expanded = false
                    contentItem: Item {
                        Row {
                            id: colRow
                            anchors.centerIn: parent
                            spacing: 4
                            TLabel { text: qsTr("Összecsukás"); font.pixelSize: 12; font.weight: Theme.weightMedium }
                            TIcon { name: "chevron-up"; size: 13; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                    background: Rectangle {
                        radius: 4
                        color: Theme.stateLayer
                        opacity: collapseBtn.down ? Theme.pressedOpacity : collapseBtn.hovered ? Theme.hoverOpacity : 0
                        TFocusRing { visible: collapseBtn.visualFocus; targetRadius: 4 }
                    }
                }
            }

            Flickable {
                id: listFlick
                width: parent.width + 6
                height: Math.min(contentHeight, 400)
                contentHeight: devCol.implicitHeight
                clip: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: TScrollBar {}

                Column {
                    id: devCol
                    width: parent.width - 6
                    Repeater {
                        model: root.vm ? root.vm.devices : null
                        Column {
                            id: dev
                            required property int index
                            required property string name
                            required property string rawName
                            required property int group
                            required property bool groupFirst
                            required property bool selected
                            required property bool locked
                            required property bool isDefault
                            required property string appName
                            required property int level
                            required property int peak
                            required property string status
                            required property string statusText
                            required property bool toggleable
                            width: devCol.width

                            RecorderGroupHeader {      // csoportfej (a Beállításokkal közös)
                                visible: dev.groupFirst
                                height: visible ? 26 : 0
                                topPadding: 8
                                group: dev.group
                            }
                            Rectangle {                // eszköz-sor
                                x: -4
                                width: dev.width + 10
                                height: 44
                                radius: 5
                                color: dev.status === "signalUnrecorded" ? Theme.accentSoft : "transparent"

                                RecorderSwitch {
                                    id: sw
                                    x: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    checked: dev.selected
                                    locked: dev.locked
                                    enabled: dev.toggleable
                                    toolTipText: dev.status === "disconnected" ? qsTr("Az eszközt leválasztották; a sávja lezárult")
                                        : root.recording && dev.selected ? qsTr("Kikapcsolás: a sáv itt lezárul; újra bekapcsolva új szakasz indul")
                                        : root.recording ? qsTr("Bekapcsolás: a sávja ettől a pillanattól indul") : ""
                                    onToggled: {
                                        root.vm.toggleDevice(dev.index)
                                        checked = Qt.binding(function() { return dev.selected })
                                    }
                                }
                                Column {
                                    x: 4 + 30 + 10
                                    width: parent.width - x - 10 - meterCol.width - 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    TLabel {
                                        width: parent.width
                                        text: dev.name
                                        color: dev.selected ? Theme.text : Theme.textMuted
                                        font.pixelSize: 13
                                        font.weight: dev.selected ? Theme.weightSemiBold : Theme.weightRegular
                                        elide: Text.ElideRight
                                    }
                                    Row {
                                        id: metaRow
                                        width: parent.width
                                        spacing: 6
                                        RecorderDefaultPill {
                                            id: defPill
                                            visible: dev.isDefault
                                        }
                                        TLabel {
                                            id: appLabel
                                            visible: dev.appName !== ""
                                            text: "▸ " + dev.appName
                                            color: Theme.accent
                                            font.pixelSize: 11
                                            font.weight: Theme.weightSemiBold
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        TLabel {
                                            id: rawLabel
                                            width: metaRow.width - (defPill.visible ? defPill.width + 6 : 0)
                                                   - (appLabel.visible ? appLabel.width + 6 : 0)
                                            text: dev.rawName
                                            mono: true; muted: true
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            anchors.verticalCenter: parent.verticalCenter
                                            HoverHandler { id: rawHover }
                                            TToolTip { visible: rawHover.hovered && rawLabel.truncated; text: dev.rawName }
                                        }
                                    }
                                }
                                Column {
                                    id: meterCol
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 3
                                    VuMeter {
                                        anchors.right: parent.right
                                        level: dev.level; peak: dev.peak
                                        active: dev.selected
                                    }
                                    TLabel {
                                        anchors.right: parent.right
                                        height: 12
                                        text: dev.statusText
                                        color: dev.status === "signalUnrecorded" ? Theme.accent
                                             : dev.status === "silentWarn" ? Theme.warnInk
                                             : dev.status === "disconnected" ? Theme.dangerInk : Theme.textMuted
                                        font.pixelSize: 10
                                        font.weight: Theme.weightSemiBold
                                    }
                                }
                            }
                        }
                    }
                }
            }
            TLabel {
                visible: root.recording
                width: parent.width
                topPadding: 8
                text: qsTr("A rögzített sávok felvétel közben nem kapcsolhatók ki. Új eszköz bekapcsolható: a sávja attól a pillanattól indul.")
                muted: true
                font.pixelSize: 12
                cssLineHeight: 1.4
                wrapMode: Text.Wrap
            }
        }
    }

    // ---- R07: bezárás felvétel közben — lap a címsor alatt, fátyol fölött ----
    Item {
        id: sheetLayer
        visible: root.sheetOpen
        x: 1
        y: titleBar.y + titleBar.height
        width: parent.width - 2
        height: parent.height - y - 1
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusDialog - 1
            color: Theme.scrim
            Rectangle { width: parent.width; height: parent.radius; color: Theme.scrim }
        }
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.sheetOpen = false }

        Rectangle {
            id: sheet
            anchors.bottom: parent.bottom
            width: parent.width
            implicitHeight: sheetCol.implicitHeight + 28
            height: implicitHeight
            radius: Theme.radiusDialog - 1
            color: Theme.raised
            Rectangle { width: parent.width; height: parent.radius; color: Theme.raised }
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            MouseArea { anchors.fill: parent }       // a lapon a kattintás nem zár

            Column {
                id: sheetCol
                x: 14; y: 14
                width: parent.width - 28
                spacing: 10
                Column {
                    width: parent.width
                    spacing: 3
                    TLabel { text: qsTr("A felvétel még fut"); font.pixelSize: 15; font.weight: Theme.weightSemiBold }
                    TLabel {
                        width: parent.width
                        text: qsTr("Bezárás helyett a háttérben is folytatódhat; a tálca-ikonról bármikor visszahozod.")
                        muted: true
                        font.pixelSize: 13
                        cssLineHeight: 1.45
                        wrapMode: Text.Wrap
                    }
                }
                RecorderButton {
                    width: parent.width
                    implicitHeight: 34
                    kind: "primary"
                    iconName: "arrow-down-to-line"
                    fontSize: 14
                    text: qsTr("Fusson a háttérben")
                    onClicked: { root.sheetOpen = false; root.hideRequested() }
                }
                Row {
                    width: parent.width
                    spacing: 6
                    RecorderButton {
                        width: (parent.width - 6) / 2
                        kind: "dangerOutline"
                        text: qsTr("Leállítás és bezárás")
                        onClicked: { root.sheetOpen = false; root.closeAfterStop = true; root.vm.stop() }
                    }
                    RecorderButton {
                        width: (parent.width - 6) / 2
                        kind: "outline"
                        fontWeight: Theme.weightMedium
                        text: qsTr("Mégse")
                        onClicked: root.sheetOpen = false
                    }
                }
            }
        }
    }
}
