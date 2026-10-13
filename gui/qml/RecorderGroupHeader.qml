import QtQuick

// Device group header in the recorder's device list AND on the Settings "Recording" page:
// "MICROPHONES what you say" - "OUTPUTS what you hear" - "OTHER INPUTS".
//   RecorderGroupHeader { group: dev.group; visible: dev.groupFirst }
Row {
    property int group: 0          // 0 microphone - 1 output - 2 other input

    spacing: 6
    TLabel {
        text: group === 0 ? qsTr("MIKROFONOK") : group === 1 ? qsTr("HANGKIMENETEK") : qsTr("EGYÉB BEMENETEK")
        muted: true
        font.pixelSize: 11
        font.weight: Theme.weightSemiBold
        font.letterSpacing: 0.66
    }
    TLabel {
        text: group === 0 ? qsTr("amit mondasz") : group === 1 ? qsTr("amit hallasz") : ""
        muted: true
        font.pixelSize: 11
    }
}
