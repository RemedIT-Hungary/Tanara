import QtQuick
import QtQuick.Templates as T

// A megbeszélés címkesora a fejlécben (C03/C04): mindig 26 px magas és sosem tör, így a később
// érkező javaslat nem tolja el a felületet. Egy sorban, 6 px-es közökkel:
//   [felrakott címkék…] [+N] [+ Címke]  |  [✦] felirat  [≤ 2 javaslat] [+N] Mind  (i) Miért?
// Számolás közben: | ⟳ „Javaslatok készülnek…”. Javaslat nélkül a „+ Címke” után semmi.
// Billentyű: ha a sor (vagy egy chipje) fókuszban van, a T megnyitja a beviteli mezőt.
//
// Adat: a `model` (MeetingTagsModel) — ha meg van adva, a sor maga hívja a műveleteit, és a
// jeleket csak tájékoztatásul adja ki. A tags / suggestions / suggestionLabel / computing
// property-k felülírhatók; model nélkül saját, kitalált adatos modellt használ (`demoState`).
FocusScope {
    id: root

    property var model: null
    property string demoState: ""
    property var tags: effectiveModel ? effectiveModel.tags : []
    property var suggestions: effectiveModel ? effectiveModel.suggestions : []
    property string suggestionLabel: effectiveModel ? effectiveModel.suggestionLabel : ""
    property bool computing: effectiveModel ? effectiveModel.computing : false
    property bool editable: true
    // A beviteli mező modellje (megadható közösnek; nélküle saját).
    property var inputModel: null
    property int maxVisibleTags: 6
    property int maxVisibleSuggestions: 2
    // Galéria / kép: ennek a chipnek fókusz-kinézete van; a „Miért?” panel nyitva.
    property int demoFocusedTag: -1
    property bool demoWhyOpen: false
    property alias whyPopover: why
    property alias input: input

    signal addRequested(string name, bool isNew)
    signal removeRequested(string id)
    signal acceptRequested(int index)
    signal rejectRequested(int index)
    signal acceptAllRequested()
    signal whyRequested()
    signal tagClicked(string id)

    readonly property var effectiveModel: model ? model : ownModel
    readonly property bool llm: suggestions.length > 0 && suggestions[0].source === "llm"
    readonly property bool hasSuggestions: suggestions.length > 0 && !computing
    property bool adding: false

    function startAdd() {
        if (!editable) return
        adding = true
        input.open()
    }
    function stopAdd() {
        adding = false
        input.close()
        root.forceActiveFocus()
    }
    function openWhy() {
        root.whyRequested()
        why.open()
    }

    implicitHeight: 26
    implicitWidth: line.implicitWidth
    activeFocusOnTab: true
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Címkék")

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_T && event.modifiers === Qt.NoModifier && !adding) {
            startAdd()
            event.accepted = true
        }
    }

    MeetingTagsModel {
        id: ownModel
        demoState: root.demoState
    }
    TagInputModel {
        id: ownInputModel
        controller: App.controller
        excludeIds: root.effectiveModel ? root.effectiveModel.tagIds : []
    }

    // ---- mérés: hány felrakott címke fér ki (legfeljebb maxVisibleTags) ----
    Row {
        id: measure
        visible: false
        spacing: 6
        Repeater {
            id: measureRepeater
            model: root.tags
            TagChip { required property var modelData; text: modelData.name; removable: root.editable }
        }
    }
    TagChip { id: overflowMeasure; visible: false; kind: "overflow"; text: "+" + root.tags.length }

    readonly property int visibleTagCount: {
        const n = root.tags.length
        const room = root.width - addSlot.width - (suggestionPart.visible ? suggestionPart.implicitWidth + 6 : 0)
                     - (computingPart.visible ? computingPart.implicitWidth + 6 : 0)
        let used = 0, k = 0
        for (; k < Math.min(n, root.maxVisibleTags); ++k) {
            const chip = measureRepeater.itemAt(k)
            const w = (chip ? chip.implicitWidth : 80) + 6
            const rest = k + 1 < n ? overflowMeasure.implicitWidth + 6 : 0
            if (used + w + rest > room) break
            used += w
        }
        return k
    }

    Row {
        id: line
        height: parent.height
        spacing: 6

        Repeater {
            model: root.tags.slice(0, root.visibleTagCount)
            TagChip {
                required property var modelData
                required property int index
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.name
                removable: root.editable
                stateFocused: visualFocus || root.demoFocusedTag === index
                onClicked: root.tagClicked(modelData.id)
                onRemoveRequested: {
                    root.removeRequested(modelData.id)
                    if (root.effectiveModel) root.effectiveModel.remove(modelData.id)
                }
            }
        }
        TagChip {
            id: tagOverflow
            visible: root.tags.length > root.visibleTagCount
            anchors.verticalCenter: parent.verticalCenter
            kind: "overflow"
            text: "+" + (root.tags.length - root.visibleTagCount)
            toolTipText: root.tags.slice(root.visibleTagCount).map(t => "#" + t.name).join("  ")
            onClicked: hiddenMenu.open()
            TMenu {
                id: hiddenMenu
                y: tagOverflow.height + 4
                Repeater {
                    model: root.tags.slice(root.visibleTagCount)
                    TMenuItem {
                        required property var modelData
                        text: "#" + modelData.name
                        onTriggered: root.tagClicked(modelData.id)
                    }
                }
            }
        }

        // „+ Címke” — szerkesztéskor helyén a beviteli mező.
        Item {
            id: addSlot
            visible: root.editable
            width: root.adding ? input.width : addButton.implicitWidth
            height: parent.height

            T.AbstractButton {
                id: addButton
                visible: !root.adding
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: addRow.implicitWidth + 16
                implicitHeight: 24
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Címke hozzáadása")
                onClicked: root.startAdd()
                Keys.onReturnPressed: click()
                contentItem: Item {
                    Row {
                        id: addRow
                        anchors.centerIn: parent
                        spacing: 4
                        TIcon { anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: 12; color: Theme.textMuted }
                        TLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Címke")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightMedium
                        }
                    }
                }
                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusTag
                        color: Theme.stateLayer
                        opacity: addButton.down ? Theme.pressedOpacity : addButton.hovered ? Theme.hoverOpacity : 0
                    }
                    TFocusRing { visible: addButton.visualFocus; targetRadius: Theme.radiusTag }
                }
                TToolTip { visible: addButton.hovered; text: qsTr("Címke hozzáadása (T)") }
            }
            TagInput {
                id: input
                visible: root.adding
                width: 180
                anchors.verticalCenter: parent.verticalCenter
                model: root.inputModel ? root.inputModel : ownInputModel
                onTagChosen: (name, isNew) => {
                    root.addRequested(name, isNew)
                    if (root.effectiveModel) root.effectiveModel.add(name)
                }
                onBackspaceOnEmpty: {
                    if (root.tags.length === 0) return
                    const last = root.tags[root.tags.length - 1]
                    root.removeRequested(last.id)
                    if (root.effectiveModel) root.effectiveModel.remove(last.id)
                }
                onClosed: root.stopAdd()
            }
        }

        // ---- javaslatok ----
        Row {
            id: suggestionPart
            visible: root.hasSuggestions
            height: parent.height
            spacing: 6

            Item {
                width: 13; height: parent.height
                Rectangle { anchors.centerIn: parent; width: 1; height: 16; color: Theme.border }
            }
            TIcon {
                visible: root.llm
                anchors.verticalCenter: parent.verticalCenter
                name: "sparkles"
                size: 13
                color: Theme.textMuted
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: root.suggestionLabel
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            Repeater {
                model: root.suggestions.slice(0, root.maxVisibleSuggestions)
                TagChip {
                    required property var modelData
                    required property int index
                    anchors.verticalCenter: parent.verticalCenter
                    kind: modelData.isNew ? "llmNew" : "suggested"
                    text: modelData.name
                    toolTipText: qsTr("Kattintás: hozzáadás · ×: nem illik ide")
                    onClicked: {
                        root.acceptRequested(index)
                        if (root.effectiveModel) root.effectiveModel.accept(index)
                    }
                    onRemoveRequested: {
                        root.rejectRequested(index)
                        if (root.effectiveModel) root.effectiveModel.reject(index)
                    }
                }
            }
            TagChip {
                visible: root.suggestions.length > root.maxVisibleSuggestions
                anchors.verticalCenter: parent.verticalCenter
                kind: "overflow"
                text: "+" + (root.suggestions.length - root.maxVisibleSuggestions)
                toolTipText: qsTr("Az összes javaslat a „Miért?” alatt")
                onClicked: root.openWhy()
            }
            T.AbstractButton {
                id: allButton
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: allText.implicitWidth + 8
                implicitHeight: 24
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Minden javaslat hozzáadása")
                onClicked: {
                    root.acceptAllRequested()
                    if (root.effectiveModel) root.effectiveModel.acceptAll()
                }
                Keys.onReturnPressed: click()
                contentItem: TLabel {
                    id: allText
                    text: qsTr("Mind")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightMedium
                    font.underline: allButton.hovered
                }
                background: Item { TFocusRing { visible: allButton.visualFocus; targetRadius: Theme.radiusTag } }
            }
            T.AbstractButton {
                id: whyButton
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: whyRow.implicitWidth + 10
                implicitHeight: 24
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Miért ezek a javaslatok?")
                onClicked: why.opened ? why.close() : root.openWhy()
                Keys.onReturnPressed: click()
                contentItem: Item {
                    Row {
                        id: whyRow
                        anchors.centerIn: parent
                        spacing: 4
                        TIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "info"; size: 13
                            color: why.visible ? Theme.text : Theme.textMuted
                        }
                        TLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Miért?")
                            color: why.visible ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
                background: Rectangle {
                    radius: Theme.radiusTag
                    color: why.visible ? Theme.sunken : whyButton.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                    TFocusRing { visible: whyButton.visualFocus; targetRadius: Theme.radiusTag }
                }
                TagWhyPopover {
                    id: why
                    x: -200
                    y: whyButton.height + 6
                    suggestions: root.effectiveModel && root.effectiveModel.suggestions.length >= 0
                                 ? root.effectiveModel.whyData() : []
                    onAcceptRequested: (index) => {
                        root.acceptRequested(index)
                        if (root.effectiveModel) root.effectiveModel.accept(index)
                        if (root.suggestions.length === 0) why.close()
                    }
                    onRejectRequested: (index) => {
                        root.rejectRequested(index)
                        if (root.effectiveModel) root.effectiveModel.reject(index)
                        if (root.suggestions.length === 0) why.close()
                    }
                }
            }
        }

        // ---- számolás közben ----
        Row {
            id: computingPart
            visible: root.computing
            height: parent.height
            spacing: 6
            Item {
                width: 13; height: parent.height
                Rectangle { anchors.centerIn: parent; width: 1; height: 16; color: Theme.border }
            }
            TSpinner {
                anchors.verticalCenter: parent.verticalCenter
                size: 13
                color: Theme.textMuted
                running: visible
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Javaslatok készülnek…")
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    Component.onCompleted: if (demoWhyOpen) Qt.callLater(why.open)
}
