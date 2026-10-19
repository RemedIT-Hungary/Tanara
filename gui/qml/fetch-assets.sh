#!/usr/bin/env bash
# Re-downloads the bundled external assets (Lucide icons, IBM Plex fonts) from their official
# sources. The downloaded files are under version control: run this only when adding an icon or
# changing a version:   gui/qml/fetch-assets.sh [more-icon-names...]
set -euo pipefail
cd "$(dirname "$0")"

LUCIDE_VERSION=0.460.0     # the same as in the design references (lucide-static, ISC)
PLEX_REF=master            # IBM/plex (SIL OFL 1.1)

ICONS=(file-text sparkles user-check search users settings fingerprint ellipsis undo-2 redo-2
  plus panel-left chevron-down check wand-sparkles play pause volume-2 mic monitor-speaker
  speaker audio-lines triangle-alert lock rotate-ccw list-tree list-filter copy folder-open
  grip-vertical trash-2 inbox radar arrow-right minus square x chevrons-left-right pencil
  chevron-right chevron-up chevron-left info circle-alert circle-check loader-circle refresh-cw
  user user-plus volume-x mic-off external-link clock sun moon circle-dot ellipsis-vertical
  file-audio file-video import plug key-round cloud monitor eye eye-off circle-x braces
  arrow-right-left merge headphones quote calendar phone book-open list-checks "$@")

mkdir -p icons fonts
for n in "${ICONS[@]}"; do
  [ -f "icons/$n.svg" ] || curl -sfL -o "icons/$n.svg" \
    "https://unpkg.com/lucide-static@${LUCIDE_VERSION}/icons/$n.svg" || echo "HIBA: $n" >&2
done
curl -sfL -o icons/LICENSE "https://unpkg.com/lucide-static@${LUCIDE_VERSION}/LICENSE"

# Csak a spec által használt súlyok: Sans 400/500/600/700, Mono 400/500/600.
for f in Sans-Regular Sans-Medium Sans-SemiBold Sans-Bold Mono-Regular Mono-Medium Mono-SemiBold; do
  fam=$(echo "${f%%-*}" | tr 'A-Z' 'a-z')
  [ -f "fonts/IBMPlex$f.ttf" ] || curl -sfL -o "fonts/IBMPlex$f.ttf" \
    "https://github.com/IBM/plex/raw/${PLEX_REF}/packages/plex-$fam/fonts/complete/ttf/IBMPlex$f.ttf" \
    || echo "HIBA: $f" >&2
done
curl -sfL -o fonts/OFL.txt "https://github.com/IBM/plex/raw/${PLEX_REF}/LICENSE.txt"
