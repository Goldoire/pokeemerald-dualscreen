#!/usr/bin/env python3
"""Touch/lifecycle regression pass on the dedicated 1240×1080 emulator bottom screen.

Launch a debug APK and continue a copied save before running. This changes only
AVD preferences, then restores them; it never saves the game or touches a device.
Screenshots are evidence for a visual review, not an automatic appearance check.
"""
import argparse
import json
from pathlib import Path
import time

import emulator as emu


def wait_for(predicate, description, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = emu.snapshot()
        if predicate(state):
            return state
        time.sleep(0.3)
    raise AssertionError(f"Timed out: {description}")


def tap(x, y):
    emu.adb("shell", "input", "-d", emu.bottom_id(), "tap", str(x), str(y))


def swipe(x1, y1, x2, y2):
    emu.adb("shell", "input", "-d", emu.bottom_id(), "swipe", str(x1), str(y1), str(x2), str(y2), "600")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=emu.CACHE / "smoke")
    args = parser.parse_args()
    emu.require_ours()
    original = emu.snapshot()
    assert original.get("inGame") == 1 and original.get("inBattle") == 0, "Continue a copied save first"
    initial = original["platformSettings"]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checks = []

    def changed(setting, value, description):
        state = wait_for(lambda s: s["platformSettings"][setting] == value, description)
        assert state["inGame"] == 1
        checks.append(description)

    def unchanged(before, description):
        time.sleep(0.5)
        assert emu.snapshot()["platformSettings"] == before, description
        checks.append(description)

    try:
        tap(1120, 1020)  # Settings
        tap(180, 140)    # Display
        new_wide = 1 - initial[7]
        tap(900 if new_wide else 330, 650)
        changed(7, new_wide, "Widescreen touch choice reaches the native renderer")
        emu.capture(output / "display")

        tap(1050, 140)   # 3D
        renderer = 1 - initial[11]
        tap(900 if renderer else 330, 392)
        changed(11, renderer, "Renderer choice saves without restarting the live engine")
        assert emu.snapshot()["activeVoxelRenderer"] == original["activeVoxelRenderer"]
        checks.append("Pending renderer choice leaves the active renderer unchanged")
        camera = 1 - initial[14]
        tap(900 if camera else 330, 650)
        changed(14, camera, "Camera preset applies through touch")
        emu.capture(output / "restart-reminder")

        before = emu.snapshot()["platformSettings"]
        swipe(900, 780, 900, 300)
        unchanged(before, "Dragging across choice chips scrolls without changing a setting")
        emu.capture(output / "angle-zoom")
        # A row hidden behind the section header must not receive this tap.
        tap(900, 80)
        unchanged(before, "Clipped rows cannot be activated through the header")
        tap(710, 445)
        changed(15, 3, "45-degree camera angle applies through touch")
        tap(755, 705)
        changed(16, 2, "110-percent camera zoom applies through touch")
        emu.capture(output / "live-camera")
        # Scroll to the final two rows; distance deliberately reaches the clamp.
        swipe(900, 780, 900, 300)
        swipe(900, 780, 900, 300)
        lighting = 1 - initial[18]
        tap(900 if lighting else 330, 550)
        changed(18, lighting, "Lighting choice applies through touch")
        battles = 1 - initial[17]
        tap(900 if battles else 330, 810)
        changed(17, battles, "Voxel battle choice applies through touch")
        emu.capture(output / "lighting-battles")
        before = emu.snapshot()["platformSettings"]

        # Foreground/background recreates Presentation on the same display.
        emu.adb("shell", "input", "keyevent", "3")
        time.sleep(0.5)
        emu.launch()
        unchanged(before, "Background and resume retain the selected native preferences")
        time.sleep(1)
        tap(1120, 1020)
        tap(1050, 140)
        assert emu.snapshot()["activeVoxelRenderer"] == original["activeVoxelRenderer"]
        emu.capture(output / "resumed")

        # Logical IDs change after display recreation; all taps must rediscover it.
        old_id = emu.bottom_id()
        emu.adb("emu", "multidisplay", "del", "1")
        time.sleep(0.5)
        emu.adb("emu", "multidisplay", "add", "1", "1240", "1080", "320", "75")
        time.sleep(1)
        # This app attaches its Presentation on resume, so trigger that lifecycle.
        emu.adb("shell", "input", "keyevent", "3")
        time.sleep(0.5)
        emu.launch()
        new_id = emu.bottom_id()
        assert old_id != new_id, "Expected a new Android logical display ID"
        # Native snapshots remain available while the new Java window is being
        # laid out. Allow its first frame before tapping its fresh hit boxes.
        time.sleep(1)
        emu.capture(output / "display-recreated-ready")
        tap(1120, 1020)
        tap(180, 140)
        tap(900 if initial[7] else 330, 650)
        changed(7, initial[7], "Touch works after presentation display recreation")
        emu.capture(output / "display-recreated")

        tap(120, 1020)   # Party
        emu.capture(output / "party")
        tap(370, 1020)   # Map
        emu.capture(output / "map")
        current = emu.snapshot()
        assert len(current.get("party", [])) > 0 and current.get("player", {}).get("mapName")
        checks.append("Live party and current map are published after lifecycle tests")
    finally:
        for setting, value in enumerate(initial):
            if emu.snapshot()["platformSettings"][setting] != value:
                emu.broadcast("SETTING", {"setting": setting, "value": value})
        wait_for(lambda s: s["platformSettings"] == initial, "restore AVD preferences")

    result = {"checks": checks, "initialSettings": initial,
              "finalSettings": emu.snapshot()["platformSettings"],
              "map": original["player"]["mapName"], "bottomResolution": [1240, 1080]}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: {len(checks)} integration checks; screenshots and result: {output}")


if __name__ == "__main__":
    main()
