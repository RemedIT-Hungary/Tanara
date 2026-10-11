import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Hangmodell-kártya (Beállítások › Beszélők, B08): név + „ajánlott” jelölés, állapot-pirula,
// „Bekapcsolva” kapcsoló (csak meglévő modellnél), a letöltött fájl menüje (Ellenőrzés újra,
// Törlés…); egy mondat arról, mire jó; licenc + forrás link + méret + dimenzió (a letöltés ELŐTT
// is látszik); állapotfüggő művelet: Letöltés / folyamatjelző + Mégse / ellenőrzés… / hiba (pl.
// „méret nem egyezik”) + Újra / offline. A törlés megerősítése a lapé (deleteRequested).
//   SettingsVoiceModelCard { list: vm.voiceModels; model: <a VoiceModelListModel egy sora> }
Rectangle {
    id: root

    property var list: null              // VoiceModelListModel (a műveletekhez)
    // A sor szerepei (a Repeater delegate-je adja át).
    property string modelId: ""
    property string name: ""
    property string blurb: ""
    property string license: ""
    property string licenseNote: ""
    property string sourceText: ""
    property string sourceUrl: ""
    property string sizeText: ""
    property string dimText: ""
    property string state_: "missing"    // missing | downloading | verifying | downloaded | bundled | error | offline
    property real progress: -1
    property string progressText: ""
    property string errorText: ""
    property bool enabledModel: false
    property bool canEnable: false
    property bool recommended: false
    property bool corrupt: false         // a fájl mérete eltér a várttól
    property bool hasFile: false         // van letöltött fájl (<metaDir>/models) — törölhető
    property bool canDelete: false       // most törölhető (nem töltődik / ellenőrződik)
    // Képernyőképhez: a menü nyitva.
    property bool demoMenuOpen: false

    readonly property bool failed: state_ === "error"

    // A „Törlés…” menüpont: a lap kérdez rá (TDialog), és ő hívja a list.deleteModel-t.
    signal deleteRequested(string modelId, string name)

    function openMenu() { menu.open() }
    Component.onCompleted: if (demoMenuOpen) Qt.callLater(openMenu)

    objectName: "voiceModelCard-" + modelId
    implicitHeight: col.implicitHeight + 28
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: failed ? Theme.dangerLine : Theme.border

    // Kis szöveges link (a forrás-lap).
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

        // ---- fej: név, ajánlott, állapot, kapcsoló ----
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
                      : root.state_ === "error" ? "failed" : "neutral"
                text: root.state_ === "downloaded" ? qsTr("letöltve")
                    : root.state_ === "bundled" ? qsTr("a csomagban")
                    : root.state_ === "downloading" ? qsTr("letöltés… %1").arg(root.progressText)
                    : root.state_ === "verifying" ? qsTr("ellenőrzés…")
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
            // A letöltött fájl műveletei (a csomagban lévőnél nincs).
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

        // ---- méret · dimenzió · licenc · forrás (a letöltés előtt is látszik) ----
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

        // ---- nincs letöltve: Letöltés ----
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

        // ---- letöltés fut: sáv + Mégse ----
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

        // ---- hiba: üzenet + Újra ----
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

        // ---- offline: a letöltés nem indul (a magyarázat a lap tetején, egyszer) ----
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
