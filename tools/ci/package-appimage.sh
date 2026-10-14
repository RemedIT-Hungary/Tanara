#!/usr/bin/env bash
# Build the Tanara AppImage (x86_64). Meant for ubuntu-22.04 (the oldest glibc that the CI offers,
# so the AppImage runs on older distributions too) with Qt 6.11.x from aqtinstall
# (jurplel/install-qt-action, arch linux_gcc_64). See docs/BUILD.md, chapter "Packaging".
#
# Usage:
#   tools/ci/package-appimage.sh --install-deps    install the apt packages (uses sudo unless root)
#   QT_ROOT_DIR=/path/to/6.11.1/gcc_64 tools/ci/package-appimage.sh [tag]
#
#   tag            optional release tag (v0.5.11); the script fails if it differs from the CMake version
# Environment:
#   QT_ROOT_DIR    Qt install (set by install-qt-action)             required
#   WORK           scratch folder (default: <repo>/build-appimage)
#   OUT_DIR        where Tanara-<version>-x86_64.AppImage goes (default: $WORK/out)
#   JOBS           build jobs (default: nproc)
#   RUN_TESTS=1    run ctest before packaging (CI already did it on the commit; off by default)
#
# What goes into the AppImage (all downloads are pinned and SHA256-checked, see DOWNLOADS below):
#   usr/bin/{tanara,tanara-cli,tanara-watcher}   Tanara (AppRun starts one of the three, see packaging/linux/AppRun)
#   usr/bin/{ffmpeg,ffprobe}                     BtbN LGPL static build (the MediaToolLocator finds it in $APPDIR/usr/bin)
#   usr/bin/models/campplus_sv_zh_en_16k.onnx    CAM++ speaker model (Apache-2.0)
#   usr/lib                                      Qt 6.11 + QML imports + plugins, ONNX Runtime 1.20.1, QtKeychain, libsecret ...
#   usr/share/licenses/tanara                    LICENSE, THIRD_PARTY_NOTICES.md, licenses/, copyright files of the system libraries
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ "${1:-}" = "--install-deps" ]; then
    SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    $SUDO apt-get update
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ninja-build git curl ca-certificates xz-utils file patchelf \
        python3 desktop-file-utils \
        libgl1-mesa-dev libglu1-mesa-dev libegl1 libopengl0 libxkbcommon-dev libxkbcommon-x11-0 \
        libfontconfig1 libfreetype6 libdbus-1-3 libglib2.0-0 libsecret-1-dev \
        libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
        libxcb-render-util0 libxcb-shape0 libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 \
        libxcb-sync1 libxcb-util1 libx11-xcb1 libxrender1 libpulse0 libasound2 \
        libwayland-client0 libwayland-cursor0 libwayland-egl1 \
        libxrandr2 libxext6 libxi6 libxcursor1 libxfixes3 libxtst6 libxcomposite1 libxdamage1 libxinerama1
    exit 0
fi

: "${QT_ROOT_DIR:?QT_ROOT_DIR is not set (the Qt 6.11 linux_gcc_64 folder)}"
[ -x "$QT_ROOT_DIR/bin/qmake" ] || { echo "no qmake in $QT_ROOT_DIR/bin" >&2; exit 2; }

VERSION="$("$ROOT/tools/ci/package-version.sh" ${1:+"$1"})"
WORK="${WORK:-$ROOT/build-appimage}"
OUT_DIR="${OUT_DIR:-$WORK/out}"
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$WORK" "$OUT_DIR"
WORK="$(cd "$WORK" && pwd)"; OUT_DIR="$(cd "$OUT_DIR" && pwd)"
echo "== Tanara $VERSION AppImage, work folder $WORK"

