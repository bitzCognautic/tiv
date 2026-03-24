#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
VERSION="$(cat "$ROOT/VERSION")"
APPDIR="$ROOT/dist/AppDir"

if ! command -v appimagetool >/dev/null 2>&1; then
  echo "Missing: appimagetool" >&2
  echo "Install it, then re-run this script." >&2
  exit 2
fi

cd "$ROOT"
make clean
make

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/scalable/apps"

install -m 0755 "$ROOT/tiv" "$APPDIR/usr/bin/tiv"
install -m 0644 "$ROOT/packaging/tiv.desktop" "$APPDIR/usr/share/applications/tiv.desktop"
install -m 0644 "$ROOT/packaging/tiv.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/tiv.svg"
install -m 0755 "$ROOT/packaging/appimage/AppRun" "$APPDIR/AppRun"

# AppImage tooling expects these at the AppDir root too.
ln -sf "usr/share/applications/tiv.desktop" "$APPDIR/tiv.desktop"
ln -sf "usr/share/icons/hicolor/scalable/apps/tiv.svg" "$APPDIR/tiv.svg"

OUTDIR="$ROOT/dist"
mkdir -p "$OUTDIR"

ARCH="$(uname -m)"
appimagetool "$APPDIR" "$OUTDIR/tiv-$VERSION-$ARCH.AppImage"

echo "Built: $OUTDIR/tiv-$VERSION-$ARCH.AppImage"
echo "Note: this AppImage does not bundle GTK4; your system must provide GTK4."
