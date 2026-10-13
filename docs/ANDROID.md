# Building the Android fork

This guide builds the native Android app. Run commands from the repository root unless stated otherwise. The upstream GBA ROM instructions remain in [INSTALL.md](../INSTALL.md).

## Requirements

- Git with submodule support, Bash, Make, a host C/C++ compiler, libpng headers, and pkg-config for the asset tools.
- Python 3; `pyelftools` is also needed for release packaging.
- JDK 17. Set `JAVA_HOME` to that installation.
- Android SDK platform **36**, NDK **26.3.11579264**, CMake **3.22.1**, and SDK Build Tools containing `zipalign` and `apksigner`.
- ARM GNU tools providing `arm-none-eabi-as`, `arm-none-eabi-ld`, `arm-none-eabi-objcopy`, and `arm-none-eabi-cpp`. Put them on `PATH`, or set `DEVKITARM` / `ARM_NONE_EABI_TOOLCHAIN` to the toolchain directory.

The Android project uses AGP 8.1.1 and the SDL submodule's Gradle 8.1.1 wrapper. Use the wrapper, rather than an unrelated system Gradle version. The packaged ABI is `armeabi-v7a`; the device must support 32-bit ARM apps. The app declares Android API 21 as its minimum.

## Build

After cloning this repository:

```sh
git submodule update --init --recursive
```

Set `ANDROID_HOME` to the SDK directory, or set `sdk.dir` in the ignored `android/local.properties`. For example, on macOS with Android Studio and devkitARM installed:

```sh
export JAVA_HOME="$(/usr/libexec/java_home -v 17)"
export ANDROID_HOME="$HOME/Library/Android/sdk"
export DEVKITARM=/opt/devkitpro/devkitARM
bash tools/dualscreen/build_android.sh
```

On Linux, use the actual SDK and ARM toolchain locations on your machine. All paths may contain spaces. The script builds the host tools, generates maps and binary assets, and runs `:app:assembleDebug`. The first build needs network access for Gradle dependencies. Subsequent builds may use `--offline` once dependencies are cached.

Output: `android/app/build/outputs/apk/debug/app-debug.apk`.

This development APK contains the built-in assets and does **not** ask for a ROM. For the first-launch ROM picker, run the packaging step below.

Assembly preprocessing uses `arm-none-eabi-cpp`, including on macOS; no temporary CMake edits or SDK symlinks are required. If editing C headers, assembly includes, or game assets, clean the Android build before rebuilding because the native preprocessing rules do not track every included file:

```sh
bash android/SDL2/android-project/gradlew -p android clean
bash tools/dualscreen/build_android.sh
```

The existing SDL lifecycle patch in `android/patches/` is an optional upstream workaround; the build script does not apply it automatically.

## Package an APK with the ROM picker

Use your own Pokémon Emerald USA/Europe ROM, SHA-1 `f3ae088181bf583e55daf962a92bb46f4f1d07b7`.

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install pyelftools
bash tools/dualscreen/package_release.sh "/path/to/Pokemon Emerald.gba" "pokeemerald-dualscreen-release.apk"
```

The packaging tool uses the matching unstripped library to locate asset ranges, clears those ranges in the APK library, and adds `assets/asset_manifest.bin`. First launch checks the user's ROM and restores those ranges. Keep the unstripped library and debug APK from the same build.

`BUILD_TOOLS` may point to a specific SDK Build Tools directory. Otherwise the script selects the highest installed numeric version under `ANDROID_HOME` (or `ANDROID_SDK_ROOT`), then the usual macOS/Linux SDK locations. Temporary packaging files are isolated per invocation and removed on exit.

For a stable release signature, configure these environment variables before packaging:

| Variable | Purpose |
| --- | --- |
| `DUALSCREEN_KEYSTORE` | Keystore path |
| `DUALSCREEN_KEYSTORE_PASS` | Keystore password |
| `DUALSCREEN_KEY_ALIAS` | Alias; defaults to `dualscreen` |
| `DUALSCREEN_KEY_PASS` | Key password; defaults to the keystore password |

Without a configured release key, packaging uses the local Android debug key for testing. Public releases should consistently use the same release key. APKs, signing keys, ROMs, saves, and the Python environment are excluded from Git.

Install or update without intentionally clearing data:

```sh
adb install -r pokeemerald-dualscreen-release.apk
```

The application ID is still `com.pokeemerald.dualscreen`, and the version remains based on upstream 0.5.0. An APK signed with another key cannot update an installed copy. Back up saves before uninstalling or clearing app data. This fork does not install alongside upstream under a separate application ID.

## Code map

| File | Responsibility |
| --- | --- |
| `android/app/src/main/java/com/pokeemerald/experimental/DualScreenView.java` | Settings pages, touch targets, controller focus, outline drawing |
| `DualScreenPresentation.java` in the same package | Secondary-display view and input forwarding |
| `PokeEmeraldActivity.java` | Controller events, stick repeat, folder picker, native save-open callback |
| `SaveFileLocation.java` | Persistent folder permission, file selection, migration, seekable file descriptor |
| `RomGateActivity.java` | ROM gate and recovery when a configured save folder is unavailable |
| `src/platform/sdl2.c` | Native save stream, default `.srm` fallback, flash reads and writes |
| `tools/dualscreen/` | Android asset preparation, APK packaging, existing asset/save utilities |

The Java save callback returns `-1` for the default path, `-2` for startup failure, or an owned file descriptor. Native code wraps that descriptor with `fdopen` and uses the same stream for reads and writes. A folder change takes effect only on the next process start.

## Device verification

Compilation does not validate physical controller mappings, secondary-display placement, or Android document providers. Before publishing an APK, check:

- A packaged clean install requests the ROM; a valid ROM boots, and a wrong ROM is rejected.
- Default `.sav` loads and saves across a full restart. With `.sav` absent, `.srm` does the same.
- Selecting an empty custom folder copies the previous save; selecting a folder with a save loads that save. Test both extensions, preferred filenames, and the ambiguous multiple-file error.
- Revoking folder access shows recovery instead of silently loading another file. Verify Original location uses the default save.
- Extra controller input is off initially and persists when enabled. L1/R1 cycle tabs, the right stick moves focus, and Y activates it.
- Party details hide the outline and return to the selected Pokémon. Bag pocket selection stays on the chosen pocket. Scroll through a long Bag/settings list.
- Battles disable extra navigation and the outline, while existing battle controls work; leaving battle restores the enabled extra mappings.
- Holding the stick, then backgrounding, disconnecting the controller, or entering battle stops repeats. Touch and normal game inputs still work.

See [SETTINGS.md](SETTINGS.md) for the user-facing behavior these checks cover.
