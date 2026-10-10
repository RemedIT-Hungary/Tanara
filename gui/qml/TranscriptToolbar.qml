import QtQuick
import QtQuick.Layouts

// Az Átirat fül eszköz-sora (design/handoff-v3 V4, 3. döntés): 46 px, alul 1 px vonal.
// Olvasás / Javítás szegmens (Ctrl+E) · állapot („1298 megszólalás · 5 beszélő”, hangelemzés) ·
// … · „Ki volt ott?” · „N átnézendő sor” (a szűrő; 0-nál az újraellenőrzés) · keresés.
// Nincs állandó visszavonás-gomb (Ctrl+Z, értesítő sáv) és sáv-kapcsoló (Nézet › Beszélő-
// oszlopok, Ctrl+L); a beszélőnkénti idővonal a lejátszó fölötti térkép-dokkba került (MapDock).
// A Javítás mód tartalma (sín, jelölők, Átnézendő csoportok) a szerkesztőé: Javítás módban az
// átnézendő-gomb az „Átnézendő N" chip (handoff-v3 E1/E2: kerek, mono darabszám, bekapcsolva
// accentSoft + ×), és az állapot az Átnézendő nézetben „84 / 1298 sor · 3 csoport".
Item {
    id: root

    required property var vm                // TranscriptEditorViewModel
    property bool searchOpen: false
    property bool fixMode: false
    // Megmaradt a régi felület (a térkép-dokk kapja a Main.qml-ben).
    property real viewportStart: 0
    property real viewportSize: 0

    signal fixModeToggled(bool on)          // Olvasás / Javítás
    signal participantsRequested()          // „Ki volt ott?”
    signal searchStepRequested(int direction)
    signal nextUncertainRequested()         // „Következő átnézendő" (B)
    signal recheckRequested()               // 0 átnézendő: újraellenőrzés a megerősített sorok alapján
    signal seekRequested(real fraction)
    signal speakerClicked(string speakerKey, Item anchor)
    signal voiceprintClicked(string speakerKey, Item anchor)

    implicitHeight: 46
    height: implicitHeight

    function openSearch() {
        searchOpen = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    function closeSearch() {
        searchOpen = false
        vm.searchQuery = ""
    }
    // A hanglenyomat-jel a térkép-dokkban él; önálló képernyőképnél az állapot-felirat a horgony.
    function voiceprintMark(speakerKey) { return statusLabel }

    // Csendes eszköz-gomb (28 px): ikon + felirat; `checked`: accentSoft / accent.
    component QuietButton: Item {
        id: qb
        property string text: ""
        property string iconName: ""
        property bool checked: false
        property string toolTipText: ""
        signal clicked()
        implicitHeight: 28
        implicitWidth: qbRow.implicitWidth + (text === "" ? 0 : 20)
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text !== "" ? text : toolTipText
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()
        readonly property color ink: !enabled ? Theme.borderStrong : checked ? Theme.accent : Theme.textMuted
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: qb.checked ? Theme.accentSoft : "transparent"
            border.width: qb.checked ? 1 : 0
            border.color: Theme.accentLine
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Theme.stateLayer
                opacity: !qb.enabled ? 0 : qbTap.pressed ? Theme.pressedOpacity : qbHover.hovered ? Theme.hoverOpacity : 0
            }
            TFocusRing { visible: qb.activeFocus; targetRadius: parent.radius }
        }
        Row {
            id: qbRow
            anchors.centerIn: parent
            spacing: 6
            TIcon {
                visible: qb.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: qb.iconName
                size: 14
                color: qb.ink
                width: qb.text === "" ? 30 : 14
            }
            TLabel {
                visible: qb.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: qb.text
                color: qb.checked ? Theme.accent : !qb.enabled ? Theme.borderStrong : Theme.textMuted
                font.pixelSize: Theme.fontSmall
                font.weight: qb.checked ? Theme.weightSemiBold : Theme.weightRegular
            }
        }
        HoverHandler { id: qbHover }
        TapHandler { id: qbTap; enabled: qb.enabled; onTapped: qb.clicked() }
        TToolTip { visible: qb.toolTipText !== "" && qbHover.hovered; text: qb.toolTipText }
    }

    RowLayout {
        anchors { fill: parent; leftMargin: Theme.space5; rightMargin: 20 }
        spacing: 10

        // ---- Olvasás / Javítás ----
        Rectangle {
            id: segment
            objectName: "modeSegment"
            implicitWidth: segRow.implicitWidth + 6
            implicitHeight: 32
            radius: 7
            color: Theme.sunken
            Row {
                id: segRow
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: [
                        { label: qsTr("Olvasás"), icon: "book-open", fix: false, name: "modeRead" },
                        { label: qsTr("Javítás"), icon: "pencil", fix: true, name: "modeFix" }
                    ]
                    Item {
                        id: seg
                        required property var modelData
                        objectName: modelData.name
                        readonly property bool active: root.fixMode === modelData.fix
                        implicitWidth: segContent.implicitWidth + 22
                        implicitHeight: 26
                        width: implicitWidth
                        height: implicitHeight
                        activeFocusOnTab: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.label
                        Accessible.checked: active
                        Keys.onSpacePressed: root.fixModeToggled(modelData.fix)
                        Rectangle {
                            anchors.fill: parent
                            radius: 5
                            color: seg.active ? Theme.raised : segHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                            border.width: seg.active ? 1 : 0
                            border.color: Theme.border
                            TFocusRing { visible: seg.activeFocus; targetRadius: parent.radius }
                        }
                        Row {
                            id: segContent
                            anchors.centerIn: parent
                            spacing: 6
                            TIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: seg.modelData.icon
                                size: 14
                                color: seg.active ? Theme.text : Theme.textMuted
                            }
                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: seg.modelData.label
                                color: seg.active ? Theme.text : Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                                font.weight: seg.active ? Theme.weightSemiBold : Theme.weightRegular
                            }
                        }
                        HoverHandler { id: segHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.fixModeToggled(seg.modelData.fix) }
                        TToolTip {
                            visible: segHover.hovered
                            text: seg.modelData.fix ? qsTr("Javítás: beszélő-oszlopok, jelölők, átnézendő sorok (Ctrl+E)")
                                                    : qsTr("Olvasás: tiszta lista (Ctrl+E)")
                        }
                    }
                }
            }
        }
        TLabel {
            visible: !root.searchOpen
            text: "Ctrl+E"
            mono: true
            muted: true
            font.pixelSize: Theme.fontMicro
        }

        // ---- állapot ----
        TLabel {
            id: statusLabel
            objectName: "transcriptStatus"
            visible: !root.searchOpen
            Layout.leftMargin: 6
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            text: root.vm.uncertainOnly && root.fixMode
                  ? qsTr("%1 / %2 sor · %n csoport", "", root.vm.reviewGroupCount).arg(root.vm.reviewCount).arg(root.vm.utteranceCount)
                  : qsTr("%1 megszólalás · %2 beszélő").arg(root.vm.utteranceCount).arg(root.vm.speakerCount)
            muted: true
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        // Hang-elemzés: futás közben halk folyamatjelző; ha nem érhető el, egy rövid jelzés.
        Row {
            visible: !root.searchOpen && root.vm.embeddingRunning
            spacing: 8
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Hangelemzés… %1%").arg(Math.round(root.vm.embeddingProgress * 100))
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            TProgressBar {
                anchors.verticalCenter: parent.verticalCenter
                width: 64
                value: root.vm.embeddingProgress
            }
        }
        Row {
            objectName: "voiceNote"
            visible: !root.searchOpen && !root.vm.embeddingRunning && root.vm.voiceNote !== ""
            spacing: 6
            TIcon { anchors.verticalCenter: parent.verticalCenter; name: "info"; size: 13; color: Theme.textMuted }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Hangelemzés nélkül")
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            HoverHandler { id: voiceNoteHover }
            TToolTip { visible: voiceNoteHover.hovered; text: root.vm.voiceNote; delay: 150 }
        }

        // ---- keresés (az állapot helyén) ----
        RowLayout {
            visible: root.searchOpen
            Layout.fillWidth: true
            spacing: 6
            TSearchField {
                id: searchField
                objectName: "transcriptSearch"
                Layout.fillWidth: true
                Layout.maximumWidth: 300
                Layout.preferredHeight: 28
                placeholderText: qsTr("Keresés az átiratban")
                onTextChanged: root.vm.searchQuery = text
                Keys.onReturnPressed: event => root.searchStepRequested(event.modifiers & Qt.ShiftModifier ? -1 : 1)
                Keys.onEnterPressed: event => root.searchStepRequested(event.modifiers & Qt.ShiftModifier ? -1 : 1)
                Keys.onEscapePressed: root.closeSearch()
                Connections {
                    target: root.vm
                    function onSearchChanged() {
                        if (root.vm.searchQuery !== "" && !root.searchOpen) root.searchOpen = true
                        if (searchField.text !== root.vm.searchQuery) searchField.text = root.vm.searchQuery
                    }
                }
            }
            TLabel {
                visible: root.vm.searchQuery.trim() !== ""
                text: root.vm.searchMatchCount === 0 ? qsTr("Nincs találat")
                    : qsTr("%1 / %2").arg(root.vm.searchCurrent + 1).arg(root.vm.searchMatchCount)
                mono: root.vm.searchMatchCount > 0
                color: root.vm.searchMatchCount === 0 ? Theme.dangerInk : Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
            QuietButton {
                iconName: "chevron-up"
                enabled: root.vm.searchMatchCount > 0
                toolTipText: qsTr("Előző találat (Shift+Enter)")
                onClicked: root.searchStepRequested(-1)
            }
            QuietButton {
                iconName: "chevron-down"
                enabled: root.vm.searchMatchCount > 0
                toolTipText: qsTr("Következő találat (Enter)")
                onClicked: root.searchStepRequested(1)
            }
            Item { Layout.fillWidth: true }
        }

        // Átnézendő: olvasáskor csendes „N átnézendő sor", Javítás módban az „Átnézendő N" chip.
        // Nincs (több) átnézendő sor: az újraellenőrzést kínálja — a megerősített és javított
        // sorok hangjához méri a többit.
        Item {
            id: reviewButton
            objectName: "uncertainChip"
            readonly property int count: root.vm.reviewCount
            readonly property bool offersRecheck: !root.vm.uncertainOnly && count === 0 && root.vm.uncertainCount === 0
                                                  && root.vm.voiceAvailable && !root.vm.embeddingRunning
            readonly property bool checked: root.vm.uncertainOnly
            readonly property bool chip: root.fixMode && !offersRecheck
            readonly property string toolTipText: !root.vm.voiceAvailable ? root.vm.voiceNote
                       : root.vm.uncertainOnly ? qsTr("Minden sor mutatása")
                       : offersRecheck && root.vm.canRecheck
                         ? qsTr("Nincs átnézendő sor. Kattints, és a megerősített és javított sorok hangja alapján újraellenőrzöm a többit.")
                       : offersRecheck ? qsTr("Nincs átnézendő sor. Újraellenőrzéshez: %1").arg(root.vm.recheckBlocker)
                       : qsTr("A kétes sorok okok szerint csoportosítva, csoportonként egy döntéssel (B: a következő)")
            enabled: root.vm.voiceAvailable || root.vm.uncertainOnly
            implicitWidth: chip ? chipRow.implicitWidth + 22 : quiet.implicitWidth
            implicitHeight: 28
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Átnézendő")
            Keys.onSpacePressed: clicked()
            Keys.onReturnPressed: clicked()
            function clicked() {
                if (offersRecheck) {
                    root.recheckRequested()
                    return
                }
                const on = !root.vm.uncertainOnly
                if (on && !root.fixMode) root.fixModeToggled(true)
                root.vm.uncertainOnly = on
            }

            QuietButton {
                id: quiet
                visible: !reviewButton.chip
                enabled: reviewButton.enabled
                checked: reviewButton.checked
                iconName: reviewButton.offersRecheck && root.vm.canRecheck ? "refresh-cw" : "list-checks"
                text: reviewButton.offersRecheck ? qsTr("Nincs átnézendő sor")
                                                 : qsTr("%n átnézendő sor", "", reviewButton.count)
                toolTipText: reviewButton.toolTipText
                onClicked: reviewButton.clicked()
            }
            Rectangle {
                visible: reviewButton.chip
                anchors.fill: parent
                radius: height / 2
                color: reviewButton.checked ? Theme.accentSoft : Theme.raised
                border.width: 1
                border.color: reviewButton.checked ? Theme.accent : Theme.borderStrong
                opacity: reviewButton.enabled ? 1 : 0.5
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.stateLayer
                    opacity: chipTap.pressed ? Theme.pressedOpacity : chipHover.hovered ? Theme.hoverOpacity : 0
                }
                TFocusRing { visible: reviewButton.activeFocus; targetRadius: parent.radius }
                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: 6
                    TIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "list-checks"
                        size: 14
                        color: reviewButton.checked ? Theme.accent : Theme.text
                    }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Átnézendő")
                        color: reviewButton.checked ? Theme.accent : Theme.text
                        font.pixelSize: Theme.fontSmall
                        font.weight: reviewButton.checked ? Theme.weightSemiBold : Theme.weightMedium
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: countText.implicitWidth + 10
                        height: 18
                        radius: Theme.radiusTag
                        color: reviewButton.checked ? "transparent" : Theme.sunken
                        TLabel {
                            id: countText
                            objectName: "reviewCount"
                            anchors.centerIn: parent
                            text: String(reviewButton.count)
                            mono: true
                            color: reviewButton.checked ? Theme.accent : Theme.text
                            font.pixelSize: Theme.fontCaption
                            font.weight: Theme.weightMedium
                        }
                    }
                    TIcon {
                        visible: reviewButton.checked
                        anchors.verticalCenter: parent.verticalCenter
                        name: "x"
                        size: 12
                        color: Theme.accent
                    }
                    // Háttér-elemzés közben finom jelzés.
                    TSpinner {
                        visible: root.vm.reviewRunning && !reviewButton.checked
                        anchors.verticalCenter: parent.verticalCenter
                        size: 11
                        color: Theme.textMuted
                    }
                }
                HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { id: chipTap; enabled: reviewButton.enabled; onTapped: reviewButton.clicked() }
                TToolTip { visible: chipHover.hovered && reviewButton.toolTipText !== ""; text: reviewButton.toolTipText }
            }
        }
        QuietButton {
            visible: root.vm.uncertainOnly && root.vm.reviewCount > 0
            iconName: "chevrons-down"
            toolTipText: qsTr("Következő átnézendő sor (B)")
            onClicked: root.nextUncertainRequested()
        }
        QuietButton {
            objectName: "whoWasThereButton"
            visible: !root.searchOpen
            text: qsTr("Ki volt ott?")
            iconName: "users"
            toolTipText: qsTr("A résztvevők átnézése: kik voltak ott, és melyik beszélő kicsoda")
            onClicked: root.participantsRequested()
        }
        QuietButton {
            objectName: "searchButton"
            iconName: root.searchOpen ? "x" : "search"
            toolTipText: root.searchOpen ? qsTr("Keresés bezárása") : qsTr("Keresés az átiratban (Ctrl+Shift+F)")
            onClicked: root.searchOpen ? root.closeSearch() : root.openSearch()
        }
    }

    TDivider { anchors.bottom: parent.bottom; width: parent.width }
}
