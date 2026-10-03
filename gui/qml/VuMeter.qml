import QtQuick

// Szintmérő: 14 szegmens (5×10 px, 2 px köz, sugár 1). Az i-edik szegmens világít, ha
// i < level; 0–8 vuLow, 9–11 vuMid, 12–13 vuHigh. Csúcstartás: 1 px-es belső keret a `peak`
// indexű szegmensen. Nem rögzített eszköznél (active: false) a világító szegmensek vuOff.
//   VuMeter { level: model.level; peak: model.peak; active: model.selected }
Row {
    id: root
    property int level: 0          // 0..14
    property int peak: -1          // szegmens-index, -1 = nincs
    property bool active: true     // rögzített / kiválasztott eszköz

    spacing: 2

    function segColor(i) {
        if (!root.active) return Theme.vuOff
        return i < 9 ? Theme.vuLow : i < 12 ? Theme.vuMid : Theme.vuHigh
    }

    Repeater {
        model: Theme.vuSegments
        Rectangle {
            required property int index
            width: 5; height: 10; radius: 1
            color: index < root.level ? root.segColor(index) : Theme.vuTrack
            border.width: index === root.peak && index >= root.level ? 1 : 0
            border.color: root.segColor(index)
        }
    }
}
