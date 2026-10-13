# Pokémon Emerald Dual Screen

![Showcase](docs/screenshots/showcase.png)

A dual-screen mod of the [Pokémon Emerald decompilation](https://github.com/pret/pokeemerald) for the AYN Thor and other dual-screen Android devices. The game runs natively, no emulator involved. 

This fork builds on [Goldoire/pokeemerald-dualscreen](https://github.com/Goldoire/pokeemerald-dualscreen). It was created to support the second analog stick and, primarily, make saves easier to share through Syncthing.

The changes focus on three things:

- **More controller inputs:** navigate the bottom screen with L1/R1, the right analog stick, and Y.
- **`.srm` save support:** load and update raw `.srm` saves alongside the existing `.sav` support.
- **Custom save paths:** choose a save folder, including a local folder you synchronize with Syncthing.

Syncthing handles synchronization separately; this app reads and writes the selected local save file. See the [settings and synchronization guide](docs/SETTINGS.md) and [Android build guide](docs/ANDROID.md).

The packaged APK uses your own Pokémon Emerald ROM to restore game assets on first launch. Development APKs skip that step; use the release-packaging workflow before distributing a build.
  
ROM hacks are not supported; this fork keeps the vanilla Emerald experience.

## Instructions

1. Build and package this fork's APK using the [Android build guide](docs/ANDROID.md), then install it.
  The [upstream releases](https://github.com/Goldoire/pokeemerald-dualscreen/releases) contain the original mod, without this fork's additions.
  Android will warn about an unknown developer.
2. Launch the app and tap "Select ROM" when asked, then pick your
  Pokémon Emerald (USA/Europe) ROM. It is checked against SHA-1
   `f3ae088181bf583e55daf962a92bb46f4f1d07b7`.
   (You can also drop the ROM at `Android/data/com.pokeemerald.dualscreen/files/baserom.gba`
   beforehand to skip the picker.)
3. That's it. The app restores the game data once and boots straight into
  the game. Future launches skip this step.

## Saves

You can bring an existing ordinary 128 KB GBA flash save from an emulator or cartridge dump. Supported filenames end in `.sav` or `.srm`; savestates are not supported.

The default location is `Android/data/com.pokeemerald.dualscreen/files/pokeemerald.sav`. If that file is absent, `pokeemerald.srm` in the same directory is loaded and updated instead.

To use another folder, open the bottom-screen settings → **SUPER SECRET SETTINGS** → **CHANGE SAVE FILE PATH**. Save your game, then fully close and reopen the app to activate the new folder. Existing saves are loaded from that folder; an empty folder receives a copy of the previous save. See [save selection and recovery](docs/SETTINGS.md#save-folder) before moving files.

## Features

- **Native widescreen!** 16:9 Widescreen for the top screen, without stretching the image
- **Fast forward:** without speeding up the music, at 2x, 3x, and 4x
- **Party**: icons, HP and status for all six. Tap a Pokémon for its
stats, nature, ability, moves and exp.
- **Gen 4 style battles**: use touch/controls on the bottom screen to select between options and moves.
- **Battle hints** (off by default): effectiveness carets on each move
and the foe's weaknesses on its card.
- **Map**: the Hoenn Pokénav map with your live position and the name of
where you are.
- **Bag**: all five pockets with live quantities.
- **Trainer card**: badges, money, playtime, based on the in-game card
- **Custom save folder**: choose a writable folder using Android's folder picker; load and update raw `.sav` or `.srm` files there.
- **Optional bottom-screen controller navigation**: L1/R1 change tabs, the right stick moves a dark-gray focus outline, and Y activates the selected option. Extra navigation pauses during battles and resumes afterward.

## Development

See [Android setup and packaging](docs/ANDROID.md) for a build from source and the [device verification checklist](docs/ANDROID.md#device-verification). [INSTALL.md](INSTALL.md) retains the upstream GBA ROM build instructions.

## Credits

- [Goldoire/pokeemerald-dualscreen](https://github.com/Goldoire/pokeemerald-dualscreen): the original dual-screen mod and UI this fork extends.
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
your own legally obtained copy. Do not commit ROMs, personal saves, or signing keys.
