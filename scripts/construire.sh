#!/bin/zsh
# Construit groovebox, lance les essais automatiques, puis installe l'application.
set -euo pipefail

racine="${0:A:h:h}"
outils="$HOME/Code/.outils-groovebox/bin"
export PATH="$outils:$PATH"

cmake -S "$racine" -B "$racine/construction" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$racine/construction"

"$racine/construction/groovebox_essais_artefacts/Release/groovebox_essais"

if [[ "${1:-}" != "--sans-installer" ]]; then
  destination="/Applications"
  [[ -w "$destination" ]] || destination="$HOME/Applications"
  mkdir -p "$destination"
  rm -rf "$destination/groovebox.app"
  cp -R "$racine/construction/groovebox_artefacts/Release/groovebox.app" "$destination/"
  echo "Installé dans $destination/groovebox.app"
fi
