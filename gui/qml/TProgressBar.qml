import QtQuick
import QtQuick.Templates as T

// Folyamatjelző. thickness: 4 (feladat-sáv, keresősáv) vagy 6 (szakasz-lista). indeterminate:
// futó csík. trackColor: accentSoft dobozban (TaskStrip) állítsd Theme.bg-re.
T.ProgressBar {
    id: control

    property int thickness: 4
    property color trackColor: Theme.sunken
    property color fillColor: Theme.accent

    implicitWidth: 160
    implicitHeight: thickness

    background: Rectangle {
        radius: control.thickness / 2
        color: control.trackColor
    }
    contentItem: Item {
        clip: true
        Rectangle {
            visible: !control.indeterminate
            width: Math.round(control.visualPosition * parent.width)
            height: parent.height
            radius: control.thickness / 2
            color: control.fillColor
        }
        Rectangle {
            id: runner
            visible: control.indeterminate
            width: Math.round(parent.width * 0.3)
            height: parent.height
            radius: control.thickness / 2
            color: control.fillColor
            XAnimator on x {
                running: control.indeterminate && control.visible
                loops: Animation.Infinite
                from: -runner.width
                to: control.width
                duration: 1400
            }
        }
    }
}
