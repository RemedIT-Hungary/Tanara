import QtQuick

// A Személyek ablak „Címkék” szakasza (vault UX-terv 2., 3.2, 7.): a „Becenevek” fölött.
//  - szerkeszthető személy: TagField (chipek ×-szel + beviteli mező; keresés a címkekészletben,
//    új címke is), alatta a javaslat-sor: „Ezeken szokott ott lenni: [+ #Nordvik 11/14] … · Mind”
//    (legfeljebb 3 látszik, a többi „+N” mögött; kattintás = elfogadás, × = elutasítás);
//    helyette muted sor, ha a statisztika még számol, vagy ha a személy kevés megbeszélésen volt;
//  - saját személy: csak egy muted magyarázó sor (a címkék rá nem adnak bizonyítékot);
//  - névtelen mintatartó: a szakasz nem látszik (a hívó rejti: vm.tagsMode === "none").
//   PersonTagsSection { vm: peopleViewModel; width: … }
Column {
    id: root

    property var vm: null
    property bool expanded: false
    readonly property string mode: vm ? vm.tagsMode : "none"
    readonly property var suggestions: vm ? vm.tagSuggestions : []
    readonly property int shownLimit: 3
    property alias field: field

    spacing: 8

    Connections {
        target: root.vm
        function onSelectionChanged() { root.expanded = false }
    }

    TSectionLabel { text: qsTr("Címkék") }

    TLabel {
        objectName: "personTagsSelf"
        visible: root.mode === "self"
        width: parent.width
        text: qsTr("Minden megbeszéléseden ott vagy, ezért a címkék rád nem adnak bizonyítékot.")
        muted: true
        font.pixelSize: Theme.fontCaption
        cssLineHeight: 1.4
        wrapMode: Text.Wrap
    }

    TagField {
        id: field
        objectName: "personTagField"
        visible: root.mode === "edit"
        width: parent.width
        tags: root.vm ? root.vm.personTags : []
        placeholderText: qsTr("Címke hozzáadása…")
        onAddRequested: (name, isNew) => root.vm.addPersonTag(name)
        onRemoveRequested: (id) => root.vm.removePersonTag(id)
    }

    // A javaslat-sor: helyet foglal, nem ugrik (szöveg vagy chipek).
    Flow {
        id: suggestionRow
        objectName: "personTagSuggestions"
        visible: root.mode === "edit" && (root.vm.tagHintText !== "" || root.suggestions.length > 0)
        width: parent.width
        spacing: 6

        TLabel {
            visible: root.vm && root.vm.tagHintText !== ""
            height: 24
            verticalAlignment: Text.AlignVCenter
            text: root.vm ? root.vm.tagHintText : ""
            muted: true
            font.pixelSize: Theme.fontCaption
        }
        TLabel {
            visible: root.suggestions.length > 0
            height: 24
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Ezeken szokott ott lenni:")
            muted: true
            font.pixelSize: Theme.fontCaption
        }
        Repeater {
            model: root.expanded ? root.suggestions : root.suggestions.slice(0, root.shownLimit)
            TagChip {
                required property var modelData
                objectName: "personTagSuggestion"
                kind: "suggested"
                text: modelData.name
                countText: modelData.countText
                toolTipText: modelData.toolTip
                onClicked: root.vm.acceptTagSuggestion(modelData.id)
                onRemoveRequested: root.vm.rejectTagSuggestion(modelData.id)
            }
        }
        TagChip {
            objectName: "personTagOverflow"
            visible: !root.expanded && root.suggestions.length > root.shownLimit
            kind: "overflow"
            text: "+" + (root.suggestions.length - root.shownLimit)
            toolTipText: qsTr("A többi javaslat")
            onClicked: root.expanded = true
        }
        TLabel {
            visible: root.suggestions.length > 1
            height: 24
            verticalAlignment: Text.AlignVCenter
            text: "·"
            muted: true
            font.pixelSize: Theme.fontCaption
        }
        TButton {
            objectName: "personTagAcceptAll"
            visible: root.suggestions.length > 1
            height: 24
            variant: "ghost"
            text: qsTr("Mind")
            leftPadding: 4; rightPadding: 4
            font.pixelSize: Theme.fontCaption
            toolTipText: qsTr("Mindegyik javaslat felrakása")
            contentItem: TLabel {
                text: qsTr("Mind")
                color: Theme.accent
                font.pixelSize: Theme.fontCaption
                font.weight: Theme.weightMedium
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: root.vm.acceptAllTagSuggestions()
        }
    }
}
