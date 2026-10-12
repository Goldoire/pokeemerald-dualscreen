# Settings

Open the **OPTION** tab on the bottom screen. Options are laid out like the
game's own option menu: each box shows its name over the current choice. Tap
the right half of a box to step forward, the left half to step back. The box
you last touched is described in the window at the bottom.

## GAME

The game's own START → OPTION settings, so you never need the START menu for
them. They live in your save: a change takes effect at once and is kept the
next time you save in game. They unlock once you continue your adventure.

| Option | Choices |
| --- | --- |
| TEXT SPEED | SLOW, MID, FAST |
| BATTLE SCENE | ON, OFF (move animations) |
| BATTLE STYLE | SHIFT, SET |
| SOUND | MONO, STEREO |
| BUTTON MODE | NORMAL, LR, L=A |
| FRAME | TYPE1 to TYPE20, the border on menus and text boxes |
| VOLUME | 0 to 10 (a port setting, saved with the others below) |

## DISPLAY

| Option | Choices |
| --- | --- |
| WIDESCREEN | 16:9 overworld with square pixels |
| BACKGROUND | ART, BLACK or WHITE around the game screen |
| TOUCH PAD | On-screen buttons over the game |

## PLAY

| Option | Choices |
| --- | --- |
| BATTLE MENU | BOTTOM (DS style, touch) or TOP (classic) |
| BATTLE HINTS | Move effectiveness and the foe's weaknesses |
| FAST FORWARD | OFF, 2X, 3X, 4X; R2 toggles it |
| FF MUSIC | NORMAL keeps the tempo, FAST speeds it up too |
| STICK NAV | Controller navigation for the bottom screen, see [below](#more-controller-inputs) |
| SAVE FOLDER | Tap to choose a folder, see [below](#save-folder) |

## 3D

The voxel overworld is **experimental and off by default**. Turn on
**VOXEL 3D**, then fully close and reopen the app; the help window reminds you
until you do. The other 3D options stay locked while it is off.

| Option | Choices |
| --- | --- |
| VOXEL 3D | OFF, ON (applies after a restart) |
| CAMERA | PLAY follows close at 55°; DIORAMA frames more of the map at 36° |
| 3D ANGLE | PRESET (the camera's own), 35, 40, 45, 55 or 62 degrees |
| 3D ZOOM | 90, 100, 110 or 120 percent; higher is closer |
| LIGHTING | ART keeps the original shading; SUN adds a fixed sun and cast shadows outdoors |
| 3D BATTLES | Stage supported battles on the 3D map |

Angle and zoom change live in the overworld; they do not change the fitted
battle cameras.

## How settings are stored

Port settings (everything except GAME) are written to `pokeemerald.cfg`
straight away. Android queues each validated request for the SDL render
thread, which applies it and writes the file; repeated requests before its next
frame are coalesced. GAME options are queued the same way and written into the
save block by the game thread. Out-of-range values are rejected on the native
side. Developers can run the [emulator pass](EMULATOR.md#regression-pass) to
exercise every section by touch.

## Save folder

**SAVE FOLDER** opens Android's folder picker. Select a folder with persistent read/write access. A local folder is recommended: stream-only cloud providers cannot provide the seekable file required by the game's flash storage.

1. Back up an existing save before importing or moving it.
2. Select the destination folder. The running game continues using its current save until the next process start.
3. Save through the game's normal Save menu, fully close the app, and reopen it. Returning from the folder picker or putting the app in the background is not a restart.

At startup, the selected folder is checked in this order:

| Contents | File used |
| --- | --- |
| `pokeemerald.sav` exists | `pokeemerald.sav` |
| Otherwise, `pokeemerald.srm` exists | `pokeemerald.srm` |
| Otherwise, exactly one `.sav` or `.srm` exists | That file, keeping its filename |
| No matching save exists | Create `pokeemerald.sav`, copying the previous save if available |
| Multiple matching files without a preferred name | Stop and ask for an unambiguous folder |

Extensions are matched without regard to case; the two preferred filenames above use their exact lowercase spelling. Only files directly inside the folder are considered. `.srm` support reads the same raw 128 KB flash data as `.sav`; it does not convert emulator savestates, archives, or unrelated game saves.

When switching between custom folders, an empty destination receives the last active custom save. When first switching from the default location, it receives the default save. Copying uses a temporary filename and then renames it; an existing destination save is loaded instead of overwritten by migration. Normal in-game saving then updates that selected file.

If access is lost, the app presents **Save folder unavailable**. Choose the folder again, choose another folder, or select **Original location**. Original location switches back to the app's existing default save; it does not copy the custom save back. To bring that progress back, copy the file yourself while the app is closed. The app does not silently switch to another save after a custom-folder error.

Without a custom folder, the default is `Android/data/com.pokeemerald.dualscreen/files/pokeemerald.sav`, falling back to `pokeemerald.srm` only when `.sav` is absent. Android may remove the default app directory during uninstall. Keep backups outside that directory.

## Using a Syncthing folder

Syncthing compatibility is the main reason for the custom-folder and `.srm` changes. Choose the local folder used by your Syncthing setup through **SAVE FOLDER**. If sharing a raw `.srm` save with another device, the app keeps that filename and updates it directly.

The app does not run Syncthing, merge conflicting saves, or reload a file while the game is running. Save in-game and fully close the app before switching devices, allow synchronization to finish, then launch on the other device. Avoid playing the same shared save on two devices at once, and keep backups. Apply the folder-selection rules above if the synchronized folder contains more than one game's save.

## More controller inputs

**STICK NAV** (OPTION → PLAY) is off by default. Turn it on to navigate the secondary-screen UI with a controller:

| Input | Action outside battle |
| --- | --- |
| L1 / R1 | Previous / next bottom-screen tab, wrapping at either end |
| Right analog stick | Move focus between available buttons or rows; hold to repeat |
| Y | Activate the focused option |

A dark-gray outline marks focus. Party and Bag use a thicker outline. Party detail pages hide the outline; Y returns to the party list. Returning from details restores the previously focused Pokémon. Activating a Bag pocket keeps focus on that pocket, including Berries. Map and Trainer Card currently have no selectable targets for the right stick.

The extra mappings and outline turn off during battles. Existing battle controls continue to handle battle menus. After leaving battle, extra navigation returns if the setting is still enabled. The preference survives app restarts; focus memory lasts for the lifetime of the secondary-screen view. Backgrounding the app or losing the secondary display can recreate that view.

Android controllers must expose L1/R1/Y and right-stick axes. The app checks Z/RZ, with RX/RY as a fallback. Touch interaction and the normal game controls remain available.