# ---------------------------------------------------------------------------------------------
# DOWNLOADS (name|url|sha256|file). Keep ffmpeg/onnxruntime/model in step with
# tools/ci/windows-deps.ps1, tools/ci/package-windows.ps1 and THIRD_PARTY_NOTICES.md.
# ---------------------------------------------------------------------------------------------
FFMPEG_TAG="autobuild-2026-09-30-13-08"
FFMPEG_DIR="ffmpeg-n9.0.2-17-g2a571b6068-linux64-lgpl-9.0"
DOWNLOADS=(
  "ffmpeg|https://github.com/BtbN/FFmpeg-Builds/releases/download/$FFMPEG_TAG/$FFMPEG_DIR.tar.xz|2d41cbea0ca1a15029b638330740f78d6f5aeb062355bf425433fc938705350a|ffmpeg.tar.xz"
  "onnxruntime|https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-x64-1.20.1.tgz|67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105|onnxruntime.tgz"
  "campplus|https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx|aa3cfc16963a10586a9393f5035d6d6b57e98d358b347f80c2a30bf4f00ceba2|campplus_sv_zh_en_16k.onnx"
  "qtkeychain|https://github.com/frankosterfeld/qtkeychain/archive/refs/tags/0.16.0.tar.gz|3be26ec4ae30eecf0c2ff7572ba83799791b157c76e15a05ef35f23dc25e4054|qtkeychain.tar.gz"
  "linuxdeploy|https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage|c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d|linuxdeploy-x86_64.AppImage"
  "linuxdeploy-plugin-qt|https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage|15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724|linuxdeploy-plugin-qt-x86_64.AppImage"
  "cmake|https://github.com/Kitware/CMake/releases/download/v3.31.9/cmake-3.31.9-linux-x86_64.tar.gz|312d78e0b7c5c9b4a97a87a46c3e525aa84895fe8135c238c4616bba73dc9518|cmake.tar.gz"
  "appimagetool|https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage|ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0|appimagetool-x86_64.AppImage"
)

DL="$WORK/dl"
mkdir -p "$DL"
for entry in "${DOWNLOADS[@]}"; do
    IFS='|' read -r name url sha file <<<"$entry"
    if [ -f "$DL/$file" ] && echo "$sha  $DL/$file" | sha256sum -c --status -; then
        echo "-- $name: cached"; continue
    fi
    echo "-- $name: downloading"
    curl -fL --retry 3 --retry-delay 5 -sS -o "$DL/$file" "$url"
    echo "$sha  $DL/$file" | sha256sum -c - || { echo "$name: checksum mismatch" >&2; exit 1; }
