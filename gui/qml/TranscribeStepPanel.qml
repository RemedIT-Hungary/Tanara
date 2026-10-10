import QtQuick
import QtQuick.Layouts

// Az Áttekintés „Átirat” lépésébe ágyazott indító (design/handoff-v3 V5, U6): átirat előtt a
// FELDOLGOZÁS oszlopban itt indul az átírás. A régi átirat előtti nézet (PreTranscriptView)
// 3. lépésének és hiba-kártyájának tartalma, a 272 px-es lépés-oszlophoz tömörítve:
//   - hiányzó beállítás (warn doboz, a gomb a Beállítások megfelelő lapjára visz);
//   - Tanara Cloud: Gyors / Pontos szint és nyelv;
//   - „Átírás indítása” (primary) + szolgáltató, ill. a zárolás oka;
//   - hiba: danger doboz az üzenettel, „Újra” és a javító művelet.
// vm: PreTranscriptViewModel · shell: ShellActions vagy null · meetingId: a megbeszélés.
ColumnLayout {
    id: root

    property var vm: null
    property var shell: null
    property string meetingId: ""

    readonly property bool failed: vm !== null && vm.state === "failed"
    readonly property var blocker: vm ? vm.blocker : ({})

    signal startRequested()

    // A hiányzó átíró-szolgáltatóhoz a Beállítások az STT-kártyát kiemelve nyílik (B04).
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

    // ---- hiba ----
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

    // ---- hiányzó beállítás ----
    Rectangle {
        objectName: "transcribeBlocker"
        visible: !root.failed && root.blocker.title !== undefined
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

    // ---- Tanara Cloud: szint és nyelv ----
    Flow {
        visible: !root.failed && root.vm !== null && root.vm.cloudSelected
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
        visible: !root.failed && root.vm !== null && root.vm.cloudSelected && root.vm.cloudWarning !== ""
        Layout.fillWidth: true
        text: root.vm ? root.vm.cloudWarning : ""
        color: Theme.warnInk
        font.pixelSize: 12
        cssLineHeight: 1.4
        wrapMode: Text.Wrap
    }

    // ---- indítás ----
    ColumnLayout {
        visible: !root.failed
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
