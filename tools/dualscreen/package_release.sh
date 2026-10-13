#!/bin/bash
# Builds a distributable APK: the debug APK with libmain.so's asset bytes
# zeroed and the extraction manifest packaged in (see make_asset_holes.py).
# Usage: tools/dualscreen/package_release.sh <emerald_rom.gba> [out.apk]
set -euo pipefail
cd "$(dirname "$0")/../.."
ROM="${1:-}"
OUT="${2:-pokeemerald-dualscreen-release.apk}"
APK=android/app/build/outputs/apk/debug/app-debug.apk
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [ -z "$SDK" ]; then
    if [ -d "$HOME/Library/Android/sdk" ]; then
        SDK="$HOME/Library/Android/sdk"
    else
        SDK="$HOME/Android/Sdk"
    fi
fi
# Override BUILD_TOOLS for a specific installed version. No symlinks required.
if [ -z "${BUILD_TOOLS:-}" ]; then
    BUILD_TOOLS=$(python3 - "$SDK" <<'PYSDK'
from pathlib import Path
import sys
versions = [p for p in (Path(sys.argv[1]) / "build-tools").glob("*")
            if all(part.isdigit() for part in p.name.split("."))
            and (p / "zipalign").is_file() and (p / "apksigner").is_file()]
if not versions:
    sys.exit("Install Android SDK Build Tools or set BUILD_TOOLS to their directory.")
print(max(versions, key=lambda p: tuple(map(int, p.name.split(".")))))
PYSDK
    )
fi
[ -f "$ROM" ] || { echo "usage: $0 <emerald_rom.gba> [out.apk]" >&2; exit 1; }
[ -f "$APK" ] || { echo "Run tools/dualscreen/build_android.sh first." >&2; exit 1; }
shopt -s nullglob
LIBRARIES=(android/app/build/intermediates/cxx/Debug/*/obj/armeabi-v7a/libmain.so)
[ "${#LIBRARIES[@]}" -eq 1 ] || {
    echo "Expected one unstripped libmain.so; clean and rebuild the Android app." >&2
    exit 1
}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir "$WORK/apk"

unzip -q "$APK" -d "$WORK/apk"
python3 tools/dualscreen/make_asset_holes.py build \
    "${LIBRARIES[0]}" "$ROM" \
    "$WORK/apk/lib/armeabi-v7a/libmain.so" "$WORK/apk/lib/armeabi-v7a/libmain.so" \
    "$WORK/apk/assets/asset_manifest.bin"

# Repack. resources.arsc must be STORED (uncompressed) for API 30+.
rm -f "$WORK/apk/META-INF"/*.SF "$WORK/apk/META-INF"/*.MF "$WORK/apk/META-INF"/*.RSA 2>/dev/null || true
(cd "$WORK/apk" && zip -qr ../repacked.apk . -x resources.arsc \
            && zip -q -0 ../repacked.apk resources.arsc)
"$BUILD_TOOLS/zipalign" -f 4 "$WORK/repacked.apk" "$WORK/aligned.apk"

# Sign with the release key when it is configured, else the debug key. Android
# refuses an update signed by a different key than the installed APK, so every
# published release has to carry the same signature: set these and keep the
# keystore backed up, because losing it strands everyone on their install.
#   DUALSCREEN_KEYSTORE       path to the keystore
#   DUALSCREEN_KEYSTORE_PASS  store password (key password defaults to it)
#   DUALSCREEN_KEY_ALIAS      key alias (default: dualscreen)
if [ -n "${DUALSCREEN_KEYSTORE:-}" ]; then
    [ -f "$DUALSCREEN_KEYSTORE" ] || { echo "keystore not found: $DUALSCREEN_KEYSTORE"; exit 1; }
    : "${DUALSCREEN_KEYSTORE_PASS:?Set DUALSCREEN_KEYSTORE_PASS for the release key}"
    export DUALSCREEN_KEYSTORE_PASS
    export DUALSCREEN_KEY_PASS="${DUALSCREEN_KEY_PASS:-$DUALSCREEN_KEYSTORE_PASS}"
    echo "signing with release key: $DUALSCREEN_KEYSTORE"
    "$BUILD_TOOLS/apksigner" sign --ks "$DUALSCREEN_KEYSTORE" \
        --ks-pass env:DUALSCREEN_KEYSTORE_PASS \
        --key-pass env:DUALSCREEN_KEY_PASS \
        --ks-key-alias "${DUALSCREEN_KEY_ALIAS:-dualscreen}" \
        --out "$OUT" "$WORK/aligned.apk"
else
    echo "warning: DUALSCREEN_KEYSTORE unset, signing with the debug key"
    "$BUILD_TOOLS/apksigner" sign --ks ~/.android/debug.keystore \
        --ks-pass pass:android --key-pass pass:android \
        --out "$OUT" "$WORK/aligned.apk"
fi
echo "release APK -> $OUT"
unzip -l "$OUT" | grep -E "libmain|manifest"
