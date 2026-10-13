pragma Singleton
import QtQuick

// Shared key reader for the editing views (Fix, To review, sweep mode): it returns the PHYSICAL
// key plus modifiers, independent of the keyboard layout. Why: on a Hungarian layout
// Shift+digit produces punctuation (Shift+6 = "/"), so `event.key` is not a digit with Shift held.
// The digit row 1-9 and comma / period are therefore also read from the scancode (Linux X11 /
// Wayland: xkb keycode = evdev + 8; Windows: PC scancode); letters (J, M, R, B...) follow the layout label.
// "0" is the top-left key on the Hungarian layout, so `event.key` (Qt.Key_0) covers it.
// On macOS QML gets no native code: Shift+digit works by `event.key` only there.
//
//   Keys.onPressed: event => {
//       const k = TranscriptKeys.decode(event)
//       if (k.name === "digit" && k.plain) moveTo(k.digit - 1, k.shift)   // Shift = not a sample
//   }
//
// decode() → { name, digit, shift, ctrl, alt, meta, plain }
//   name: "digit" | "J" | "M" | "R" | "B" | "Z" | "Y" | "C" | "A" | "Enter" | "Space" | "Escape" |
//         "Left" | "Right" | "Up" | "Down" | "Comma" | "Period" | "BracketLeft" | "BracketRight" |
//         "Shift" | "" (other)
// "[" / "]" (sweep mode: tempo down / up) are the two keys next to P, by physical position
// (on the Hungarian layout they type two accented letters).
//   digit: 0-9 (name === "digit"), otherwise -1
//   plain: no Ctrl / Alt / Meta (Shift is allowed: it is the "not a sample" modifier)
QtObject {
    // Platform-dependent scancodes (tests can override: "linux" | "windows" | "")
    property string platform: Qt.platform.os === "windows" ? "windows"
                            : Qt.platform.os === "linux" ? "linux" : ""

    // Scancodes of the digit row 1-9 (index = digit - 1), comma and period.
    readonly property var linuxDigits: [10, 11, 12, 13, 14, 15, 16, 17, 18]
    readonly property var windowsDigits: [2, 3, 4, 5, 6, 7, 8, 9, 10]
    readonly property var linuxPunct: ({ comma: 59, period: 60 })
    readonly property var windowsPunct: ({ comma: 51, period: 52 })
    readonly property var linuxBrackets: ({ left: 34, right: 35 })
    readonly property var windowsBrackets: ({ left: 26, right: 27 })

    function decode(event) {
        return decodeRaw(event.key, event.modifiers, event.nativeScanCode || 0)
    }

    function decodeRaw(key, modifiers, scanCode) {
        const out = {
            name: "", digit: -1,
            shift: (modifiers & Qt.ShiftModifier) !== 0,
            ctrl: (modifiers & Qt.ControlModifier) !== 0,
            alt: (modifiers & Qt.AltModifier) !== 0,
            meta: (modifiers & Qt.MetaModifier) !== 0,
            plain: false
        }
        out.plain = !out.ctrl && !out.alt && !out.meta
        // 1) Digit: by layout (and the numeric keypad), then the physical digit row.
        if (key >= Qt.Key_0 && key <= Qt.Key_9) {
            out.name = "digit"
            out.digit = key - Qt.Key_0
            return out
        }
        const digits = platform === "windows" ? windowsDigits : platform === "linux" ? linuxDigits : []
        const at = scanCode > 0 ? digits.indexOf(scanCode) : -1
        if (at >= 0) {
            out.name = "digit"
            out.digit = at + 1
            return out
        }
        // 2) Comma / period: with Shift they type "?" / ":" on the Hungarian layout.
        const punct = platform === "windows" ? windowsPunct : platform === "linux" ? linuxPunct : ({})
        if (key === Qt.Key_Comma || (scanCode > 0 && scanCode === punct.comma)) { out.name = "Comma"; return out }
        if (key === Qt.Key_Period || (scanCode > 0 && scanCode === punct.period)) { out.name = "Period"; return out }
        // 3) "[" / "]" (the two keys next to P).
        const br = platform === "windows" ? windowsBrackets : platform === "linux" ? linuxBrackets : ({})
        if (key === Qt.Key_BracketLeft || (scanCode > 0 && scanCode === br.left)) { out.name = "BracketLeft"; return out }
        if (key === Qt.Key_BracketRight || (scanCode > 0 && scanCode === br.right)) { out.name = "BracketRight"; return out }
        // 4) Letters and control keys by layout.
        switch (key) {
        case Qt.Key_J: out.name = "J"; break
        case Qt.Key_M: out.name = "M"; break
        case Qt.Key_R: out.name = "R"; break
        case Qt.Key_B: out.name = "B"; break
        case Qt.Key_Z: out.name = "Z"; break
        case Qt.Key_Y: out.name = "Y"; break
        case Qt.Key_C: out.name = "C"; break
        case Qt.Key_A: out.name = "A"; break
        case Qt.Key_Return:
        case Qt.Key_Enter: out.name = "Enter"; break
        case Qt.Key_Space: out.name = "Space"; break
        case Qt.Key_Escape: out.name = "Escape"; break
        case Qt.Key_Left: out.name = "Left"; break
        case Qt.Key_Right: out.name = "Right"; break
        case Qt.Key_Up: out.name = "Up"; break
        case Qt.Key_Down: out.name = "Down"; break
        case Qt.Key_Shift: out.name = "Shift"; break
        }
        return out
    }
}
