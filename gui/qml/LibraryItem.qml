import QtQuick
import QtQuick.Layouts

// One item of the library list: title (14/600, one line), below it meta ("Oct 2 · 30 min") and
// the three state icons (transcript / summary / identified), on a third row the tags as plain
// text ("#Nordvik  #Partners  +1"; no tags: no third row). When searching, the third row is the
// match: a tag match ("tag: #Nordvik", with a tag icon) and/or the transcript excerpt. In
// multi-select every row starts with a checkbox (16 px).
// It receives the LibraryListModel roles as `required property`.
Item {
    id: root

    required property int index
    required property string meetingId
    required property string meta
    required property string titleBefore
    required property string titleMatch
    required property string titleAfter
    required property bool hasSnippet
    required property string snippetBefore
    required property string snippetMatch
    required property string snippetAfter
    required property int snippetMs
    required property string transcriptState
    required property string summaryState
    required property string identifyState
    required property string transcriptTip
    required property string summaryTip
    required property string identifyTip
    required property var tagLine
    required property bool tagMatch
    required property string tagMatchName

    property bool selected: false
    property bool keyboardFocused: false
    // Multi-select: whether the checkbox is visible, and whether it is checked.
    property bool selectionMode: false
    property bool checked: false
    // modifiers: Qt.ControlModifier / Qt.ShiftModifier (Ctrl / Shift+click)
    signal activated(int modifiers)
    // Click on the checkbox (like Ctrl+click).
    signal checkToggled()
    signal contextMenuRequested(real x, real y)

    // The tag-match row only when the title did not match (on a title match the plain tag row).
    readonly property bool showTagHit: tagMatch && titleMatch === ""
    readonly property bool showTags: !hasSnippet && !showTagHit && tagLine.length > 0

    implicitHeight: column.implicitHeight + 18
    Accessible.role: Accessible.ListItem
    Accessible.name: titleBefore + titleMatch + titleAfter

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.selected ? Theme.accentSoft : "transparent"
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.stateLayer
            opacity: tap.pressed ? Theme.pressedOpacity
                   : hover.hovered && !root.selected ? Theme.hoverOpacity : 0
        }
        Rectangle {
            visible: root.keyboardFocused && root.selected
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1.5
            border.color: Theme.accent
        }
    }

    // Checkbox (16 px, accent when checked) - only during multi-select.
    Rectangle {
        id: checkBox
        visible: root.selectionMode
        x: 10; y: 11
        width: 16; height: 16
        radius: 4
        color: root.checked ? Theme.accent : Theme.raised
        border.width: root.checked ? 0 : 1.5
        border.color: Theme.borderStrong
        TIcon {
            anchors.centerIn: parent
            visible: root.checked
            name: "check"
            size: 12
            strokeWidth: 3
            color: Theme.textOnAccent
        }
    }

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: root.selectionMode ? 36 : 10; rightMargin: 10; topMargin: 9 }
        spacing: 4

        LibraryHighlightText {
            Layout.fillWidth: true
            before: root.titleBefore
            match: root.titleMatch
            after: root.titleAfter
            pixelSize: Theme.fontBody
            weight: Theme.weightSemiBold
            color: Theme.text
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 5
            TLabel {
                Layout.fillWidth: true
                text: root.meta
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
            TStatusIcon { kind: "transcript"; state: root.transcriptState; toolTipText: root.transcriptTip }
            TStatusIcon { kind: "summary"; state: root.summaryState; toolTipText: root.summaryTip }
            TStatusIcon { kind: "identified"; state: root.identifyState; toolTipText: root.identifyTip }
        }
        // Search: the match is in a tag name ("tag: #Nordvik", the tag highlighted).
        RowLayout {
            visible: root.showTagHit
            Layout.fillWidth: true
            spacing: 5
            TIcon { name: "tag"; size: 12; color: Theme.textMuted }
            LibraryHighlightText {
                Layout.fillWidth: true
                before: qsTr("címke:")
                match: "#" + root.tagMatchName
            }
        }
        // Search: an excerpt of the transcript with the match.
        LibraryHighlightText {
            visible: root.hasSnippet
            Layout.fillWidth: true
            before: root.snippetBefore
            match: root.snippetMatch
            after: root.snippetAfter
        }
        // The tags as plain text, in one row (what does not fit: "+N").
        Row {
            visible: root.showTags
            Layout.fillWidth: true
            spacing: 8
            clip: true
            Repeater {
                model: root.showTags ? root.tagLine : []
                TLabel {
                    required property string modelData
                    text: modelData
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.NoWrap
                }
            }
        }
    }

    HoverHandler { id: hover }
    TapHandler {
        id: tap
        acceptedButtons: Qt.LeftButton
        onTapped: (eventPoint) => {
            const p = eventPoint.position
            if (root.selectionMode && p.x < checkBox.x + checkBox.width + 6)
                root.checkToggled()
            else
                root.activated(tap.point.modifiers)
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint) => root.contextMenuRequested(eventPoint.position.x, eventPoint.position.y)
    }
}
