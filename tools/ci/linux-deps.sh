#!/usr/bin/env bash
# Install the build and test dependencies of Tanara on Fedora (the CI container is fedora:44).
# Usage: tools/ci/linux-deps.sh      (run as root, as in the container; uses sudo otherwise)
set -euo pipefail

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

PACKAGES=(
    # toolchain
    gcc-c++ cmake ninja-build ccache git
    # Qt 6 (qt6-qttools-devel: LinguistTools; qt6-linguist: lrelease/lupdate)
    qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel
    qt6-qtmultimedia-devel qt6-qtsvg-devel qt6-qttools-devel qt6-linguist
    # voice-ID
    onnxruntime-devel
    # OS keychain for the API keys (optional; without it: 0600 file only)
    qtkeychain-qt6-devel
    # run-time tools used by tests (ffmpeg/ffprobe, node for the mock gateway)
    ffmpeg-free nodejs24 python3
    # needed by actions/cache and actions/checkout inside a bare container
    tar zstd
)

$SUDO dnf install -y --setopt=install_weak_deps=False "${PACKAGES[@]}"
