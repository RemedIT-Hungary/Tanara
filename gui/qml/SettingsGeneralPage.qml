import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// General: language and theme, the folders, the account part (where the keys are, the first
// steps link). Each block is a jump point (SettingsSection). (Your own name and voiceprint are on
// the People page.)
Column {
    id: root

    property var vm: null

    spacing: 22

    // ---- APPEARANCE ----
    SettingsSection {
        key: "appearance"
        width: parent.width
        Column {
            width: parent.width
            spacing: 12
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
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- FOLDERS ----
    SettingsSection {
        key: "folders"
        width: parent.width
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
                            // For a pinned folder (TANARA_HOME) there is nothing to browse. We hide the button instead
                            // of disabling it: a disabled button's dashed border (Shape) would stick out of the scrolled
                            // area's clip with the software renderer.
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
                    // Further recording roots (only on the recordings row): the library lists every root;
                    // new recordings go into the primary one (the row above).
                    Column {
                        id: extraRoots
                        visible: folder.modelData.key === "audio" && !folder.modelData.locked
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: extraRoots.visible && root.vm ? root.vm.recordingRoots : []
                            RowLayout {
                                id: rootRow
                                required property var modelData
                                width: extraRoots.width
                                spacing: 6
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: Theme.controlHeight
                                    radius: Theme.radiusControl
                                    color: Theme.sunken
                                    border.width: 1
                                    border.color: Theme.border
                                    TLabel {
                                        x: 10
                                        width: parent.width - 20
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: rootRow.modelData.path
                                        mono: true
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideMiddle
                                    }
                                }
                                TLabel {
                                    text: qsTr("%n megbeszélés", "", rootRow.modelData.meetings)
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                }
                                TButton {
                                    objectName: "makePrimaryRootButton"
                                    text: qsTr("Legyen elsődleges")
                                    font.pixelSize: Theme.fontSmall
                                    onClicked: root.vm.makePrimaryRecordingRoot(rootRow.modelData.path)
                                }
                                TIconButton {
                                    objectName: "removeRootButton"
                                    iconName: "x"
                                    iconSize: 15
                                    visible: rootRow.modelData.removable
                                    toolTipText: qsTr("Eltávolítás a listából (a mappa üres)")
                                    onClicked: root.vm.removeRecordingRoot(rootRow.modelData.path)
                                }
                            }
                        }
                        TButton {
                            objectName: "addRecordingRootButton"
                            text: qsTr("Újabb felvétel-mappa…")
                            font.pixelSize: Theme.fontSmall
                            onClicked: root.vm.addRecordingRoot()
                        }
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
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- ACCOUNT: where the keys are kept, the first steps link ----
    SettingsSection {
        key: "account"
        width: parent.width
        spacing: 12
        TSectionLabel { text: qsTr("Fiók") }

        // Where the keys are (keychain / file), + a warning if the keychain was wanted but unavailable.
        TBanner {
            objectName: "secretsWarning"
            visible: root.vm !== null && root.vm.secretsWarning !== ""
            width: parent.width
            tone: "warn"
            text: root.vm ? root.vm.secretsWarning : ""
        }
        TLabel {
            objectName: "secretsLocation"
            width: parent.width
            text: root.vm ? root.vm.secretsLocationText : ""
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }

        // The first steps (small link; only in the main window flow).
        Row {
            id: onboardingRow
            visible: root.vm ? root.vm.onboardingAvailable : false
            width: parent.width
            spacing: 6
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
}
