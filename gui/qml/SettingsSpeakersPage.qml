import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Speakers: your own name and voiceprint, the speaker-recognition voice models (download,
// verify, delete, enable, rescan), the local speaker separation (the switch + the segmentation
// models: same rows, no enable switch) and the voiceprint backfill status (k / N; when done, the
// list of skipped samples per person). Enabling the models is part of the draft (Save); download
// and delete are immediate.
// Demo states: verify (verification + size mismatch), delete (delete confirmation open),
// partial (skipped-samples list expanded).
//   SettingsSpeakersPage { vm: model }
Column {
    id: root

    property var vm: null
    readonly property var models: vm ? vm.voiceModels : null
    readonly property var segModels: vm ? vm.segmentationModels : null
    readonly property string demoState: models ? models.demoState : ""
    property bool skipsExpanded: false

    spacing: 22

    // On opening the page: rescan and verify the files (size).
    onVisibleChanged: if (visible && models) models.refresh()

    function askDelete(id, name, list) {
        deleteDialog.list = list || root.models
        deleteDialog.modelId = id
        deleteDialog.modelName = name
        deleteDialog.errorText = ""
        deleteDialog.open()
    }

    // ---- confirmation for deleting a downloaded model ----
    TDialog {
        id: deleteDialog
        objectName: "voiceModelDeleteDialog"
        property var list: root.models
        property string modelId: ""
        property string modelName: ""
        property string errorText: ""
        title: qsTr("Törlöd a(z) %1 modellt?").arg(modelName)
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.45
            text: deleteDialog.list === root.models
                ? qsTr("A letöltött modellfájl törlődik a gépről. A vele készült hanglenyomatok megmaradnak, és ha később újra letöltöd, ismét használhatók.")
                : qsTr("A letöltött modellfájl törlődik a gépről. Később újra letöltheted.")
        }
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            text: deleteDialog.list === root.models
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
                    const error = deleteDialog.list.deleteModel(deleteDialog.modelId)
                    if (error !== "") deleteDialog.errorText = error
                    else deleteDialog.accept()
                }
            }
        ]
    }

    // Demo states that need an open surface.
    function applyDemo() {
        if (root.demoState === "B08delete") root.askDelete("campplus", "CAM++")
        else if (root.demoState === "B08partial") root.skipsExpanded = true
    }
    onDemoStateChanged: Qt.callLater(applyDemo)
    Component.onCompleted: Qt.callLater(applyDemo)

    // ---- YOU ----
    Column {
        width: parent.width
        spacing: 12
        TSectionLabel { text: qsTr("Te") }
        Column {
            width: parent.width
            spacing: 6
            TLabel { text: qsTr("Saját neved"); font.weight: Theme.weightSemiBold }
            SettingsTextField {
                id: nameField
                objectName: "userNameField"
                width: Math.min(parent.width, 320)
                value: root.vm ? root.vm.userName : ""
                hasError: root.vm && root.vm.errors.userName !== undefined
                Accessible.name: qsTr("Saját neved")
                onEdited: (t) => root.vm.userName = t
            }
            TLabel {
                width: parent.width
                text: root.vm && root.vm.errors.userName !== undefined ? root.vm.errors.userName
                    : qsTr("Így jelensz meg az átiratokban; a saját hanglenyomatod ehhez a névhez tartozik.")
                color: root.vm && root.vm.errors.userName !== undefined ? Theme.dangerInk : Theme.textMuted
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
        }
        Rectangle {
            width: parent.width
            height: 40
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            TIcon {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                name: "fingerprint"
                size: 16
                color: root.vm && root.vm.hasVoiceprint ? Theme.successInk : Theme.textMuted
            }
            TLabel {
                x: 38
                width: parent.width - 38 - peopleLink.width - 24
                anchors.verticalCenter: parent.verticalCenter
                text: root.vm ? root.vm.voiceprintText : ""
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
            T.AbstractButton {
                id: peopleLink
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: linkRow.implicitWidth + 12
                height: 28
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.role: Accessible.Link
                Accessible.name: qsTr("Személyek kezelése")
                onClicked: root.vm.openPeople()
                Keys.onReturnPressed: click()
                Keys.onSpacePressed: click()
                background: Rectangle {
                    radius: Theme.radiusControl
                    color: Theme.stateLayer
                    opacity: peopleLink.down ? Theme.pressedOpacity : peopleLink.hovered ? Theme.hoverOpacity : 0
                    TFocusRing { visible: peopleLink.visualFocus }
                }
                contentItem: Item {
                    Row {
                        id: linkRow
                        anchors.centerIn: parent
                        spacing: 4
                        TLabel {
                            text: qsTr("Személyek kezelése")
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightMedium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        TIcon { name: "arrow-right"; size: 14; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
            }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- VOICE MODELS ----
    Column {
        width: parent.width
        spacing: 12
        TSectionLabel { text: qsTr("Hangmodellek") }
        TLabel {
            width: parent.width
            text: qsTr("Ezekkel ismeri fel a Tanara, ki beszél. A modellek a gépeden futnak; a felvétel nem megy sehova.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }

        // No model at all: voice analysis is off.
        Rectangle {
            objectName: "voiceModelsNone"
            visible: root.models !== null && !root.models.anyAvailable
            width: parent.width
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
                        enabled: root.models !== null && root.models.defaultState === "missing"
                        text: qsTr("WeSpeaker letöltése (%1)").arg(root.models ? root.models.defaultSizeText : "")
                        onClicked: root.models.download("wespeaker-resnet34-lm")
                    }
                    TLabel {
                        visible: root.models !== null && root.models.defaultState === "downloading"
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("letöltés… %1").arg(root.models ? root.models.defaultProgressText : "")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }

        // There are models, but none is enabled.
        TBanner {
            objectName: "voiceModelsAllOff"
            visible: root.models !== null && root.models.anyAvailable && root.models.activeCount === 0
                && !root.models.fallbackActive
            width: parent.width
            tone: "warn"
            text: qsTr("Egy hangmodell sincs bekapcsolva, ezért a hangelemzés ki van kapcsolva.")
        }

        // WeSpeaker is missing (or not enabled), so the CAM++ fallback runs.
        Rectangle {
            objectName: "voiceModelsFallback"
            visible: root.models !== null && root.models.fallbackActive
            width: parent.width
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
                    text: root.models && root.models.defaultState === "missing"
                        ? qsTr("Most a CAM++ tartalék fut, mert a WeSpeaker nincs letöltve.")
                        : qsTr("Most a CAM++ tartalék fut, mert a WeSpeaker nincs bekapcsolva.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                TButton {
                    objectName: "voiceModelsFallbackDownload"
                    visible: root.models !== null && root.models.defaultState === "missing"
                    size: "small"
                    iconName: "arrow-down-to-line"
                    text: qsTr("Letöltés (%1)").arg(root.models ? root.models.defaultSizeText : "")
                    onClicked: root.models.download("wespeaker-resnet34-lm")
                }
            }
        }

        // No network: the download does not start.
        TBanner {
            objectName: "voiceModelsOffline"
            visible: root.models !== null && root.models.offline
            width: parent.width
            tone: "warn"
            iconName: "cloud"
            text: qsTr("Nincs internetkapcsolat: a letöltés most nem indul. A már letöltött modellek működnek.")
        }

        Repeater {
            model: root.models
            SettingsVoiceModelCard {
                // Roles go through `model` (the "state" / "enabled" names are the Item's own properties).
                required property var model
                width: parent.width
                list: root.models
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

        // ---- backfilling samples with the new model ----
        Rectangle {
            objectName: "voiceBackfill"
            visible: root.models !== null && root.models.backfillState !== ""
            width: parent.width
            implicitHeight: bfCol.implicitHeight + 24
            radius: Theme.radiusControl
            color: root.models && root.models.backfillState === "partial" ? Theme.warnSoft : Theme.sunken
            border.width: 1
            border.color: root.models && root.models.backfillState === "partial" ? Theme.warnLine : Theme.border
            Column {
                id: bfCol
                x: 12; y: 12
                width: parent.width - 24
                spacing: 8
                RowLayout {
                    width: parent.width
                    spacing: 8
                    TSpinner {
                        visible: root.models !== null && root.models.backfillState === "running"
                        size: 14
                        color: Theme.textMuted
                    }
                    TIcon {
                        visible: root.models !== null && root.models.backfillState !== "running"
                        name: root.models && root.models.backfillState === "partial" ? "triangle-alert" : "circle-check"
                        size: 15
                        color: root.models && root.models.backfillState === "partial" ? Theme.warnInk : Theme.successInk
                    }
                    TLabel {
                        objectName: "voiceBackfillText"
                        Layout.fillWidth: true
                        text: root.models ? root.models.backfillText : ""
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    T.AbstractButton {
                        id: bfLink
                        objectName: "voiceBackfillPeople"
                        visible: root.models !== null && root.models.backfillState === "partial"
                        implicitWidth: bfLinkRow.implicitWidth + 12
                        implicitHeight: 26
                        hoverEnabled: true
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Link
                        Accessible.name: qsTr("Személyek")
                        onClicked: {
                            const skips = root.models.backfillSkips
                            if (skips.length > 0) root.vm.openPerson(skips[0].person)
                            else root.vm.openPeople()
                        }
                        Keys.onReturnPressed: click()
                        Keys.onSpacePressed: click()
                        background: Rectangle {
                            radius: Theme.radiusControl
                            color: Theme.stateLayer
                            opacity: bfLink.down ? Theme.pressedOpacity : bfLink.hovered ? Theme.hoverOpacity : 0
                            TFocusRing { visible: bfLink.visualFocus }
                        }
                        contentItem: Item {
                            Row {
                                id: bfLinkRow
                                anchors.centerIn: parent
                                spacing: 4
                                TLabel {
                                    text: qsTr("Személyek")
                                    color: Theme.accent
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Theme.weightMedium
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                TIcon { name: "arrow-right"; size: 14; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                    }
                }
                TProgressBar {
                    visible: root.models !== null && root.models.backfillState === "running"
                    width: parent.width
                    thickness: 4
                    indeterminate: root.models !== null && root.models.backfillProgress < 0
                    value: root.models ? Math.max(0, root.models.backfillProgress) : 0
                }
                // Done, but skipped samples remain: whose, which, why (dropdown list).
                T.AbstractButton {
                    id: skipsToggle
                    objectName: "voiceBackfillDetails"
                    visible: root.models !== null && root.models.backfillState === "partial"
                        && root.models.backfillSkips.length > 0
                    implicitWidth: skipsToggleRow.implicitWidth + 8
                    implicitHeight: 24
                    hoverEnabled: true
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: skipsToggleLabel.text
                    onClicked: root.skipsExpanded = !root.skipsExpanded
                    Keys.onReturnPressed: click()
                    Keys.onSpacePressed: click()
                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.stateLayer
                        opacity: skipsToggle.down ? Theme.pressedOpacity : skipsToggle.hovered ? Theme.hoverOpacity : 0
                        TFocusRing { visible: skipsToggle.visualFocus }
                    }
                    contentItem: Item {
                        Row {
                            id: skipsToggleRow
                            anchors.verticalCenter: parent.verticalCenter
                            x: 4
                            spacing: 4
                            TIcon {
                                name: root.skipsExpanded ? "chevron-down" : "chevron-right"
                                size: 14
                                color: Theme.text
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            TLabel {
                                id: skipsToggleLabel
                                text: root.skipsExpanded ? qsTr("Melyik minták?") : qsTr("Melyik minták? (%1)").arg(root.models ? root.models.backfillSkips.length : 0)
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightMedium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
                Column {
                    objectName: "voiceBackfillSkips"
                    visible: skipsToggle.visible && root.skipsExpanded
                    width: parent.width
                    spacing: 0
                    Repeater {
                        model: root.models ? root.models.backfillSkips : []
                        Rectangle {
                            id: skipRow
                            required property var modelData
                            required property int index
                            objectName: "voiceBackfillSkip"
                            width: parent.width
                            implicitHeight: skipLayout.implicitHeight + 12
                            color: "transparent"
                            Rectangle { visible: skipRow.index > 0; width: parent.width; height: 1; color: Theme.warnLine; opacity: 0.6 }
                            RowLayout {
                                id: skipLayout
                                x: 0; y: 6
                                width: parent.width
                                spacing: 10
                                // The person's name: a link to the People window (they get selected).
                                T.AbstractButton {
                                    id: personLink
                                    objectName: "voiceBackfillSkipPerson"
                                    Layout.preferredWidth: 150
                                    Layout.alignment: Qt.AlignTop
                                    implicitHeight: 20
                                    hoverEnabled: true
                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Link
                                    Accessible.name: skipRow.modelData.person
                                    onClicked: root.vm.openPerson(skipRow.modelData.person)
                                    Keys.onReturnPressed: click()
                                    Keys.onSpacePressed: click()
                                    background: Item { TFocusRing { visible: personLink.visualFocus; targetRadius: 4 } }
                                    contentItem: TLabel {
                                        text: skipRow.modelData.person
                                        color: Theme.accent
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Theme.weightMedium
                                        font.underline: personLink.hovered
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                }
                                Column {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    TLabel {
                                        width: parent.width
                                        text: skipRow.modelData.sample
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                    TLabel {
                                        width: parent.width
                                        text: skipRow.modelData.reason + " · " + qsTr("hiányzik: %1").arg(skipRow.modelData.models)
                                        muted: true
                                        font.pixelSize: Theme.fontCaption
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
                TLabel {
                    visible: root.models !== null && root.models.backfillState !== "done"
                    width: parent.width
                    text: root.models && root.models.backfillState === "partial"
                        ? qsTr("Ezekhez a mintákhoz már nincs meg a felvétel, ezért csak a régi modellel számolnak. A Személyek ablakban újra felveheted őket.")
                        : qsTr("Közben dolgozhatsz; a felismerés a régi mintákkal is működik.")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    cssLineHeight: 1.4
                    wrapMode: Text.Wrap
                }
            }
        }

        // Fusion + the recommended default.
        Column {
            width: parent.width
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
                onClicked: root.models.rescan()
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
    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- LOCAL SPEAKER SEPARATION (diarization) ----
    Column {
        objectName: "diarizationSection"
        width: parent.width
        spacing: 12
        TSectionLabel { text: qsTr("Beszélők elkülönítése (helyi)") }
        SettingsSwitchRow {
            objectName: "localDiarizationSwitch"
            width: parent.width
            text: qsTr("Beszélők felismerése helyben, ha a leíró szolgáltató nem adja")
            helper: qsTr("A Whisper és a Cloud Fast csomag nem választja szét a beszélőket, ezért ezekhez kell; a Soniox maga megteszi, ahhoz nem. Az átirat után a gépeden fut, a felvétel nem megy sehova.")
            checked: root.vm ? root.vm.localDiarization : true
            onToggled: (on) => root.vm.localDiarization = on
        }
        // No segmentation model: the local separation cannot run.
        TBanner {
            objectName: "segmentationNone"
            visible: root.segModels !== null && !root.segModels.anyAvailable
            width: parent.width
            tone: "warn"
            text: qsTr("Nincs szegmentáló modell — a Whisper-átirat beszélők nélkül marad. Töltsd le az alapmodellt.")
        }
        TLabel {
            width: parent.width
            text: qsTr("A szegmentáló modell azt mondja meg, ki mikor beszél; a beszélők szétválasztásához a fenti hangmodell is kell.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
        Repeater {
            model: root.segModels
            SettingsVoiceModelCard {
                required property var model
                width: parent.width
                list: root.segModels
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
                onDeleteRequested: (id, name) => root.askDelete(id, name, root.segModels)
            }
        }
    }
}
