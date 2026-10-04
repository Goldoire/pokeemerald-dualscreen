<p align="center">
  <img src="docs/branding/pokemon-emerald-dual-screen.png" alt="Pokémon Emerald Dual Screen" width="520">
</p>

<h1 align="center">Pokémon Emerald Dual Screen</h1>

<p align="center">
  <a href="https://github.com/Goldoire/pokeemerald-dualscreen/releases/latest">Download</a> ·
  <a href="#screenshots">Screenshots</a> ·
  <a href="#features">Features</a> ·
  <a href="#getting-started">Getting started</a> ·
  <a href="#documentation">Documentation</a>
</p>

A dual-screen mod of the [Pokémon Emerald decompilation](https://github.com/pret/pokeemerald) for the AYN Thor and other dual-screen Android devices. The game runs natively, no emulator involved.

No ROM or copyrighted assets are included. Provide your own Pokémon Emerald (USA/Europe) ROM to get the game up and running. This is meant to be a vanilla dual-screen experience; ROM hacks aren't supported.

## Screenshots

<table>
  <tr>
    <td align="center"><strong>Widescreen and map</strong><br><img src="docs/screenshots/shot_map_top.png" alt="Widescreen gameplay in Littleroot Town" width="390"><br><img src="docs/screenshots/shot_map_bottom.png" alt="Hoenn map with Littleroot Town marked" width="300"></td>
    <td align="center"><strong>Battle menus</strong><br><img src="docs/screenshots/shot_battle_top.png" alt="A wild Pokémon battle" width="390"><br><img src="docs/screenshots/shot_battle_bottom.png" alt="Fight, bag, run and Pokémon buttons on the touch screen" width="300"></td>
  </tr>
</table>

<details>
<summary><strong>More screenshots</strong></summary>

<table>
  <tr>
    <td align="center"><strong>Moves</strong><br><img src="docs/screenshots/shot_moves_top.png" alt="Wild battle while choosing a move" width="390"><br><img src="docs/screenshots/shot_moves_bottom.png" alt="Move selection with type, power, accuracy and PP" width="300"></td>
    <td align="center"><strong>Party</strong><br><img src="docs/screenshots/shot_party_top.png" alt="Littleroot Town while checking the party" width="390"><br><img src="docs/screenshots/shot_party_bottom.png" alt="Six Pokémon with HP bars" width="300"></td>
  </tr>
  <tr>
    <td align="center"><strong>Battle bag</strong><br><img src="docs/screenshots/shot_battlebag_top.png" alt="Wild battle with the bag open" width="390"><br><img src="docs/screenshots/shot_battlebag_bottom.png" alt="Battle items sorted into items, Poké Balls and berries" width="300"></td>
    <td align="center"><strong>Trainer card</strong><br><img src="docs/screenshots/shot_card_top.png" alt="Littleroot Town while viewing the trainer card" width="390"><br><img src="docs/screenshots/shot_card_bottom.png" alt="Trainer card with badges and play time" width="300"></td>
  </tr>
  <tr>
    <td align="center"><strong>3D view</strong><br><img src="docs/screenshots/shot_voxel_top.png" alt="Littleroot Town in the voxel renderer" width="390"><br><img src="docs/screenshots/shot_voxel_bottom.png" alt="Party menu below the 3D view" width="300"></td>
    <td align="center"><strong>Settings</strong><br><img src="docs/screenshots/shot_settings_top.png" alt="The game stays on the top screen while settings are open" width="390"><br><img src="docs/screenshots/shot_settings_bottom.png" alt="Display settings and the OPTION tab" width="300"></td>
  </tr>
</table>

</details>

## Features

| Feature | Description |
| --- | --- |
| Native widescreen | 16:9 widescreen for the top screen, without stretching the image. |
| Fast forward | 2×, 3× and 4×, without speeding up the music. |
| Party | Icons, HP and status for all six. Tap a Pokémon for its stats, nature, ability, moves and EXP. |
| Gen 4 style battles | Use touch or physical controls on the bottom screen to select actions and moves, use items and switch Pokémon. |
| Battle hints | Move effectiveness and the foe's weaknesses. Off by default. |
| Map | The Hoenn Pokénav map with your live position and the name of where you are. |
| Bag | All five pockets with live quantities. |
| Trainer card | Badges, money and playtime, based on the in-game card. |
| 3D mode | A voxel renderer for the overworld, with camera and lighting options. |

3D mode is experimental. Open **OPTION → 3D** to change the renderer and camera. The [settings guide](docs/SETTINGS.md) explains the options.

## Getting started

1. Install the APK from the [latest release](https://github.com/Goldoire/pokeemerald-dualscreen/releases/latest).
2. Launch the app and tap **Select ROM** when asked, then pick your Pokémon Emerald (USA/Europe) ROM.
3. That's it. The app restores the game data once and boots straight into the game. Future launches skip this step.

You can bring an existing save with you. Any ordinary **128 KB GBA flash save**, the same file an emulator or a cartridge dumper writes, can be imported. This doesn't include savestates or other formats. See [install and saves](docs/INSTALLATION.md) for the steps.

## Documentation

| Guide | Description |
| --- | --- |
| [Install and saves](docs/INSTALLATION.md) | Installation, updates, save imports and backups. |
| [Settings](docs/SETTINGS.md) | Display, controls, sound and 3D options. |
| [Build from source](docs/DEVELOPMENT.md) | Build your own Android APK. |
| [Run on PC](docs/EMULATOR.md) | Run the Android app in a dual-screen emulator. |

## Credits

Based on [pret/pokeemerald](https://github.com/pret/pokeemerald) and [gradenGnostic's native port](https://github.com/gradenGnostic/pokeemerald-multiplatform). See the full [credits](docs/CREDITS.md) for contributors and third-party code.

Unofficial fan project. Pokémon belongs to its respective owners. [License](LICENSE).
