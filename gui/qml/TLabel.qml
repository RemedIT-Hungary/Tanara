import QtQuick

// Base text element with the theme's fonts. All visible text should come from this (or from a
// control), not from a raw Text - so the font family and color stay in one place.
//   TLabel { text: qsTr("Title"); font.pixelSize: Theme.fontTitle; font.weight: Theme.weightSemiBold }
//   TLabel { text: "00:17"; mono: true; muted: true; font.pixelSize: Theme.fontCaption }
Text {
    property bool mono: false      // IBM Plex Mono (timestamp, duration, key hint)
    property bool muted: false     // secondary text (Theme.textMuted)
    // CSS-like line height (a multiplier of the font size, e.g. 1.55 in the transcript). Qt's own
    // lineHeight would multiply the font's natural line spacing, which is too loose vs. the spec.
    property real cssLineHeight: 0

    color: muted ? Theme.textMuted : Theme.text
    font.family: mono ? Theme.fontMono : Theme.fontSans
    font.pixelSize: Theme.fontBody
    textFormat: Text.PlainText
    lineHeightMode: cssLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
    lineHeight: cssLineHeight > 0 ? Math.round(font.pixelSize * cssLineHeight) : 1.0
}
