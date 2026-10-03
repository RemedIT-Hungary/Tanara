import QtQuick

// Szakaszcímke: 12/600, nagybetűs, +0.06em betűköz, halvány (pl. „DÖNTÉSEK”, „MA”).
TLabel {
    muted: true
    font.pixelSize: Theme.fontCaption
    font.weight: Theme.weightSemiBold
    font.capitalization: Font.AllUppercase
    font.letterSpacing: Theme.labelSpacing
}
