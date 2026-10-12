pragma Singleton
import QtQuick

// A szerkesztő-nézetek (Javítás, Átnézendő, söprés) KÖZÖS billentyű-olvasója: a FIZIKAI
// billentyűt adja vissza a módosítókkal, a kiosztástól függetlenül. Ok: a magyar kiosztáson a
// Shift+számjegy írásjel (Shift+6 = „/”), így az `event.key` Shift mellett nem számjegy. A
// számsor 1–9 és a vessző / pont ezért a scancode-ból is olvasható (Linux X11 / Wayland: xkb
// keycode = evdev + 8; Windows: PC scancode); a betűk (J, M, R, B…) a kiosztás felirata szerint.
// A „0” a magyar kiosztáson a bal felső billentyű: azt az `event.key` (Qt.Key_0) adja.
// macOS-en a QML nem kapja meg a natív kódot: ott a Shift+szám csak az `event.key` szerint megy.
//
//   Keys.onPressed: event => {
//       const k = TranscriptKeys.decode(event)
//       if (k.name === "digit" && k.plain) moveTo(k.digit - 1, k.shift)   // Shift = nem minta
//   }
//
// decode() → { name, digit, shift, ctrl, alt, meta, plain }
//   name: "digit" | "J" | "M" | "R" | "B" | "Z" | "Y" | "C" | "A" | "Enter" | "Space" | "Escape" |
//         "Left" | "Right" | "Up" | "Down" | "Comma" | "Period" | "Shift" | "" (egyéb)
//   digit: 0–9 (name === "digit"), különben -1
//   plain: nincs Ctrl / Alt / Meta (a Shift megengedett — az a „nem minta” módosító)
QtObject {
    // Platform-függő scancode-ok (a tesztek felülírhatják: "linux" | "windows" | "")
    property string platform: Qt.platform.os === "windows" ? "windows"
                            : Qt.platform.os === "linux" ? "linux" : ""

    // A számsor 1–9 scancode-ja (index = számjegy − 1), a vessző és a pont.
    readonly property var linuxDigits: [10, 11, 12, 13, 14, 15, 16, 17, 18]
    readonly property var windowsDigits: [2, 3, 4, 5, 6, 7, 8, 9, 10]
    readonly property var linuxPunct: ({ comma: 59, period: 60 })
    readonly property var windowsPunct: ({ comma: 51, period: 52 })

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
        // 1) Számjegy: a kiosztás szerinti (és a numerikus billentyűzet), majd a fizikai számsor.
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
        // 2) Vessző / pont: Shift mellett a magyar kiosztáson „?” / „:”.
        const punct = platform === "windows" ? windowsPunct : platform === "linux" ? linuxPunct : ({})
        if (key === Qt.Key_Comma || (scanCode > 0 && scanCode === punct.comma)) { out.name = "Comma"; return out }
        if (key === Qt.Key_Period || (scanCode > 0 && scanCode === punct.period)) { out.name = "Period"; return out }
        // 3) Betűk és vezérlők a kiosztás szerint.
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
