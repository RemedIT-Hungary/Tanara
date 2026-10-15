import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// The model rows of a Processing card (S2): "Beszélők felismerése" (the voice models: download,
// verify, delete, enable; the empty state with the WeSpeaker download, the CAM++ fallback line, the
// offline notice, the fusion note and "Újraolvasás") or "Beszélők elkülönítése" (the segmentation
// models: same rows, no enabled switch, the warning without a model). Enabling is part of the
// draft (Save); download and delete are immediate. The delete confirmation is asked here.
// Demo state B08delete: the confirmation open.
//   SettingsModelList { list: vm.voiceModels; vm: model }
//   SettingsModelList { list: vm.segmentationModels; vm: model }
ColumnLayout {
    id: root

    property var vm: null
    property var list: null              // VoiceModelListModel
    readonly property bool segmentation: list !== null && list.kind === "segmentation"
    readonly property string demoState: list ? list.demoState : ""

    spacing: 12

    // On opening the card: rescan and verify the voice models' files (size).
    onVisibleChanged: if (visible && list && !segmentation) list.refresh()

    function askDelete(id, name) {
        deleteDialog.modelId = id
        deleteDialog.modelName = name
        deleteDialog.errorText = ""
        deleteDialog.open()
    }
    function applyDemo() {
        if (root.demoState === "B08delete" && !root.segmentation) root.askDelete("campplus", "CAM++")
    }
    onDemoStateChanged: Qt.callLater(applyDemo)
    Component.onCompleted: Qt.callLater(applyDemo)

    // ---- confirmation for deleting a downloaded model ----
    TDialog {
        id: deleteDialog
        objectName: "voiceModelDeleteDialog"
        property string modelId: ""
        property string modelName: ""
        property string errorText: ""
        title: qsTr("Törlöd a(z) %1 modellt?").arg(modelName)
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.45
            text: !root.segmentation
                ? qsTr("A letöltött modellfájl törlődik a gépről. A vele készült hanglenyomatok megmaradnak, és ha később újra letöltöd, ismét használhatók.")
                : qsTr("A letöltött modellfájl törlődik a gépről. Később újra letöltheted.")
        }
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            text: !root.segmentation
                ? qsTr("Amíg nincs meg, a felismerés a többi bekapcsolt modellel megy (ha egy sincs, a CAM++ tartalékkal).")
                : qsTr("Amíg nincs szegmentáló modell, a Whisper-átirat beszélők nélkül marad.")
        }
        TLabel {
            objectName: "voiceModelDeleteError"
            visible: deleteDialog.errorText !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dangerInk
            font.pixelSize: Theme.fontSmall
            text: deleteDialog.errorText
        }
        actions: [
            TButton { text: qsTr("Mégse"); onClicked: deleteDialog.reject() },
            TButton {
                objectName: "voiceModelDeleteConfirm"
                text: qsTr("Törlés")
                variant: "danger"
                onClicked: {
                    const error = root.list.deleteModel(deleteDialog.modelId)
                    if (error !== "") deleteDialog.errorText = error
                    else deleteDialog.accept()
                }
            }
        ]
    }

    // Built only for its own kind (the two lists share the objectNames of their rows).
    Loader {
        active: !root.segmentation
        visible: active
        Layout.fillWidth: true
        sourceComponent: ColumnLayout {
            width: parent ? parent.width : 0
            spacing: 12
            TLabel {
                Layout.fillWidth: true
                text: qsTr("Ezekkel ismeri fel a Tanara, ki beszél. A modellek a gépeden futnak; a felvétel nem megy sehova.")
                muted: true
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }

            // No model at all: voice analysis is off.
            Rectangle {
                objectName: "voiceModelsNone"
                visible: root.list !== null && !root.list.anyAvailable
                Layout.fillWidth: true
                implicitHeight: noneCol.implicitHeight + 28
                radius: Theme.radiusPopup
                color: Theme.accentSoft
                border.width: 1
                border.color: Theme.accentLine
                TIcon { x: 14; y: 16; name: "audio-lines"; size: 18; color: Theme.accent }
                Column {
                    id: noneCol
                    x: 44; y: 14
                    width: parent.width - 58
                    spacing: 6
                    TLabel {
                        width: parent.width
                        text: qsTr("A hangelemzés ki van kapcsolva")
                        font.weight: Theme.weightSemiBold
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        width: parent.width
                        text: qsTr("Nincs letöltött hangmodell. Az átirat így is szerkeszthető, de a beszélőket nem ismerem fel hangra, és hanglenyomat sem készül.")
                        font.pixelSize: Theme.fontSmall
                        cssLineHeight: 1.45
                        wrapMode: Text.Wrap
                    }
                    Item { width: 1; height: 4 }
                    Row {
                        spacing: 10
                        TButton {
                            objectName: "voiceModelsNoneDownload"
                            variant: "primary"
                            size: "small"
                            iconName: "arrow-down-to-line"
                            enabled: root.list !== null && root.list.defaultState === "missing"
                            text: qsTr("WeSpeaker letöltése (%1)").arg(root.list ? root.list.defaultSizeText : "")
                            onClicked: root.list.download("wespeaker-resnet34-lm")
                        }
                        TLabel {
                            visible: root.list !== null && root.list.defaultState === "downloading"
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("letöltés… %1").arg(root.list ? root.list.defaultProgressText : "")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }

            // There are models, but none is enabled.
            TBanner {
                objectName: "voiceModelsAllOff"
                visible: root.list !== null && root.list.anyAvailable && root.list.activeCount === 0
                    && !root.list.fallbackActive
                Layout.fillWidth: true
                tone: "warn"
                text: qsTr("Egy hangmodell sincs bekapcsolva, ezért a hangelemzés ki van kapcsolva.")
            }

            // WeSpeaker is missing (or not enabled), so the CAM++ fallback runs.
            Rectangle {
                objectName: "voiceModelsFallback"
                visible: root.list !== null && root.list.fallbackActive
                Layout.fillWidth: true
                implicitHeight: fbRow.implicitHeight + 20
                radius: Theme.radiusControl
                color: Theme.sunken
                border.width: 1
                border.color: Theme.border
                RowLayout {
                    id: fbRow
                    x: 14; y: 10
                    width: parent.width - 28
                    spacing: 10
                    TIcon { name: "info"; size: 16; color: Theme.textMuted }
                    TLabel {
                        objectName: "voiceModelsFallbackText"
                        Layout.fillWidth: true
                        text: root.list && root.list.defaultState === "missing"
                            ? qsTr("Most a CAM++ tartalék fut, mert a WeSpeaker nincs letöltve.")
                            : qsTr("Most a CAM++ tartalék fut, mert a WeSpeaker nincs bekapcsolva.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TButton {
                        objectName: "voiceModelsFallbackDownload"
                        visible: root.list !== null && root.list.defaultState === "missing"
                        size: "small"
                        iconName: "arrow-down-to-line"
                        text: qsTr("Letöltés (%1)").arg(root.list ? root.list.defaultSizeText : "")
                        onClicked: root.list.download("wespeaker-resnet34-lm")
                    }
                }
            }

            // No network: the download does not start.
            TBanner {
                objectName: "voiceModelsOffline"
                visible: root.list !== null && root.list.offline
                Layout.fillWidth: true
                tone: "warn"
                iconName: "cloud"
                text: qsTr("Nincs internetkapcsolat: a letöltés most nem indul. A már letöltött modellek működnek.")
            }

            Repeater {
                model: root.list
                SettingsVoiceModelCard {
                    // Roles go through `model` (the "state" / "enabled" names are the Item's own properties).
                    required property var model
                    Layout.fillWidth: true
                    list: root.list
                    modelId: model.modelId
                    name: model.name
                    blurb: model.blurb
                    license: model.license
                    licenseNote: model.licenseNote
                    sourceText: model.sourceText
                    sourceUrl: model.sourceUrl
                    sizeText: model.sizeText
                    dimText: model.dimText
                    state_: model.state
                    progress: model.progress
                    progressText: model.progressText
                    errorText: model.errorText
                    enabledModel: model.enabled
                    canEnable: model.canEnable
                    recommended: model.recommended
                    corrupt: model.corrupt
                    hasFile: model.hasFile
                    canDelete: model.canDelete
                    onDeleteRequested: (id, name) => root.askDelete(id, name)
                }
            }

            // Fusion + the recommended default.
            Column {
                Layout.fillWidth: true
                spacing: 4
                TLabel {
                    width: parent.width
                    text: qsTr("Több bekapcsolt modell együtt dönt; a lassabb modellek lassítják az elemzést.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
                TLabel {
                    width: parent.width
                    text: qsTr("Ajánlott: WeSpeaker egyedül; a CAM++ tartalék.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
            }

            // A model file copied in by hand (e.g. from another machine): rescan.
            Row {
                spacing: 6
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Kézzel másoltál be modellfájlt?")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                }
                T.AbstractButton {
                    id: rescanLink
                    objectName: "voiceModelsRescan"
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: rescanRow.implicitWidth + 8
                    implicitHeight: 24
                    hoverEnabled: true
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Link
                    Accessible.name: qsTr("Újraolvasás")
                    onClicked: root.list.rescan()
                    Keys.onReturnPressed: click()
                    Keys.onSpacePressed: click()
                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.stateLayer
                        opacity: rescanLink.down ? Theme.pressedOpacity : rescanLink.hovered ? Theme.hoverOpacity : 0
                        TFocusRing { visible: rescanLink.visualFocus }
                    }
                    contentItem: Item {
                        Row {
                            id: rescanRow
                            anchors.centerIn: parent
                            spacing: 4
                            TIcon { name: "refresh-cw"; size: 13; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                            TLabel {
                                text: qsTr("Újraolvasás")
                                color: Theme.accent
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightMedium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                    TToolTip { visible: rescanLink.hovered; text: qsTr("A modellek mappájának újraolvasása és a fájlok ellenőrzése") }
                }
            }
        }
    }

    // Built only for its own kind (the two lists share the objectNames of their rows).
    Loader {
        active: root.segmentation
        visible: active
        Layout.fillWidth: true
        sourceComponent: ColumnLayout {
            width: parent ? parent.width : 0
            spacing: 12
            // No segmentation model: the local separation cannot run.
            TBanner {
                objectName: "segmentationNone"
                visible: root.list !== null && !root.list.anyAvailable
                Layout.fillWidth: true
                tone: "warn"
                text: qsTr("Nincs szegmentáló modell — a Whisper-átirat beszélők nélkül marad. Töltsd le az alapmodellt.")
            }
            TLabel {
                Layout.fillWidth: true
                text: qsTr("A szegmentáló modell azt mondja meg, ki mikor beszél; a beszélők szétválasztásához a lenti hangmodell is kell.")
                muted: true
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
            Repeater {
                model: root.list
                SettingsVoiceModelCard {
                    required property var model
                    Layout.fillWidth: true
                    list: root.list
                    selectable: false
                    modelId: model.modelId
                    name: model.name
                    blurb: model.blurb
                    license: model.license
                    licenseNote: model.licenseNote
                    sourceText: model.sourceText
                    sourceUrl: model.sourceUrl
                    sizeText: model.sizeText
                    dimText: model.dimText
                    state_: model.state
                    progress: model.progress
                    progressText: model.progressText
                    errorText: model.errorText
                    enabledModel: model.enabled
                    canEnable: model.canEnable
                    recommended: model.recommended
                    corrupt: model.corrupt
                    hasFile: model.hasFile
                    canDelete: model.canDelete
                    onDeleteRequested: (id, name) => root.askDelete(id, name)
                }
            }
        }
    }
}
