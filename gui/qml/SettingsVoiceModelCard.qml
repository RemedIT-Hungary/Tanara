import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Voice model card (Settings > Speakers): name + "recommended" mark, status pill, "Enabled"
// switch (only for an existing model), menu of the downloaded file (Verify again, Delete...);
// a sentence on what it is for; license + source link + size + dimension (visible BEFORE
// download); state-dependent action: Download / progress + Cancel / verifying... / error (e.g.
// "size mismatch") + Retry / offline; a SHA-256 mismatch: a warning + "Mégis használom" (the override;
// the model is then marked "ellenőrizetlen") + Retry. The page owns the delete confirmation (deleteRequested).
//   SettingsVoiceModelCard { list: vm.voiceModels; model: <one row of VoiceModelListModel> }
Rectangle {
    id: root

    property var list: null              // VoiceModelListModel (for the actions)
    // Row roles (passed by the Repeater delegate).
    property string modelId: ""
    property string name: ""
    property string blurb: ""
    property string license: ""
    property string licenseNote: ""
    property string sourceText: ""
    property string sourceUrl: ""
    property string sizeText: ""
    property string dimText: ""
    property string state_: "missing"    // missing | downloading | verifying | downloaded | bundled | error | offline | mismatch | unverified
    property real progress: -1
    property string progressText: ""
    property string errorText: ""
    property bool enabledModel: false
    property bool canEnable: false
    property bool recommended: false
    property bool corrupt: false         // file size differs from the expected one
    property bool hasFile: false         // a downloaded file exists (<metaDir>/models): deletable
    property bool canDelete: false       // deletable now (not downloading / verifying)
    // For screenshots: menu open.
    property bool demoMenuOpen: false

    readonly property bool failed: state_ === "error"

    // The "Delete..." menu item: the page asks for confirmation (TDialog) and calls list.deleteModel.
    signal deleteRequested(string modelId, string name)

    function openMenu() { menu.open() }
    Component.onCompleted: if (demoMenuOpen) Qt.callLater(openMenu)

    objectName: "voiceModelCard-" + modelId
    implicitHeight: col.implicitHeight + 28
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: failed ? Theme.dangerLine : state_ === "mismatch" ? Theme.warnLine : Theme.border

    // Small text link (the source page).
    component LinkButton: T.AbstractButton {
        id: link
        property string iconName: ""
        implicitWidth: linkRow.implicitWidth + 4
        implicitHeight: 20
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.role: Accessible.Link
        Accessible.name: text
        Keys.onReturnPressed: click()
        Keys.onSpacePressed: click()
        contentItem: Item {
            Row {
                id: linkRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                TLabel {
                    text: link.text
                    color: Theme.accent
                    font.pixelSize: Theme.fontCaption
                    font.underline: link.hovered
                    anchors.verticalCenter: parent.verticalCenter
                }
                TIcon {
                    visible: link.iconName !== ""
                    name: link.iconName
                    size: 12
                    color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        background: Item { TFocusRing { visible: link.visualFocus; targetRadius: 4 } }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    ColumnLayout {
        id: col
        x: 16; y: 14
        width: parent.width - 32
        spacing: 8

        // ---- head: name, recommended, status, switch ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TLabel {
                text: root.name
                font.pixelSize: 15
                font.weight: Theme.weightSemiBold
            }
            TPill {
                visible: root.recommended
                text: qsTr("ajánlott")
                tone: "accent"
            }
            SettingsStatusPill {
                objectName: "voiceModelStatus-" + root.modelId
                status: root.state_ === "downloaded" || root.state_ === "bundled" ? "ok"
                      : root.state_ === "downloading" || root.state_ === "verifying" ? "testing"
                      : root.state_ === "error" || root.state_ === "mismatch" ? "failed" : "neutral"
                text: root.state_ === "downloaded" ? qsTr("letöltve")
                    : root.state_ === "bundled" ? qsTr("a csomagban")
                    : root.state_ === "downloading" ? qsTr("letöltés… %1").arg(root.progressText)
                    : root.state_ === "verifying" ? qsTr("ellenőrzés…")
                    : root.state_ === "mismatch" ? qsTr("SHA nem egyezik")
                    : root.state_ === "unverified" ? qsTr("ellenőrizetlen")
                    : root.state_ === "error" && root.corrupt ? qsTr("hiba (méret nem egyezik)")
                    : root.state_ === "error" ? qsTr("hiba")
                    : root.state_ === "offline" ? qsTr("offline")
                    : qsTr("nincs letöltve")
            }
            Item { Layout.fillWidth: true }
            TLabel {
                text: qsTr("Bekapcsolva")
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
                color: root.canEnable ? Theme.text : Theme.textMuted
                TapHandler {
                    enabled: root.canEnable
                    onTapped: root.list.setEnabled(root.modelId, !root.enabledModel)
                }
            }
            RecorderSwitch {
                id: sw
                objectName: "voiceModelSwitch-" + root.modelId
                enabled: root.canEnable
                checked: root.enabledModel && root.canEnable
                toolTipText: root.canEnable ? qsTr("%1 bekapcsolása").arg(root.name)
                                            : qsTr("Előbb töltsd le a modellt")
                Accessible.name: qsTr("%1 bekapcsolva").arg(root.name)
                onToggled: {
                    root.list.setEnabled(root.modelId, checked)
                    checked = Qt.binding(function() { return root.enabledModel && root.canEnable })
                }
            }
            // Actions on the downloaded file (none for a bundled one).
            TIconButton {
                id: menuButton
                objectName: "voiceModelMenu-" + root.modelId
                visible: root.hasFile
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                variant: "flat"
                iconName: "ellipsis"
                iconSize: 14
                iconColor: Theme.textMuted
                toolTipText: qsTr("A modellfájl műveletei")
                Accessible.name: qsTr("%1: a modellfájl műveletei").arg(root.name)
                onClicked: menu.open()
                Rectangle { z: -1; anchors.fill: parent; radius: Theme.radiusControl; color: Theme.sunken; visible: menu.visible }

                TMenu {
                    id: menu
                    width: 240
                    x: menuButton.width + 2 - width
                    y: menuButton.height + 6
                    TMenuItem {
                        objectName: "voiceModelVerify-" + root.modelId
                        implicitHeight: 34
                        text: qsTr("Ellenőrzés újra")
                        iconName: "refresh-cw"
                        enabled: root.canDelete
                        onTriggered: root.list.verify(root.modelId)
                    }
                    TMenuSeparator {}
                    TMenuItem {
                        objectName: "voiceModelDelete-" + root.modelId
                        implicitHeight: 34
                        text: root.canDelete ? qsTr("Törlés…") : qsTr("Törlés… (letöltés közben nem)")
                        iconName: "trash-2"
                        danger: true
                        enabled: root.canDelete
                        onTriggered: root.deleteRequested(root.modelId, root.name)
                    }
                }
            }
        }

        TLabel {
            Layout.fillWidth: true
            text: root.blurb
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }

        // ---- size - dimension - license - source (visible before download too) ----
        Flow {
            Layout.fillWidth: true
            spacing: 6
            TLabel {
                text: [root.sizeText, root.dimText].filter(function(s) { return s !== "" }).join(" · ")
                      + " · " + qsTr("Licenc: %1").arg(root.license)
                      + (root.licenseNote !== "" ? ", " + root.licenseNote : "")
                      + " · " + qsTr("forrás:")
                muted: true
                font.pixelSize: Theme.fontCaption
                height: 20
                verticalAlignment: Text.AlignVCenter
            }
            LinkButton {
                objectName: "voiceModelSource-" + root.modelId
                text: root.sourceText
                iconName: "external-link"
                width: Math.min(implicitWidth, root.width - 32)
                onClicked: root.list.openSource(root.modelId)
                TToolTip { visible: parent.hovered; text: root.sourceUrl }
            }
        }

        // ---- not downloaded: Download ----
        RowLayout {
            visible: root.state_ === "missing"
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 8
            TButton {
                objectName: "voiceModelDownload-" + root.modelId
                size: "small"
                iconName: "arrow-down-to-line"
                text: qsTr("Letöltés (%1)").arg(root.sizeText)
                onClicked: root.list.download(root.modelId)
            }
            Item { Layout.fillWidth: true }
        }

        // ---- download running: bar + Cancel ----
        RowLayout {
            visible: root.state_ === "downloading"
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 12
            TProgressBar {
                Layout.fillWidth: true
                thickness: 6
                indeterminate: root.progress < 0
                value: Math.max(0, root.progress)
            }
            TLabel {
                text: root.progressText
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            TButton {
                objectName: "voiceModelCancel-" + root.modelId
                size: "small"
                text: qsTr("Mégse")
                onClicked: root.list.cancel(root.modelId)
            }
        }

        // ---- error: message + Retry ----
        Rectangle {
            visible: root.failed
            Layout.fillWidth: true
            Layout.topMargin: 2
            implicitHeight: errRow.implicitHeight + 16
            radius: Theme.radiusControl
            color: Theme.dangerSoft
            border.width: 1
            border.color: Theme.dangerLine
            RowLayout {
                id: errRow
                x: 10; y: 8
                width: parent.width - 20
                spacing: 10
                TLabel {
                    Layout.fillWidth: true
                    text: root.errorText
                    color: Theme.dangerInk
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.4
                    wrapMode: Text.Wrap
                }
                TButton {
                    objectName: "voiceModelRetry-" + root.modelId
                    size: "small"
                    text: qsTr("Újra")
                    onClicked: root.list.retry(root.modelId)
                }
            }
        }

        // ---- SHA-256 mismatch: warning + "Mégis használom" (override) + Retry ----
        Rectangle {
            visible: root.state_ === "mismatch"
            Layout.fillWidth: true
            Layout.topMargin: 2
            implicitHeight: shaRow.implicitHeight + 16
            radius: Theme.radiusControl
            color: Theme.warnSoft
            border.width: 1
            border.color: Theme.warnLine
            RowLayout {
                id: shaRow
                x: 10; y: 8
                width: parent.width - 20
                spacing: 10
                TLabel {
                    objectName: "voiceModelShaWarning-" + root.modelId
                    Layout.fillWidth: true
                    text: root.errorText
                    color: Theme.warnInk
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.4
                    wrapMode: Text.Wrap
                }
                TButton {
                    objectName: "voiceModelAccept-" + root.modelId
                    size: "small"
                    text: qsTr("Mégis használom")
                    onClicked: root.list.acceptMismatch(root.modelId)
                }
                TButton {
                    objectName: "voiceModelShaRetry-" + root.modelId
                    size: "small"
                    text: qsTr("Újra")
                    onClicked: root.list.retry(root.modelId)
                }
            }
        }

        // ---- accepted despite the SHA-256 mismatch: a standing note ----
        TLabel {
            objectName: "voiceModelUnverified-" + root.modelId
            visible: root.state_ === "unverified"
            Layout.fillWidth: true
            text: root.errorText
            color: Theme.warnInk
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.4
            wrapMode: Text.Wrap
        }

        // ---- offline: download does not start (explained once at the top of the page) ----
        RowLayout {
            visible: root.state_ === "offline"
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 8
            TButton {
                objectName: "voiceModelOfflineRetry-" + root.modelId
                size: "small"
                iconName: "refresh-cw"
                text: qsTr("Letöltés újrapróbálása")
                onClicked: { root.list.refresh(); root.list.download(root.modelId) }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
