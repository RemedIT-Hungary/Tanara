import QtQuick
import QtQuick.Layouts

// First steps > Services: one row about the voice model. If the recommended model (WeSpeaker)
// is missing, a "Download voice model (28 MB)" button using the same downloader as Settings >
// Speakers; skippable (the step's "Skip" or simply "Continue"). Hidden when the model is there.
//   VoiceModelOnboardingRow { demoState: "B08none" }   (the list takes the App's services)
Rectangle {
    id: root

    property string demoState: ""
    readonly property string st: list.defaultState

    // Shown only if there was something to do (the model was missing) - after a successful
    // download the confirmation stays.
    property bool wasMissing: false
    onStChanged: if (st !== "" && st !== "downloaded" && st !== "bundled") wasMissing = true

    objectName: "voiceModelOnboarding"
    visible: wasMissing
    implicitHeight: col.implicitHeight + 28
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: st === "error" ? Theme.dangerLine : Theme.border

    VoiceModelListModel {
        id: list
        onOpenUrlRequested: (url) => Qt.openUrlExternally(url)
    }
    Component.onCompleted: if (demoState !== "") list.demoState = demoState

    ColumnLayout {
        id: col
        x: 16; y: 14
        width: parent.width - 32
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TIcon {
                name: root.st === "downloaded" || root.st === "bundled" ? "circle-check" : "audio-lines"
                size: 16
                color: root.st === "downloaded" || root.st === "bundled" ? Theme.successInk : Theme.accent
            }
            TLabel {
                Layout.fillWidth: true
                text: root.st === "downloaded" || root.st === "bundled"
                    ? qsTr("A hangmodell letöltve: a Tanara felismeri, ki beszél.")
                    : qsTr("Hangmodell a beszélők felismeréséhez")
                font.weight: Theme.weightSemiBold
                wrapMode: Text.Wrap
            }
        }
        TLabel {
            visible: root.st !== "downloaded" && root.st !== "bundled"
            Layout.fillWidth: true
            text: qsTr("Nélküle is minden működik, csak a beszélőket neked kell elnevezned. Licenc: Apache-2.0; később a Beállítások › Beszélők lapon is letöltheted.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
        RowLayout {
            visible: root.st === "missing" || root.st === "offline" || root.st === "error"
            Layout.fillWidth: true
            spacing: 10
            TButton {
                objectName: "voiceModelOnboardingDownload"
                size: "small"
                iconName: "arrow-down-to-line"
                text: qsTr("Hangmodell letöltése (%1)").arg(list.defaultSizeText)
                onClicked: {
                    if (root.st === "error") list.retry("wespeaker-resnet34-lm")
                    else { list.refresh(); list.download("wespeaker-resnet34-lm") }
                }
            }
            TLabel {
                Layout.fillWidth: true
                visible: root.st === "offline" || root.st === "error"
                text: root.st === "offline" ? qsTr("Nincs internetkapcsolat.") : list.defaultErrorText
                color: root.st === "error" ? Theme.dangerInk : Theme.textMuted
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            visible: root.st === "downloading"
            Layout.fillWidth: true
            spacing: 12
            TProgressBar {
                Layout.fillWidth: true
                thickness: 6
                value: Math.max(0, list.defaultProgress)
                indeterminate: list.defaultProgress < 0
            }
            TLabel { text: list.defaultProgressText; mono: true; muted: true; font.pixelSize: Theme.fontCaption }
            TButton { size: "small"; text: qsTr("Mégse"); onClicked: list.cancel("wespeaker-resnet34-lm") }
        }
    }
}
