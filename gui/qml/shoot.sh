#!/usr/bin/env bash
# Képernyőkép-készlet a QML-felületről, látható ablak nélkül (offscreen + szoftveres renderer).
#   gui/qml/shoot.sh                 → build/shots/*.png  (galéria + a főablak állapotai, 2 témában)
#   gui/qml/shoot.sh Gallery         → csak az adott oldal(ak): <Típus>[:SZxM[:név=érték,...]]
#   gui/qml/shoot.sh SummaryTab:900x600 'Main:1280x820:shellState="empty"'
# Környezet: TANARA_BIN (alapértelmezés: build/gui/tanara), SHOT_DIR (build/shots), SHOT_SCALE (1).
set -euo pipefail
cd "$(dirname "$0")/../.."

BIN="${TANARA_BIN:-build/gui/tanara}"
OUT="${SHOT_DIR:-build/shots}"
SCALE="${SHOT_SCALE:-1}"
mkdir -p "$OUT"

if [ $# -eq 0 ]; then
  set -- "Gallery:1280x2900" "Gallery:1280x820:overlay=\"dialog\"" "Gallery:1280x820:overlay=\"menu\"" \
         "Main:1280x820" "Main:1280x820:shellState=\"empty\"" "Main:1280x820:shellState=\"noSelection\"" \
         "Main:1280x820:shellState=\"preTranscript\",taskRunning=true" \
         "SettingsWindow:900x800:demoState=\"B01\"" "SettingsWindow:900x780:demoState=\"B02\"" \
         "SettingsWindow:900x680:demoState=\"B03\"" "SettingsWindow:900x780:demoState=\"B04\"" \
         "SettingsWindow:900x880:demoState=\"B05\"" "SettingsWindow:900x760:demoState=\"B06\"" \
         "SettingsWindow:900x720:demoState=\"B07\""
fi

for spec in "$@"; do
  IFS=':' read -r page size props <<<"$spec"
  size="${size:-1280x820}"
  args=()
  suffix=""
  if [ -n "${props:-}" ]; then
    IFS=',' read -ra kvs <<<"$props"
    for kv in "${kvs[@]}"; do
      args+=(--qml-prop "$kv")
      suffix+="-$(echo "${kv#*=}" | tr -cd 'A-Za-z0-9')"
    done
  fi
  for theme in light dark; do
    "$BIN" --qml-shot "$OUT/$page$suffix-$theme.png" --qml-page "$page" --theme "$theme" \
           --size "$size" --scale "$SCALE" "${args[@]}" 2>&1 | grep -v '\[INF\] tanara.app' || true
  done
done
