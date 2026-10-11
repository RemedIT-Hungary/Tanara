import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// B08 — Beszélők: a saját név és hanglenyomat (az Általános lapról ide költözött), a
// beszélő-felismerő hangmodellek (letöltés, bekapcsolás) és a lenyomat-pótlás állapota.
// A modellek bekapcsolása a piszkozat része (Mentés); a letöltés azonnali.
//   SettingsSpeakersPage { vm: model }
Column {
    id: root

    property var vm: null
    readonly property var models: vm ? vm.voiceModels : null

    spacing: 22

    // ---- TE ----
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

    // ---- HANGMODELLEK ----
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

        // Nincs egyetlen modell sem: a hangelemzés ki van kapcsolva.
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

        // Van modell, de egy sincs bekapcsolva.
        TBanner {
            objectName: "voiceModelsAllOff"
            visible: root.models !== null && root.models.anyAvailable && root.models.activeCount === 0
                && !root.models.fallbackActive
            width: parent.width
            tone: "warn"
            text: qsTr("Egy hangmodell sincs bekapcsolva, ezért a hangelemzés ki van kapcsolva.")
        }

        // A WeSpeaker nincs meg (vagy nincs bekapcsolva), ezért a CAM++ tartalék fut.
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

        // Nincs hálózat: a letöltés nem indul.
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
                // A szerepek a `model`-en át (a „state” / „enabled” név az Item saját tulajdonsága).
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
            }
        }

        // ---- a minták pótlása az új modellel ----
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
                        onClicked: root.vm.openPeople()
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

        // Fúzió + ajánlott alapállapot.
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
    }
}
