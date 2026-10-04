import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Összefoglaló fül — M07: kész összefoglaló. Fent az elavult-sáv (ha a készítése óta
// beszélőt javítottak), két oszlop: balra a tartalom, jobbra a résztvevők beszédidő-aránnyal,
// a keletkezés adatai és a műveletek.
//
// A gyors összefoglalónak két formája van, a bal oszlop tetején kapcsolóval:
//   „Vezetői összefoglaló” (alap): áttekintés, döntések, nyitott kérdések, teendők;
//   „Memó”: időrendi jegyzet szakaszonként (cím, időkeret → ugrás a lejátszóban és az
//   átiratban, pontok), sok szakasznál tartalomjegyzékkel.
// A régi (memó előtti) összefoglalónál a memó helyén rövid magyarázat + „Újragenerálás”.
// Témánkénti összefoglalónál nincs kapcsoló: a vezetői rész alatt a témaszekciók állnak.
//
// A jobb oszlopban a megbeszélés-megjegyzés összecsukható blokkja (alapból csukva: az eleje
// látszik); nyitva szerkeszthető, sablon-javaslatokkal. Ha az összefoglaló óta változott, az
// „Újragenerálás” alatt szelíd jelzés áll (a könyvtárban nem lesz tőle elavult).
Flickable {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null

    readonly property bool narrow: width < 720
    readonly property bool hasSwitch: vm.memoState !== "none"
    readonly property bool showMemo: hasSwitch && vm.section === "memo"
    // Tartalomjegyzék: csak ha a memó már nem tekinthető át egy pillantással.
    readonly property bool tocVisible: vm.memo.length >= 6
    readonly property bool anyStamp: {
        for (let i = 0; i < vm.decisions.length; ++i)
            if (vm.decisions[i].ms >= 0) return true
        for (let j = 0; j < vm.openQuestions.length; ++j)
            if (vm.openQuestions[j].ms >= 0) return true
        return false
    }

    contentWidth: width
    contentHeight: col.implicitHeight + 18 + 24
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    T.ScrollBar.vertical: TScrollBar {}

    function regenerate() {
        noteEditor.commit()   // a függő megjegyzés-piszkozat már az új futásba kerüljön
        // Témánkénti összefoglalónál a témákat kell újraelemezni → a munkaterület nyílik meg.
        if (vm.mode === "topics" && vm.hasTopics) vm.topicsOpen = true
        else if (root.shell) root.shell.startQuickSummary(root.meetingId)
    }
    function openTopics() {
        if (vm.hasTopics) vm.topicsOpen = true
        else if (root.shell) root.shell.startTopicExtraction(root.meetingId)
    }
    function seek(ms) {
        if (root.shell && ms >= 0) root.shell.seekTo(root.meetingId, ms)
    }
    function copy(part) {
        if (!root.vm.copyToClipboard(part) || !root.shell) return
        root.shell.toast(part === "exec" ? qsTr("A vezetői összefoglaló a vágólapra került.")
                       : part === "memo" ? qsTr("A memó a vágólapra került.")
                       : qsTr("Az összefoglaló a vágólapra került."))
    }
    // A memó egy szakaszára görget (tartalomjegyzékből).
    function scrollToSection(index) {
        const it = memoRepeater.itemAt(index)
        if (!it) return
        const y = it.mapToItem(col, 0, 0).y + col.y - 12
        root.contentY = Math.max(0, Math.min(y, root.contentHeight - root.height))
    }

    // Kattintható időbélyeg (accent, egyenközű): a lejátszót és az átiratot oda ugratja.
    component StampLink: Item {
        id: link
        property string text: ""
        property real ms: -1
        implicitWidth: linkLabel.implicitWidth
        implicitHeight: 21
        TLabel {
            id: linkLabel
            anchors.verticalCenter: parent.verticalCenter
            text: link.text
            mono: true
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightMedium
            font.underline: linkHover.hovered || linkFocus.activeFocus
            color: Theme.accent
        }
        FocusScope {
            id: linkFocus
            anchors.fill: linkLabel
            activeFocusOnTab: true
            Accessible.role: Accessible.Link
            Accessible.name: qsTr("Ugrás ide: %1").arg(link.text)
            Keys.onReturnPressed: root.seek(link.ms)
            Keys.onSpacePressed: root.seek(link.ms)
            HoverHandler { id: linkHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.seek(link.ms) }
        }
    }

    // Kis pont a felsorolás elején.
    component Bullet: Rectangle {
        implicitWidth: 4; implicitHeight: 4; radius: 2
        color: Theme.borderStrong
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 9
        Layout.leftMargin: 2
    }

    // Időbélyeges lista (döntések, nyitott kérdések): az időbélyeg-oszlop csak akkor van,
    // ha az adatban tényleg van időbélyeg.
    component StampedList: ColumnLayout {
        id: stamped
        property string heading: ""
        property var items: []             // [{ text, ms, stamp }]
        visible: items.length > 0
        spacing: 8
        TSectionLabel { text: stamped.heading }
        Repeater {
            model: stamped.items
            RowLayout {
                id: stampedRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                Item {
                    visible: root.anyStamp
                    implicitWidth: 44
                    implicitHeight: 21
                    Layout.alignment: Qt.AlignTop
                    StampLink {
                        visible: stampedRow.modelData.ms >= 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: stampedRow.modelData.stamp
                        ms: stampedRow.modelData.ms
                    }
                }
                Bullet { visible: !root.anyStamp }
                TLabel {
                    Layout.fillWidth: true
                    text: stampedRow.modelData.text
                    cssLineHeight: 1.5
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // Pontokba szedett lista (döntések a témaszekciókban, teendők felelőssel).
    component PointList: ColumnLayout {
        property string heading: ""
        property var items: []             // string vagy { text, owner, due }
        visible: items.length > 0
        spacing: 4
        TLabel {
            text: parent.heading
            muted: true
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
        }
        Repeater {
            model: parent.items
            RowLayout {
                required property var modelData
                readonly property bool plain: typeof modelData === "string"
                Layout.fillWidth: true
                spacing: 8
                Bullet {}
                TLabel {
                    Layout.fillWidth: true
                    text: parent.plain ? parent.modelData
                        : parent.modelData.text
                          + (parent.modelData.owner ? " — " + parent.modelData.owner : "")
                          + (parent.modelData.due ? " (" + parent.modelData.due + ")" : "")
                    cssLineHeight: 1.5
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    ColumnLayout {
        id: col
        x: 24; y: 18
        width: root.width - 48
        spacing: 16

        SummaryStatusBanners {
            Layout.fillWidth: true
            vm: root.vm
            meetingId: root.meetingId
            shell: root.shell
            showBlocker: false
            onRetryRequested: root.regenerate()
        }

        // ---- elavult-sáv ----
        TBanner {
            visible: root.vm.stale
            Layout.fillWidth: true
            tone: "warn"
            text: root.vm.staleCount > 0
                ? qsTr("Az összefoglaló óta %n beszélőt javítottál, ezért a felelősök és a résztvevők elavultak lehetnek.", "", root.vm.staleCount)
                : qsTr("Az összefoglaló óta változtak a beszélők, ezért a felelősök és a résztvevők elavultak lehetnek.")
            TButton {
                text: root.vm.mode === "topics" && root.vm.hasTopics ? qsTr("Témák újraelemzése") : qsTr("Frissítés")
                size: "small"
                implicitHeight: 30
                leftPadding: 12; rightPadding: 12
                font.weight: Theme.weightSemiBold
                enabled: !root.vm.jobRunning
                onClicked: root.regenerate()
            }
            TButton {
                text: qsTr("Rendben így")
                variant: "ghost"; size: "small"
                implicitHeight: 30
                onClicked: root.vm.dismissStale()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.narrow ? 1 : 2
            columnSpacing: 32
            rowSpacing: 24

            // ================= bal oszlop: tartalom =================
            ColumnLayout {
                id: leftCol
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 18

                // ---- a két forma közti váltó (a rövid forma az alap) ----
                SettingsSegmented {
                    visible: root.hasSwitch
                    Layout.bottomMargin: -2
                    value: root.vm.section
                    options: [
                        { value: "exec", label: qsTr("Vezetői összefoglaló") },
                        { value: "memo", label: root.vm.memoState === "ready"
                                                ? qsTr("Memó · %n szakasz", "", root.vm.memo.length)
                                                : qsTr("Memó") },
                    ]
                    onPicked: (v) => { root.vm.section = v; root.contentY = 0 }
                }

                // ================= vezetői összefoglaló =================
                ColumnLayout {
                    visible: !root.showMemo && root.vm.execSummary !== ""
                    Layout.fillWidth: true
                    spacing: 6
                    // A kapcsoló már megnevezi a részt; címke csak nélküle (témánkénti mód).
                    TSectionLabel { visible: !root.hasSwitch; text: qsTr("Vezetői összefoglaló") }
                    TLabel {
                        Layout.fillWidth: true
                        // A Qt a rögzített sormagasság többletét a sor FÖLÉ teszi (a CSS felezi) →
                        // 3 px-lel feljebb toljuk, hogy a címke és a következő szakasz köze stimmeljen.
                        Layout.topMargin: -3
                        Layout.bottomMargin: 3
                        text: root.vm.execSummary
                        textFormat: Text.MarkdownText
                        font.pixelSize: 15
                        cssLineHeight: 1.6
                        wrapMode: Text.Wrap
                    }
                }

                StampedList {
                    visible: !root.showMemo && items.length > 0
                    Layout.fillWidth: true
                    heading: qsTr("Döntések")
                    items: root.vm.decisions
                }

                StampedList {
                    visible: !root.showMemo && items.length > 0
                    Layout.fillWidth: true
                    heading: qsTr("Nyitott kérdések")
                    items: root.vm.openQuestions
                }

                ColumnLayout {
                    visible: !root.showMemo && root.vm.actions.length > 0
                    Layout.fillWidth: true
                    spacing: 6
                    TSectionLabel { text: qsTr("Teendők") }
                    Repeater {
                        model: root.vm.actions
                        ColumnLayout {
                            id: actionRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 0
                            TDivider { Layout.fillWidth: true }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 7
                                Layout.bottomMargin: 7
                                spacing: 12
                                // A teendőknek nincs „kész” állapota az adatban → pont, nem jelölőnégyzet.
                                Item {
                                    implicitWidth: 16; implicitHeight: 20
                                    Layout.alignment: Qt.AlignVCenter
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 6; height: 6; radius: 3
                                        color: Theme.borderStrong
                                    }
                                }
                                TLabel {
                                    Layout.fillWidth: true
                                    text: actionRow.modelData.text
                                    cssLineHeight: 1.4
                                    wrapMode: Text.Wrap
                                }
                                // Több felelős → több chip (szűk helyen több sorba törnek).
                                Flow {
                                    visible: actionRow.modelData.owners.length > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.maximumWidth: Math.max(160, col.width * 0.3)
                                    spacing: 4
                                    Repeater {
                                        model: actionRow.modelData.owners
                                        SummaryOwnerChip {
                                            required property var modelData
                                            name: modelData.name
                                            speakerIndex: modelData.index
                                            maximumWidth: Math.max(160, col.width * 0.3)
                                        }
                                    }
                                }
                                TLabel {
                                    visible: actionRow.modelData.due !== ""
                                    Layout.minimumWidth: 64
                                    Layout.maximumWidth: 110
                                    Layout.alignment: Qt.AlignVCenter
                                    text: actionRow.modelData.due
                                    mono: true; muted: true
                                    font.pixelSize: Theme.fontCaption
                                    horizontalAlignment: Text.AlignRight
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }

                // ---- témánkénti összefoglaló: témaszekciók ----
                ColumnLayout {
                    visible: !root.showMemo && root.vm.topicSections.length > 0
                    Layout.fillWidth: true
                    spacing: 14
                    TSectionLabel { text: qsTr("Témák") }
                    Repeater {
                        model: root.vm.topicSections
                        ColumnLayout {
                            id: section
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 6
                            TDivider { visible: section.index > 0; Layout.fillWidth: true; Layout.bottomMargin: 8 }
                            TLabel {
                                Layout.fillWidth: true
                                text: section.modelData.title
                                font.pixelSize: 15
                                font.weight: Theme.weightSemiBold
                                wrapMode: Text.Wrap
                            }
                            TLabel {
                                visible: text !== ""
                                Layout.fillWidth: true
                                text: section.modelData.detail
                                textFormat: Text.MarkdownText
                                cssLineHeight: 1.5
                                wrapMode: Text.Wrap
                            }
                            PointList {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                heading: qsTr("Döntések")
                                items: section.modelData.decisions
                            }
                            PointList {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                heading: qsTr("Nyitott kérdések")
                                items: section.modelData.openQuestions || []
                            }
                            PointList {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                heading: qsTr("Teendők")
                                items: section.modelData.actions
                            }
                        }
                    }
                }

                // ================= memó =================
                // Régi összefoglaló: a memó helyén magyarázat + újragenerálás.
                Rectangle {
                    visible: root.showMemo && root.vm.memoState === "missing"
                    Layout.fillWidth: true
                    Layout.maximumWidth: 640
                    implicitHeight: missingCol.implicitHeight + 36
                    radius: Theme.radiusPopup
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    ColumnLayout {
                        id: missingCol
                        anchors { fill: parent; leftMargin: 20; rightMargin: 20; topMargin: 18; bottomMargin: 18 }
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            TIcon { name: "file-text"; size: 16; color: Theme.textMuted }
                            TLabel {
                                Layout.fillWidth: true
                                text: qsTr("Ehhez az összefoglalóhoz nincs memó")
                                font.weight: Theme.weightSemiBold
                                wrapMode: Text.Wrap
                            }
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: qsTr("Ez az összefoglaló még a memó bevezetése előtt készült. Újragenerálva a vezetői összefoglaló mellé időrendi, szakaszonkénti memó is készül.")
                            muted: true
                            cssLineHeight: 1.5
                            wrapMode: Text.Wrap
                        }
                        TButton {
                            Layout.topMargin: 4
                            text: qsTr("Újragenerálás")
                            size: "small"
                            implicitHeight: 30
                            leftPadding: 12; rightPadding: 12
                            iconName: "rotate-ccw"
                            iconSize: 13
                            enabled: root.vm.canRun && !root.vm.jobRunning
                            toolTipText: root.vm.canRun ? "" : (root.vm.blocker.reason || "")
                            onClicked: root.regenerate()
                        }
                    }
                }

                // Tartalomjegyzék: két oszlopban, oszloponként felülről lefelé.
                ColumnLayout {
                    visible: root.showMemo && root.tocVisible
                    Layout.fillWidth: true
                    spacing: 4
                    TSectionLabel { text: qsTr("Tartalom") }
                    GridLayout {
                        id: toc
                        readonly property int cols: leftCol.width >= 560 ? 2 : 1
                        Layout.fillWidth: true
                        flow: GridLayout.TopToBottom
                        rows: Math.ceil(root.vm.memo.length / cols)
                        columnSpacing: 24
                        rowSpacing: 0
                        Repeater {
                            model: root.showMemo && root.tocVisible ? root.vm.memo : []
                            T.AbstractButton {
                                id: tocEntry
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1      // egyenlő oszlopok
                                implicitHeight: 26
                                hoverEnabled: true
                                activeFocusOnTab: true
                                Accessible.name: modelData.title
                                onClicked: root.scrollToSection(index)
                                Keys.onReturnPressed: click()
                                background: Item { TFocusRing { visible: tocEntry.visualFocus; targetRadius: 4 } }
                                contentItem: RowLayout {
                                    spacing: 10
                                    TLabel {
                                        Layout.preferredWidth: 52
                                        text: tocEntry.modelData.stamp
                                        mono: true; muted: true
                                        font.pixelSize: Theme.fontCaption
                                    }
                                    TLabel {
                                        Layout.fillWidth: true
                                        text: tocEntry.modelData.title
                                        font.pixelSize: Theme.fontSmall
                                        font.underline: tocEntry.hovered
                                        color: tocEntry.hovered ? Theme.accent : Theme.text
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                    TDivider { Layout.fillWidth: true; Layout.topMargin: 10 }
                }

                // A szakaszok: cím, időkeret (ugrás), pontok.
                ColumnLayout {
                    visible: root.showMemo && root.vm.memoState === "ready"
                    Layout.fillWidth: true
                    spacing: 16
                    Repeater {
                        id: memoRepeater
                        model: root.showMemo ? root.vm.memo : []
                        ColumnLayout {
                            id: memoSection
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 6
                            TDivider { visible: memoSection.index > 0; Layout.fillWidth: true; Layout.bottomMargin: 10 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                TLabel {
                                    Layout.fillWidth: true
                                    text: memoSection.modelData.title !== "" ? memoSection.modelData.title
                                                                            : qsTr("Egyéb")
                                    font.pixelSize: 15
                                    font.weight: Theme.weightSemiBold
                                    wrapMode: Text.Wrap
                                }
                                StampLink {
                                    visible: memoSection.modelData.startMs >= 0
                                    Layout.alignment: Qt.AlignTop
                                    text: memoSection.modelData.range
                                    ms: memoSection.modelData.startMs
                                }
                            }
                            Repeater {
                                model: memoSection.modelData.points
                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Bullet {}
                                    TLabel {
                                        Layout.fillWidth: true
                                        text: parent.modelData
                                        cssLineHeight: 1.5
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ================= jobb oszlop: résztvevők, metaadat, műveletek =================
            ColumnLayout {
                Layout.preferredWidth: 250
                Layout.maximumWidth: root.narrow ? col.width : 250
                Layout.fillWidth: root.narrow
                Layout.alignment: Qt.AlignTop
                spacing: 16

                ColumnLayout {
                    visible: root.vm.participants.length > 0
                    Layout.fillWidth: true
                    spacing: 8
                    TSectionLabel { text: qsTr("Résztvevők") }
                    Repeater {
                        model: root.vm.participants
                        RowLayout {
                            id: person
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10
                            TAvatar {
                                visible: person.modelData.colorIndex >= 0
                                name: person.modelData.name
                                speakerIndex: Math.max(0, person.modelData.colorIndex)
                            }
                            // Az összefoglaló említi, de nem a meeting beszélője: semleges jel.
                            Rectangle {
                                visible: person.modelData.colorIndex < 0
                                implicitWidth: 24; implicitHeight: 24; radius: 12
                                color: Theme.sunken
                                border.width: 1
                                border.color: Theme.borderStrong
                                TIcon { anchors.centerIn: parent; name: "user"; size: 12; color: Theme.textMuted }
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: person.modelData.name
                                elide: Text.ElideRight
                            }
                            TLabel {
                                visible: person.modelData.percent >= 0
                                text: person.modelData.percent + "%"
                                mono: true; muted: true
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    TDivider { Layout.fillWidth: true; Layout.bottomMargin: 8 }
                    TLabel {
                        Layout.fillWidth: true
                        text: root.vm.modeLabel
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        visible: text !== ""
                        Layout.fillWidth: true
                        text: root.vm.metaLine
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        visible: text !== ""
                        Layout.fillWidth: true
                        text: root.vm.modelLine
                        mono: true; muted: true
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideMiddle
                    }
                }

                // ---- megbeszélés-megjegyzés (összecsukható) ----
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TDivider { Layout.fillWidth: true; Layout.bottomMargin: 2 }
                    T.AbstractButton {
                        id: noteToggle
                        Layout.fillWidth: true
                        implicitHeight: 28
                        hoverEnabled: true
                        Accessible.name: qsTr("Megjegyzés a megbeszéléshez")
                        onClicked: root.vm.noteOpen = !root.vm.noteOpen
                        background: Rectangle {
                            radius: Theme.radiusControl
                            color: Theme.stateLayer
                            opacity: noteToggle.down ? Theme.pressedOpacity : noteToggle.hovered ? Theme.hoverOpacity : 0
                            TFocusRing { visible: noteToggle.visualFocus }
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            TIcon { name: "pencil"; size: 14; color: Theme.textMuted }
                            TLabel {
                                Layout.fillWidth: true
                                text: qsTr("Megjegyzés a megbeszéléshez")
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightMedium
                                elide: Text.ElideRight
                            }
                            TIcon {
                                name: root.vm.noteOpen ? "chevron-up" : "chevron-down"
                                size: 14
                                color: Theme.textMuted
                            }
                        }
                    }
                    // Csukva: az eleje (vagy mire jó), és az észlelt hívás.
                    TLabel {
                        visible: !root.vm.noteOpen
                        Layout.fillWidth: true
                        text: root.vm.note.note.trim() !== ""
                              ? root.vm.note.note.trim()
                              : qsTr("Nincs megjegyzés. Ide írhatod a neveket, szakszavakat és a félrehallott szavak helyes alakját.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        visible: !root.vm.noteOpen && root.vm.note.detectedCallApp !== ""
                        Layout.fillWidth: true
                        spacing: 6
                        TIcon { name: "radar"; size: 13; color: Theme.textMuted }
                        TLabel {
                            Layout.fillWidth: true
                            text: qsTr("Észlelt hívás: %1").arg(root.vm.note.detectedCallApp)
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                    }
                    MeetingNoteEditor {
                        id: noteEditor
                        visible: root.vm.noteOpen
                        Layout.fillWidth: true
                        compact: true
                        model: root.vm.note
                        fieldHeight: 110
                        helperText: qsTr("Nevek, szakszavak, ismert félrehallások (A „…” helyesen: …). Az újragenerált összefoglaló és egy újra-átírás is ezt kapja.")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Témánkénti elemzés")
                        size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "list-tree"
                        horizontalAlignment: Qt.AlignLeft
                        enabled: root.vm.hasTopics || root.vm.canRun
                        toolTipText: enabled ? "" : (root.vm.blocker.reason || "")
                        onClicked: root.openTopics()
                    }
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Újragenerálás")
                        variant: "ghost"; size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "rotate-ccw"
                        horizontalAlignment: Qt.AlignLeft
                        enabled: root.vm.canRun && !root.vm.jobRunning
                        toolTipText: root.vm.canRun ? "" : (root.vm.blocker.reason || "")
                        onClicked: root.regenerate()
                    }
                    // A megjegyzés az összefoglaló óta változott: szelíd jelzés, nem elavult-jel.
                    RowLayout {
                        visible: root.vm.noteChangedSinceSummary
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.bottomMargin: 2
                        spacing: 6
                        TIcon {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 2
                            name: "info"; size: 13; color: Theme.textMuted
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: qsTr("A megjegyzés azóta változott; újrageneráláskor már az új számít.")
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }
                    }
                    // Memós összefoglalónál a menü választ: vezetői rész, memó vagy mindkettő.
                    TButton {
                        id: copyButton
                        Layout.fillWidth: true
                        text: qsTr("Másolás")
                        variant: "ghost"; size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "copy"
                        trailingIconName: root.vm.memoState === "ready" ? "chevron-down" : ""
                        iconSize: 14
                        horizontalAlignment: Qt.AlignLeft
                        onClicked: {
                            if (root.vm.memoState === "ready")
                                copyMenu.popup(copyButton, 0, copyButton.height + 4)
                            else
                                root.copy("all")
                        }
                    }
                    TMenu {
                        id: copyMenu
                        TMenuItem {
                            text: qsTr("Vezetői összefoglaló")
                            onTriggered: root.copy("exec")
                        }
                        TMenuItem {
                            text: qsTr("Memó")
                            onTriggered: root.copy("memo")
                        }
                        TMenuItem {
                            text: qsTr("Mindkettő")
                            onTriggered: root.copy("all")
                        }
                    }
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Megnyitás mappában")
                        variant: "ghost"; size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "folder-open"
                        horizontalAlignment: Qt.AlignLeft
                        onClicked: if (root.shell) root.shell.revealInFolder(root.meetingId)
                    }
                }
            }
        }
    }
}
