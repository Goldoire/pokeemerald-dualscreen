# Building the native port

The Android build is currently ARMv7 (32-bit), with Android SDK 36, NDK
26.3.11579264, CMake 3.22.1, and JDK 17. The existing Gradle wrapper uses
Gradle 8.1.1. On Windows, use a Linux filesystem in WSL for builds; this
pipeline invokes POSIX tools and arm-none-eabi binutils.

Install the host C/C++ tools, pkg-config, libpng development files,
arm-none-eabi binutils, the Android tools listed above, and git. Configure
the Android SDK path in `android/local.properties`, and set `JAVA_HOME`
and `ANDROID_HOME` for your local installations.

```sh
git submodule update --init --recursive
git -C android/SDL2 apply --unidiff-zero ../patches/sdl2-android-lifecycle.patch
make tools
make -f Makefile_pc generated
android/SDL2/android-project/gradlew -p android :app:assembleDebug
```

Apply the SDL2 lifecycle patch once per initialized submodule checkout. If
it is already applied, do not apply it again. The generated headers and
data are prerequisites for the CMake build.

The debug APK is written to:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

A development build can contain reconstructed game data. Use the release
packaging tool when preparing a distribution:

```sh
tools/dualscreen/package_release.sh /path/to/your/emerald.gba output.apk
```

This uses the built APK and unstripped library to create the asset-hole
manifest and remove the matched game-data bytes. Check the resulting
package before publication. Set the existing release keystore variables
documented in that script for an update signed with the published app's
key; an APK signed with another key cannot update an installed release.

For Linux development, install multilib and 32-bit SDL2, SDL2_image, and
OpenGL development libraries. The experimental voxel path requires GL:

```sh
PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig make -f Makefile_pc linux -j4
./pokeemerald --voxel
```

The Windows SDL2 target remains in the tree, but the current experimental
voxel implementation is guarded for Linux and Android. Do not advertise
Windows voxel support based on the presence of the Windows build target.

Before releasing voxel changes, verify maps and connected edges, doors and
dialogue, battle entry and return, bag and party actions, save/load, bottom
display input, and Android pause/resume. Record both display captures and
frame timing on the AYN Thor. Host geometry tests cannot establish device
performance or Android lifecycle correctness.
