import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Az „Imént" sor (az Átnézendő-újratervezés közös alapja, koncepció 1.3): a szerkesztő alján,
// a lista alatt, FIX helyen — nem toast, nem tűnik el magától. Kimondja az utolsó döntést
// („12:58 · 1 sor átkerült ide: Fehér Gábor") és a mellékhatását (chipek: „minta lett" /
// „nem minta" / „nem azonosítható", „2 másik sor javaslata változott"), mellette
// Visszavonás (Ctrl+Z) és az előző 5 döntés lenyitható mini-naplója. Ha a döntés mintája eltér a
// beszélő többi sorától, egy második sor figyelmeztet: „Ez a minta eltér X többi sorától (hang
// 41%). Biztos?" + „Legyen nem minta".
// Az átsorolás folytatásai a sorban maradnak, amíg a következő szerkesztés le nem cseréli őket:
//   Hasonló N sor is (+ Megmutatom) · „<Forrás> mind a N sora" · Hanglenyomat készítése · ×;
// új személy után (E4) „Még N sor hangja hasonlít…" + „Mind a N hozzá", a hanglenyomat
// készültsége, a címke-kérdés (TagLearnBar); két hasonló hang esetén a páros átnézés ajánlata.
// A × csak az ajánlatokat zárja; az „Imént" sor marad.
Rectangle {
    id: root

    required property var vm                // TranscriptEditorViewModel
    readonly property var last: vm.lastDecision
    readonly property bool hasLast: (last.text || "") !== ""
    readonly property bool change: vm.changeActive
    readonly property bool offer: change && vm.suggestionActive   // „Hasonló N sor is" vár válaszra
    readonly property bool pairOffer: change && vm.pairOfferActive // a két hasonló hang átnézése
    // Új személy (E4): a hozzá hasonló sorok ajánlata és a hanglenyomat készültsége.
    readonly property bool newPerson: vm.changeNewPerson
    readonly property var similar: vm.newPersonSimilar
    readonly property bool similarOffer: newPerson && (similar.count || 0) > 0
    readonly property var readiness: vm.newPersonReadiness
    readonly property bool readinessShown: newPerson && (readiness.text || "") !== ""
    // Új személy: a címke-kérdés (vagy a már megadott válasz).
    readonly property var tagQuestion: vm.changeTagQuestion
    readonly property bool tagAsked: (tagQuestion.text || "") !== ""
    readonly property bool tagAnswered: (tagQuestion.answer || "") !== ""
    // Eltérő minta (a legutóbbi mintát adó döntés).
    readonly property var deviation: vm.sampleDeviation
    readonly property bool warnShown: (deviation.utteranceId || "") !== ""
    readonly property var log: vm.decisionLog
    property bool logOpen: false
    onLogOpenChanged: logOpen && log.length > 0 ? logPopup.open() : logPopup.close()
    onLogChanged: if (log.length === 0) logOpen = false

    // A tömeges folytatás megerősítést kér: a hívó nyitja a párbeszédablakot.
    signal restRequested()
    // A hanglenyomat panelje (a „Hanglenyomat, ha elég" mellett, ha a hívó meg akarja mutatni).
    signal voiceprintPanelRequested(string speakerKey)

    implicitHeight: column.implicitHeight + 1
    color: Theme.surface

    // A sor művelete (felirat + opcionális ikon). `strong`: keretes, hangsúlyos.
    component BarAction: Item {
        id: action
        property string text: ""
        property string iconName: ""
        property bool strong: false
        property string toolTipText: ""
        property string keyText: ""
        property real maximumWidth: 320
        signal clicked()

        implicitWidth: Math.min(maximumWidth, content.implicitWidth + 20)
        width: implicitWidth
        height: 28
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text !== "" ? text : toolTipText
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: action.strong ? Theme.raised : "transparent"
            border.width: action.strong ? 1 : 0
            border.color: Theme.borderStrong
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Theme.stateLayer
                opacity: tap.pressed ? Theme.pressedOpacity : hover.hovered ? Theme.hoverOpacity : 0
            }
            TFocusRing { visible: action.activeFocus; targetRadius: parent.radius }
        }
        Row {
            id: content
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            TIcon {
                visible: action.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: action.iconName
                size: 14
                color: Theme.text
            }
            TLabel {
                visible: action.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, action.maximumWidth - 20 - (action.iconName !== "" ? 20 : 0)
                                                - (action.keyText !== "" ? keyLabel.implicitWidth + 6 : 0))
                elide: Text.ElideMiddle
                text: action.text
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
            }
            TLabel {
                id: keyLabel
                visible: action.keyText !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: action.keyText
                mono: true
                muted: true
                font.pixelSize: Theme.fontMicro
            }
        }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: tap; onTapped: action.clicked() }
        TToolTip { visible: action.toolTipText !== "" && hover.hovered; text: action.toolTipText }
    }

    // Mellékhatás-chip: „minta lett" (success), „nem minta" / „nem azonosítható" (semleges),
    // „N másik sor javaslata változott" (accent).
    component EffectChip: Rectangle {
        id: effect
        property string kind: ""
        property string text: ""
        readonly property color ink: kind === "sample" ? Theme.successInk
                                   : kind === "changed" ? Theme.accent : Theme.textMuted
        implicitWidth: effectRow.implicitWidth + 14
        implicitHeight: 20
        radius: height / 2
        color: kind === "sample" ? Theme.successSoft : kind === "changed" ? Theme.accentSoft : Theme.sunken
        Row {
            id: effectRow
            anchors.centerIn: parent
            spacing: 4
            Item {
                visible: effect.kind === "sample" || effect.kind === "noSample"
                anchors.verticalCenter: parent.verticalCenter
                width: 12; height: 12
                TIcon {
                    anchors.centerIn: parent
                    name: "audio-lines"
                    size: 12
                    color: effect.kind === "noSample" ? Theme.alpha(effect.ink, 0.6) : effect.ink
                }
                Rectangle {
                    visible: effect.kind === "noSample"
                    anchors.centerIn: parent
                    width: 14; height: 1.5
                    rotation: -45
                    color: effect.ink
                }
            }
            TLabel {
                visible: effect.kind === "ignored"
                anchors.verticalCenter: parent.verticalCenter
                text: "?"
                color: effect.ink
                font.pixelSize: Theme.fontMicro
                font.weight: Theme.weightBold
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: effect.text
                color: effect.ink
                font.pixelSize: Theme.fontMicro
                font.weight: Theme.weightSemiBold
            }
        }
    }

    // Elválasztó a sor részei között.
    component RowRule: Rectangle {
        x: 24
        width: parent ? parent.width - 40 : 0
        height: 1
        color: Theme.border
    }

    TDivider { anchors { left: parent.left; right: parent.right; top: parent.top } }

    Column {
        id: column
        y: 1
        width: parent.width

        // ---- az „Imént" sor ----
        Item {
            id: mainRow
            objectName: "imentRow"
            width: parent.width
            height: 40

            RowLayout {
                anchors { fill: parent; leftMargin: 24; rightMargin: 12 }
                spacing: 8

                // A döntés igéje ugyanazzal az ikonnal, mint a fókusz-kártya gombjain (S7):
                // → áthelyezés · ✓ marad · ⊘ nem azonosítható.
                TIcon {
                    objectName: "imentIcon"
                    readonly property string verb: root.last.undone === true ? "" : (root.last.verb || "")
                    Layout.alignment: Qt.AlignVCenter
                    name: root.vm.changeVoiceprintCreated ? "fingerprint" : root.newPerson ? "user-plus"
                        : root.last.undone === true ? "undo-2"
                        : verb === "move" ? "arrow-right" : verb === "keep" ? "check" : verb === "ignore" ? "ban"
                        : "circle-check"
                    size: 15
                    strokeWidth: verb !== "" ? 2.2 : 1.75
                    color: root.newPerson ? Theme.success : verb === "move" ? Theme.accent
                         : verb === "keep" ? Theme.success : Theme.textMuted
                }
                TLabel {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("IMÉNT")
                    muted: true
                    font.pixelSize: Theme.fontMicro
                    font.weight: Theme.weightSemiBold
                    font.letterSpacing: 0.66
                }
                TLabel {
                    objectName: "imentTime"
                    Layout.alignment: Qt.AlignVCenter
                    visible: !root.change && (root.last.time || "") !== ""
                    text: root.last.time || ""
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
                TLabel {
                    id: message
                    objectName: "changeText"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.minimumWidth: Math.min(implicitWidth, 120)
                    Layout.preferredWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth
                    text: root.change ? root.vm.changeText
                        : root.hasLast ? root.last.text
                        : qsTr("Még nincs döntés — itt látszik az utolsó, és innen visszavonható (Ctrl+Z).")
                    muted: !root.change && !root.hasLast
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSmall
                    font.weight: root.change || root.hasLast ? Theme.weightSemiBold : Theme.weightRegular
                    HoverHandler { id: messageHover }
                    TToolTip { visible: messageHover.hovered && message.truncated; text: message.text }
                }
                Repeater {
                    objectName: "imentChips"
                    model: root.last.undone === true ? [] : (root.last.chips || [])
                    EffectChip {
                        required property var modelData
                        objectName: "imentChip"
                        Layout.alignment: Qt.AlignVCenter
                        kind: modelData.kind
                        text: modelData.text
                    }
                }
                Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

                Row {
                    id: actions
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6
                    BarAction {
                        objectName: "changeVoiceprint"
                        visible: root.vm.changeVoiceprintOffer
                        iconName: "fingerprint"
                        text: qsTr("Hanglenyomat készítése")
                        strong: true
                        toolTipText: qsTr("Még nincs hanglenyomata. Az itteni, hosszabb soraiból most készíthető — ebből "
                                          + "ismeri fel a program a következő megbeszéléseken")
                        onClicked: root.vm.createVoiceprintFromChange()
                    }
                    BarAction {
                        objectName: "changeSimilar"
                        visible: root.offer && !root.newPerson
                        iconName: "wand-sparkles"
                        text: qsTr("Hasonló %n sor is", "", root.vm.suggestionCount)
                        strong: true
                        toolTipText: qsTr("Még %n sor hasonlít erre a hangra: ezek is átkerülnek ide: %1", "",
                                          root.vm.suggestionCount).arg(root.vm.suggestionTargetName)
                        onClicked: root.vm.acceptSuggestion()
                    }
                    BarAction {
                        objectName: "changeShow"
                        visible: root.offer && !root.newPerson
                        text: root.vm.suggestionShown ? qsTr("Elrejtem") : qsTr("Megmutatom")
                        toolTipText: qsTr("A hasonló sorok kiemelése a sávokon és az áttekintőn")
                        onClicked: root.vm.suggestionShown = !root.vm.suggestionShown
                    }
                    BarAction {
                        objectName: "changeRest"
                        visible: root.change && root.vm.changeRestCount > 0 && !root.newPerson
                        maximumWidth: Math.max(140, root.width - 720)
                        text: root.vm.changeRestText
                        toolTipText: qsTr("A beszélő összes megmaradt sora is átkerül (előbb megerősítést kér): %1")
                                         .arg(root.vm.changeRestText)
                        onClicked: root.restRequested()
                    }
                    BarAction {
                        objectName: "changeUndo"
                        visible: root.change ? root.vm.changeUndoable : root.vm.canUndo
                        iconName: "undo-2"
                        text: qsTr("Visszavonás")
                        keyText: "Ctrl+Z"
                        strong: root.change
                        toolTipText: root.vm.changeVoiceprintCreated
                                     ? qsTr("A most készült hanglenyomat törlése (az elnevezés marad)")
                                     : qsTr("Visszavonás: %1").arg(root.vm.undoText)
                        onClicked: root.change ? root.vm.undoChange() : root.vm.undo()
                    }
                    BarAction {
                        id: logToggle
                        objectName: "decisionLogToggle"
                        visible: root.log.length > 0
                        iconName: root.logOpen ? "chevron-down" : "chevron-up"
                        toolTipText: qsTr("Az előző döntések (legfeljebb 5)")
                        onClicked: root.logOpen = !root.logOpen
                    }
                    BarAction {
                        objectName: "changeClose"
                        visible: root.change
                        iconName: "x"
                        toolTipText: qsTr("Az ajánlatok bezárása (az „Imént” sor marad)")
                        onClicked: root.vm.dismissChange()
                    }
                }
            }
        }

        // ---- eltérő minta: figyelmeztetés + „Legyen nem minta" ----
        Item {
            id: warnRow
            objectName: "sampleWarning"
            visible: root.warnShown
            width: parent.width
            height: visible ? 34 : 0
            Row {
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                TIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "triangle-alert"
                    size: 14
                    color: Theme.warnInk
                }
                TLabel {
                    objectName: "sampleWarningText"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, warnRow.width - 24 - 22 - noisyButton.width - 40)
                    elide: Text.ElideRight
                    text: root.deviation.text || ""
                    color: Theme.warnInk
                    font.pixelSize: Theme.fontSmall
                }
                BarAction {
                    id: noisyButton
                    objectName: "sampleWarningNoisy"
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Legyen nem minta")
                    strong: true
                    toolTipText: qsTr("A sor marad a beszélőnél, de a hangja nem lesz minta (egy lépés, Ctrl+Z)")
                    onClicked: root.vm.setUtteranceNoisy(root.deviation.utteranceId, true)
                }
            }
        }

        // ---- új személy: a hozzá hasonló sorok (E4) ----
        Item {
            id: similarRow
            objectName: "newPersonSimilar"
            visible: root.similarOffer
            width: parent.width
            height: visible ? 40 : 0
            RowRule {}
            TIcon {
                id: wandIcon
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                name: "wand-sparkles"
                size: 15
                color: Theme.accent
            }
            TLabel {
                id: similarText
                objectName: "newPersonSimilarText"
                anchors.left: wandIcon.right
                anchors.leftMargin: 8
                anchors.right: similarActions.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.StyledText
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSmall
                text: qsTr("Még <b>%n sor</b> hangja hasonlít a most megadott %1 sorhoz", "", root.similar.count || 0)
                          .arg(root.similar.lines || 0)
                      + ((root.similar.detail || "") !== "" ? " <font color=\"" + Theme.textMuted + "\">(" + root.similar.detail + ")</font>" : "")
                      + "."
                HoverHandler { id: similarHover }
                TToolTip { visible: similarHover.hovered && similarText.truncated; text: similarText.text }
            }
            Row {
                id: similarActions
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                BarAction {
                    objectName: "similarShow"
                    text: root.vm.similarShown ? qsTr("Megmutatva a sínen") : qsTr("Megmutatom a sínen")
                    strong: root.vm.similarShown
                    toolTipText: qsTr("A hasonló sorok kék kerettel a sínen és jellel a térképen")
                    onClicked: root.vm.similarShown = !root.vm.similarShown
                }
                BarAction {
                    objectName: "similarAccept"
                    text: qsTr("Mind a %1 hozzá").arg(root.similar.count || 0)
                    strong: true
                    toolTipText: qsTr("Mind %1 sorai közé kerülnek — egy lépésben visszavonható (Ctrl+Z)").arg(root.similar.name || "")
                    onClicked: root.vm.acceptNewPersonSimilar()
                }
            }
        }

        // ---- új személy: a hanglenyomat készültsége (E4) ----
        Item {
            id: readinessRow
            objectName: "newPersonReadiness"
            visible: root.readinessShown
            width: parent.width
            height: visible ? 30 : 0
            TIcon {
                id: printIcon
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                name: "fingerprint"
                size: 15
                color: Theme.textMuted
            }
            TLabel {
                objectName: "newPersonReadinessText"
                anchors.left: printIcon.right
                anchors.leftMargin: 8
                anchors.right: readinessAction.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: root.readiness.text || ""
                muted: true
                font.pixelSize: Theme.fontSmall - 0.5
            }
            TLabel {
                id: readinessAction
                objectName: "newPersonVoiceprint"
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: root.readiness.action || ""
                color: root.readiness.pending === true ? Theme.textMuted : Theme.accent
                font.pixelSize: Theme.fontSmall - 0.5
                font.weight: Theme.weightSemiBold
                font.underline: root.readiness.pending !== true && readinessHover.hovered
                HoverHandler { id: readinessHover; cursorShape: root.readiness.pending === true ? Qt.ArrowCursor : Qt.PointingHandCursor }
                TapHandler {
                    enabled: root.readiness.pending !== true
                    onTapped: root.vm.requestNewPersonVoiceprint()
                }
                TToolTip {
                    visible: readinessHover.hovered
                    text: root.readiness.ready === true
                          ? qsTr("A hanglenyomat most elkészül az itteni soraiból")
                          : qsTr("Amint elég tiszta beszéd gyűlik össze (további sorok hozzá), a hanglenyomat elkészül")
                }
            }
        }

        // ---- a két hasonló hang átnézésének ajánlata ----
        Item {
            id: pairRow
            objectName: "pairOffer"
            visible: root.pairOffer
            width: parent.width
            height: visible ? 40 : 0
            RowRule {}
            TIcon {
                id: pairIcon
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                name: "users"
                size: 15
                color: Theme.textMuted
            }
            TLabel {
                id: pairText
                objectName: "pairOfferText"
                anchors.left: pairIcon.right
                anchors.leftMargin: 8
                anchors.right: pairActions.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: root.vm.pairOfferText
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSmall
                HoverHandler { id: pairTextHover }
                TToolTip { visible: pairTextHover.hovered && pairText.truncated; text: pairText.text }
            }
            Row {
                id: pairActions
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                BarAction {
                    objectName: "pairOfferAccept"
                    iconName: "refresh-cw"
                    text: qsTr("Átnézés")
                    strong: true
                    toolTipText: qsTr("Csak a két beszélő sorait mérem a megerősített soraik hangjához; a kétségeseket "
                                      + "az Átnézendő szűrőbe teszem (visszavonható: Ctrl+Z)")
                    onClicked: root.vm.acceptPairOffer()
                }
                BarAction {
                    objectName: "pairOfferDecline"
                    text: qsTr("Most nem")
                    toolTipText: qsTr("Ebben a munkamenetben erre a két beszélőre nem kérdezek rá újra")
                    onClicked: root.vm.declinePairOffer()
                }
            }
        }

        // ---- új személy: címke-kérdés (UX-terv 5.; a TagLearnBar sötét formanyelvén) ----
        Item {
            id: tagRow
            objectName: "newPersonTagRow"
            visible: root.change && (root.tagAsked || root.tagAnswered)
            width: parent.width
            height: !visible ? 0 : (root.tagAsked ? tagBar.implicitHeight : 40) + 8
            Rectangle {
                x: 16
                y: 2
                width: parent.width - 32
                height: parent.height - 6
                radius: Theme.radiusPopup
                color: Theme.text
                TagLearnBar {
                    id: tagBar
                    visible: root.tagAsked
                    width: parent.width
                    embedded: true
                    question: root.tagQuestion
                    onAnswered: (yes) => root.vm.answerChangeTag(yes)
                    onPairAnswered: (index, yes) => root.vm.answerChangeTagPair(index, yes)
                }
                TIcon {
                    id: tagDoneIcon
                    visible: root.tagAnswered
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    name: "tag"
                    size: 15
                    color: Theme.accentLine
                }
                TLabel {
                    objectName: "newPersonTagAnswer"
                    visible: root.tagAnswered
                    anchors.left: tagDoneIcon.right
                    anchors.leftMargin: 8
                    anchors.right: tagUndo.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.tagQuestion.answer || ""
                    color: Theme.bg
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSmall
                }
                Item {
                    id: tagUndo
                    objectName: "newPersonTagUndo"
                    visible: root.tagAnswered
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: tagUndoRow.implicitWidth + 20
                    height: 28
                    Accessible.role: Accessible.Button
                    Accessible.name: tagUndoLabel.text
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusControl
                        color: Theme.alpha(Theme.bg, tagUndoHover.hovered ? 0.16 : 0)
                    }
                    Row {
                        id: tagUndoRow
                        anchors.centerIn: parent
                        spacing: 6
                        TIcon { anchors.verticalCenter: parent.verticalCenter; name: "undo-2"; size: 14; color: Theme.bg }
                        TLabel {
                            id: tagUndoLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Visszavonás")
                            color: Theme.bg
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightMedium
                        }
                    }
                    HoverHandler { id: tagUndoHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.vm.undoChangeTagAnswer() }
                    TToolTip { visible: tagUndoHover.hovered; text: qsTr("A címke-döntés visszavonása") }
                }
            }
        }
    }

    // ---- az előző döntések (mini-napló), az „Imént" sor fölé nyílik ----
    TPopover {
        id: logPopup
        objectName: "decisionLog"
        focus: false            // a billentyűk a szerkesztőéi maradnak (Ctrl+Z, 0, J…)
        x: Math.max(8, root.width - width - 12)
        y: -height - 4
        width: Math.min(560, root.width - 24)
        padding: 8
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
        onClosed: root.logOpen = false
        contentItem: Column {
            spacing: 2
            TLabel {
                leftPadding: 6
                bottomPadding: 4
                text: qsTr("AZ ELŐZŐ DÖNTÉSEK")
                muted: true
                font.pixelSize: Theme.fontMicro
                font.weight: Theme.weightSemiBold
                font.letterSpacing: 0.66
            }
            Repeater {
                model: root.log
                Row {
                    id: entry
                    required property var modelData
                    objectName: "decisionLogEntry"
                    spacing: 8
                    height: 24
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 44
                        leftPadding: 6
                        text: entry.modelData.time || ""
                        mono: true
                        muted: true
                        font.pixelSize: Theme.fontCaption
                    }
                    TIcon {
                        readonly property string verb: entry.modelData.undone === true ? "" : (entry.modelData.verb || "")
                        anchors.verticalCenter: parent.verticalCenter
                        name: entry.modelData.undone === true ? "undo-2"
                            : verb === "move" ? "arrow-right" : verb === "keep" ? "check" : verb === "ignore" ? "ban" : "circle-check"
                        size: 13
                        strokeWidth: 2.2
                        color: verb === "move" ? Theme.accent : verb === "keep" ? Theme.success : Theme.textMuted
                    }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, logPopup.width - 16 - 52 - 160 - 21)
                        text: entry.modelData.text
                        elide: Text.ElideRight
                        muted: entry.modelData.undone === true
                        font.pixelSize: Theme.fontSmall
                    }
                    Repeater {
                        model: entry.modelData.undone === true ? [] : (entry.modelData.chips || [])
                        EffectChip {
                            required property var modelData
                            anchors.verticalCenter: parent.verticalCenter
                            kind: modelData.kind
                            text: modelData.text
                        }
                    }
                }
            }
        }
    }
}
