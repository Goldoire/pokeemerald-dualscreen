# Install and bring your save

Pokémon Emerald Dual Screen runs on Android with the game on the top display
and a touch interface on the lower display. The release requires your own
supported Pokémon Emerald ROM to prepare its game data on first launch.

## Install

1. Download the APK from the [latest release](https://github.com/Goldoire/pokeemerald-dualscreen/releases/latest).
2. Open the APK and allow installation from that source when Android asks.
3. Launch the app, tap **Select ROM**, and choose your Pokémon Emerald
   (USA/Europe) ROM.
4. Wait for game-data preparation to finish. Future launches boot into the game.

The ROM picker accepts the original USA/Europe game with this SHA-1:

```text
f3ae088181bf583e55daf962a92bb46f4f1d07b7
```

Patched ROMs and ROM hacks are not supported by the release's ROM picker.

Alternatively, place the supported ROM at this location before launching:

```text
Android/data/com.pokeemerald.dualscreen/files/baserom.gba
```

Direct access to `Android/data` depends on your Android version, device,
and file manager. Use the in-app ROM picker when that folder is inaccessible.

## Import a save

Use an ordinary **128 KB GBA flash save** from an emulator or cartridge dumper.
Emulator savestates are a different format and cannot be imported.

1. Close the app.
2. Back up any existing `pokeemerald.sav` before replacing it.
3. Copy your save to the following location, naming it `pokeemerald.sav`:

   ```text
   Android/data/com.pokeemerald.dualscreen/files/pokeemerald.sav
   ```

4. Launch the app and select **Continue**.

The save filename must match exactly, even if the source emulator gave it
another name. Access to this folder has the same Android file-manager
limitations described above.

## Back up or transfer a save

Save through the game's own **SAVE** menu, then close the app before copying
`pokeemerald.sav` out of the folder above. Keep that copy somewhere separate
from the app's data. You can import it on another device using the same steps.

Back up saves before replacing files, uninstalling the app, or trying
experimental builds.

## Update

Install the newer official APK over the existing app. An APK with the same
application identity and signing key can update the installation while keeping
its app data. Back up your save first; uninstalling or clearing app data can
remove it.

The README's development previews may show features that are absent from the
published APK. See the [release notes](https://github.com/Goldoire/pokeemerald-dualscreen/releases)
for the features included in each download. Building an APK yourself is covered
in the [development guide](DEVELOPMENT.md).
