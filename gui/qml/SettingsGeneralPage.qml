import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// B01 — Általános: saját név + hanglenyomat, nyelv és téma, a három mappa.
Column {
    id: root

    property var vm: null

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

    // ---- MEGJELENÉS ----
    Column {
        width: parent.width
        spacing: 12
        topPadding: -4
        TSectionLabel { text: qsTr("Megjelenés") }
        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 16
            rowSpacing: 12
            TLabel {
                Layout.preferredWidth: 140
                text: qsTr("Nyelv")
                font.weight: Theme.weightSemiBold
            }
            SettingsCombo {
                Layout.preferredWidth: 320
                Layout.maximumWidth: 320
                Layout.fillWidth: true
                options: root.vm ? root.vm.uiLanguageOptions : []
                value: root.vm ? root.vm.uiLanguage : ""
                onPicked: (v) => root.vm.uiLanguage = v
            }
            Item { visible: restartNote.visible; Layout.preferredWidth: 140; implicitHeight: 1 }
            TLabel {
                id: restartNote
                visible: root.vm && root.vm.languageNeedsRestart
                Layout.fillWidth: true
                Layout.topMargin: -6
                text: qsTr("A nyelv a Tanara következő indításakor vált.")
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
            TLabel {
                Layout.preferredWidth: 140
                text: qsTr("Téma")
                font.weight: Theme.weightSemiBold
            }
            SettingsSegmented {
                options: [
                    { value: "system", label: qsTr("Rendszer"), iconName: "monitor" },
                    { value: "light", label: qsTr("Világos"), iconName: "sun" },
                    { value: "dark", label: qsTr("Sötét"), iconName: "moon" }
                ]
                value: root.vm ? root.vm.themeMode : "system"
                onPicked: (v) => root.vm.themeMode = v
            }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- MAPPÁK ----
    Column {
        width: parent.width
        spacing: 14
        topPadding: -4
        TSectionLabel { text: qsTr("Mappák") }
        Repeater {
            model: root.vm ? root.vm.folders : []
            Column {
                id: folder
                required property var modelData
                width: parent.width
                spacing: 6
                Row {
                    spacing: 10
                    TLabel {
                        text: folder.modelData.label
                        font.weight: Theme.weightSemiBold
                        anchors.baseline: usage.baseline
                    }
                    TLabel {
                        id: usage
                        text: folder.modelData.usage
                        muted: true
                        font.pixelSize: Theme.fontCaption
                    }
                }
                RowLayout {
                    width: parent.width
                    spacing: 6
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.controlHeight
                        radius: Theme.radiusControl
                        color: Theme.sunken
                        border.width: 1
                        border.color: folder.modelData.error !== "" ? Theme.danger : Theme.border
                        TLabel {
                            id: pathLabel
                            x: 10
                            width: parent.width - 20
                            anchors.verticalCenter: parent.verticalCenter
                            text: folder.modelData.path
                            mono: true
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideMiddle
                            HoverHandler { id: pathHover }
                            TToolTip { visible: pathHover.hovered && pathLabel.truncated; text: folder.modelData.path }
                        }
                    }
                    TButton {
                        text: qsTr("Tallózás…")
                        // Rögzített mappánál (TANARA_HOME) nincs mit tallózni. Elrejtjük, nem
                        // letiltjuk: a letiltott gomb szaggatott kerete (Shape) a szoftveres
                        // rendererrel kilógna a görgetett terület vágásából.
                        visible: !folder.modelData.locked
                        font.pixelSize: Theme.fontSmall
                        onClicked: root.vm.browseFolder(folder.modelData.key)
                    }
                    TIconButton {
                        iconName: "folder-open"
                        iconSize: 15
                        toolTipText: qsTr("Megnyitás a fájlkezelőben")
                        onClicked: root.vm.openFolder(folder.modelData.key)
                    }
                }
                TLabel {
                    width: parent.width
                    text: folder.modelData.error !== "" ? folder.modelData.error : folder.modelData.hint
                    color: folder.modelData.error !== "" ? Theme.dangerInk : Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
                TBanner {
                    visible: folder.modelData.warning !== ""
                    width: parent.width
                    tone: "warn"
                    text: folder.modelData.warning
                }
            }
        }
    }

    // ---- ELSŐ LÉPÉSEK (kis hivatkozás; csak a főablak folyamatában) ----
    Rectangle { visible: onboardingRow.visible; width: parent.width; height: 1; color: Theme.border }
    Row {
        id: onboardingRow
        visible: root.vm ? root.vm.onboardingAvailable : false
        width: parent.width
        spacing: 6
        topPadding: -6
        TLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Az első indításkor látott bevezető:")
            muted: true
            font.pixelSize: Theme.fontSmall
        }
        T.AbstractButton {
            id: onboardingLink
            objectName: "onboardingLink"
            anchors.verticalCenter: parent.verticalCenter
            width: onboardingLinkRow.implicitWidth + 12
            height: 28
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.Link
            Accessible.name: qsTr("Első lépések")
            onClicked: root.vm.openOnboarding()
            Keys.onReturnPressed: click()
            Keys.onSpacePressed: click()
            background: Rectangle {
                radius: Theme.radiusControl
                color: Theme.stateLayer
                opacity: onboardingLink.down ? Theme.pressedOpacity : onboardingLink.hovered ? Theme.hoverOpacity : 0
                TFocusRing { visible: onboardingLink.visualFocus }
            }
            contentItem: Item {
                Row {
                    id: onboardingLinkRow
                    anchors.centerIn: parent
                    spacing: 4
                    TLabel {
                        text: qsTr("Első lépések")
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
