import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Settings > People and samples: your own name and voiceprint (with a link to the People window) and
// the voiceprint backfill status (k / N; when done with skips, the list of skipped samples per
// person, each name a link to that person). The voice and segmentation models live on the
// Processing page (SettingsModelList).
// Demo state B08partial: the skipped-samples list expanded.
//   SettingsPeoplePage { vm: model }
Column {
    id: root

    property var vm: null
    readonly property var models: vm ? vm.voiceModels : null
    readonly property string demoState: models ? models.demoState : ""
    property bool skipsExpanded: false

    spacing: 22

    function applyDemo() {
        if (root.demoState === "B08partial") root.skipsExpanded = true
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
}
