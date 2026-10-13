# Settings in development builds

The lower-screen settings page has Display, Play, Audio and 3D sections.
Tap an individual choice to select it. Scrolling or cancelling a gesture
does not apply a choice, and hidden rows cannot receive taps through the
header or tab bar. Settings keep the game's font and menu colors.

Changes are saved to the existing port config. Android sends validated
setting requests to the SDL render thread, which applies display changes
and writes the config. Repeated requests before its next iteration are
coalesced. Neither these options nor their layout change the save format.

The experimental 3D section provides:

- **Voxel renderer:** applies when the app restarts. The restart reminder
  remains visible after changing other options, until the original renderer
  choice is restored or the app restarts.
- **Camera:** Play retains the close 55-degree camera; Diorama retains the
  wider 36-degree camera.
- **Camera angle:** Preset keeps that camera's original angle. Individual
  choices are 35, 40, 45, 55 or 62 degrees.
- **Camera zoom:** 90, 100, 110 or 120 percent. Higher zoom brings the camera
  closer. Angle and zoom update during overworld play; they do not change
  the fitted battle cameras.
- **Voxel battles:** permits battles staged on the map. Off retains the
  original battle scenery while keeping the voxel overworld.
- **Lighting:** ART retains original shading. SUN uses a fixed northwest
  light outdoors, geometry-cast terrain shadows and terrain-clipped character
  silhouette shadows. This remains opt-in while device performance is checked.

Existing configs default to Preset, 100% zoom and voxel battles enabled,
matching previous behavior. New settings are appended after the existing
IDs, and malformed/out-of-range JNI values are rejected.

Run the portable camera, lighting, shadow clipping and caster checks with:

```sh
python3 tools/research/run_voxel_tests.py
python3 tools/research/run_voxel_tests.py --sanitize
```

The settings Java and native changes have been compiled in an Android debug
APK. Portable tests cover original camera presets, invalid choices, zoom
direction and repeated-frame stability. Linux runtime checks exercise live
angle/zoom, lighting changes, indoor/outdoor transitions, rejected invalid
settings and unchanged world geometry. See [lighting validation](VOXEL_LIGHTING.md).
The [PC Android emulator pass](EMULATOR.md) exercises real bottom-screen choices,
scrolling, clipping, background/resume and display recreation. Renderer reminders
compare the requested setting with the active native renderer, so recreating a
Presentation retains a pending restart notice.
Touch layout, camera occlusion, lifecycle restoration and battle entry/return
still need handheld verification before a release.

The **MORE** section of the settings page holds the save folder and controller options.

## Save folder

**CHANGE SAVE FILE PATH** opens Android's folder picker. Select a folder with persistent read/write access. A local folder is recommended: stream-only cloud providers cannot provide the seekable file required by the game's flash storage.

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

Syncthing compatibility is the main reason for the custom-folder and `.srm` changes. Choose the local folder used by your Syncthing setup through **CHANGE SAVE FILE PATH**. If sharing a raw `.srm` save with another device, the app keeps that filename and updates it directly.

The app does not run Syncthing, merge conflicting saves, or reload a file while the game is running. Save in-game and fully close the app before switching devices, allow synchronization to finish, then launch on the other device. Avoid playing the same shared save on two devices at once, and keep backups. Apply the folder-selection rules above if the synchronized folder contains more than one game's save.

## More controller inputs

**MORE CONTROLLER INPUTS** is off by default. Turn it on to navigate the secondary-screen UI with a controller:

| Input | Action outside battle |
| --- | --- |
| L1 / R1 | Previous / next bottom-screen tab, wrapping at either end |
| Right analog stick | Move focus between available buttons or rows; hold to repeat |
| Y | Activate the focused option |

A dark-gray outline marks focus. Party and Bag use a thicker outline. Party detail pages hide the outline; Y returns to the party list. Returning from details restores the previously focused Pokémon. Activating a Bag pocket keeps focus on that pocket, including Berries. Map and Trainer Card currently have no selectable targets for the right stick.

The extra mappings and outline turn off during battles. Existing battle controls continue to handle battle menus. After leaving battle, extra navigation returns if the setting is still enabled. The preference survives app restarts; focus memory lasts for the lifetime of the secondary-screen view. Backgrounding the app or losing the secondary display can recreate that view.

Android controllers must expose L1/R1/Y and right-stick axes. The app checks Z/RZ, with RX/RY as a fallback. Touch interaction and the normal game controls remain available.
