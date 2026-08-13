#!/bin/bash
# Builds a distributable APK: the debug APK with libmain.so's asset bytes
# zeroed and the extraction manifest packaged in (see make_asset_holes.py).
# Usage: tools/dualscreen/package_release.sh <emerald_rom.gba> [out.apk]
set -e
cd "$(dirname "$0")/../.."
ROM="$1"
OUT="${2:-pokeemerald-dualscreen-release.apk}"
CXX_DIR=android/app/.cxx/Debug/*/armeabi-v7a
UNSTRIPPED=android/app/build/intermediates/cxx/Debug/*/obj/armeabi-v7a/libmain.so
APK=android/app/build/outputs/apk/debug/app-debug.apk
BUILD_TOOLS="$HOME/Android/sdk/build-tools/36.0.0"
WORK=$(mktemp -d)

[ -f "$ROM" ] || { echo "usage: $0 <emerald_rom.gba> [out.apk]"; exit 1; }

unzip -q "$APK" -d "$WORK"
python3 tools/dualscreen/make_asset_holes.py build \
    $UNSTRIPPED "$ROM" \
    "$WORK/lib/armeabi-v7a/libmain.so" "$WORK/lib/armeabi-v7a/libmain.so" \
    "$WORK/assets/asset_manifest.bin"

# Repack; the manifest must not be compressed oddly, plain deflate is fine.
rm -f "$WORK/META-INF"/*.SF "$WORK/META-INF"/*.MF "$WORK/META-INF"/*.RSA 2>/dev/null || true
(cd "$WORK" && zip -qr ../repacked.apk .)
"$BUILD_TOOLS/zipalign" -f 4 "$WORK/../repacked.apk" "$WORK/../aligned.apk"
"$BUILD_TOOLS/apksigner" sign --ks ~/.android/debug.keystore \
    --ks-pass pass:android --key-pass pass:android \
    --out "$OUT" "$WORK/../aligned.apk"
rm -rf "$WORK" "$WORK/../repacked.apk" "$WORK/../aligned.apk"
echo "release APK -> $OUT"
unzip -l "$OUT" | grep -E "libmain|manifest"
