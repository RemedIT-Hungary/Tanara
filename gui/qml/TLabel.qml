import QtQuick

// Alap szövegelem a téma betűivel. Minden látható szöveg ebből (vagy vezérlőből) készüljön,
// ne nyers Text-ből — így a betűcsalád és a szín egy helyen marad.
//   TLabel { text: qsTr("Cím"); font.pixelSize: Theme.fontTitle; font.weight: Theme.weightSemiBold }
//   TLabel { text: "00:17"; mono: true; muted: true; font.pixelSize: Theme.fontCaption }
Text {
    property bool mono: false      // IBM Plex Mono (időbélyeg, időtartam, billentyű-tipp)
    property bool muted: false     // másodlagos szöveg (Theme.textMuted)
    // CSS-szerű sormagasság (a betűméret szorzója, pl. 1.55 az átiratban). A Qt saját
    // lineHeight-ja a betű természetes sorközét szorozná, ami a spechez képest túl laza.
    property real cssLineHeight: 0

    color: muted ? Theme.textMuted : Theme.text
    font.family: mono ? Theme.fontMono : Theme.fontSans
    font.pixelSize: Theme.fontBody
    textFormat: Text.PlainText
    lineHeightMode: cssLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
    lineHeight: cssLineHeight > 0 ? Math.round(font.pixelSize * cssLineHeight) : 1.0
}
