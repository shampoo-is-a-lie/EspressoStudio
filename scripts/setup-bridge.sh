#!/usr/bin/env bash
# Set up the Windows-plugin bridge (Wine + yabridge) inside the espresso-dev
# container. Windows VST3s installed into the Wine prefix's standard VST3 dir
# then appear in EspressoStudio's plugin browser after a rescan.
set -e

YABRIDGE_VERSION="${YABRIDGE_VERSION:-5.1.1}"

toolbox run -c espresso-dev bash -c '
  set -e

  command -v wine >/dev/null || sudo dnf install -y wine

  if [ ! -x "$HOME/.local/share/yabridge/yabridgectl" ]; then
    echo "downloading yabridge '"$YABRIDGE_VERSION"'…"
    curl -sL -o /tmp/yabridge.tar.gz \
      "https://github.com/robbert-vdh/yabridge/releases/download/'"$YABRIDGE_VERSION"'/yabridge-'"$YABRIDGE_VERSION"'.tar.gz"
    mkdir -p "$HOME/.local/share"
    tar -xzf /tmp/yabridge.tar.gz -C "$HOME/.local/share"
  fi

  export PATH="$PATH:$HOME/.local/share/yabridge"

  echo "initialising Wine prefix (first run takes a minute)…"
  WINEDEBUG=-all wineboot -u 2>/dev/null || true

  mkdir -p "$HOME/.wine/drive_c/Program Files/Common Files/VST3"
  yabridgectl add "$HOME/.wine/drive_c/Program Files/Common Files/VST3" 2>/dev/null || true
  yabridgectl sync

  echo
  echo "Bridge ready."
  echo "  1. Install Windows VST3s into:  ~/.wine/drive_c/Program Files/Common Files/VST3"
  echo "     (run installers with:  toolbox run -c espresso-dev wine <installer.exe>)"
  echo "  2. Re-run this script (or: yabridgectl sync)"
  echo "  3. Hit scan in EspressoStudio'"'"'s FX panel"
'
