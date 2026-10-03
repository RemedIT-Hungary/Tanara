import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Összefoglaló fül — M07: kész összefoglaló. Fent az elavult-sáv (ha a készítése óta
// beszélőt javítottak), két oszlop: balra a tartalom (vezetői összefoglaló, döntések,
// teendők, témánkénti módban a témaszekciók), jobbra a résztvevők beszédidő-aránnyal, a
// keletkezés adatai és a műveletek. Régi (csak markdown) összefoglaló ugyanígy jelenik meg.
Flickable {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null

    readonly property bool narrow: width < 720
    readonly property bool anyStamp: {
        for (let i = 0; i < vm.decisions.length; ++i)
            if (vm.decisions[i].ms >= 0) return true
        return false
    }

    contentWidth: width
    contentHeight: col.implicitHeight + 18 + 24
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    T.ScrollBar.vertical: TScrollBar {}

    function regenerate() {
        // Témánkénti összefoglalónál a témákat kell újraelemezni → a munkaterület nyílik meg.
        if (vm.mode === "topics" && vm.hasTopics) vm.topicsOpen = true
        else if (root.shell) root.shell.startQuickSummary(root.meetingId)
    }
    function openTopics() {
        if (vm.hasTopics) vm.topicsOpen = true
        else if (root.shell) root.shell.startTopicExtraction(root.meetingId)
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
                Rectangle {
                    implicitWidth: 4; implicitHeight: 4; radius: 2
                    color: Theme.borderStrong
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 9
                    Layout.leftMargin: 2
                }
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
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 18

                ColumnLayout {
                    visible: root.vm.execSummary !== ""
                    Layout.fillWidth: true
                    spacing: 6
                    TSectionLabel { text: qsTr("Vezetői összefoglaló") }
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

                ColumnLayout {
                    visible: root.vm.decisions.length > 0
                    Layout.fillWidth: true
                    spacing: 8
                    TSectionLabel { text: qsTr("Döntések") }
                    Repeater {
                        model: root.vm.decisions
                        RowLayout {
                            id: decisionRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 12
                            // Időbélyeg-hivatkozás: csak ha az adatban tényleg van időbélyeg.
                            Item {
                                visible: root.anyStamp
                                implicitWidth: 44
                                implicitHeight: 21
                                Layout.alignment: Qt.AlignTop
                                TLabel {
                                    id: stamp
                                    visible: decisionRow.modelData.ms >= 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: decisionRow.modelData.stamp
                                    mono: true
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Theme.weightMedium
                                    font.underline: stampHover.hovered || stampFocus.activeFocus
                                    color: Theme.accent
                                }
                                FocusScope {
                                    id: stampFocus
                                    anchors.fill: stamp
                                    visible: stamp.visible
                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Link
                                    Accessible.name: qsTr("Ugrás ide: %1").arg(decisionRow.modelData.stamp)
                                    Keys.onReturnPressed: seek()
                                    Keys.onSpacePressed: seek()
                                    function seek() {
                                        if (root.shell) root.shell.seekTo(root.meetingId, decisionRow.modelData.ms)
                                    }
                                    HoverHandler { id: stampHover; cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: stampFocus.seek() }
                                }
                            }
                            Rectangle {
                                visible: !root.anyStamp
                                implicitWidth: 4; implicitHeight: 4; radius: 2
                                color: Theme.borderStrong
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 9
                                Layout.leftMargin: 2
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: decisionRow.modelData.text
                                cssLineHeight: 1.5
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: root.vm.actions.length > 0
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
                    visible: root.vm.topicSections.length > 0
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
                                heading: qsTr("Teendők")
                                items: section.modelData.actions
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
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Másolás")
                        variant: "ghost"; size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "copy"
                        horizontalAlignment: Qt.AlignLeft
                        onClicked: {
                            if (root.vm.copyToClipboard() && root.shell)
                                root.shell.toast(qsTr("Az összefoglaló a vágólapra került."))
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
