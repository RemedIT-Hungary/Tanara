import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Az Átnézendő fókusz-kártyája (S7, a „fókusz-kártya mock” nyugodt változata): bal oldalon nagy,
// kerek lejátszó-gomb (alatta „Space”); jobbra a sor beszélője (avatar, név, idő, ok) KÖZVETLENÜL
// a szöveg fölött (mint az átiratban), a szöveg (max. 3 sor), alatta a hullámforma + idő +
// bizonyíték-chipek. Alatta a fix gomb-sor (Enter · J · 0 · M · →: hely, szélesség, billentyű
// sosem változik; a nevek a feliraton, a művelet ikonja színes, a gomb maga színtelen), a
// „Shift = nem minta” sor és a teljes nevű beszélő-számsor (1–9 = a sín oszlopai, „most” szürke
// kitöltés, „javasolt” vékony accent keret — ez az egyetlen kék a kártyán).
// A kártya magassága nem függ a sortól: a szöveg-blokk fix, a számsor magassága csak a sín
// beszélőitől függ (minden chip helyet tart a „most” / „javasolt” jelnek, így menet közben nem
// rendeződik át). A helyét a ReviewFocusView adja.
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
    signal newSpeakerClicked()
    signal playClicked()

    readonly property string utteranceId: item.utteranceId || ""
    readonly property bool valid: utteranceId !== ""
    readonly property string state_: item.state || ""
    readonly property bool alreadyDecided: state_ === "alreadyDecided"
    readonly property string speakerName: item.speakerName || ""
    readonly property string likelyKey: item.likelyKey || ""
    readonly property string likelyName: item.likelyName || ""
    readonly property string likelyNewSide: item.likelyNewSide || ""
    readonly property bool hasSuggestion: !alreadyDecided && (likelyName !== "" || likelyNewSide !== "")
                                          && likelyKey !== (item.speakerKey || "")
    readonly property bool suggestsNew: hasSuggestion && likelyName === "" && likelyNewSide !== ""
    // A bizonyíték-szövegek röviden (a javaslat alszövegéhez): „hang 91% · a mikrofonon szólt”.
    readonly property string evidenceText: (item.reasons || []).map(r => r.text || "").filter(t => t !== "").slice(0, 2).join(" · ")
    // Az Enter-gomb: a javasolt név (→), új beszélő, vagy ha nincs javaslat, a mostani név (✓ marad).
    readonly property string enterText: !hasSuggestion ? speakerName
                                       : suggestsNew ? qsTr("Új beszélő") : likelyName
    readonly property string enterSub: !hasSuggestion ? qsTr("marad · (= J)")
        : suggestsNew ? (likelyNewSide === "remote" ? qsTr("javaslat · hívás hangja") : qsTr("javaslat · mikrofon"))
        : evidenceText !== "" ? qsTr("javaslat · %1").arg(evidenceText) : qsTr("javaslat")
    readonly property string enterIcon: !hasSuggestion ? "check" : suggestsNew ? "user-plus" : "arrow-right"
    readonly property alias otherAnchor: otherButton

    // Rögzített méretek.
    readonly property int pad: 18
    readonly property int nameRowHeight: 26
    readonly property int textBlockHeight: 84
    readonly property int playerRowHeight: 24
    readonly property int bodyHeight: nameRowHeight + 4 + textBlockHeight + 6 + playerRowHeight
    readonly property int buttonHeight: 48
    readonly property int fixedHeight: 16 + bodyHeight + 14 + buttonHeight + 8 + 20 + 10 + 1 + 12 + 14
    // Minden chip helyet tart a jelnek (a leghosszabb: „javasolt”).
    readonly property real markWidth: Math.max(markProbe.implicitWidth, markProbe2.implicitWidth)

    objectName: "reviewFocusCard"
    implicitHeight: fixedHeight + numbers.height
    height: implicitHeight
    radius: Theme.radiusDialog
    color: Theme.raised
    border.width: 1
    border.color: Theme.border
    clip: true

    function fmt(ms) {
        const s = Math.max(0, Math.round(ms / 1000))
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    // Determinisztikus „hullámforma” a sor azonosítójából (a csík a lejátszás haladását mutatja).
    function barHeight(i) {
        let seed = 7
        const id = root.utteranceId
        for (let k = 0; k < id.length; ++k) seed = (seed * 31 + id.charCodeAt(k)) % 233280
        const v = ((seed + i * 9301 + 49297) * 7919) % 233280 / 233280
        return 3 + v * 15 * (0.55 + 0.45 * Math.sin(i / 4 + seed % 7))
    }

    TLabel { id: markProbe; visible: false; text: qsTr("javasolt"); font.pixelSize: Theme.fontMicro; font.weight: Theme.weightSemiBold }
    TLabel { id: markProbe2; visible: false; text: qsTr("most"); font.pixelSize: Theme.fontMicro; font.weight: Theme.weightSemiBold }

    // ---- nagy lejátszó-gomb + „Space” ----
    T.Button {
        id: playButton
        objectName: "reviewFocusPlay"
        x: root.pad
        y: 18
        width: 56
        height: 56
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        enabled: root.valid
        onClicked: root.playClicked()
        background: Rectangle {
            radius: width / 2
            color: Theme.text
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.stateLayer
                opacity: playButton.down ? Theme.pressedOpacity * 2 : playButton.hovered ? Theme.hoverOpacity * 3 : 0
            }
        }
        contentItem: Item {
            TIcon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: root.playing ? 0 : 2
                name: root.playing ? "pause" : "play"
                size: 24
                strokeWidth: 2.4
                color: Theme.raised
            }
        }
        TToolTip {
            visible: playButton.hovered
            text: qsTr("Lejátszás / szünet (Szóköz) · újra az elejéről: R")
        }
    }
    TLabel {
        x: playButton.x
        y: playButton.y + playButton.height + 6
        width: playButton.width
        horizontalAlignment: Text.AlignHCenter
        text: "Space"
        mono: true
        muted: true
        font.pixelSize: Theme.fontMicro
    }

    // ---- a sor: beszélő a szöveg fölött → szöveg → hullámforma + bizonyítékok ----
    Column {
        id: body
        x: playButton.x + playButton.width + 16
        y: 16
        width: root.width - x - root.pad
        spacing: 0

        RowLayout {
            objectName: "reviewFocusNameRow"
            width: body.width
            height: root.nameRowHeight
            spacing: 8
            TAvatar {
                name: root.speakerName
                speakerIndex: root.item.colorIndex || 0
                size: 24
            }
            TLabel {
                objectName: "reviewFocusSpeaker"
                text: root.valid ? root.speakerName : ""
                font.pixelSize: 17
                font.weight: Theme.weightSemiBold
                color: Theme.speakerInk(root.item.colorIndex || 0)
                elide: Text.ElideRight
                Layout.maximumWidth: 280
            }
            TLabel {
                objectName: "reviewFocusReason"
                text: !root.valid ? ""
                      : "· " + (root.item.timeLabel || "")
                        + ((root.item.reasonTitle || "") !== "" ? " · " + root.item.reasonTitle
                           : root.item.reasonKind === "uncertain" ? " · " + qsTr("hangra kétes") : "")
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 40
            }
            // Állapot-jelek (csak ha a kurzor egy nem nyitott soron áll, vagy változott valami).
            TPill {
                objectName: "reviewFocusUpdated"
                visible: root.item.suggestionUpdated === true && !root.alreadyDecided
                text: qsTr("javaslat frissült")
            }
            TPill {
                objectName: "reviewFocusSettled"
                visible: root.alreadyDecided
                tone: "success"
                text: qsTr("✓ magától eldőlt")
            }
            TPill {
                objectName: "reviewFocusDecided"
                readonly property string kind: root.item.decidedKind || ""
                visible: root.state_ === "decided" || root.state_ === "ignored"
                tone: root.state_ === "ignored" ? "neutral" : "success"
                text: root.state_ === "ignored" ? qsTr("eldöntve: nem azonosítható")
                    : kind === "corrected" ? qsTr("eldöntve: javítva · %1").arg(root.item.sampleState === "noSample" ? qsTr("nem minta") : qsTr("minta"))
                    : qsTr("eldöntve: marad · %1").arg(root.item.sampleState === "noSample" ? qsTr("nem minta") : qsTr("minta"))
            }
            TPill {
                visible: root.state_ === "later"
                text: qsTr("◇ később hagyva")
            }
            TPill {
                visible: root.item.admitted === true
                tone: root.item.consequence === true ? "warn" : "neutral"
                text: root.item.consequence === true ? qsTr("új · a döntéseid miatt") : qsTr("új")
            }
        }
        Item { width: 1; height: 4 }
        TLabel {
            objectName: "reviewFocusText"
            width: body.width
            height: root.textBlockHeight
            verticalAlignment: Text.AlignTop
            text: root.valid ? "„" + (root.item.lineText || "") + "”" : ""
            font.pixelSize: 20
            font.weight: Theme.weightSemiBold
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 1.25
        }
        Item { width: 1; height: 6 }
        RowLayout {
            width: body.width
            height: root.playerRowHeight
            spacing: 12
            Item {
                id: wave
                objectName: "reviewFocusWave"
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                Layout.preferredHeight: 20
                clip: true
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Repeater {
                        model: Math.max(0, Math.floor(wave.width / 5))
                        delegate: Rectangle {
                            required property int index
                            width: 3
                            height: Math.max(3, root.barHeight(index))
                            radius: 1
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.playing && index / Math.max(1, Math.floor(wave.width / 5)) < root.playPos
                                   ? Theme.text : Theme.borderStrong
                        }
                    }
                }
            }
            TLabel {
                objectName: "reviewFocusTime"
                text: root.fmt(root.playPos * root.durationMs) + " / " + root.fmt(root.durationMs)
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            // Bizonyíték-chipek; szűk helyen levágva.
            Item {
                Layout.minimumWidth: 0
                Layout.maximumWidth: chips.implicitWidth
                Layout.preferredWidth: Math.min(chips.implicitWidth, body.width * 0.55)
                Layout.preferredHeight: 22
                clip: true
                Row {
                    id: chips
                    objectName: "reviewFocusReasons"
                    anchors.verticalCenter: parent.verticalCenter
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
    }

    // ---- döntés-gombok: hely, szélesség, sorrend, billentyű fix ----
    Item {
        id: buttonRow
        objectName: "reviewFocusButtons"
        x: root.pad
        y: 16 + root.bodyHeight + 14
        width: root.width - 2 * root.pad
        height: root.buttonHeight
        readonly property var weights: [1.25, 1.15, 1.15, 0.9, 0.9]
        readonly property real unit: (width - 4 * 8) / 5.35
        function xOf(i) {
            let x = 0
            for (let k = 0; k < i; ++k) x += weights[k] * unit + 8
            return x
        }
        ReviewFocusKeyButton {
            objectName: "reviewFocusEnter"
            x: 0
            width: buttonRow.weights[0] * buttonRow.unit
            height: buttonRow.height
            variant: "calm"
            keyText: "Enter"
            iconName: root.enterIcon
            iconColor: root.hasSuggestion ? Theme.accent : Theme.success
            text: root.enterText
            subText: root.enterSub
            enabled: root.valid
            nonSample: root.shiftHeld
            toolTipText: root.hasSuggestion ? qsTr("A sor ide kerül: %1 · Shift: nem minta").arg(root.enterText)
                                            : qsTr("A név marad: %1 (nincs jobb jelölt) · Shift: nem minta").arg(root.speakerName)
            onClicked: root.enterClicked()
        }
        ReviewFocusKeyButton {
            objectName: "reviewFocusConfirm"
            x: buttonRow.xOf(1)
            width: buttonRow.weights[1] * buttonRow.unit
            height: buttonRow.height
            variant: "calm"
            keyText: "J"
            iconName: "check"
            iconColor: Theme.success
            text: root.speakerName
            subText: qsTr("marad")
            enabled: root.valid
            dimmed: !root.hasSuggestion
            nonSample: root.shiftHeld
            toolTipText: qsTr("A mostani név marad (%1) · Shift: nem minta").arg(root.speakerName)
            onClicked: root.confirmClicked()
        }
        ReviewFocusKeyButton {
            objectName: "reviewFocusIgnore"
            x: buttonRow.xOf(2)
            width: buttonRow.weights[2] * buttonRow.unit
            height: buttonRow.height
            variant: "calm"
            keyText: "0"
            iconName: "ban"
            iconColor: Theme.text
            text: root.state_ === "ignored" ? qsTr("Újra azonosítható") : qsTr("Nem azonosítható")
            subText: root.state_ === "ignored" ? qsTr("újra számít a hangja") : qsTr("a név marad, nem minta")
            enabled: root.valid
            toolTipText: qsTr("A név marad, de a hang-elemzés teljesen kihagyja: nem minta, nem bizonyíték")
            onClicked: root.ignoreClicked()
        }
        ReviewFocusKeyButton {
            id: otherButton
            objectName: "reviewFocusOther"
            x: buttonRow.xOf(3)
            width: buttonRow.weights[3] * buttonRow.unit
            height: buttonRow.height
            variant: "calm"
            keyText: "M"
            iconName: "user-search"
            iconColor: Theme.text
            text: qsTr("Más…")
            subText: qsTr("személyválasztó")
            enabled: root.valid
            toolTipText: qsTr("A sor panelje: személyválasztó, új beszélő")
            onClicked: root.otherClicked(otherButton)
        }
        ReviewFocusKeyButton {
            objectName: "reviewFocusLater"
            x: buttonRow.xOf(4)
            width: buttonRow.weights[4] * buttonRow.unit
            height: buttonRow.height
            variant: "calm"
            keyText: "→"
            iconName: "clock"
            iconColor: Theme.text
            text: qsTr("Később")
            subText: qsTr("a lista végére")
            enabled: root.valid
            toolTipText: qsTr("Nem döntés: a sor a kör végére kerül (◇)")
            onClicked: root.laterClicked()
        }
    }

    // ---- „Shift = ugyanez, de nem minta” ----
    Row {
        id: shiftHint
        objectName: "reviewFocusShiftHint"
        x: root.pad
        y: buttonRow.y + buttonRow.height + 8
        height: 20
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: shiftKey.implicitWidth + 10
            height: 18
            radius: 4
            color: root.shiftHeld ? Theme.warnSoft : Theme.sunken
            border.width: 1
            border.color: root.shiftHeld ? Theme.warnLine : Theme.border
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
            text: qsTr("= ugyanez, de nem minta")
            font.pixelSize: Theme.fontCaption
            color: root.shiftHeld ? Theme.warnInk : Theme.textMuted
        }
    }

    // ---- beszélő-számsor: teljes nevek, a sín sorrendjében (1–9), + új beszélő ----
    Rectangle {
        x: 0
        y: shiftHint.y + shiftHint.height + 10
        width: root.width
        height: 1
        color: Theme.border
    }
    Flow {
        id: numbers
        objectName: "reviewFocusNumbers"
        x: root.pad
        y: shiftHint.y + shiftHint.height + 10 + 1 + 12
        width: root.width - 2 * root.pad
        spacing: 8
        Repeater {
            id: laneRepeater
            model: root.lanes.slice(0, 9)
            delegate: Rectangle {
                id: chip
                required property var modelData
                required property int index
                readonly property bool suggested: root.hasSuggestion && root.likelyKey !== "" && modelData.key === root.likelyKey
                readonly property bool current: root.valid && modelData.key === (root.item.speakerKey || "")
                objectName: "reviewFocusLane"
                height: 30
                width: chipRow.implicitWidth + 18
                radius: 15
                color: chip.current ? Theme.sunken
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
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        height: 18
                        radius: 4
                        color: Theme.sunken
                        border.width: chip.current ? 1 : 0
                        border.color: Theme.border
                        TLabel {
                            anchors.centerIn: parent
                            text: String(chip.index + 1)
                            mono: true
                            font.pixelSize: Theme.fontMicro
                            font.weight: Theme.weightSemiBold
                        }
                    }
                    TAvatar {
                        anchors.verticalCenter: parent.verticalCenter
                        name: chip.modelData.name || ""
                        speakerIndex: chip.modelData.colorIndex || 0
                        size: 20
                    }
                    TLabel {
                        objectName: "reviewFocusLaneName"
                        anchors.verticalCenter: parent.verticalCenter
                        text: chip.modelData.name || ""
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightMedium
                    }
                    // A jel helye minden chipen megvan (a sor nem rendeződik át).
                    TLabel {
                        objectName: "reviewFocusLaneMark"
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.markWidth
                        text: chip.suggested ? qsTr("javasolt") : chip.current ? qsTr("most") : ""
                        font.pixelSize: Theme.fontMicro
                        font.weight: Theme.weightSemiBold
                        color: chip.suggested ? Theme.accent : Theme.textMuted
                    }
                }
                HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.laneClicked(chip.index) }
                TToolTip {
                    visible: chipHover.hovered
                    text: (chip.current ? qsTr("%1: marad") : qsTr("→ %1")).arg(chip.modelData.name || "")
                          + (root.shiftHeld ? qsTr(" · nem minta") : "")
                }
            }
        }
        // „+ Új beszélő…”: javasoltként, ha a javaslat egy új beszélő.
        Rectangle {
            id: newChip
            objectName: "reviewFocusLaneNew"
            readonly property bool suggested: root.suggestsNew
            height: 30
            width: newRow.implicitWidth + 18
            radius: 15
            color: newHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
            border.width: suggested ? 1.5 : 0
            border.color: Theme.accent
            TDashedRect {
                anchors.fill: parent
                radius: 15
                visible: !newChip.suggested
            }
            Row {
                id: newRow
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    radius: 4
                    color: Theme.sunken
                    TLabel {
                        anchors.centerIn: parent
                        text: "+"
                        mono: true
                        font.pixelSize: Theme.fontMicro
                        font.weight: Theme.weightSemiBold
                    }
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Új beszélő…")
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightMedium
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.markWidth
                    text: newChip.suggested ? qsTr("javasolt") : ""
                    font.pixelSize: Theme.fontMicro
                    font.weight: Theme.weightSemiBold
                    color: Theme.accent
                }
            }
            HoverHandler { id: newHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.newSpeakerClicked() }
            TToolTip {
                visible: newHover.hovered
                text: qsTr("A sor egy új, névtelen beszélőhöz (+)") + (root.shiftHeld ? qsTr(" · nem minta") : "")
            }
        }
    }
}
