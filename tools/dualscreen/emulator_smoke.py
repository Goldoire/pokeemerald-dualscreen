#!/usr/bin/env python3
"""Touch/lifecycle regression pass on the dedicated 1240×1080 emulator bottom screen.

Launch a debug APK and continue a copied save before running. This changes only
AVD preferences and steps game options back to where they were; it never saves
the game or touches a device. Screenshots are evidence for a visual review,
not an automatic appearance check.
"""
import argparse
import json
from pathlib import Path
import time

import emulator as emu

# Bottom-screen geometry, mirroring DualScreenView.drawSettings().
W, H = 1240, 1080
SCALE = W / 440
PAD = W * 0.03
LINE_H = 15 * SCALE
TAB_BAR = H * 0.105
SECTION_TOP = LINE_H * 1.6 + PAD * 0.6
SECTION_H = LINE_H * 1.75
SECTION_W = (W - PAD * 2) / 4
HELP_TOP = H - TAB_BAR - PAD * 0.5 - LINE_H * 3.1
GRID_TOP = SECTION_TOP + SECTION_H + PAD * 0.6
GAP = PAD * 0.45
CELL_W = (W - PAD * 2 - GAP) / 2
CELL_H = min(LINE_H * 3.2, (HELP_TOP - PAD * 0.6 - GRID_TOP - GAP * 3) / 4)

GAME, DISPLAY, PLAY, THREE_D = range(4)
# Slots within each section, in settingRows order.
TEXT_SPEED, FRAME = 0, 5
WIDESCREEN = 0
VOXEL, CAMERA, ANGLE, ZOOM, LIGHTING, BATTLES = range(6)


def wait_for(predicate, description, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = emu.snapshot()
        if predicate(state):
            return state
        time.sleep(0.3)
    raise AssertionError(f"Timed out: {description}")


def tap(x, y):
    emu.adb("shell", "input", "-d", emu.bottom_id(), "tap", str(int(x)), str(int(y)))


def swipe(x1, y1, x2, y2):
    emu.adb("shell", "input", "-d", emu.bottom_id(), "swipe",
            str(int(x1)), str(int(y1)), str(int(x2)), str(int(y2)), "600")


def open_option_tab():
    tap(W * 4.5 / 5, H - TAB_BAR / 2)


def open_section(index):
    tap(PAD + index * SECTION_W + (SECTION_W - PAD * 0.3) / 2, SECTION_TOP + SECTION_H / 2)


def cell_center(slot):
    left = PAD + (slot % 2) * (CELL_W + GAP)
    top = GRID_TOP + (slot // 2) * (CELL_H + GAP)
    return left, top


def step(slot, direction=1):
    left, top = cell_center(slot)
    tap(left + CELL_W * (0.8 if direction > 0 else 0.2), top + CELL_H / 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=emu.CACHE / "smoke")
    args = parser.parse_args()
    emu.require_ours()
    original = emu.snapshot()
    assert original.get("inGame") == 1 and original.get("inBattle") == 0, "Continue a copied save first"
    initial = original["platformSettings"]
    initial_options = original["gameOptions"]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checks = []

    def changed(setting, value, description):
        state = wait_for(lambda s: s["platformSettings"][setting] == value, description)
        assert state["inGame"] == 1
        checks.append(description)

    def option(index, value, description):
        wait_for(lambda s: s["gameOptions"][index] == value, description)
        checks.append(description)

    def unchanged(before, description):
        time.sleep(0.5)
        assert emu.snapshot()["platformSettings"] == before, description
        checks.append(description)

    try:
        open_option_tab()
        open_section(GAME)
        step(TEXT_SPEED, 1)
        option(0, (initial_options[0] + 1) % 3, "TEXT SPEED steps forward from the bottom screen")
        step(TEXT_SPEED, -1)
        option(0, initial_options[0], "TEXT SPEED steps back")
        step(FRAME, 1)
        option(5, (initial_options[5] + 1) % 20, "FRAME steps forward and the field reloads it")
        emu.capture(output / "game")
        step(FRAME, -1)
        option(5, initial_options[5], "FRAME steps back")

        open_section(DISPLAY)
        new_wide = 1 - initial[7]
        step(WIDESCREEN, 1)
        changed(7, new_wide, "Widescreen touch choice reaches the native renderer")
        emu.capture(output / "display")

        open_section(THREE_D)
        if initial[11] == 0:
            before = emu.snapshot()["platformSettings"]
            step(CAMERA, 1)
            unchanged(before, "3D options stay locked while VOXEL 3D is off")
            emu.capture(output / "voxel-gated")
        renderer = 1 - initial[11]
        step(VOXEL, 1)
        changed(11, renderer, "Renderer choice saves without restarting the live engine")
        assert emu.snapshot()["activeVoxelRenderer"] == original["activeVoxelRenderer"]
        checks.append("Pending renderer choice leaves the active renderer unchanged")
        if renderer == 0:
            # Turning it off locks the rest; turn it back on to exercise them.
            step(VOXEL, 1)
            changed(11, 1, "Renderer choice turns back on")
        camera = 1 - initial[14]
        step(CAMERA, 1)
        changed(14, camera, "Camera preset applies through touch")
        emu.capture(output / "restart-reminder")

        before = emu.snapshot()["platformSettings"]
        left, top = cell_center(ANGLE)
        swipe(left + CELL_W * 0.8, top + CELL_H / 2, left + CELL_W * 0.8, top - CELL_H * 1.5)
        unchanged(before, "Dragging across option boxes does not change a setting")
        tap(W / 2, LINE_H * 0.8)
        unchanged(before, "The header does not activate anything")
        step(ANGLE, 1)
        changed(15, (initial[15] + 1) % 6, "Camera angle steps through touch")
        step(ZOOM, -1)
        changed(16, (initial[16] + 3) % 4, "Camera zoom steps back through touch")
        lighting = 1 - initial[18]
        step(LIGHTING, 1)
        changed(18, lighting, "Lighting choice applies through touch")
        battles = 1 - initial[17]
        step(BATTLES, 1)
        changed(17, battles, "Voxel battle choice applies through touch")
        emu.capture(output / "three-d")
        before = emu.snapshot()["platformSettings"]

        # Foreground/background recreates Presentation on the same display.
        emu.adb("shell", "input", "keyevent", "3")
        time.sleep(0.5)
        emu.launch()
        unchanged(before, "Background and resume retain the selected native preferences")
        time.sleep(1)
        open_option_tab()
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
        open_option_tab()
        open_section(DISPLAY)
        step(WIDESCREEN, 1)
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
              "gameOptions": emu.snapshot()["gameOptions"],
              "map": original["player"]["mapName"], "bottomResolution": [W, H]}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: {len(checks)} integration checks; screenshots and result: {output}")


if __name__ == "__main__":
    main()
