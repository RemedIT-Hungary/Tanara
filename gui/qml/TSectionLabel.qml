import QtQuick

// Section label: 12/600, uppercase, +0.06em letter spacing, faded (e.g. "DECISIONS", "TODAY").
TLabel {
    muted: true
    font.pixelSize: Theme.fontCaption
    font.weight: Theme.weightSemiBold
    font.capitalization: Font.AllUppercase
    font.letterSpacing: Theme.labelSpacing
}
