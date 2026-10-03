#!/usr/bin/env python3
"""Run the real ARMv7 Android app on a dedicated, two-display PC emulator."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
AVD = "emerald_dualscreen"
PORT = 5580
SERIAL = f"emulator-{PORT}"
PACKAGE = "com.pokeemerald.dualscreen"
IMAGE = "system-images;android-30;google_apis;x86_64"
SDK = Path(os.environ.get("ANDROID_HOME", os.environ.get("ANDROID_SDK_ROOT", str(Path.home() / "Android/sdk"))))
ADB = SDK / "platform-tools/adb"
CACHE = ROOT / ".cache/dualscreen-emulator"


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=kwargs.pop("timeout", 60),
                          **kwargs).stdout.strip()


def adb(*args):
    return run([ADB, "-s", SERIAL, *args])


def console(*args):
    output = adb("emu", *args)
    if "KO:" in output or not output.endswith("OK"):
        raise RuntimeError(output or "Emulator console did not answer")
    return output


def require_ours():
    lines = adb("emu", "avd", "name").splitlines()
    if not lines:
        raise RuntimeError(f"{SERIAL} did not answer the AVD identity check")
    name = lines[0].strip()
    if name != AVD:
        raise RuntimeError(f"{SERIAL} belongs to {name!r}, not {AVD!r}; refusing to operate on it")


def setup():
    manager = SDK / "cmdline-tools/latest/bin/avdmanager"
    image = SDK / "system-images/android-30/google_apis/x86_64/package.xml"
    if not image.exists() or not (SDK / "emulator/emulator").exists():
        raise RuntimeError(f'Install SDK packages first: sdkmanager "emulator" "platform-tools" "{IMAGE}"')
    home = Path(os.environ.get("ANDROID_AVD_HOME", str(Path.home() / ".android/avd")))
    config = home / f"{AVD}.avd/config.ini"
    if not config.exists():
        print(run([manager, "create", "avd", "-n", AVD, "-k", IMAGE,
                   "--device", "pixel_2"], input="no\n"))
    values = dict(line.split("=", 1) for line in config.read_text().splitlines() if "=" in line)
    values.update({"hw.lcd.width": "1920", "hw.lcd.height": "1080", "hw.lcd.density": "320",
                   "hw.initialOrientation": "landscape", "hw.ramSize": "4096",
                   "hw.gpu.enabled": "yes", "hw.gpu.mode": "host", "hw.keyboard": "yes",
                   "showDeviceFrame": "no", "skin.dynamic": "yes", "skin.name": "1920x1080",
                   "disk.dataPartition.size": "6G", "hw.display1.width": "1240",
                   "hw.display1.height": "1080", "hw.display1.density": "320",
                   # public + presentation + own-content + supports-touch
                   "hw.display1.flag": "75", "hw.display1.xOffset": "340",
                   "hw.display1.yOffset": "1080"})
    config.write_text("\n".join(f"{k}={v}" for k, v in values.items()) + "\n")
    print(f"Configured {AVD}: 1920×1080 top, 1240×1080 bottom")


def start(gui, gpu):
    devices = run([ADB, "devices"])
    if any(line.startswith(SERIAL + "\t") for line in devices.splitlines()):
        require_ours()
        print(f"Already running: {SERIAL}")
        return
    setup()
    print(run([SDK / "emulator/emulator", "-accel-check"]))
    CACHE.mkdir(parents=True, exist_ok=True)
    args = [SDK / "emulator/emulator", "-avd", AVD, "-port", str(PORT), "-no-audio",
            "-no-snapshot", "-no-boot-anim", "-gpu", gpu, "-memory", "4096",
            "-cores", "4", "-camera-back", "none", "-camera-front", "none",
            "-feature", "MultiDisplay", "-no-nested-warnings"]
    args += ["-scale", "0.4"] if gui else ["-no-window"]
    with (CACHE / "emulator.log").open("w") as output:
        proc = subprocess.Popen([str(a) for a in args], stdout=output, stderr=subprocess.STDOUT,
                                start_new_session=True)
    print(f"Started PID {proc.pid}; log: {CACHE / 'emulator.log'}", flush=True)
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"Emulator exited; inspect {CACHE / 'emulator.log'}")
        try:
            if adb("shell", "getprop", "sys.boot_completed") == "1":
                require_ours()
                adb("shell", "am", "broadcast", "-a", "com.android.emulator.multidisplay.START",
                    "-n", "com.android.emulator.multidisplay/.MultiDisplayServiceReceiver", "--user", "0")
                # Set explicitly as older emulator versions ignore the AVD flag.
                for attempt in range(15):
                    try:
                        console("multidisplay", "add", "1", "1240", "1080", "320", "75")
                        bottom_id()
                        break
                    except RuntimeError:
                        if attempt == 14:
                            raise
                        time.sleep(1)
                print(f"Ready: {SERIAL}")
                return
        except subprocess.CalledProcessError:
            pass
        time.sleep(2)
    raise RuntimeError("Boot timed out; inspect the emulator log")


def launch():
    print(adb("shell", "am", "start", "-n", f"{PACKAGE}/com.pokeemerald.experimental.RomGateActivity"))


def install(apk):
    print(adb("install", "-r", apk.resolve()))
    launch()


def load_save(source):
    # The user explicitly invokes this command to replace this AVD's save only.
    if source.stat().st_size not in (131072, 131088):
        raise RuntimeError("Expected a 128 KB flash save (optional 16-byte emulator footer)")
    print(adb("root"))
    run([ADB, "-s", SERIAL, "wait-for-device"])
    require_ours()
    adb("shell", "am", "force-stop", PACKAGE)
    adb("push", source.resolve(), "/data/local/tmp/emerald-simulator.sav")
    # Bypass Android 11's shell scoped-storage mount, using rooted AVD only.
    dest = f"/data/media/0/Android/data/{PACKAGE}/files"
    adb("shell", "mkdir", "-p", dest)
    adb("shell", "cp", "/data/local/tmp/emerald-simulator.sav", f"{dest}/pokeemerald.sav")
    uid = adb("shell", "run-as", PACKAGE, "id", "-u")
    adb("shell", "chown", f"{uid}:{uid}", f"{dest}/pokeemerald.sav")
    adb("shell", "chmod", "660", f"{dest}/pokeemerald.sav")
    adb("shell", "rm", "/data/local/tmp/emerald-simulator.sav")
    launch()
    print("Copied save into the dedicated AVD; source unchanged")


def bottom_id():
    dump = adb("shell", "dumpsys", "display")
    for line in dump.splitlines():
        if 'DisplayInfo{"Emulator 2D Display"' in line and "FLAG_PRESENTATION" in line:
            match = re.search(r"displayId (\d+)", line)
            if match:
                return match.group(1)
    raise RuntimeError("No emulator presentation display found")


def capture(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(("top", "bottom")):
        with tempfile.TemporaryDirectory(dir=directory) as tmp:
            adb("emu", "screenrecord", "screenshot", "--display", str(index), tmp)
            shots = list(Path(tmp).glob("*.png"))
            if len(shots) != 1:
                raise RuntimeError(f"No unambiguous screenshot for display {index}")
            target = directory / f"{name}.png"
            shots[0].replace(target)
            print(target)


def broadcast(action, extras):
    args = ["shell", "am", "broadcast", "-a", f"{PACKAGE}.DEBUG_{action}", "-p", PACKAGE]
    for key, value in extras.items():
        args += ["--ei", key, str(value)]
    return adb(*args)


def snapshot():
    output = broadcast("SNAPSHOT", {})
    match = re.search(r'data="(.*)"', output)
    if not match:
        raise RuntimeError("No snapshot returned; install a current debug APK and launch the game")
    return json.loads(match.group(1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("setup")
    p = sub.add_parser("start")
    p.add_argument("--gui", action="store_true", help="show the emulator through Linux/WSLg")
    p.add_argument("--gpu", choices=("host", "software", "swiftshader", "swangle", "lavapipe"), default="host")
    p = sub.add_parser("install")
    p.add_argument("apk", type=Path, nargs="?", default=ROOT / "android/app/build/outputs/apk/debug/app-debug.apk")
    p = sub.add_parser("load-save", help="replace only the dedicated AVD's save with a copy")
    p.add_argument("save", type=Path)
    p = sub.add_parser("capture")
    p.add_argument("directory", type=Path, nargs="?", default=CACHE / "captures")
    p = sub.add_parser("tap")
    p.add_argument("x", type=int)
    p.add_argument("y", type=int)
    p = sub.add_parser("key", help="GBA button mask: A=1 B=2 Start=8 Right=16 Left=32 Up=64 Down=128")
    p.add_argument("mask", type=int)
    p.add_argument("--frames", type=int, default=2)
    p = sub.add_parser("setting")
    p.add_argument("id", type=int)
    p.add_argument("value", type=int)
    for name in ("launch", "stop", "info", "snapshot"):
        sub.add_parser(name)
    args = parser.parse_args()
    if args.command == "setup":
        setup()
        return
    if args.command == "start":
        start(args.gui, args.gpu)
        return
    require_ours()
    if args.command == "install":
        install(args.apk)
    elif args.command == "load-save":
        load_save(args.save)
    elif args.command == "launch":
        launch()
    elif args.command == "stop":
        print(console("kill"))
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if not any(line.startswith(SERIAL + "\t") for line in run([ADB, "devices"]).splitlines()):
                return
            time.sleep(0.5)
        raise RuntimeError(f"{SERIAL} has not disconnected yet")
    elif args.command == "capture":
        capture(args.directory.resolve())
    elif args.command == "tap":
        print(adb("shell", "input", "-d", bottom_id(), "tap", str(args.x), str(args.y)))
    elif args.command == "key":
        print(broadcast("KEYS", {"mask": args.mask, "frames": args.frames, "release": 2}))
    elif args.command == "setting":
        print(broadcast("SETTING", {"setting": args.id, "value": args.value}))
    elif args.command == "snapshot":
        print(json.dumps(snapshot(), indent=2))
    elif args.command == "info":
        print(f"AVD: {AVD}\nADB: {SERIAL}\nBottom logical display: {bottom_id()}")
        print(adb("shell", "getprop", "ro.product.cpu.abilist"))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stderr, file=sys.stderr)
        sys.exit(1)
