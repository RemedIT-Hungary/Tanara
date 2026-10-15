import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// One capability card of Settings > Processing (S2). Every card has the same shape: the header shows
// the name, the status pill (Beállítva · Nincs beállítva · Modell hiányzik · Hamarosan · Ellenőrizetlen
// modell), the "where it runs" badge at the right and the chevron, with one sentence below the name;
// the whole header opens / closes the card. The body starts with "Hol fusson?" (the capability's
// route as a segmented control: every segment is always shown, a disabled one tells why in its
// tooltip), an optional hint, then the children: only the chosen route's settings.
// The card's data is its entry in vm.capabilities (route, status, where, options); the open state is
// vm.expandedCards.
//   SettingsCapabilityCard { vm: model; key: "stt"; title: qsTr("Átírás"); blurb: qsTr("…")
//                            TLabel { visible: card.route === "local"; … } }
Rectangle {
    id: root

    property var vm: null
    property string key: ""
    property string title: ""
    property string blurb: ""
    // Under the segmented control (e.g. why only one route is possible).
    property string choiceHint: ""
    default property alias body: bodyCol.data

    // This card's entry of vm.capabilities. The binding reads the list itself (a helper function's
    // reads are not tracked reliably in compiled bindings).
    readonly property var entry: {
        const caps = root.vm ? root.vm.capabilities : []
        for (let i = 0; i < caps.length; ++i)
            if (caps[i].key === root.key) return caps[i]
        return null
    }
    readonly property string route: entry ? entry.route : ""
    readonly property string status: entry ? entry.status : "none"
    readonly property string where: entry ? entry.where : ""
    readonly property string ownRoute: entry ? entry.ownRoute : "local"
    readonly property bool expanded: root.vm !== null && root.vm.expandedCards.indexOf(root.key) >= 0
    // Narrow card: short segment labels filling the row, the badge under the sentence.
    readonly property bool narrow: width < 576

    readonly property string badgeIcon: where === "external" ? "key-round" : where === "cloud" ? "cloud"
                                      : where === "ask" ? "split" : where === "basic" ? "list" : "monitor"
    readonly property string badgeText: {
        switch (root.where) {
        case "local-tanara": return qsTr("Helyben · a Tanara futtatja")
        case "local-server": return qsTr("Helyben · saját szerver")
        case "external": return qsTr("Külső szolgáltató · saját kulcs")
        case "cloud": return qsTr("Tanara Cloud")
        case "ask": return qsTr("Kérdez minden futás előtt")
        case "basic": return qsTr("Alap · beágyazás nélkül")
        }
        return ""
    }
    readonly property string pillText: status === "ok" ? qsTr("Beállítva")
                                     : status === "missing" ? qsTr("Modell hiányzik")
                                     : status === "unverified" ? qsTr("Ellenőrizetlen modell")
                                     : status === "soon" ? qsTr("Hamarosan") : qsTr("Nincs beállítva")
    readonly property string pillTone: status === "ok" ? "success"
                                     : status === "missing" || status === "unverified" ? "warn" : "neutral"

    // The segments: the view model's options with their labels (long, or short on a narrow card).
    readonly property var segments: {
        const opts = root.entry ? root.entry.options : []
        const out = []
        for (let i = 0; i < opts.length; ++i) {
            const o = opts[i]
            let label = ""
            switch (o.value) {
            case "local": label = qsTr("Helyben"); break
            case "external": label = root.narrow ? qsTr("Külső") : qsTr("Külső szolgáltató"); break
            case "cloud": label = root.narrow ? qsTr("Cloud") : qsTr("Tanara Cloud"); break
            case "ask": label = root.narrow ? qsTr("Kérdezzen") : qsTr("Kérdezzen minden alkalommal"); break
            case "basic": label = qsTr("Alap"); break
            }
            out.push({ value: o.value, label: label, enabled: o.enabled, toolTip: o.reason })
        }
        return out
    }

    objectName: "card-" + key
    implicitHeight: col.implicitHeight
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: status === "missing" || status === "unverified" ? Theme.warnLine : Theme.border

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 0

        // ---- header (the whole row is the toggle) ----
        T.AbstractButton {
            id: head
            objectName: "cardHeader-" + root.key
            Layout.fillWidth: true
            implicitHeight: headCol.implicitHeight + 28
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: root.title + " — " + root.pillText + (root.expanded ? "" : " — " + qsTr("Kinyitás"))
            onClicked: root.vm.setCardExpanded(root.key, !root.expanded)
            Keys.onReturnPressed: click()
            Keys.onSpacePressed: click()
            background: Rectangle {
                radius: Theme.radiusPopup
                color: head.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                TFocusRing { visible: head.visualFocus; targetRadius: Theme.radiusPopup }
            }
            contentItem: Item {
                ColumnLayout {
                    id: headCol
                    x: 18; y: 14
                    width: parent.width - 36
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        TLabel {
                            text: root.title
                            font.pixelSize: 15
                            font.weight: Theme.weightSemiBold
                        }
                        TPill {
                            objectName: "cardStatus-" + root.key
                            text: root.pillText
                            tone: root.pillTone
                        }
                        Item { Layout.fillWidth: true }
                        // The where-it-runs badge (wide card: in the head row).
                        Rectangle {
                            objectName: "cardBadge-" + root.key
                            visible: !root.narrow && root.badgeText !== ""
                            implicitWidth: badgeRow.implicitWidth + 16
                            implicitHeight: 22
                            radius: 11
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.border
                            Row {
                                id: badgeRow
                                anchors.centerIn: parent
                                spacing: 5
                                TIcon { name: root.badgeIcon; size: 12; color: Theme.textMuted; anchors.verticalCenter: parent.verticalCenter }
                                TLabel { text: root.badgeText; muted: true; font.pixelSize: Theme.fontCaption; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                        TIcon { name: root.expanded ? "chevron-up" : "chevron-down"; size: 16; color: Theme.textMuted }
                    }
                    TLabel {
                        Layout.fillWidth: true
                        text: root.blurb
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    // Narrow card: the badge goes under the sentence.
                    Row {
                        visible: root.narrow && root.badgeText !== ""
                        spacing: 5
                        TIcon { name: root.badgeIcon; size: 12; color: Theme.textMuted; anchors.verticalCenter: parent.verticalCenter }
                        TLabel { text: root.badgeText; muted: true; font.pixelSize: Theme.fontCaption; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
            }
        }

        // ---- body ----
        ColumnLayout {
            objectName: "cardBody-" + root.key
            visible: root.expanded
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.bottomMargin: 18
            spacing: 12

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

            TLabel {
                Layout.topMargin: 2
                text: qsTr("Hol fusson?")
                font.weight: Theme.weightSemiBold
                font.pixelSize: Theme.fontSmall
            }
            SettingsSegmented {
                objectName: "cardRoute-" + root.key
                Layout.fillWidth: root.narrow
                Layout.maximumWidth: parent.width
                stretch: root.narrow
                value: root.route
                options: root.segments
                onPicked: (v) => root.vm.setRoute(root.entry ? root.entry.cap : "", v)
            }
            TLabel {
                visible: root.choiceHint !== ""
                Layout.fillWidth: true
                text: root.choiceHint
                muted: true
                font.pixelSize: Theme.fontCaption
                cssLineHeight: 1.4
                wrapMode: Text.Wrap
            }

            ColumnLayout {
                id: bodyCol
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 12
            }
        }
    }
}
