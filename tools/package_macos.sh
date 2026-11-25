#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
ninja -C "${1:-$root/build/macos-arm64}" -j 2 CastawayRecomp_app
app="$root/out/darwin/CastawayRecomp.app"
plutil -lint "$app/Contents/Info.plist"
codesign --verify --deep --strict "$app"
printf '%s\n' "$app"
