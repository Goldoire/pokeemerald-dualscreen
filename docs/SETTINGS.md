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

Existing configs default to Preset, 100% zoom and voxel battles enabled,
matching previous behavior. New settings are appended after the existing
IDs, and malformed/out-of-range JNI values are rejected.

Run the portable camera checks with:

```sh
python3 tools/research/run_voxel_tests.py
```

The settings Java and native changes have been compiled in an Android debug
APK. Camera tests cover original presets, invalid choices, zoom direction
and repeated-frame stability. Touch layout, camera occlusion and battle
entry/return still need handheld verification before a release.
