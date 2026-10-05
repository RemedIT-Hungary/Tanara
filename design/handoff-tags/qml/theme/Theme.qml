pragma Singleton
import QtQuick

// Tanara — "Print" theme tokens. Generated from the HTML design references (OKLCH → sRGB hex).
// Usage: import "theme" ; color: Theme.text ; Theme.dark = true
QtObject {
    id: theme
    property bool dark: false

    // ---- Colour: surfaces & text ----
    readonly property color bg: dark ? "#121416" : "#f6f5f3"
    readonly property color surface: dark ? "#191b1d" : "#fbfbf9"
    readonly property color raised: dark ? "#232529" : "#ffffff"
    readonly property color sunken: dark ? "#0d0e11" : "#ecebe8"
    readonly property color border: dark ? "#313336" : "#dbdbd8"
    readonly property color borderStrong: dark ? "#505357" : "#b5b4b1"
    readonly property color text: dark ? "#ecebe8" : "#191b1d"
    readonly property color textMuted: dark ? "#acaba7" : "#585b5f"
    readonly property color accent: dark ? "#7eb1f3" : "#2f62ac"
    readonly property color onAccent: dark ? "#09121f" : "#fcfcfc"
    readonly property color accentSoft: dark ? "#1e2e47" : "#e1ecfc"
    readonly property color accentLine: dark ? "#304d78" : "#bacfef"
    readonly property color warn: dark ? "#e6b55d" : "#d79628"
    readonly property color warnSoft: dark ? "#3e2d10" : "#fcedcd"
    readonly property color warnLine: dark ? "#6c5019" : "#eac992"
    readonly property color warnInk: dark ? "#f3d086" : "#784900"
    readonly property color success: dark ? "#74c692" : "#3a8357"
    readonly property color successSoft: dark ? "#193825" : "#daf3e1"
    readonly property color successInk: dark ? "#a7e1ba" : "#115531"
    readonly property color danger: dark ? "#da534f" : "#ba3535"
    readonly property color dangerSoft: dark ? "#3e1e1c" : "#ffece9"
    readonly property color dangerLine: dark ? "#843c38" : "#f9bdb7"
    readonly property color dangerInk: dark ? "#febab4" : "#9b1e22"
    readonly property color rec: dark ? "#e9504d" : "#d42f34"
    readonly property color scrim: dark ? "#80000000" : "#5913161b"
    readonly property color shadow: dark ? "#80000000" : "#2e11161f"

    // ---- Speaker palette (fixed order, assigned by first appearance in a meeting) ----
    // line = lane block / ring / solid; soft = chip & avatar fill; ink = name label on bg
    readonly property var speakerLineLight: ["#c15e50", "#008dbe", "#47944c", "#ab61a5", "#ad7300", "#627ace", "#00988a", "#bd5b7d", "#868604", "#8d6cc2", "#ba6826"]
    readonly property var speakerSoftLight: ["#ffdfd8", "#ceeefe", "#d8efd8", "#f8dff5", "#f8e5cb", "#dde7ff", "#cbf1eb", "#ffdde7", "#e9ebcc", "#ece2ff", "#fee2cf"]
    readonly property var speakerInkLight:  ["#822d22", "#005681", "#135d1d", "#70306c", "#724000", "#34468e", "#006056", "#7e2a4a", "#525100", "#583a84", "#7c3600"]
    readonly property var speakerLineDark:  ["#eb8373", "#32b3e6", "#6dba70", "#d285cb", "#d49838", "#859ff6", "#00beaf", "#e680a1", "#aaab3f", "#b191ea", "#e28d4f"]
    readonly property var speakerSoftDark:  ["#4d2b25", "#143a4b", "#243c24", "#442b42", "#453114", "#2b3450", "#0c3e39", "#4b2a34", "#373816", "#392f4c", "#4a2e1a"]
    readonly property var speakerInkDark:   ["#ffbbae", "#91ddff", "#ace1ad", "#f3bced", "#f4c98e", "#bacfff", "#89e5d9", "#ffb9ce", "#d4d791", "#dac4ff", "#ffc298"]
    readonly property int speakerCount: 11
    function speakerLine(i) { return (dark ? speakerLineDark : speakerLineLight)[i % speakerCount] }
    function speakerSoft(i) { return (dark ? speakerSoftDark : speakerSoftLight)[i % speakerCount] }
    function speakerInk(i)  { return (dark ? speakerInkDark  : speakerInkLight)[i % speakerCount] }
    readonly property color onSpeaker: dark ? "#16181c" : "#fcfcfc"

    // ---- Typography (IBM Plex, SIL OFL) ----
    readonly property string fontSans: "IBM Plex Sans"
    readonly property string fontMono: "IBM Plex Mono"
    readonly property int fontTitle: 20        // meeting title, 600
    readonly property int fontHeading: 16      // section / dialog title, 600
    readonly property int fontBody: 14         // default UI + transcript text, 400, line-height 1.55 in transcript
    readonly property int fontSmall: 13        // secondary text, buttons in toolbars
    readonly property int fontCaption: 12      // meta, timestamps (mono), section labels (600, uppercase, +0.06em)
    readonly property int fontMicro: 11        // pills, monograms
    readonly property real transcriptLineHeight: 1.55

    // ---- Spacing (4-pt grid) ----
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 32
    readonly property int space7: 48

    // ---- Radius ----
    readonly property int radiusLane: 3        // lane blocks, small marks
    readonly property int radiusTag: 4         // tag chips (square-ish, distinct from person/status pills)
    readonly property int radiusControl: 6     // buttons, fields, cards, banners
    readonly property int radiusPopup: 8       // menus, popovers, list containers
    readonly property int radiusDialog: 10     // dialogs, window
    readonly property int radiusPill: 999

    // ---- Sizes ----
    readonly property int controlHeight: 34    // primary/secondary buttons, fields
    readonly property int controlHeightSmall: 28
    readonly property int titleBarHeight: 36
    readonly property int sidebarWidth: 276
    readonly property int playerHeight: 52
    readonly property int laneWidth: 24        // speaker rail column
    readonly property int laneGroupWidth: 20   // collapsed "+N" column
    readonly property int iconSize: 16         // 15–16 in UI, 20 in icon grid, stroke 1.75
    readonly property int avatarSize: 24       // 20 in rail header, 28 in larger headers

    // ---- Elevation ----
    readonly property color shadowColor: dark ? "#80000000" : "#2e11161f"
    readonly property int shadowBlur: 32
    readonly property int shadowOffsetY: 12

    // ---- Motion ----
    readonly property int durationFast: 120
    readonly property int durationNormal: 200
    readonly property int easing: Easing.OutCubic
    // ---- Recorder & level meters ----
    readonly property color successLine: dark ? "#2b6241" : "#acd7ba"
    readonly property color recSoft: dark ? "#3b1c1a" : "#ffece9"
    readonly property color recLine: dark ? "#742e2b" : "#fdc9c4"
    readonly property color recInk: dark ? "#ffb7b0" : "#9b1e22"
    readonly property color vuTrack: dark ? "#27292c" : "#e5e4e2"
    readonly property color vuOff: dark ? "#7e8084" : "#84868a"
    readonly property color vuLow: dark ? "#55c483" : "#389560"
    readonly property color vuMid: dark ? "#f0ba59" : "#e1a035"
    readonly property color vuHigh: dark ? "#ec5b57" : "#c53637"
    // VU: 14 segments, 5×10 px, gap 2, radius 1. Segment i lit when i < level; 0–8 vuLow, 9–11 vuMid, 12–13 vuHigh.
    // Unselected device: lit segments use vuOff. Peak-hold = 1px inset outline on segment (peak-1), decay ~1.5 s.
    readonly property int vuSegments: 14
    readonly property int recorderWidth: 380
}
