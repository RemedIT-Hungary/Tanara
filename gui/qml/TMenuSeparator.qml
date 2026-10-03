import QtQuick
import QtQuick.Templates as T

// Menü-elválasztó (1 px, 4/6 margó).
T.MenuSeparator {
    implicitWidth: 40
    implicitHeight: 9
    leftPadding: 6; rightPadding: 6; topPadding: 4; bottomPadding: 4
    contentItem: Rectangle { implicitHeight: 1; color: Theme.border }
}
