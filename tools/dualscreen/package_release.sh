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

# Repack. resources.arsc must be STORED (uncompressed) for API 30+.
rm -f "$WORK/META-INF"/*.SF "$WORK/META-INF"/*.MF "$WORK/META-INF"/*.RSA 2>/dev/null || true
(cd "$WORK" && zip -qr ../repacked.apk . -x resources.arsc \
            && zip -q -0 ../repacked.apk resources.arsc)
"$BUILD_TOOLS/zipalign" -f 4 "$WORK/../repacked.apk" "$WORK/../aligned.apk"

# Sign with the release key when it is configured, else the debug key. Android
# refuses an update signed by a different key than the installed APK, so every
# published release has to carry the same signature: set these and keep the
# keystore backed up, because losing it strands everyone on their install.
#   DUALSCREEN_KEYSTORE       path to the keystore
#   DUALSCREEN_KEYSTORE_PASS  store password (key password defaults to it)
#   DUALSCREEN_KEY_ALIAS      key alias (default: dualscreen)
if [ -n "$DUALSCREEN_KEYSTORE" ]; then
    [ -f "$DUALSCREEN_KEYSTORE" ] || { echo "keystore not found: $DUALSCREEN_KEYSTORE"; exit 1; }
    echo "signing with release key: $DUALSCREEN_KEYSTORE"
    "$BUILD_TOOLS/apksigner" sign --ks "$DUALSCREEN_KEYSTORE" \
        --ks-pass "pass:$DUALSCREEN_KEYSTORE_PASS" \
        --key-pass "pass:${DUALSCREEN_KEY_PASS:-$DUALSCREEN_KEYSTORE_PASS}" \
        --ks-key-alias "${DUALSCREEN_KEY_ALIAS:-dualscreen}" \
        --out "$OUT" "$WORK/../aligned.apk"
else
    echo "warning: DUALSCREEN_KEYSTORE unset, signing with the debug key"
    "$BUILD_TOOLS/apksigner" sign --ks ~/.android/debug.keystore \
        --ks-pass pass:android --key-pass pass:android \
        --out "$OUT" "$WORK/../aligned.apk"
fi
rm -rf "$WORK" "$WORK/../repacked.apk" "$WORK/../aligned.apk"
echo "release APK -> $OUT"
unzip -l "$OUT" | grep -E "libmain|manifest"
