# Pokémon Emerald Dual Screen

![Showcase](docs/screenshots/showcase.png)

A dual-screen mod of the [Pokémon Emerald decompilation](https://github.com/pret/pokeemerald)
for the AYN Thor and other dual-screen Android devices. The game runs
natively on the top screen, no emulator involved. The bottom screen shows
your party, the map, your bag and your trainer card, and gives you Gen 4
style touch battles.

No ROM or copyrighted assets are included. Everything the bottom screen
shows is decoded at runtime from the game's own data.

## Instructions

1. Install the APK from the [releases page](https://github.com/Goldoire/pokeemerald-dualscreen/releases).
   It is debug-signed, so Android will warn about an unknown developer.
2. Launch the app and tap "Select ROM" when asked, then pick your
   Pokémon Emerald (USA/Europe) ROM. It is checked against SHA-1
   `f3ae088181bf583e55daf962a92bb46f4f1d07b7`.
   (You can also drop the ROM at `Android/data/com.pokeemerald.experimental/files/baserom.gba`
   beforehand to skip the picker.)
3. That's it. The app restores the game data once and boots straight into
   the game. Later launches go right to it.

The APK ships with every asset byte zeroed out, and your own copy of the
game fills them back in at boot. Nothing is written back to your ROM.

## Features

- **Party**: icons, HP and status for all six. Tap a Pokémon for its
  stats, nature, ability, moves and exp.
- **Gen 4 style battles**: use touch on the bottom screen to select
  between options and moves.
- **Map**: the Hoenn Pokénav map with your live position and the name of
  where you are.
- **Bag**: all five pockets with live quantities.
- **Trainer card**: badges, money, playtime, based on the in-game card

Everything is drawn with the game's own font and sprites, decoded at
runtime.

## Settings

The gear tab on the bottom screen holds the mod's settings: background
art or plain black/white, widescreen, touch-control overlay, battle menu
placement, fast forward, volume, and the experimental voxel renderer.
Changes are saved immediately and survive a restart.

## Building (Linux / WSL)

Prereqs: `gcc-multilib`, 32-bit SDL2 dev libs, `g++`, `pkg-config`,
`libpng-dev`, `binutils-arm-none-eabi`, JDK 17, Android SDK (platform 36,
build-tools 36, NDK 26.3.11579264, cmake 3.22.1).

```sh
git submodule update --init --recursive
git -C android/SDL2 apply --unidiff-zero ../patches/sdl2-android-lifecycle.patch

make tools                          # host-side asset tools
make -f Makefile_pc generated       # generated headers the CMake build needs
PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig make -f Makefile_pc linux -j8
                                    # desktop build; also converts all graphics

echo sdk.dir=$HOME/Android/sdk > android/local.properties
JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64 \
    android/SDL2/android-project/gradlew -p android :app:assembleDebug
```

The APK lands in `android/app/build/outputs/apk/debug/`. That build has
the assets compiled in and needs no ROM. To make a distributable,
asset-free APK, run `tools/dualscreen/package_release.sh <your_rom.gba>`.

`tools/dualscreen/savetool.py` can inspect and edit saves for testing
(`info` / `teleport` / `heal` / `money`).

Upstream's experimental 2.5D voxel renderer is inherited and available on
the Linux build via `./pokeemerald --voxel` (see the multiplatform repo
for details).

## How it works

The whole game compiles into a single native library, so the bottom
screen reads the running game's state directly instead of scanning its
memory from outside:

- `src/platform/dualscreen_bridge.c` snapshots party, battle, overworld
  and bag state to JSON once per frame window (at vblank, while the game
  thread is parked) and exposes it over JNI, along with runtime-decoded
  graphics: mon icons, the region map, badges, trainer pics, the game font.
- The Android side (`android/app/src/main/java/.../DualScreen*.java`)
  draws the bottom-screen UI on the secondary display through the
  `Presentation` API, polling the bridge. The window is non-focusable so
  controller input never leaves the game.
- Touch input reaches the game through a virtual key queue consumed by
  `Platform_GetKeyInput`, one button state per frame.

## Credits

- [pret/pokeemerald](https://github.com/pret/pokeemerald): the decompilation
  this is built on.
- [gradenGnostic/pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform):
  the native SDL2 port.
- [samyost1/tmc-android](https://github.com/samyost1/tmc-android) and
  [samyost1/zelda3-android](https://github.com/samyost1/zelda3-android):
  the dual-screen blueprint this follows.

The dual-screen mod was made with the help of Claude Code and other AI
coding tools.

This project builds on a decompilation of a copyrighted game. Play it with
your own legally obtained copy; nothing proprietary ships in this
repository.