done
chmod +x "$DL"/*.AppImage

# Unpack into fresh folders (never run anything with the download folder as cwd).
DEPS="$WORK/deps"
rm -rf "$DEPS"; mkdir -p "$DEPS/ffmpeg" "$DEPS/onnxruntime" "$DEPS/qtkeychain-src" "$DEPS/cmake"
tar -xzf "$DL/cmake.tar.gz" -C "$DEPS/cmake" --strip-components=1
# Ubuntu 22.04 has CMake 3.22; Qt 6.11 needs a newer one ("Qt6::QmlIntegration not found" otherwise).
export PATH="$DEPS/cmake/bin:$PATH"
tar -xJf "$DL/ffmpeg.tar.xz" -C "$DEPS/ffmpeg"
tar -xzf "$DL/onnxruntime.tgz" -C "$DEPS/onnxruntime"
tar -xzf "$DL/qtkeychain.tar.gz" -C "$DEPS/qtkeychain-src" --strip-components=1
FF="$DEPS/ffmpeg/$FFMPEG_DIR"
ORT="$DEPS/onnxruntime/onnxruntime-linux-x64-1.20.1"
# The release's CMake config points at lib64/ and include/onnxruntime/, but the archive has lib/
# and the headers directly in include/.
ln -sfn lib "$ORT/lib64"
mkdir -p "$ORT/include/onnxruntime"
cp "$ORT"/include/*.h "$ORT/include/onnxruntime/"

# The FFmpeg build must be LGPL: no --enable-gpl, no --enable-nonfree; libopus and libmp3lame present.
ffconf="$("$FF/bin/ffmpeg" -hide_banner -version | grep '^configuration:')"
case "$ffconf" in *--enable-gpl*|*--enable-nonfree*) echo "FFmpeg build is not LGPL-only" >&2; exit 1;; esac
ffenc="$("$FF/bin/ffmpeg" -hide_banner -encoders 2>/dev/null)"   # not piped into grep -q: SIGPIPE + pipefail
case "$ffenc" in *libopus*) ;; *) echo "ffmpeg lacks libopus" >&2; exit 1;; esac
case "$ffenc" in *libmp3lame*) ;; *) echo "ffmpeg lacks libmp3lame" >&2; exit 1;; esac

# ---------------------------------------------------------------------------------------------
# QtKeychain 0.16.0 (BSD-3-Clause), shared, against the same Qt. Ubuntu 22.04 has no Qt 6 package.
# ---------------------------------------------------------------------------------------------
QTKC="$WORK/qtkeychain"
cmake -S "$DEPS/qtkeychain-src" -B "$WORK/qtkeychain-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_WITH_QT6=ON -DBUILD_TRANSLATIONS=OFF -DBUILD_TEST_APPLICATION=OFF \
    "-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR" "-DCMAKE_INSTALL_PREFIX=$QTKC" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$WORK/qtkeychain-build" -j"$JOBS"
cmake --install "$WORK/qtkeychain-build"

# ---------------------------------------------------------------------------------------------
# Tanara
# ---------------------------------------------------------------------------------------------
BUILD="$WORK/build"
export LD_LIBRARY_PATH="$ORT/lib:$QTKC/lib:$QT_ROOT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR;$QTKC" \
    "-Donnxruntime_DIR=$ORT/lib/cmake/onnxruntime" \
    -DTANARA_BUILD_VOICEID=ON -DTANARA_USE_KEYCHAIN=ON
cmake --build "$BUILD" -j"$JOBS"
if [ "${RUN_TESTS:-0}" = "1" ]; then
    TANARA_HOME="$(mktemp -d)" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        ctest --test-dir "$BUILD" -j"$JOBS" --output-on-failure
fi
lddout="$(ldd "$BUILD/cli/tanara-cli")"
case "$lddout" in
    *libqt6keychain*) ;;
    *) echo "tanara-cli does not link QtKeychain (the API keys would fall back to the file backend)" >&2; exit 1;;
esac

# ---------------------------------------------------------------------------------------------
# AppDir
# ---------------------------------------------------------------------------------------------
APPDIR_="$WORK/AppDir"
rm -rf "$APPDIR_"
install -d "$APPDIR_/usr/bin/models" "$APPDIR_/usr/share/licenses/tanara/licenses" \
           "$APPDIR_/usr/share/metainfo" "$APPDIR_/usr/share/applications"
install -m755 "$BUILD/gui/tanara" "$BUILD/cli/tanara-cli" "$BUILD/watcher/tanara-watcher" "$APPDIR_/usr/bin/"
install -m755 "$FF/bin/ffmpeg" "$FF/bin/ffprobe" "$APPDIR_/usr/bin/"
install -m644 "$DL/campplus_sv_zh_en_16k.onnx" "$APPDIR_/usr/bin/models/"
install -m644 "$ROOT/packaging/linux/eu.remedit.Tanara.metainfo.xml" "$APPDIR_/usr/share/metainfo/"

LIC="$APPDIR_/usr/share/licenses/tanara"
install -m644 "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$LIC/"
install -m644 "$ROOT"/packaging/licenses/* "$LIC/licenses/"
install -m644 "$FF/LICENSE.txt" "$LIC/licenses/FFmpeg-LICENSE.txt"
install -m644 "$ORT/LICENSE" "$LIC/licenses/onnxruntime-LICENSE.txt"
install -m644 "$ORT/ThirdPartyNotices.txt" "$LIC/licenses/onnxruntime-ThirdPartyNotices.txt"

# ---------------------------------------------------------------------------------------------
# linuxdeploy: copy the libraries, QML imports and Qt plugins into the AppDir
# ---------------------------------------------------------------------------------------------
TOOLS="$DL"                                  # linuxdeploy finds the plugin next to itself or on PATH
export PATH="$TOOLS:$PATH"
ln -sf "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage" "$TOOLS/linuxdeploy-plugin-qt"
export APPIMAGE_EXTRACT_AND_RUN=1            # no FUSE in containers / CI
export NO_STRIP=1                            # the bundled strip is too old for some libraries
# linuxdeploy-plugin-qt deploys EVERY Qt SQL driver and fails on the ones whose client library is
# missing (Firebird, PostgreSQL, ...). Tanara needs only SQLite, so the plugin gets a qmake wrapper
# that points QT_INSTALL_PLUGINS at a folder of symlinks without the other SQL drivers.
PLUGINS_TRIMMED="$WORK/qt-plugins-trimmed"
rm -rf "$PLUGINS_TRIMMED"; mkdir -p "$PLUGINS_TRIMMED/sqldrivers"
for d in "$QT_ROOT_DIR"/plugins/*/; do
    n="$(basename "$d")"
    [ "$n" = "sqldrivers" ] || ln -s "$d" "$PLUGINS_TRIMMED/$n"
done
ln -s "$QT_ROOT_DIR/plugins/sqldrivers/libqsqlite.so" "$PLUGINS_TRIMMED/sqldrivers/libqsqlite.so"
cat > "$WORK/qmake-wrapper" <<WRAP
#!/bin/sh
real="$QT_ROOT_DIR/bin/qmake"
if [ "\$1" = "-query" ] && [ -z "\$2" ]; then
    "\$real" -query | sed 's#^QT_INSTALL_PLUGINS:.*#QT_INSTALL_PLUGINS:$PLUGINS_TRIMMED#'
