import QtQuick
import QtQuick.Templates as T

// „Miért ezek?” (T02): 420 px-es panel a címkesor „Miért?” gombja alatt. Javaslatonként: a
// chip, „Hozzáadás” (elsődleges, 26 px) és „Nem illik ide”; az indokok rácsban (130 px | többi):
// „Közös résztvevő”, „Közös kifejezések”, „Hasonló cím”; a „Hasonló megbeszélés” linkek accent
// színnel (megnyitják azt a megbeszélést). Alul: az elutasított javaslat itt nem jön újra.
//   suggestions: [{ name, isNew, reasons: [{ kind, values, label?, text? }],
//                   similarMeetings: [{ meetingId, title, dateText }] }] (MeetingTagsModel.whyData())
// Adat nélkül kitalált mintát mutat (galéria, kép).
TPopover {
    id: popover

    property var suggestions: sample.whyData()
    signal acceptRequested(int index)
    signal rejectRequested(int index)
    signal meetingRequested(string meetingId)

    function reasonLabel(r) {
        if (r.label) return r.label
        return r.kind === "participant" ? qsTr("Közös résztvevő")
             : r.kind === "title" ? qsTr("Hasonló cím") : qsTr("Közös kifejezések")
    }

    width: 420
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent

    MeetingTagsModel { id: sample; demoState: "why" }

    contentItem: Flickable {
        implicitHeight: Math.min(body.implicitHeight + 28, 560)
        contentHeight: body.implicitHeight + 28
        contentWidth: width
        clip: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: TScrollBar {}

        Column {
            id: body
            x: 16; y: 14
            width: popover.width - 32
            spacing: 12

            TLabel {
                text: qsTr("Miért ezek?")
                font.weight: Theme.weightSemiBold
            }

            Repeater {
                model: popover.suggestions
                Column {
                    id: block
                    required property var modelData
                    required property int index
                    width: body.width
                    spacing: 7

                    // Felső elválasztó: a vonal alatt 12 px (5 + a 7-es köz).
                    Item {
                        width: parent.width
                        height: 5
                        Rectangle { width: parent.width; height: 1; color: Theme.border }
                    }

                    Item {
                        width: parent.width
                        height: 26
                        TagChip {
                            anchors.verticalCenter: parent.verticalCenter
                            kind: block.modelData.isNew ? "llmNew" : "suggested"
                            removable: false
                            focusPolicy: Qt.NoFocus
                            text: block.modelData.name
                            maxWidth: 220
                            onClicked: popover.acceptRequested(block.index)
                        }
                        Row {
                            anchors.right: parent.right
                            spacing: 8
                            TButton {
                                height: 26
                                text: qsTr("Hozzáadás")
                                variant: "primary"
                                radius: 5
                                font.pixelSize: Theme.fontSmall
                                leftPadding: 10; rightPadding: 10
                                onClicked: popover.acceptRequested(block.index)
                            }
                            TButton {
                                height: 26
                                text: qsTr("Nem illik ide")
                                radius: 5
                                font.pixelSize: Theme.fontSmall
                                leftPadding: 10; rightPadding: 10
                                onClicked: popover.rejectRequested(block.index)
                            }
                        }
                    }

                    Repeater {
                        model: block.modelData.reasons
                        Item {
                            required property var modelData
                            width: block.width
                            height: Math.max(reasonKey.implicitHeight, reasonValue.implicitHeight)
                            TLabel {
                                id: reasonKey
                                width: 130
                                text: popover.reasonLabel(parent.modelData)
                                muted: true
                                font.pixelSize: Theme.fontSmall
                                cssLineHeight: 1.4
                                wrapMode: Text.Wrap
                            }
                            TLabel {
                                id: reasonValue
                                x: 140
                                width: parent.width - 140
                                text: parent.modelData.text !== undefined ? parent.modelData.text : parent.modelData.values.join(", ")
                                font.pixelSize: Theme.fontSmall
                                cssLineHeight: 1.4
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Item {
                        visible: block.modelData.similarMeetings.length > 0
                        width: block.width
                        height: Math.max(similarKey.implicitHeight, links.implicitHeight)
                        TLabel {
                            id: similarKey
                            width: 130
                            text: qsTr("Hasonló megbeszélés")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            cssLineHeight: 1.4
                            wrapMode: Text.Wrap
                        }
                        Column {
                            id: links
                            x: 140
                            width: parent.width - 140
                            spacing: 2
                            Repeater {
                                model: block.modelData.similarMeetings
                                T.AbstractButton {
                                    id: link
                                    required property var modelData
                                    width: Math.min(implicitWidth, links.width)
                                    implicitWidth: linkText.implicitWidth
                                    implicitHeight: linkText.implicitHeight
                                    hoverEnabled: true
                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Link
                                    Accessible.name: linkText.text
                                    onClicked: popover.meetingRequested(modelData.meetingId)
                                    Keys.onReturnPressed: click()
                                    contentItem: TLabel {
                                        id: linkText
                                        text: link.modelData.dateText !== "" ? link.modelData.title + " · " + link.modelData.dateText
                                                                             : link.modelData.title
                                        color: Theme.accent
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Theme.weightMedium
                                        font.underline: link.hovered || link.visualFocus
                                        cssLineHeight: 1.4
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Column {
                width: body.width
                spacing: 0
                Rectangle { width: parent.width; height: 1; color: Theme.border }
                TLabel {
                    width: parent.width
                    topPadding: 10
                    text: qsTr("Az elutasított javaslatot ennél a megbeszélésnél nem ajánljuk újra.")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
