#!/bin/bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SOURCE="$ROOT/CastawayRecomp"
WORK="$HOME/Library/Application Support/CastawayRecomp/Build-$(date +%Y%m%d-%H%M%S)"
APP_OUT="$HOME/Applications"
LOG="$HOME/Library/Logs/CastawayRecomp-Installer.log"
mkdir -p "$WORK" "$APP_OUT" "$(dirname "$LOG")"
exec > >(tee "$LOG") 2>&1
clear
printf '\n  CASTAWAY RECOMP\n  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
printf '  Instalador para Apple Silicon\n\n'
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
  echo "Este instalador requiere un Mac con Apple Silicon."
  read -r -p "Pulsa Enter para cerrar."
  exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Instala Xcode Command Line Tools y vuelve a abrir este instalador."
  xcode-select --install || true
  read -r -p "Pulsa Enter para cerrar."
  exit 1
fi
if ! command -v brew >/dev/null; then
  echo "Instala Homebrew desde https://brew.sh y vuelve a abrir este instalador."
  read -r -p "Pulsa Enter para cerrar."
  exit 1
fi
echo "Selecciona tu imagen legal del juego (.iso, .rvz o .wbfs). El extractor nativo admite los tres formatos."
IMAGE="$(osascript -e 'POSIX path of (choose file with prompt "Elige tu propia imagen de The Sims 2: Castaway para Wii")')"
[[ -f "$IMAGE" ]] || { echo "No se seleccionó una imagen."; exit 1; }
brew install cmake ninja python@3.12 sdl3
cp -R "$SOURCE" "$WORK/source"
cd "$WORK/source"
mkdir -p game/extracted
cmake -S . -B build/user -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
ninja -C build/user -j 2 bkr_install
build/user/out/bkr_install "$IMAGE" game/extracted/DATA
python3 tools/build_generated.py
cmake -S . -B build/user -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
ninja -C build/user -j 2 CastawayRecomp_app
mkdir -p "$APP_OUT"
ditto out/darwin/CastawayRecomp.app "$APP_OUT/The Sims 2 Castaway.app"
codesign --verify --deep --strict "$APP_OUT/The Sims 2 Castaway.app"
open "$APP_OUT/The Sims 2 Castaway.app"
echo "Instalación completa. App guardada en Aplicaciones."
read -r -p "Pulsa Enter para cerrar."