elif [ "\$1" = "-query" ] && [ "\$2" = "QT_INSTALL_PLUGINS" ]; then
    echo "$PLUGINS_TRIMMED"
else
    exec "\$real" "\$@"
fi
WRAP
chmod +x "$WORK/qmake-wrapper"
export QMAKE="$WORK/qmake-wrapper"
export QML_SOURCES_PATHS="$ROOT/gui/qml"
# qoffscreen: "tanara --qml-shot" and the tests run offscreen; qwayland: native Wayland sessions.
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqwayland.so"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

RUN="$WORK/linuxdeploy-run"                  # fresh cwd, not the download folder
rm -rf "$RUN"; mkdir -p "$RUN"; cd "$RUN"
"$DL/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR_" \
    --executable "$APPDIR_/usr/bin/tanara" \
    --executable "$APPDIR_/usr/bin/tanara-cli" \
    --executable "$APPDIR_/usr/bin/tanara-watcher" \
    --desktop-file "$ROOT/packaging/linux/eu.remedit.Tanara.desktop" \
    --icon-file "$ROOT/packaging/linux/eu.remedit.Tanara.svg" \
    --custom-apprun "$ROOT/packaging/linux/AppRun" \
    --plugin qt

# linuxdeploy-plugin-qt does not know the single "libqwayland.so" platform plugin of Qt 6.11 and so
# skips the Wayland client's shell / graphics / decoration plugins; without them the Wayland platform
# plugin fails to load. They are plain copies: their RUNPATH ($ORIGIN/../../lib) is right in usr/plugins/<name>.
for d in wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client; do
    [ -d "$QT_ROOT_DIR/plugins/$d" ] && cp -r "$QT_ROOT_DIR/plugins/$d" "$APPDIR_/usr/plugins/"
done

# Fail early if the pieces the app needs at run time are missing.
for f in usr/bin/tanara usr/bin/tanara-cli usr/bin/tanara-watcher usr/bin/ffmpeg usr/bin/ffprobe \
         usr/bin/models/campplus_sv_zh_en_16k.onnx AppRun eu.remedit.Tanara.desktop; do
    [ -e "$APPDIR_/$f" ] || { echo "AppDir lacks $f" >&2; exit 1; }
done
ls "$APPDIR_"/usr/lib/libonnxruntime.so* >/dev/null
ls "$APPDIR_"/usr/lib/libqt6keychain.so* >/dev/null
ls "$APPDIR_"/usr/plugins/platforms/libqxcb.so "$APPDIR_"/usr/plugins/platforms/libqoffscreen.so >/dev/null

# Copyright files of the Ubuntu packages whose libraries were bundled (licence compliance).
if command -v dpkg >/dev/null 2>&1; then
    SYS="$LIC/system-libs"; mkdir -p "$SYS"
    : > "$SYS/PACKAGES.txt"
    for lib in "$APPDIR_"/usr/lib/*.so*; do
        base="$(basename "$lib")"
        pkg="$(dpkg -S "/lib/x86_64-linux-gnu/$base" "/usr/lib/x86_64-linux-gnu/$base" 2>/dev/null | head -n1 | cut -d: -f1 || true)"
        [ -n "$pkg" ] && echo "$pkg" >> "$SYS/PACKAGES.txt"
    done
    sort -u -o "$SYS/PACKAGES.txt" "$SYS/PACKAGES.txt"
    while read -r pkg; do
        [ -r "/usr/share/doc/$pkg/copyright" ] && cp "/usr/share/doc/$pkg/copyright" "$SYS/$pkg.copyright"
    done < "$SYS/PACKAGES.txt"
fi

# ---------------------------------------------------------------------------------------------
# Squash the AppDir into the AppImage
# ---------------------------------------------------------------------------------------------
OUTFILE="$OUT_DIR/Tanara-$VERSION-x86_64.AppImage"
rm -f "$OUTFILE"
ARCH=x86_64 "$DL/appimagetool-x86_64.AppImage" --comp zstd \
    --mksquashfs-opt -Xcompression-level --mksquashfs-opt 19 \
    "$APPDIR_" "$OUTFILE"
chmod +x "$OUTFILE"
( cd "$OUT_DIR" && sha256sum "$(basename "$OUTFILE")" > "$(basename "$OUTFILE").sha256" )
ls -l "$OUTFILE"
echo "== done: $OUTFILE"
