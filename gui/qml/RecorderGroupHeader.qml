import QtQuick

// Eszköz-csoport feje a felvevő eszközlistájában ÉS a Beállítások „Rögzítés” lapján:
// „MIKROFONOK amit mondasz” · „HANGKIMENETEK amit hallasz” · „EGYÉB BEMENETEK”.
//   RecorderGroupHeader { group: dev.group; visible: dev.groupFirst }
Row {
    property int group: 0          // 0 mikrofon · 1 hangkimenet · 2 egyéb bemenet

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
