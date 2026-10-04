import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Összefoglaló fül — M06: még nincs összefoglaló. Két választó kártya (gyors: ajánlott;
// témánkénti), alatta a szolgáltató; hiányzó beállításnál a sáv megnevezi és odavisz; futás
// közben a kártyák helyén a folyamat látszik megszakítással.
Flickable {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null

    contentWidth: width
    contentHeight: col.implicitHeight + 28 + 24
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    T.ScrollBar.vertical: TScrollBar {}

    function startQuick() { if (root.shell) root.shell.startQuickSummary(root.meetingId) }
    function startTopics() {
        if (root.vm.hasTopics) root.vm.topicsOpen = true
        else if (root.shell) root.shell.startTopicExtraction(root.meetingId)
    }

    component ChoiceCard: Rectangle {
        id: card
        property bool recommended: false
        property string iconName: ""
        property string title: ""
        property string description: ""
        property string note: ""
        property string actionText: ""
        signal triggered()

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1           // egyenlő oszlopok
        implicitHeight: cardCol.implicitHeight + 36
        radius: Theme.radiusPopup
        color: Theme.raised
        border.width: recommended ? 1.5 : 1
        border.color: recommended ? Theme.accent : Theme.border

        ColumnLayout {
            id: cardCol
            anchors { fill: parent; margins: 18 }
            spacing: 10
            TIcon { name: card.iconName; size: 18; color: card.recommended ? Theme.accent : Theme.text }
            TLabel {
                Layout.fillWidth: true
                text: card.title
                font.pixelSize: Theme.fontHeading
                font.weight: Theme.weightSemiBold
                wrapMode: Text.Wrap
            }
            TLabel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: card.description
                muted: true
                cssLineHeight: 1.5
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignTop
            }
            TLabel {
                Layout.fillWidth: true
                text: card.note
                muted: true
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
            TButton {
                text: card.actionText
                variant: card.recommended ? "primary" : "secondary"
                enabled: root.vm.canRun
                iconName: root.vm.canRun ? "" : "lock"
                iconSize: 15
                toolTipText: root.vm.canRun ? "" : (root.vm.blocker.reason || "")
                onClicked: card.triggered()
            }
        }
    }

    ColumnLayout {
        id: col
        x: 24; y: 28
        width: Math.min(root.width - 48, 820)
        spacing: 18

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            TLabel {
                Layout.fillWidth: true
                text: qsTr("Még nincs összefoglaló")
                font.pixelSize: 18
                font.weight: Theme.weightSemiBold
            }
            TLabel {
                Layout.fillWidth: true
                text: root.vm.jobRunning ? root.vm.transcriptLine
                                         : root.vm.transcriptLine + " " + qsTr("Kétféle összefoglaló készülhet:")
                muted: true
                wrapMode: Text.Wrap
            }
        }

        SummaryStatusBanners {
            Layout.fillWidth: true
            vm: root.vm
            meetingId: root.meetingId
            shell: root.shell
            showJob: false
            onRetryRequested: root.startQuick()
        }

        // ---- futó állapot (összefoglaló készül / témák javaslása) ----
        Rectangle {
            visible: root.vm.jobRunning
            Layout.fillWidth: true
            Layout.maximumWidth: 640
            implicitHeight: jobCol.implicitHeight + 36
            radius: Theme.radiusPopup
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: jobCol
                anchors { fill: parent; leftMargin: 20; rightMargin: 20; topMargin: 18; bottomMargin: 18 }
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    TSpinner { size: 16 }
                    TLabel {
                        Layout.fillWidth: true
                        text: root.vm.jobTitle
                        font.pixelSize: Theme.fontHeading
                        font.weight: Theme.weightSemiBold
                        elide: Text.ElideRight
                    }
                }
                // Több szakasz (részenkénti jegyzet + összefésülés): szakasz-lista, a jegyzetelésnél
                // valós csík a kész részekből. Egy szakasz: csík (határozatlan — a modell nem ad
                // köztes haladást) + a szakasz neve.
                ColumnLayout {
                    visible: root.vm.jobStages.length > 1
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: root.vm.jobStages
                        JobStageRow {
                            required property var modelData
                            Layout.fillWidth: true
                            labelWidth: 180
                            label: modelData.label
                            stageState: modelData.state
                            percent: modelData.percent
                            detail: modelData.detail
                            progressText: modelData.detail
                        }
                    }
                }
                TProgressBar {
                    visible: root.vm.jobStages.length <= 1
                    Layout.fillWidth: true
                    thickness: 6
                    indeterminate: root.vm.jobPercent < 0
                    value: root.vm.jobPercent < 0 ? 0 : root.vm.jobPercent / 100
                }
                TLabel {
                    visible: text !== ""
                    Layout.fillWidth: true
                    text: root.vm.jobStages.length > 1 ? ""
                        : root.vm.jobStage !== "" ? root.vm.jobStageLabel : root.vm.jobMessage
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
                RowLayout {
                    visible: root.vm.jobReusedNote !== ""
                    Layout.fillWidth: true
                    spacing: 8
                    TIcon { name: "info"; size: 14; color: Theme.textMuted; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
                    TLabel {
                        Layout.fillWidth: true
                        text: root.vm.jobReusedNote
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                }
                TDivider { Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    TLabel {
                        Layout.fillWidth: true
                        text: root.vm.jobCancelling
                              ? (root.vm.jobStages.length > 1
                                 ? qsTr("Megszakítás folyamatban; a kész részek jegyzetei megmaradnak, a következő futás onnan folytatja.")
                                 : qsTr("Megszakítás folyamatban; az átirat érintetlen marad."))
                              : qsTr("Közben nyugodtan dolgozz tovább; szólunk, ha kész.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TButton {
                        text: root.vm.jobCancelling ? qsTr("Megszakítás…") : qsTr("Megszakítás")
                        enabled: !root.vm.jobCancelling
                        size: "small"
                        implicitHeight: 30
                        leftPadding: 12; rightPadding: 12
                        onClicked: if (root.shell) root.shell.cancelJob(root.meetingId, root.vm.jobKind)
                    }
                }
            }
        }

        // ---- a két út ----
        GridLayout {
            visible: !root.vm.jobRunning
            Layout.fillWidth: true
            columns: col.width < 520 ? 1 : 2
            columnSpacing: 14
            rowSpacing: 14

            ChoiceCard {
                recommended: true
                iconName: "sparkles"
                title: qsTr("Gyors összefoglaló")
                description: qsTr("Vezetői összefoglaló döntésekkel, nyitott kérdésekkel és teendőkkel, mellette időrendi memó a megbeszélés menetéről.")
                note: qsTr("rövid megbeszélésnél kb. 1 perc; hosszabbnál részenként halad")
                actionText: qsTr("Összefoglaló készítése")
                onTriggered: root.startQuick()
            }
            ChoiceCard {
                iconName: "list-tree"
                title: qsTr("Témánkénti elemzés")
                description: qsTr("A modell témákat javasol, te átírod, törlöd vagy kiegészíted őket, aztán témánként részletes elemzés készül.")
                note: root.vm.hasTopics ? qsTr("a téma-lista már megvan; onnan folytatható, ahol abbamaradt")
                                        : qsTr("2 lépés, témánként 1–2 perc")
                actionText: root.vm.hasTopics ? qsTr("Témák megnyitása") : qsTr("Témák javaslása")
                onTriggered: root.startTopics()
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            TLabel {
                text: root.vm.cloudSelected
                    ? qsTr("Szolgáltató: %1 · %2").arg(root.vm.providerLabel).arg(root.vm.cloudTierLabel)
                    : qsTr("Szolgáltató: %1 · a modell a Beállítások › Összefoglaló alatt cserélhető").arg(root.vm.providerLabel)
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                width: Math.min(implicitWidth, col.width)
                height: Math.max(24, implicitHeight)
                verticalAlignment: Text.AlignVCenter
            }
            TButton {
                text: root.vm.cloudSelected ? qsTr("Szint módosítása") : qsTr("Megnyitás")
                variant: "ghost"; size: "small"
                implicitHeight: 24
                muted: true
                onClicked: if (root.shell) root.shell.openSettings(root.vm.cloudSelected ? "cloud" : "summary")
            }
        }

        RowLayout {
            visible: root.vm.cloudTeaser
            Layout.fillWidth: true
            spacing: 6
            TLabel {
                text: qsTr("Nem akarsz kulcsokkal bajlódni? A Tanara Cloud hamarosan jön.")
                muted: true
                font.pixelSize: Theme.fontSmall
            }
            TButton {
                text: qsTr("Érdekel")
                variant: "ghost"; size: "small"
                implicitHeight: 24
                onClicked: if (root.shell) root.shell.openSettings("cloud")
            }
            Item { Layout.fillWidth: true }
        }
    }
}
