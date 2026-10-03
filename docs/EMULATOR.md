# Dual-screen Android testing on PC

This runs the actual Android APK, SDL game surface, JNI bridge and bottom-screen
`Presentation`. It uses a dedicated Android 11 Google APIs x86_64 AVD whose ARM
translation accepts the existing `armeabi-v7a` build. No release ABI changes are
needed. The displays match our Thor captures: **1920×1080 top**, **1240×1080 bottom**.

## Setup on Linux or Windows with WSL2

Use the same Linux SDK/JDK environment as the [Android build guide](DEVELOPMENT.md).
The helper requires Python 3 and the SDK command-line tools. Set `ANDROID_HOME`
to your SDK directory; its default is `$HOME/Android/sdk`.

Install the emulator image once, accepting the SDK's license prompts:

```sh
sdkmanager "emulator" "platform-tools" "system-images;android-30;google_apis;x86_64"
```

KVM must be usable. On WSL2, check that `/dev/kvm` exists and grant your Linux
account access to the `kvm` group if it lacks it:

```sh
sudo usermod -aG kvm "$USER"
# Start a new login session, then verify:
"$ANDROID_HOME/emulator/emulator" -accel-check
```

The visible emulator uses X11/WSLg. Headless mode works without a desktop.
The helper creates `emerald_dualscreen`, uses port **5580**, and refuses to
install, send inputs, copy saves, or stop an emulator with a different AVD name.
It does not select physical devices. It preserves this AVD's userdata on restart.

From the repository root:

```sh
python3 tools/dualscreen/emulator.py start --gui
python3 tools/dualscreen/emulator.py install
```

`install` defaults to `android/app/build/outputs/apk/debug/app-debug.apk`.
Build that first using the development guide, or pass another APK path. A release
APK still requires its usual supported-ROM import. This emulator pass used a
development APK with compiled assets.

For automated tests, omit `--gui`. To switch modes, stop the current emulator
first. GUI mode presents both displays in its emulator window; click the bottom
screen to use the real touch interface.

The game's SDL keyboard mappings are arrows for movement, **Z** for A, **X** for
B, **Enter** for Start, backslash for Select, and **A/S** for L/R. The CLI `key`
command below also works when running headless.

```sh
python3 tools/dualscreen/emulator.py stop
python3 tools/dualscreen/emulator.py start
```

## Copy a save and control the game

The following command **replaces only this AVD's save** with a copy. It roots
adbd on the dedicated Google APIs emulator to bypass Android 11's shell
scoped-storage restriction; the source file is read only. Use an ordinary
128 KB flash save, optionally with a 16-byte emulator footer.

```sh
python3 tools/dualscreen/emulator.py load-save /path/to/pokeemerald.sav
python3 tools/dualscreen/emulator.py key 8     # Start: skip intro/open title menu
python3 tools/dualscreen/emulator.py key 1     # A: select Continue when visible
```

Allow the title/menu transition to finish before the next press. Other GBA masks:
B=2, Select=4, Right=16, Left=32, Up=64, Down=128, R=256, L=512. `--frames 4`
holds a button for four game frames. These commands use the existing development
receiver and virtual GBA input queue; Android key codes differ from GBA masks.

```sh
python3 tools/dualscreen/emulator.py info
python3 tools/dualscreen/emulator.py tap 1120 1020   # Bottom settings tab
python3 tools/dualscreen/emulator.py snapshot        # Debug APK only
python3 tools/dualscreen/emulator.py capture         # Actual PNGs from both displays
```

`snapshot` returns the published game snapshot, applied native preference values
(excluding queued requests), and the active renderer. A requested renderer
change remains pending until the app restarts.
The snapshot broadcast is registered only in debug builds. Screenshots and logs
go under ignored `.cache/dualscreen-emulator/`; pass an output directory to
`capture` to keep evidence elsewhere.

The lower screen must have `FLAG_PRESENTATION` and touch support. The helper
starts Android's emulator display service and applies display flags **75**
(public, presentation, own content, supports touch). Flags 0 omit presentation;
flags 11 omit touch. Android's **logical display ID changes after recreation**:
`tap` discovers it each time. Screenshot capture instead uses emulator indices
0 and 1, in separate temporary directories to avoid same-second name collisions.

## Regression pass

After continuing a copied save, run:

```sh
python3 tools/dualscreen/emulator_smoke.py
```

This exercises real bottom-screen touches, native preferences, scrolling across
chips, header clipping, background/resume and presentation-display recreation.
It restores starting preferences and writes `result.json` and screenshot pairs
for review. Review the restart reminder, row clipping, zoom labels and tab bar
visually; the assertions do not judge appearance. It never issues an in-game save.

## Local validation, 2026-10-02

The ARMv7 development APK built and installed successfully on this profile.
Classic gameplay and the voxel world both rendered on the top screen; the real
bottom interface showed the copied save's party and location. The visible WSLg
emulator with the host/Mesa backend passed 13 integration checks covering touch
choices, applied native settings, scrolling, clipping, resume and secondary-display
recreation. Starting preferences were restored. Visual review confirmed readable
zoom percentages and the renderer restart reminder, including after resume.

The save-copy command was exercised against this AVD. SHA-256 of the source save
was unchanged before and after copying. SDK, AVD userdata, APKs, saves, logs and
validation captures remain outside tracked source files.

The default graphics backend is `--gpu host`. On this WSL2 setup it uses Mesa
llvmpipe. SwiftShader displayed classic gameplay but crashed the emulator host
graphics process on entering the voxel world; use the host backend for voxel
tests here. `--gpu software`, `swangle`, `lavapipe`, and `swiftshader` are available
for diagnosis, but have not all been validated. Logs identify the selected driver.

The emulator validates Android UI and lifecycle behavior. ARM translation and
PC GPU timings do not predict Thor performance. Android 11 also does not
cover newer Android platform behavior. Battle camera framing, handheld controls,
touch feel, battery use and sustained voxel performance still need device tests.

References: [Android Emulator extended controls](https://developer.android.com/studio/run/emulator-extended-controls),
[emulator console](https://developer.android.com/studio/run/emulator-console),
[Android 11 emulator ARM support](https://android-developers.googleblog.com/2020/03/run-arm-apps-on-android-emulator.html).
