#!/bin/bash
# Run from any directory. Extra arguments are passed to Gradle.
set -euo pipefail
cd "$(dirname "$0")/../.."

if [ ! -f android/SDL2/android-project/gradlew ]; then
    echo "Initialize SDL first: git submodule update --init --recursive" >&2
    exit 1
fi

if [ -n "${DEVKITARM:-}" ]; then
    export PATH="$DEVKITARM/bin:$PATH"
elif [ -n "${ARM_NONE_EABI_TOOLCHAIN:-}" ]; then
    export PATH="$ARM_NONE_EABI_TOOLCHAIN/bin:$PATH"
fi

make -f make_tools.mk
make NODEP=1 MODERN=1 generated
python3 tools/dualscreen/prepare_android_assets.py

exec bash android/SDL2/android-project/gradlew -p android :app:assembleDebug "$@"
