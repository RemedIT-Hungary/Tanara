import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// "Which voice is it?" — the second step of "+ Participant" when the meeting has unnamed voices
// (and the "Which voice?" button of a manual row without a voice). The person is already chosen;
// every unnamed voice row can be listened to (▶) and taken with "That's them" (nameParticipant on
// that row: the voice gets the name; a manual voiceless row of the person merges into it). "None
// of them" adds the person as a separate row without a voice (addParticipant), with the sentence
// why it stays separate. Each choice is one undo step.
//   ParticipantWhichVoicePanel { vm: overviewVm; player: player }  ->  openFor(name, voices, anchor, existingId)
TPopover {
    id: control
    objectName: "whichVoicePanel"

    property var vm: null                       // OverviewViewModel
    property var player: null                   // PlayerController
    property string personName: ""
    property var voices: []                     // the unnamed voice rows
    // The person's manual row without a voice ("Which voice?"): "None of them" keeps it as it is.
    property string existingId: ""

    width: 380
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    function openFor(name, unnamed, anchor, existing) {
        personName = name
        voices = unnamed
        existingId = existing || ""
        const host = parent
        const p = anchor.mapToItem(host, 0, anchor.height)
        x = Math.max(8, Math.min(host.width - width - 8, p.x + anchor.width - width))
        y = p.y + 4
        open()
    }

    function nameVoice(id, name) { if (vm) vm.nameParticipant(id, name) }

    contentItem: ColumnLayout {
        spacing: 6

        TLabel {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.leftMargin: 14; Layout.rightMargin: 14
            text: qsTr("Melyik hang %1?").arg(control.personName)
            font.weight: Theme.weightSemiBold
            wrapMode: Text.Wrap
        }
        TLabel {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            text: qsTr("Hallgasd meg a névtelen hangokat. A választott hang az ő nevére kerül, és legközelebb felismerjük.")
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.35
            wrapMode: Text.Wrap
        }
        Repeater {
            model: control.voices
            Rectangle {
                id: voice
                required property var modelData
                objectName: "whichVoiceRow"
                Layout.fillWidth: true
                Layout.leftMargin: 6; Layout.rightMargin: 6
                implicitHeight: 42
                radius: Theme.radiusControl
                color: voiceHover.hovered ? Theme.sunken : "transparent"
                HoverHandler { id: voiceHover }
                RowLayout {
                    anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                    spacing: 8
                    Item {
                        implicitWidth: 26; implicitHeight: 26
                        SampleButton {
                            anchors.fill: parent
                            player: control.player
                            row: voice.modelData
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        TLabel {
                            Layout.fillWidth: true
                            text: voice.modelData.name || ""
                            font.weight: Theme.weightSemiBold
                            elide: Text.ElideRight
                        }
                        TLabel {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: (voice.modelData.lineCount || 0) > 0 ? qsTr("%n sor", "", voice.modelData.lineCount)
                                                                      : (voice.modelData.sub || "")
                            muted: true
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }
                    }
                    TLabel {
                        visible: voice.modelData.talkShare >= 0
                        text: Math.round(voice.modelData.talkShare * 100) + "%"
                        mono: true
                        muted: true
                        font.pixelSize: 12
                    }
                    TButton {
                        objectName: "whichVoiceThisOne"
                        text: qsTr("Ő az")
                        size: "small"
                        // Deferred: naming reloads the rows (and so the voices this repeater shows).
                        onClicked: {
                            Qt.callLater(control.nameVoice, voice.modelData.id, control.personName)
                            control.close()
                        }
                    }
                }
            }
        }
        TDivider { Layout.fillWidth: true }
        Item {
            objectName: "whichVoiceNone"
            Layout.fillWidth: true
            Layout.leftMargin: 6; Layout.rightMargin: 6
            implicitHeight: 34
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusControl
                color: Theme.stateLayer
                opacity: noneHover.hovered ? Theme.hoverOpacity : 0
            }
            RowLayout {
                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                spacing: 8
                TIcon { name: "user-plus"; size: 14; color: Theme.textMuted }
                TLabel {
                    Layout.fillWidth: true
                    text: control.existingId !== "" ? qsTr("Egyik sem — marad külön sorban, hang nélkül")
                                                    : qsTr("Egyik sem — külön sorba, hang nélkül")
                    elide: Text.ElideRight
                }
            }
            HoverHandler { id: noneHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                onTapped: {
                    if (control.existingId === "" && control.vm) control.vm.addParticipant(control.personName)
                    control.close()
                }
            }
        }
        TLabel {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            Layout.bottomMargin: 12
            text: qsTr("Miért külön sor? Ebben a felvételben még egyik hang sincs a nevéhez kötve, és ha nincs hanglenyomata, a gép nem is találhatta meg: hang nélkül vettük fel. Ha később elnevezel egy hangot a nevére, a két sor összeolvad.")
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.35
            wrapMode: Text.Wrap
        }
    }
}
