#!/usr/bin/env bash
# Screenshot set for the QML UI, no visible window (offscreen + software renderer).
#   gui/qml/shoot.sh                 -> build/shots/*.png  (gallery + main window states, 2 themes)
#   gui/qml/shoot.sh Gallery         -> only the given page(s): <Type>[:WxH[:name=value,...]]
#   gui/qml/shoot.sh SummaryTab:900x600 'Main:1280x820:shellState="empty"'
#   SHOT_DELAY=4000 gui/qml/shoot.sh Main:1280x820:demoState=\"readingMap\"   -> wait 4 s before saving
# Environment: TANARA_BIN (default: build/gui/tanara), SHOT_DIR (build/shots), SHOT_SCALE (1),
#   SHOT_DELAY (ms; passed as tanara --delay, for long transitions / async sample data).
#   Default: the tanara default (300 ms), except TranscriptTab, whose demo states load the
#   fictional meeting asynchronously, so it gets 2500 ms.
# More recipes: see "Screenshot recipes (v3)" in gui/qml/README.md.
# Examples with demo states:
#   gui/qml/shoot.sh 'Main:1280x820:demoState="overviewTranscribing"' 'TracksTab:1004x640:demoState="default"' \
#     'ParticipantsDialog:1000x760:demoState="learnQuestion"' 'MapDock:1100x200:demoState="similarMarks"' \
#     'SettingsWindow:900x880:demoState="B08verify"' 'SettingsWindow:900x880:demoState="B08partial"' \
#     'PeopleWindow:960x1100:demoState="tagsFocus"' 'TranscriptTab:1100x760:demoState="newPersonTag"'
# The Gallery "Tracks and learning" section is at the bottom of the page: full page height is 6900.
set -euo pipefail
cd "$(dirname "$0")/../.."

BIN="${TANARA_BIN:-build/gui/tanara}"
OUT="${SHOT_DIR:-build/shots}"
SCALE="${SHOT_SCALE:-1}"
DELAY="${SHOT_DELAY:-}"
mkdir -p "$OUT"

if [ $# -eq 0 ]; then
  set -- "Gallery:1280x6900" "Gallery:1280x820:overlay=\"dialog\"" "Gallery:1280x820:overlay=\"menu\"" \
         "Main:1280x820" "Main:1280x820:shellState=\"empty\"" "Main:1280x820:shellState=\"noSelection\"" \
         "Main:1280x820:shellState=\"preTranscript\",taskRunning=true" \
         "SettingsWindow:900x800:demoState=\"B01\"" "SettingsWindow:900x780:demoState=\"B02\"" \
         "SettingsWindow:900x680:demoState=\"B03\"" "SettingsWindow:900x780:demoState=\"B04\"" \
         "SettingsWindow:900x880:demoState=\"B05\"" "SettingsWindow:900x760:demoState=\"cloudComingSoon\"" \
         "SettingsWindow:900x720:demoState=\"B07\""
fi

for spec in "$@"; do
  IFS=':' read -r page size props <<<"$spec"
  size="${size:-1280x820}"
  args=()
  delay="$DELAY"
  [ -z "$delay" ] && [ "$page" = "TranscriptTab" ] && delay=2500
  [ -n "$delay" ] && args+=(--delay "$delay")
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
