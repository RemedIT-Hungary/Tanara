import QtQuick
import QtQuick.Layouts

// Az Átnézendő fókusz-kártyája (koncepció 2.1): egy kétes sor — hullámforma-csík +
// lejátszás-állapot, a sor beszélője + idő + ok-chipek, a sor szövege (max. 3 sor), a
// döntés-gombok billentyű-felirattal és a beszélő-számsor (1–9 = a sín oszlopai, a javasolt
// kiemelve). A kártya MÉRETE RÖGZÍTETT (a gombok sorrendje és helye is): a javaslat-gomb akkor is
// helyet foglal, ha nincs javaslat — ilyenkor „Jó így” kerül rá. A helyét a ReviewFocusView adja.
//   item:  a ReviewQueueModel.current térképe (utteranceId, state, likelyName, reasons …)
Rectangle {
    id: root

    property var item: ({})
    property var lanes: []                  // TranscriptEditorViewModel.lanes (a sín sorrendje)
    property bool shiftHeld: false
    property bool playing: false            // a kártya sora szól
    property real playPos: 0                // 0..1 a soron belül
    property int durationMs: 0

    signal enterClicked()
    signal confirmClicked()
    signal ignoreClicked()
    signal otherClicked(Item anchor)
    signal laterClicked()
    signal laneClicked(int lane)
    signal playClicked()

    readonly property string utteranceId: item.utteranceId || ""
    readonly property bool valid: utteranceId !== ""
    readonly property string state_: item.state || ""
    readonly property bool alreadyDecided: state_ === "alreadyDecided"
    readonly property string likelyKey: item.likelyKey || ""
    readonly property string likelyName: item.likelyName || ""
    readonly property string likelyNewSide: item.likelyNewSide || ""
    readonly property bool hasSuggestion: !alreadyDecided && (likelyName !== "" || likelyNewSide !== "")
                                          && likelyKey !== (item.speakerKey || "")
    readonly property string enterText: alreadyDecided ? qsTr("Megerősítem")
        : likelyName !== "" && hasSuggestion ? qsTr("%1 mondta").arg(likelyName)
        : likelyNewSide === "remote" && hasSuggestion ? qsTr("Új beszélő (hívás hangja)")
        : likelyNewSide === "local" && hasSuggestion ? qsTr("Új beszélő (mikrofon)")
        : qsTr("Jó így")
    readonly property alias otherAnchor: otherButton
    // Rögzített méretek: a kártya magassága sosem függ a tartalomtól.
    readonly property int textBlockHeight: 78
    readonly property int fixedHeight: 16 + 32 + 6 + 22 + 8 + textBlockHeight + 12 + Theme.controlHeight + 10 + 30 + 16

    objectName: "reviewFocusCard"
    implicitHeight: fixedHeight
    height: fixedHeight
    radius: Theme.radiusDialog
    color: Theme.raised
    border.width: 2
    border.color: root.alreadyDecided ? Theme.border : Theme.accentLine
    clip: true

    function fmt(ms) {
        const s = Math.max(0, Math.round(ms / 1000))
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    // Rövid név a számsorhoz: az utónév („Kovács Lilla” → „Lilla”); a „Távoli 1” egészben marad.
    function shortName(name) {
        const words = name.split(" ").filter(w => w !== "")
        if (words.length < 2 || /^[0-9]+$/.test(words[words.length - 1])) return name
        return words[words.length - 1]
    }
    // Determinisztikus „hullámforma” a sor azonosítójából (a csík a lejátszás haladását mutatja).
    function barHeight(i) {
        let seed = 7
        const id = root.utteranceId
        for (let k = 0; k < id.length; ++k) seed = (seed * 31 + id.charCodeAt(k)) % 233280
        const v = ((seed + i * 9301 + 49297) * 7919) % 233280 / 233280
        return 4 + v * 18 * (0.55 + 0.45 * Math.sin(i / 4 + seed % 7))
    }

    Column {
        id: col
        x: 20
        y: 16
        width: root.width - 40
        spacing: 0

        // ---- lejátszás + kié a sor + miért kétes ----
        RowLayout {
            width: col.width
            height: 32
            spacing: 10
            TIconButton {
                objectName: "reviewFocusPlay"
                iconName: root.playing ? "pause" : "play"
                variant: "solid"
                implicitWidth: 32
                implicitHeight: 32
                radius: 16
                focusPolicy: Qt.NoFocus
                toolTipText: qsTr("Lejátszás / szünet (Szóköz) · újra az elejéről: R")
                onClicked: root.playClicked()
            }
            Row {
                objectName: "reviewFocusWave"
                visible: col.width >= 720       // szűk kártyán csak a gomb és az idő
                spacing: 2
                Layout.alignment: Qt.AlignVCenter
                Repeater {
                    model: 40
                    delegate: Rectangle {
                        required property int index
                        width: 3
                        height: Math.max(3, root.barHeight(index))
                        radius: 1
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.playing && index / 40 < root.playPos ? Theme.accent : Theme.borderStrong
                    }
                }
            }
            TLabel {
                objectName: "reviewFocusTime"
                text: root.fmt(root.playPos * root.durationMs) + " / " + root.fmt(root.durationMs)
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
                Layout.preferredWidth: 78
            }
            Item { Layout.fillWidth: true; Layout.minimumWidth: 8 }
            TAvatar {
                name: root.item.speakerName || ""
                speakerIndex: root.item.colorIndex || 0
                size: 22
            }
            TLabel {
                objectName: "reviewFocusSpeaker"
                text: root.valid ? qsTr("%1 nevén").arg(root.item.speakerName || "") : ""
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightSemiBold
                color: Theme.speakerInk(root.item.colorIndex || 0)
                elide: Text.ElideRight
                Layout.maximumWidth: 220
            }
            TLabel {
                text: root.valid ? "· " + (root.item.timeLabel || "") : ""
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            // Ok-chipek (a csoport bizonyítékai); szűk helyen levágva.
            Item {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: chips.implicitWidth
                Layout.preferredWidth: chips.implicitWidth
                Layout.preferredHeight: 20
                clip: true
                Row {
                    id: chips
                    objectName: "reviewFocusReasons"
                    spacing: 6
                    Repeater {
                        model: root.item.reasons || []
                        delegate: EvidenceChip {
                            required property var modelData
                            kind: modelData.kind === "manual" ? "side" : modelData.kind
                            polarity: modelData.polarity === "support" ? "neutral" : modelData.polarity
                            side: modelData.side || ""
                            text: modelData.text || ""
                            HoverHandler { id: evHover }
                            TToolTip { visible: evHover.hovered && (modelData.detail || "") !== ""; text: modelData.detail || ""; delay: 300 }
                        }
                    }
                }
            }
        }
        Item { width: 1; height: 6 }

        // ---- állapot-jelek (a sor helye mindig megvan) ----
        Row {
            width: col.width
            height: 22
            spacing: 8
            clip: true
            TLabel {
                objectName: "reviewFocusReason"
                anchors.verticalCenter: parent.verticalCenter
                visible: text !== ""
                text: root.item.reasonTitle || (root.item.reasonKind === "uncertain" ? qsTr("Hangra kétes sor") : "")
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            TPill {
                objectName: "reviewFocusAlreadyDecided"
                anchors.verticalCenter: parent.verticalCenter
                visible: root.alreadyDecided
                tone: "success"
                text: qsTr("már eldőlt · Enter: megerősítem · →: tovább")
            }
            TPill {
                objectName: "reviewFocusUpdated"
                anchors.verticalCenter: parent.verticalCenter
                visible: root.item.suggestionUpdated === true && !root.alreadyDecided
                tone: "accent"
                text: qsTr("javaslat frissült")
            }
            TPill {
                objectName: "reviewFocusDecided"
                anchors.verticalCenter: parent.verticalCenter
                readonly property string kind: root.item.decidedKind || ""
                visible: root.state_ === "decided" || root.state_ === "ignored"
                tone: root.state_ === "ignored" ? "neutral" : "success"
                text: root.state_ === "ignored" ? qsTr("eldöntve: nem azonosítható")
                    : kind === "corrected" ? qsTr("eldöntve: javítva · %1").arg(root.item.sampleState === "noSample" ? qsTr("nem minta") : qsTr("minta"))
                    : qsTr("eldöntve: jó így · %1").arg(root.item.sampleState === "noSample" ? qsTr("nem minta") : qsTr("minta"))
            }
            TPill {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.state_ === "later"
                text: qsTr("◇ később hagyva")
            }
            TPill {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.item.admitted === true
                tone: root.item.consequence === true ? "warn" : "neutral"
                text: root.item.consequence === true ? qsTr("új · a döntéseid miatt") : qsTr("új")
            }
        }
        Item { width: 1; height: 8 }

        // ---- a sor szövege (max. 3 sor) ----
        TLabel {
            objectName: "reviewFocusText"
            width: col.width
            height: root.textBlockHeight
            verticalAlignment: Text.AlignTop
            text: root.valid ? "„" + (root.item.lineText || "") + "”" : ""
            font.pixelSize: 18
            font.weight: Theme.weightMedium
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 1.3
        }
        Item { width: 1; height: 12 }

        // ---- döntés-gombok (sorrend és hely fix) ----
        Item {
            width: col.width
            height: Theme.controlHeight
            clip: true
            // Szűk helyen a „· nem minta” utótag elmarad (a szaggatott keret és a Shift-jel marad).
            readonly property bool roomy: width >= 940
            Row {
                id: buttons
                spacing: 8
                ReviewFocusKeyButton {
                    objectName: "reviewFocusEnter"
                    keyText: "Enter"
                    variant: "primary"
                    text: root.enterText
                    enabled: root.valid
                    nonSample: root.shiftHeld && !root.alreadyDecided
                    showSuffix: parent.parent.roomy
                    toolTipText: root.alreadyDecided ? qsTr("A sor a háttérben már eldőlt: megerősítem így")
                                                     : qsTr("Az alapértelmezés: a javaslat, vagy ha nincs, „Jó így” · Shift: nem minta")
                    onClicked: root.enterClicked()
                }
                ReviewFocusKeyButton {
                    objectName: "reviewFocusConfirm"
                    keyText: "J"
                    text: qsTr("Jó így")
                    enabled: root.valid
                    dimmed: !root.hasSuggestion
                    nonSample: root.shiftHeld
                    showSuffix: parent.parent.roomy
                    toolTipText: qsTr("A mostani név marad (%1) · Shift: nem minta").arg(root.item.speakerName || "")
                    onClicked: root.confirmClicked()
                }
                ReviewFocusKeyButton {
                    objectName: "reviewFocusIgnore"
                    keyText: "0"
                    text: root.state_ === "ignored" ? qsTr("Újra azonosítható") : qsTr("Nem azonosítható")
                    enabled: root.valid
                    toolTipText: qsTr("A név marad, de a hang-elemzés teljesen kihagyja: nem minta, nem bizonyíték")
                    onClicked: root.ignoreClicked()
                }
                ReviewFocusKeyButton {
                    id: otherButton
                    objectName: "reviewFocusOther"
                    keyText: "M"
                    text: qsTr("Más…")
                    enabled: root.valid
                    toolTipText: qsTr("A sor panelje: személyválasztó, új beszélő")
                    onClicked: root.otherClicked(otherButton)
                }
                ReviewFocusKeyButton {
                    objectName: "reviewFocusLater"
                    keyText: "→"
                    variant: "ghost"
                    text: root.alreadyDecided ? qsTr("Tovább") : qsTr("Később")
                    enabled: root.valid
                    toolTipText: root.alreadyDecided ? qsTr("Átugrom; a sor a helyén marad")
                                                     : qsTr("Nem döntés: a sor a kör végére kerül (◇)")
                    onClicked: root.laterClicked()
                }
            }
            Rectangle {
                id: shiftHint
                objectName: "reviewFocusShiftHint"
                anchors.right: parent.right
                visible: buttons.implicitWidth + implicitWidth + 12 <= parent.width
                implicitWidth: shiftRow.implicitWidth + 16
                height: Theme.controlHeight
                radius: Theme.radiusControl
                color: root.shiftHeld ? Theme.warnSoft : "transparent"
                Row {
                    id: shiftRow
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: shiftKey.implicitWidth + 10
                        height: 20
                        radius: 4
                        color: Theme.sunken
                        border.width: 1
                        border.color: Theme.border
                        TLabel {
                            id: shiftKey
                            anchors.centerIn: parent
                            text: "Shift"
                            mono: true
                            font.pixelSize: Theme.fontMicro
                            font.weight: Theme.weightSemiBold
                        }
                    }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("= nem minta")
                        font.pixelSize: Theme.fontSmall
                        color: root.shiftHeld ? Theme.warnInk : Theme.textMuted
                    }
                }
            }
        }
        Item { width: 1; height: 10 }

        // ---- beszélő-számsor (1–9 = a sín oszlopai) ----
        Item {
            width: col.width
            height: 30
            clip: true
            // Szűk helyen a rövid nevek elmaradnak (szám + avatar marad).
            readonly property bool roomy: Math.min(9, root.lanes.length) * 118 <= width
            Row {
                id: numbers
                objectName: "reviewFocusNumbers"
                spacing: 6
                Repeater {
                    id: laneRepeater
                    model: root.lanes.slice(0, 9)
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        required property int index
                        readonly property bool suggested: root.hasSuggestion && root.likelyKey !== "" && modelData.key === root.likelyKey
                        readonly property bool current: modelData.key === (root.item.speakerKey || "")
                        readonly property bool roomy: numbers.parent.roomy
                        objectName: "reviewFocusLane"
                        height: 30
                        width: chipRow.implicitWidth + 16
                        radius: 15
                        color: suggested ? Theme.accentSoft
                             : chipHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                        border.width: suggested ? 1.5 : root.shiftHeld ? 0 : 1
                        border.color: suggested ? Theme.accent : Theme.border
                        TDashedRect {
                            anchors.fill: parent
                            radius: 15
                            visible: root.shiftHeld && !chip.suggested
                        }
                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 6
                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: String(chip.index + 1)
                                mono: true
                                font.pixelSize: Theme.fontCaption
                                font.weight: Theme.weightSemiBold
                                color: chip.suggested ? Theme.accent : Theme.textMuted
                            }
                            TAvatar {
                                anchors.verticalCenter: parent.verticalCenter
                                name: chip.modelData.name || ""
                                speakerIndex: chip.modelData.colorIndex || 0
                                size: 20
                            }
                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: chip.roomy
                                text: root.shortName(chip.modelData.name || "")
                                font.pixelSize: Theme.fontSmall
                                font.weight: chip.suggested ? Theme.weightSemiBold : Theme.weightRegular
                            }
                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: chip.suggested || (chip.current && chip.roomy)
                                text: chip.suggested ? qsTr("● javasolt") : qsTr("most")
                                font.pixelSize: Theme.fontMicro
                                font.weight: Theme.weightSemiBold
                                color: chip.suggested ? Theme.accent : Theme.textMuted
                            }
                        }
                        HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.laneClicked(chip.index) }
                        TToolTip {
                            visible: chipHover.hovered
                            text: (chip.current ? qsTr("%1: jó így") : qsTr("→ %1")).arg(chip.modelData.name || "")
                                  + (root.shiftHeld ? qsTr(" · nem minta") : "")
                        }
                    }
                }
            }
        }
    }
}
