# Flatpak test checklist (experiment, decision D13)

The manifest `eu.remedit.Tanara.yml` has **not** been built or run by the person who wrote it
(no `flatpak-builder` on the build machine). Treat the first build as part of the test. The Flatpak
goes into a release only after every item below passed on a real desktop and "it works well" was
decided by a person.

## Build

```bash
flatpak install --user flathub org.kde.Platform//6.11 org.kde.Sdk//6.11
cd ~/projektek/tanara          # a clean checkout; the manifest copies the working tree
flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/eu.remedit.Tanara.yml
```

If a module fails, the likely suspects are, in this order: `pipewire-tools` (meson option names
differ between PipeWire versions; the goal is only `pw-dump`), `tanara` (the `post-install`
commands expect the CMake build directory as the current directory), `qtkeychain`.
Run with a throw-away data folder first, so that nothing touches your real library:

```bash
flatpak run --env=TANARA_HOME=/var/tmp/tanara-flatpak-test eu.remedit.Tanara
```

(For the checks that need the real folders - 4, 5, 6 - leave `TANARA_HOME` out and use a
test account / empty `~/.var/app/eu.remedit.Tanara`.)

## Checklist

Mark each: OK / not OK / note. Use `flatpak run --command=tanara-cli eu.remedit.Tanara <args>` for the CLI.

1. **Start and version.** The window opens (Wayland and X11). `tanara-cli --version` prints the version.
   `tanara-cli paths show` shows data/config under `~/.var/app/eu.remedit.Tanara/...`.
2. **Microphone.** Settings -> Recording: the microphone is listed (`tanara-cli devices`). A 10 s
   test recording has sound and the level meter moves. (Permission: `--socket=pulseaudio`.)
3. **Loopback (system sound).** Play a video on the host, record with the "system sound" / monitor
   source: the recording has the video's sound. If the monitor source is not listed, note the
   output of `flatpak run --command=sh eu.remedit.Tanara -c 'pactl list short sources'` (if `pactl`
   is not there: `tanara-cli devices`).
4. **Call detection.** `flatpak run --command=pw-dump eu.remedit.Tanara | head` prints JSON (this is
   the PipeWire socket permission and the `pipewire-tools` module). Start
   `flatpak run --command=tanara-watcher eu.remedit.Tanara`, join a test call (Meet / Teams in a
   browser): the watcher detects it. If `pw-dump` fails, the detector must report "not available"
   and not crash - check that too.
5. **Recordings folder.** A new recording lands in `~/Documents/Tanara/recordings/` (the real folder,
   visible from the host). Setting another folder in Settings -> Folders to a place outside the
   permissions (for example `~/Videos/x`) must fail with a clear message, or go through the portal
   file dialog (note which).
6. **Key store.** Enter a Soniox test key in Settings. Restart. The key is still there. Check
   where it is stored: `secret-tool search --all service tanara` on the host (Secret Service), or the
   0600 file in `~/.var/app/eu.remedit.Tanara/config/tanara/` (fallback). Both are acceptable;
   note which one it was and that no key is in a world-readable file.
7. **Autostart (expected to fail today).** Settings -> watcher autostart on. The app writes
   `~/.var/app/eu.remedit.Tanara/config/autostart/tanara-watcher.desktop`: that is inside the sandbox,
   so the desktop does not read it. Real autostart needs either the Background portal
   (`org.freedesktop.portal.Background.RequestBackground`, `autostart: true`) or a host
   `~/.config/autostart` entry with `Exec=flatpak run --command=tanara-watcher eu.remedit.Tanara`.
   This needs a code change; note the result and leave it for a follow-up slice.
8. **ffmpeg codecs.** `flatpak run --command=ffmpeg eu.remedit.Tanara -hide_banner -encoders | grep -E "libopus|libmp3lame"`
   lists both. Record 10 s: the file in the library is Opus. Make a mixdown of a meeting with two
   tracks: an MP3 is produced. Import a `.m4a` and a `.wav` file (decoders).
9. **Speaker model.** `tanara-cli voice-models` shows CAM++ as available (bundled in `/app/bin/models`).
   Running a meeting through the speaker analysis does not fail with an ONNX error.
10. **Tray.** The watcher's tray icon appears (KDE, GNOME with the AppIndicator extension). Its menu
    opens the settings window.
11. **Cloud / network.** Soniox key test ("Test connection") succeeds. (Permission: `--share=network`.)
12. **Uninstall.** `flatpak uninstall --user eu.remedit.Tanara --delete-data` leaves
    `~/Documents/Tanara/` (the recordings) untouched.

## Result

Write the outcome (date, desktop, Flatpak version, each item) in the vault note for the Flatpak
experiment. Open follow-ups so far: autostart through the Background portal (item 7); building
FFmpeg/ONNX Runtime from source for Flathub.
