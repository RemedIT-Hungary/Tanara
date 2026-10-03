import QtQuick

// Forgó „folyamatban” jel (loader-circle). A running kikapcsolásával megáll.
TIcon {
    id: root
    property bool running: visible
    name: "loader-circle"
    color: Theme.accent
    RotationAnimator on rotation {
        from: 0; to: 360; duration: 900
        loops: Animation.Infinite
        running: root.running
    }
}
