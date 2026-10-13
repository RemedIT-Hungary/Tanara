import QtQuick
import QtQuick.Layouts

// Launcher embedded in the Overview "Transcript" step: before the transcript, transcription
// starts here in the PROCESSING column. It holds the content of the old pre-transcript view's
// (PreTranscriptView) step 3 and error card, condensed for the 272 px step column:
//   - missing setting (warn box, the button leads to the matching Settings page);
//   - Tanara Cloud: Fast / Accurate tier and language;
//   - "Start transcription" (primary) + provider, or the reason it is locked;
//   - error: danger box with the message, "Retry" and the fixing action;
//   - while running: a compact list of stages (state circle, name, progress), the
//     "Feel free to keep working meanwhile" row and "Cancel" (cancelJob(..., Transcribe)). The
//     overall progress and the estimated time are in the step's subtext and strip.
// vm: PreTranscriptViewModel · shell: ShellActions or null · meetingId: the meeting.
ColumnLayout {
    id: root

    property var vm: null
    property var shell: null
    property string meetingId: ""

    readonly property bool failed: vm !== null && vm.state === "failed"
    readonly property bool running: vm !== null && vm.state === "running"
    readonly property var blocker: vm ? vm.blocker : ({})

    signal startRequested()

    // For a missing transcription provider Settings opens with the STT card highlighted.
    function openSettings(page) {
        if (!root.shell) return
        if (page === "providers") root.shell.openSettings(page, "stt")
        else root.shell.openSettings(page)
    }
    function start() {
        root.startRequested()
        if (root.shell) root.shell.startTranscription(root.meetingId)
    }

    spacing: 8

    // One stage, compact (for the 272 px column): 14 px circle · name · state / percent.
    component StageLine: RowLayout {
        id: sl
        property var stage: ({})
        readonly property string st: stage.state || "waiting"
        spacing: 7
        Item {
            implicitWidth: 14; implicitHeight: 14
            Rectangle {
                anchors.fill: parent
                radius: 7
                visible: sl.st !== "waiting"
                color: sl.st === "done" ? Theme.successSoft : sl.st === "failed" ? Theme.dangerSoft : "transparent"
                border.width: sl.st === "running" ? 2 : 0
                border.color: Theme.accent
            }
            TDashedRect { anchors.fill: parent; visible: sl.st === "waiting"; radius: 7; color: Theme.borderStrong }
            TIcon {
                anchors.centerIn: parent
                visible: sl.st === "done" || sl.st === "failed" || sl.st === "skipped"
                name: sl.st === "done" ? "check" : sl.st === "failed" ? "x" : "minus"
                size: 9
                strokeWidth: 2.5
                color: sl.st === "done" ? Theme.successInk : sl.st === "failed" ? Theme.dangerInk : Theme.textMuted
            }
        }
        TLabel {
            Layout.fillWidth: true
            text: sl.stage.label || ""
            color: sl.st === "running" || sl.st === "done" ? Theme.text : Theme.textMuted
            font.pixelSize: 12
            font.weight: sl.st === "running" ? Theme.weightSemiBold : Theme.weightRegular
            elide: Text.ElideRight
        }
        TLabel {
            text: sl.st === "running" ? (sl.stage.percent >= 0 ? sl.stage.percent + "%" : qsTr("fut"))
                : sl.st === "done" ? qsTr("kész")
                : sl.st === "failed" ? qsTr("nem sikerült")
                : sl.st === "skipped" ? qsTr("kihagyva") : qsTr("vár")
            mono: sl.st === "running" && sl.stage.percent >= 0
            muted: true
            font.pixelSize: Theme.fontMicro
        }
    }

    // ---- running ----
    ColumnLayout {
        objectName: "transcribeRunning"
        visible: root.running
        Layout.fillWidth: true
        spacing: 5
        Repeater {
            model: root.running ? root.vm.stages : []
            StageLine {
                required property var modelData
                objectName: "transcribeStage"
                Layout.fillWidth: true
                stage: modelData
            }
        }
        TLabel {
            Layout.fillWidth: true
            Layout.topMargin: 3
            text: root.vm && root.vm.cancelling ? qsTr("Megszakítás folyamatban; a felvétel és a sávok érintetlenek maradnak.")
                                                : qsTr("Közben nyugodtan dolgozz tovább; szólunk, ha kész.")
            muted: true
            font.pixelSize: 12
            cssLineHeight: 1.4
            wrapMode: Text.Wrap
        }
        TButton {
            objectName: "cancelTranscriptionButton"
            visible: root.vm !== null && root.vm.cancellable
            enabled: root.vm !== null && !root.vm.cancelling
            text: qsTr("Megszakítás")
            size: "small"
            iconName: "x"
            iconSize: 14
            toolTipText: qsTr("Az átírás leáll; a felvétel és a sávok megmaradnak, később újraindítható")
            onClicked: if (root.shell) root.shell.cancelJob(root.meetingId, JobKinds.Transcribe)
        }
    }

    // ---- error ----
    Rectangle {
        objectName: "transcribeError"
        visible: root.failed
        Layout.fillWidth: true
        implicitHeight: errCol.implicitHeight + 20
        radius: Theme.radiusControl
        color: Theme.dangerSoft
        border.width: 1
        border.color: Theme.dangerLine
        ColumnLayout {
            id: errCol
            x: 10; y: 10
            width: parent.width - 20
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TIcon { name: "triangle-alert"; size: 14; color: Theme.dangerInk }
                TLabel {
                    Layout.fillWidth: true
                    text: qsTr("Az átírás nem sikerült")
                    color: Theme.dangerInk
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightSemiBold
                    wrapMode: Text.Wrap
                }
            }
            TLabel {
                Layout.fillWidth: true
                text: root.vm ? root.vm.errorMessage + " " + qsTr("A felvétel és a sávok érintetlenek.") : ""
                font.pixelSize: 12
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
            TLabel {
                visible: root.vm && root.vm.errorDetail !== ""
                Layout.fillWidth: true
                text: root.vm ? root.vm.errorDetail : ""
                mono: true
                muted: true
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.Wrap
            }
            Flow {
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 6
                TButton {
                    objectName: "transcribeRetryButton"
                    text: qsTr("Újra")
                    variant: "primary"
                    size: "small"
                    iconName: "rotate-ccw"
                    iconSize: 14
                    onClicked: root.start()
                }
                TButton {
                    objectName: "transcribeFixButton"
                    visible: root.vm && root.vm.fixActionLabel !== ""
                    text: root.vm ? root.vm.fixActionLabel : ""
                    size: "small"
                    onClicked: root.openSettings(root.vm.fixActionPage)
                }
            }
        }
    }

    // ---- missing setting ----
    Rectangle {
        objectName: "transcribeBlocker"
        visible: !root.failed && !root.running && root.blocker.title !== undefined
        Layout.fillWidth: true
        implicitHeight: blockCol.implicitHeight + 20
        radius: Theme.radiusControl
        color: Theme.warnSoft
        border.width: 1
        border.color: Theme.warnLine
        ColumnLayout {
            id: blockCol
            x: 10; y: 10
            width: parent.width - 20
            spacing: 4
            TLabel {
                Layout.fillWidth: true
                text: root.blocker.title || ""
                color: Theme.warnInk
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightSemiBold
                wrapMode: Text.Wrap
            }
            TLabel {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.blocker.text || ""
                font.pixelSize: 12
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
            TButton {
                objectName: "transcribeSettingsButton"
                visible: (root.blocker.actionLabel || "") !== ""
                Layout.topMargin: 4
                text: root.blocker.actionLabel || ""
                size: "small"
                trailingIconName: "arrow-right"
                iconSize: 14
                onClicked: root.openSettings(root.blocker.actionPage || "")
            }
        }
    }

    // ---- Tanara Cloud: tier and language ----
    Flow {
        visible: !root.failed && !root.running && root.vm !== null && root.vm.cloudSelected
        Layout.fillWidth: true
        spacing: 6
        TChip {
            visible: root.vm && root.vm.cloudExpertName === ""
            text: qsTr("Gyors")
            tone: root.vm && root.vm.cloudTier === "fast" ? "accent" : "neutral"
            onClicked: root.vm.cloudTier = "fast"
        }
        TChip {
            visible: root.vm && root.vm.cloudExpertName === ""
            text: qsTr("Pontos")
            tone: root.vm && root.vm.cloudTier === "accurate" ? "accent" : "neutral"
            onClicked: root.vm.cloudTier = "accurate"
        }
        TChip {
            visible: root.vm && root.vm.cloudExpertName !== ""
            text: root.vm ? qsTr("Expert: %1").arg(root.vm.cloudExpertName) : ""
            removable: true
            onRemoved: root.vm.cloudTier = "accurate"
        }
        TButton {
            text: root.vm ? root.vm.cloudLanguageLabel : ""
            variant: "ghost"
            size: "small"
            implicitHeight: 24
            toolTipText: qsTr("Nyelv és Expert mód…")
            onClicked: root.openSettings("cloud")
        }
    }
    TLabel {
        visible: !root.failed && !root.running && root.vm !== null && root.vm.cloudSelected && root.vm.cloudWarning !== ""
        Layout.fillWidth: true
        text: root.vm ? root.vm.cloudWarning : ""
        color: Theme.warnInk
        font.pixelSize: 12
        cssLineHeight: 1.4
        wrapMode: Text.Wrap
    }

    // ---- start ----
    ColumnLayout {
        visible: !root.failed && !root.running
        Layout.fillWidth: true
        spacing: 5
        TButton {
            objectName: "startTranscriptionButton"
            text: qsTr("Átírás indítása")
            variant: "primary"
            size: "small"
            enabled: root.vm !== null && root.vm.canStart
            iconName: enabled ? "audio-lines" : "lock"
            iconSize: 14
            onClicked: root.start()
        }
        TLabel {
            Layout.fillWidth: true
            visible: text !== ""
            text: !root.vm ? ""
                 : root.vm.canStart ? qsTr("Szolgáltató: %1").arg(root.vm.providerLabel)
                 : root.blocker.title === undefined ? (root.blocker.reason || "") : ""
            muted: true
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        RowLayout {
            visible: root.vm !== null && root.vm.cloudTeaser
            Layout.fillWidth: true
            spacing: 4
            TLabel {
                Layout.fillWidth: true
                text: qsTr("Nem akarsz kulcsokkal bajlódni? A Tanara Cloud hamarosan jön.")
                muted: true
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            TButton {
                text: qsTr("Érdekel")
                variant: "ghost"; size: "small"
                implicitHeight: 24
                onClicked: root.openSettings("cloud")
            }
        }
    }
}
