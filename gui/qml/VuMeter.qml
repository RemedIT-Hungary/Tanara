import QtQuick

// Level meter: 14 segments (5x10 px, 2 px gap, radius 1). Segment i lights if i < level; 0-8 vuLow,
// 9-11 vuMid, 12-13 vuHigh. Peak hold: a 1 px inner border on the segment at index `peak`. For a
// device that is not recording (active: false) the lit segments use vuOff.
//   VuMeter { level: model.level; peak: model.peak; active: model.selected }
Row {
    id: root
    property int level: 0          // 0..14
    property int peak: -1          // segment index, -1 = none
    property bool active: true     // recorded / selected device

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
