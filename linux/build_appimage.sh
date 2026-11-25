#!/bin/sh
# Packs the Linux build into an AppImage (one file to run; Steam Deck: add it to Steam as a non-Steam game):
#   linux/build_appimage.sh <built DisneyClassics> [version]
# Bundles SDL2, FreeType and their libraries (linuxdeploy; the graphics drivers and glibc stay the system's), the
# menus' fonts (DejaVu Sans from the build machine, Luckiest Guy) with their licences, and the readme. Output in
# dist-linux/. Needs linuxdeploy (fetched into .tools/ if missing) and the fonts-dejavu-core package.
set -e
EXE="$1"
VERSION="${2:-$(sed -n 's/.*kAppVersion = "\([^"]*\)".*/\1/p' src/app/app.h)}"
[ -x "$EXE" ] || { echo "usage: $0 <built DisneyClassics> [version]"; exit 1; }
ROOT=$(pwd)
APPDIR="$ROOT/AppDir"
rm -rf "$APPDIR" dist-linux
mkdir -p "$APPDIR/usr/bin/fonts" dist-linux .tools

cp "$EXE" "$APPDIR/usr/bin/DisneyClassics"
cp third_party/fonts/LuckiestGuy-Regular.ttf "$APPDIR/usr/bin/"
cp third_party/fonts/LICENSE.txt "$APPDIR/usr/bin/LuckiestGuy-LICENSE.txt"
for f in DejaVuSans.ttf DejaVuSans-Bold.ttf; do cp "/usr/share/fonts/truetype/dejavu/$f" "$APPDIR/usr/bin/fonts/"; done
cp /usr/share/doc/fonts-dejavu-core/copyright "$APPDIR/usr/bin/fonts/DejaVu-LICENSE.txt" 2>/dev/null || true
cp readme.txt "$APPDIR/usr/bin/readme.txt"
cp res/app_icon_preview.png .tools/mickeydonald.png

if [ ! -x .tools/linuxdeploy ]; then
    curl -fsSL -o .tools/linuxdeploy \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x .tools/linuxdeploy
fi
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
OUTPUT="Mickey-and-Donald-Collection-v$VERSION-x86_64.AppImage" \
    .tools/linuxdeploy --appdir "$APPDIR" -e "$APPDIR/usr/bin/DisneyClassics" \
    -d linux/mickeydonald.desktop -i .tools/mickeydonald.png --output appimage
mv Mickey-and-Donald-Collection-v*-x86_64.AppImage dist-linux/
ls -l dist-linux
