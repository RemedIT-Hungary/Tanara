import QtQuick

// Vékony csúszka (lejátszó: pozíció és hangerő): 4 px-es sáv accent kitöltéssel, 12 px-es
// gombbal. `value` 0…1 (kívülről kötött); húzás / kattintás közben a `moved(v)` jelet adja,
// a kötött értéket nem írja felül. Billentyűzet: ← / → `keyStep` lépéssel.
Item {
    id: root

    property real value: 0
    property real keyStep: 0.05
    property bool knobAlwaysVisible: false
    property color trackColor: Theme.border
    property string accessibleName: ""
    readonly property alias dragging: drag.active
    readonly property real shownValue: drag.active ? root.dragValue : Math.max(0, Math.min(1, root.value))
    property real dragValue: 0
    signal moved(real value)

    implicitWidth: 160
    implicitHeight: 20
    activeFocusOnTab: enabled
    Accessible.role: Accessible.Slider
    Accessible.name: accessibleName

    function valueAt(x) { return Math.max(0, Math.min(1, x / Math.max(1, root.width))) }

    Keys.onLeftPressed: root.moved(Math.max(0, root.value - root.keyStep))
    Keys.onRightPressed: root.moved(Math.min(1, root.value + root.keyStep))

    Rectangle {
        id: track
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: 4
        radius: 2
        color: root.trackColor
        Rectangle {
            width: Math.round(root.shownValue * parent.width)
            height: parent.height
            radius: 2
            color: root.enabled ? Theme.accent : Theme.borderStrong
        }
    }
    Rectangle {
        id: knob
        visible: root.enabled && (root.knobAlwaysVisible || hover.hovered || drag.active
                                  || root.activeFocus || root.shownValue > 0)
        x: Math.round(root.shownValue * root.width - width / 2)
        anchors.verticalCenter: parent.verticalCenter
        width: 12; height: 12; radius: 6
        color: Theme.accent
        TFocusRing { visible: root.activeFocus; targetRadius: 6 }
    }

    HoverHandler { id: hover; cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler {
        enabled: root.enabled
        onTapped: (eventPoint) => root.moved(root.valueAt(eventPoint.position.x))
    }
    DragHandler {
        id: drag
        enabled: root.enabled
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        dragThreshold: 0
        onActiveChanged: if (active) root.dragValue = root.valueAt(centroid.position.x)
        onCentroidChanged: {
            if (!active) return
            root.dragValue = root.valueAt(centroid.position.x)
            root.moved(root.dragValue)
        }
    }
}
