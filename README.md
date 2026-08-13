# Pokémon Emerald — Dual Screen

A dual-screen mod of the [Pokémon Emerald decompilation](https://github.com/pret/pokeemerald),
built for the AYN Thor and other dual-screen Android devices. The game runs
natively (no emulator) on the top screen while the bottom screen becomes a
live touch companion — party, map, bag, trainer card, and a Gen 4-style
touch battle interface.

Based on [pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform),
inspired by [tmc-android](https://github.com/samyost1/tmc-android).
No ROM or copyrighted assets are included; everything the bottom screen
shows is decoded at runtime from the game's own data.

## Features

- **Party** — animated icons, HP bars, status; tap a Pokémon for a detail
  page with stats, nature, ability, moves and exp.
- **Battle, Gen 4 style** — during battles the bottom screen takes over:
  big FIGHT/BAG/POKéMON/RUN buttons and a touch move grid with PP and
  types, while the top screen shows only the scene and message box. The
  game's battle engine stays fully authoritative — touches drive the real
  in-game cursor through a frame-timed virtual gamepad. (Classic top-screen
  menus are one settings toggle away, and are used automatically for
  Safari/link/tutorial battles.)
- **Map** — the real Hoenn Pokénav map, composed from the game's tile data,
  with your live location and the in-game location name.
- **Bag** — all five pockets, live quantities.
- **Trainer card** — styled after the in-game card: star tint, IDNo., the
  actual badge sprites and your trainer's front pic.
- **Settings** — background art/black/white, widescreen, touch-control
  overlay, battle menu placement. Persisted with the port's config.
- Everything renders in the game's own font, decoded from the ROM data at
  runtime.

## How it works

The whole game compiles into a single native library, so game state is
read in-process — no RAM peeking, no emulator hooks:

- `src/platform/dualscreen_bridge.c` snapshots party/battle/overworld/bag
  state to JSON once per frame window (at vblank, while the game thread is
  parked) and exposes it over JNI, along with runtime-decoded graphics
  (mon icons, the region map, badges, trainer pics, the game font).
- The Android side (`android/app/src/main/java/.../DualScreen*.java`)
  presents a canvas UI on the secondary display via the `Presentation`
  API, polling the bridge. The window is non-focusable so controller
  input never leaves the game.
- Touch input reaches the game through a virtual key queue consumed by
  `Platform_GetKeyInput`, one button state per frame.

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

The APK lands in `android/app/build/outputs/apk/debug/`. Saves live in the
app's private storage as `pokeemerald.sav` (standard 128KB flash format —
cart and emulator saves work).

`tools/dualscreen/savetool.py` can inspect and edit saves for testing
(`info` / `teleport` / `heal` / `money`).

Upstream's experimental 2.5D voxel renderer is inherited and available on
the Linux build via `./pokeemerald --voxel` (see the multiplatform repo
for details).

## Credits

- [pret/pokeemerald](https://github.com/pret/pokeemerald) — the decompilation.
- [gradenGnostic/pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform) —
  the native SDL2 port this builds on.
- [samyost1/tmc-android](https://github.com/samyost1/tmc-android) — the
  dual-screen blueprint.

This project builds on a decompilation of a copyrighted game. Build it
with your own legally obtained copy's save data; nothing proprietary ships
in this repository.
