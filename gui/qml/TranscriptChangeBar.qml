import QtQuick

// A legutóbbi átsorolás értesítő sávja a szerkesztő alján (a kijelölés sötét sávjának
// formanyelvén): kimondja, mi történt („1 sor átkerült ide: …"), és innen folytatható:
//   Visszavonás · Hasonló N sor is (+ Megmutatom) · „<Forrás> mind a N sora" · bezárás.
// Ha egy TELJES beszélő most kapott nevet, a személynek még nincs hanglenyomata és itt van
// hozzá elég anyag, a sáv felajánlja: „Hanglenyomat készítése" (magától sosem készül; sor /
// kijelölés áthelyezése után nincs ajánlat). Elkészülte után a sáv ezt mondja ki, és a
// „Visszavonás" ekkor pontosan a most készült lenyomatot törli (az elnevezés marad).
// Ha a „hasonló sorok" javaslat azért hallgat, mert a két (elnevezett) hang túl hasonló, és
// mindkettőnek van legalább 3 megerősített sora, a sáv egy második sorban a kettejük
// átnézését ajánlja: „A és B hangja hasonló. Nézzem át…?" — Átnézés / Most nem.
// A sáv a lista ALATT foglal helyet (nem takar sort). Eltűnik a következő szerkesztésre,
// bezárásra, vagy ~12 mp után — amíg javaslat vagy ajánlat vár válaszra, addig nem jár le.
Rectangle {
    id: root

    required property var vm                // TranscriptEditorViewModel
    property int timeoutMs: 12000
    readonly property bool offer: vm.suggestionActive   // „Hasonló N sor is" vár válaszra
    readonly property bool pairOffer: vm.pairOfferActive // a két hasonló hang átnézése vár válaszra

    // A tömeges folytatás megerősítést kér: a hívó nyitja a párbeszédablakot.
    signal restRequested()

    implicitHeight: pairOffer ? 44 + pairRow.height : 44
    radius: Theme.radiusPopup
    color: Theme.text

    TShadow { radius: root.radius }

    // Sötét sávra írt művelet (felirat + opcionális ikon). `strong`: keretes, hangsúlyos.
    component BarAction: Item {
        id: action
        property string text: ""
        property string iconName: ""
        property bool strong: false
        property string toolTipText: ""
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
            color: Theme.alpha(Theme.bg, tap.pressed ? 0.24 : hover.hovered ? 0.16 : action.strong ? 0.08 : 0)
            border.width: action.strong ? 1 : 0
            border.color: Theme.alpha(Theme.bg, 0.4)
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
                color: Theme.bg
            }
            TLabel {
                visible: action.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, action.maximumWidth - 20 - (action.iconName !== "" ? 20 : 0))
                elide: Text.ElideMiddle
                text: action.text
                color: Theme.bg
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
            }
        }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: tap; onTapped: action.clicked() }
        TToolTip { visible: action.toolTipText !== "" && hover.hovered; text: action.toolTipText }
    }

    HoverHandler { id: barHover }

    // Lejárat: rámutatás alatt és nyitott javaslatnál áll; minden új értesítésnél újraindul.
    Timer {
        id: expiry
        interval: root.timeoutMs
        running: root.visible && root.vm.changeActive && !barHover.hovered && !root.offer && !root.pairOffer
        onTriggered: root.vm.dismissChange()
    }
    Connections {
        target: root.vm
        function onChangeChanged() { if (expiry.running) expiry.restart() }
    }

    Item {
        id: mainRow
        width: parent.width
        height: 44

        TIcon {
            id: doneIcon
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            name: root.vm.changeVoiceprintCreated ? "fingerprint" : "circle-check"
            size: 16
            color: Theme.bg
        }
        TLabel {
            id: message
            objectName: "changeText"
            anchors.left: doneIcon.right
            anchors.leftMargin: 8
            anchors.right: actions.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.vm.changeText
            color: Theme.bg
            elide: Text.ElideRight
            font.weight: Theme.weightSemiBold
            HoverHandler { id: messageHover }
            TToolTip { visible: messageHover.hovered && message.truncated; text: message.text }
        }

        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            BarAction {
                objectName: "changeUndo"
                visible: root.vm.changeUndoable
                iconName: "undo-2"
                text: qsTr("Visszavonás")
                strong: true
                toolTipText: root.vm.changeVoiceprintCreated
                             ? qsTr("A most készült hanglenyomat törlése (az elnevezés marad)")
                             : qsTr("Az átsorolás visszavonása (Ctrl+Z)")
                onClicked: root.vm.undoChange()
            }
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
                visible: root.offer
                iconName: "wand-sparkles"
                text: qsTr("Hasonló %n sor is", "", root.vm.suggestionCount)
                strong: true
                toolTipText: qsTr("Még %n sor hasonlít erre a hangra: ezek is átkerülnek ide: %1", "",
                                  root.vm.suggestionCount).arg(root.vm.suggestionTargetName)
                onClicked: root.vm.acceptSuggestion()
            }
            BarAction {
                objectName: "changeShow"
                visible: root.offer
                text: root.vm.suggestionShown ? qsTr("Elrejtem") : qsTr("Megmutatom")
                toolTipText: qsTr("A hasonló sorok kiemelése a sávokon és az áttekintőn")
                onClicked: root.vm.suggestionShown = !root.vm.suggestionShown
            }
            BarAction {
                objectName: "changeRest"
                visible: root.vm.changeRestCount > 0
                maximumWidth: Math.max(140, root.width - 620)
                text: root.vm.changeRestText
                toolTipText: qsTr("A beszélő összes megmaradt sora is átkerül (előbb megerősítést kér): %1")
                                 .arg(root.vm.changeRestText)
                onClicked: root.restRequested()
            }
            BarAction {
                objectName: "changeClose"
                iconName: "x"
                toolTipText: qsTr("Bezárás")
                onClicked: root.vm.dismissChange()
            }
        }
    }

    // ---- a két hasonló hang átnézésének ajánlata (második sor) ----
    Item {
        id: pairRow
        objectName: "pairOffer"
        visible: root.pairOffer
        y: mainRow.height
        width: parent.width
        height: visible ? 40 : 0

        Rectangle {
            x: 12
            width: parent.width - 24
            height: 1
            color: Theme.alpha(Theme.bg, 0.18)
        }
        TIcon {
            id: pairIcon
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -2
            name: "users"
            size: 16
            color: Theme.bg
        }
        TLabel {
            id: pairText
            objectName: "pairOfferText"
            anchors.left: pairIcon.right
            anchors.leftMargin: 8
            anchors.right: pairActions.left
            anchors.rightMargin: 12
            anchors.verticalCenter: pairIcon.verticalCenter
            text: root.vm.pairOfferText
            color: Theme.bg
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSmall
            HoverHandler { id: pairTextHover }
            TToolTip { visible: pairTextHover.hovered && pairText.truncated; text: pairText.text }
        }
        Row {
            id: pairActions
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: pairIcon.verticalCenter
            spacing: 6
            BarAction {
                objectName: "pairOfferAccept"
                iconName: "refresh-cw"
                text: qsTr("Átnézés")
                strong: true
                toolTipText: qsTr("Csak a két beszélő sorait mérem a megerősített soraik hangjához; a kétségeseket "
                                  + "bizonytalannak jelölöm (visszavonható: Ctrl+Z)")
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
}
