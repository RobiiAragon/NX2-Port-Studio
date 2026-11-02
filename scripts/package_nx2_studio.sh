#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PACKAGE_DIR="$REPO_DIR/macOS/NXPortStudio"
OUTPUT="${1:-$REPO_DIR/.nx2-build/NX2 Port Studio.app}"
case "$OUTPUT" in *.app) ;; *) printf '%s\n' 'Output must end in .app' >&2; exit 1 ;; esac
if [ -e "$OUTPUT" ] || [ -L "$OUTPUT" ]; then
    printf '%s\n' 'Output already exists. Choose a new path.' >&2
    exit 1
fi
mkdir -p "$(dirname "$OUTPUT")"
OUTPUT="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
swift build --package-path "$PACKAGE_DIR" --configuration release --product NX2PortStudio --arch arm64
BIN_DIR="$(swift build --package-path "$PACKAGE_DIR" --configuration release --show-bin-path --arch arm64)"
STAGING="$(mktemp -d "$(dirname "$OUTPUT")/.nx2-package.XXXXXX")"
trap 'rm -rf "$STAGING"' EXIT
APP="$STAGING/NX2 Port Studio.app"
CONTENTS="$APP/Contents"
ENGINE="$CONTENTS/Resources/nx2-engine"
mkdir -p "$CONTENTS/MacOS" "$ENGINE/scripts" "$ENGINE/native" "$ENGINE/third_party/zbic-zstd"
cp "$BIN_DIR/NX2PortStudio" "$CONTENTS/MacOS/"
/usr/bin/strip -S "$CONTENTS/MacOS/NX2PortStudio"
cp -R "$BIN_DIR/NXPortStudio_NX2Studio.bundle" "$CONTENTS/Resources/"
ln -s ../Resources/NXPortStudio_NX2Studio.bundle "$CONTENTS/MacOS/NXPortStudio_NX2Studio.bundle"
/usr/bin/rsync -a --exclude='__pycache__/' --exclude='*.pyc' --exclude='tests/' "$REPO_DIR/nx2" "$ENGINE/"
cp "$REPO_DIR/scripts/nx2.py" "$ENGINE/scripts/"
cp "$REPO_DIR/upstreams.lock.json" "$REPO_DIR/requirements-nx2.txt" "$REPO_DIR/requirements-nx2-runtime.txt" "$REPO_DIR/LICENSE" "$REPO_DIR/LICENSE.nxnative" "$ENGINE/"
make -C "$REPO_DIR/native"
cp "$REPO_DIR/.nx2-build/native/libnx2_zbic.dylib" "$ENGINE/native/"
cp "$REPO_DIR/third_party/zbic-zstd/LICENSE.BSD" "$REPO_DIR/third_party/zbic-zstd/COPYING.GPL2" "$REPO_DIR/third_party/zbic-zstd/README.md" "$ENGINE/third_party/zbic-zstd/"
/usr/bin/codesign --force --sign - "$ENGINE/native/libnx2_zbic.dylib"
# The app contains NX2's tools and native-export descriptor. The separately
# prepared pinned source engine is never copied into this distribution.
xcrun swift "$PACKAGE_DIR/scripts/GenerateIcon.swift" "$STAGING/AppIcon.iconset"
/usr/bin/iconutil -c icns "$STAGING/AppIcon.iconset" -o "$CONTENTS/Resources/AppIcon.icns"
python3 - "$CONTENTS/Info.plist" <<'PY'
import plistlib, sys
with open(sys.argv[1], 'wb') as target:
    plistlib.dump({
        'CFBundleName': 'NX2 Port Studio', 'CFBundleDisplayName': 'NX2 Port Studio',
        'CFBundleIdentifier': 'dev.robii.nx2portstudio', 'CFBundleExecutable': 'NX2PortStudio',
        'CFBundlePackageType': 'APPL', 'CFBundleShortVersionString': '0.3.0', 'CFBundleVersion': '4',
        'CFBundleDevelopmentRegion': 'es', 'CFBundleIconFile': 'AppIcon',
        'LSMinimumSystemVersion': '14.0', 'LSArchitecturePriority': ['arm64'],
        'LSApplicationCategoryType': 'public.app-category.developer-tools',
        'NSHighResolutionCapable': True,
    }, target)
PY
/usr/bin/codesign --force --sign - "$APP"
/usr/bin/codesign --verify --strict "$APP"
mv "$APP" "$OUTPUT"
printf 'Created %s\n' "$OUTPUT"
