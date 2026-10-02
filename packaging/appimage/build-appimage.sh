#!/usr/bin/env bash
# Builds Pot_Player-<version>-x86_64.AppImage with linuxdeploy and
# linuxdeploy-plugin-qt. Run from the repository root:
#
#   VERSION=0.1.0 packaging/appimage/build-appimage.sh
#
# Needs the same build dependencies as a normal build, plus qmake6
# (so the Qt plugin can locate Qt) and, optionally, qt6-wayland.
set -euo pipefail

ARCH="${ARCH:-x86_64}"
VERSION="${VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}"
VERSION="${VERSION#v}"
ROOT="$(pwd)"
WORK="${WORK:-$ROOT/build-appimage}"
APPDIR="$WORK/AppDir"
TOOLS="$WORK/tools"

mkdir -p "$TOOLS"
fetch() {
    local url="$1" out="$TOOLS/$2"
    if [[ ! -x "$out" ]]; then
        curl -fL --retry 3 -o "$out" "$url"
        chmod +x "$out"
    fi
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" \
    linuxdeploy
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
    linuxdeploy-plugin-qt
export PATH="$TOOLS:$PATH"

cmake -S "$ROOT" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$WORK/build" --parallel
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$WORK/build"

# CI runners and containers usually have no FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1
# Tell the Qt plugin which Qt to bundle.
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
# Wayland support when the Qt Wayland plugins are installed.
if compgen -G "$("$QMAKE" -query QT_INSTALL_PLUGINS)/platforms/libqwayland-*.so" >/dev/null; then
    export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
    # Despite the name, this deploys the Wayland *client* plugins
    # (shell integration, decorations, EGL graphics integration).
    export EXTRA_QT_MODULES="waylandcompositor"
fi
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

cd "$WORK"
linuxdeploy \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/org.github.potlinux.desktop" \
    --icon-file "$ROOT/packaging/linux/org.github.potlinux.svg" \
    --plugin qt \
    --output appimage

mkdir -p "$ROOT/dist"
mv -f "$WORK"/Pot_Player-*.AppImage "$ROOT/dist/Pot_Player-${VERSION}-${ARCH}.AppImage"
echo "Created dist/Pot_Player-${VERSION}-${ARCH}.AppImage"
