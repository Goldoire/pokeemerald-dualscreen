"""Exercise live voxel settings on Linux, using a disposable copy of a save.

Requires a built native game, Xvfb and an existing save that can enter the field.
Output contains game screenshots/data: keep it local, outside the source tree.
This is a host integration check, not an Android device benchmark.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--save', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary, save, output = (p.resolve() for p in (args.binary, args.save, args.output))
    if not binary.is_file() or not save.is_file():
        parser.error('binary and save must be existing files')
    if output == save.parent:
        parser.error('use a separate output directory')
    output.mkdir(parents=True, exist_ok=True)
    log_path = output / 'runtime.log'
    with tempfile.TemporaryDirectory(prefix='emerald-runtime-') as temp:
        folder = Path(temp)
        shutil.copyfile(save, folder / 'pokeemerald.sav')
        (folder / 'pokeemerald.cfg').write_text(
            'voxelRenderer=1\nvoxelLighting=0\nvoxelZoom=1\n'
            'voxelBattles=1\nvsync=0\nwindowScale=4\n')
        fifo = folder / 'input'
        os.mkfifo(fifo)
        fd = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
        env = dict(os.environ, EMERALD_KEYFEED=str(fifo),
                   SDL_AUDIODRIVER='dummy', LIBGL_ALWAYS_SOFTWARE='1')
        with log_path.open('w') as log:
            proc = subprocess.Popen(
                ['xvfb-run', '-a', '-s', '-screen 0 1280x720x24',
                 'stdbuf', '-oL', '-eL', str(binary), '--voxel'],
                cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True)

            def check_alive():
                if proc.poll() is not None:
                    raise RuntimeError('Game exited:\n' + log_path.read_text()[-3000:])

            def send(command):
                check_alive()
                os.write(fd, (command + '\n').encode())

            def wait_file(path, timeout=15):
                deadline = time.monotonic() + timeout
                previous = 0
                while time.monotonic() < deadline:
                    check_alive()
                    size = path.stat().st_size if path.exists() else 0
                    if size > 100 and size == previous:
                        return
                    previous = size
                    time.sleep(.1)
                raise RuntimeError('No complete output ' + str(path) + '\n' +
                                   log_path.read_text()[-3000:])

            def capture(stem):
                shot, dump = folder / (stem + '.ppm'), folder / (stem + '.json')
                send('shot ' + str(shot))
                wait_file(shot)
                shutil.copyfile(shot, output / shot.name)
                try:
                    from PIL import Image
                except ImportError:
                    pass
                else:
                    # Format conversion only; preserve captured pixels exactly.
                    Image.open(shot).save(output / (stem + '.png'))
                send('dumpjson ' + str(dump))
                wait_file(dump)
                shutil.copyfile(dump, output / dump.name)
                data = json.loads(dump.read_text())
                assert data['gl']['error'] == 0, data['gl']
                return data

            def geometry(data):
                return [(i['world']['map'], i['world']['origin'], i['mesh']['verts'],
                         i['mesh']['ranges'], i['mesh']['hulls']) for i in data['instances']]

            try:
                deadline = time.monotonic() + 45
                while 'world rebuilt:' not in log_path.read_text() and time.monotonic() < deadline:
                    send('key 1 1\nkey 0 24')
                    time.sleep(.5)
                if 'world rebuilt:' not in log_path.read_text():
                    raise RuntimeError('Save did not enter the field; see ' + str(log_path))
                time.sleep(2)
                for place, group, number, x, z in [
                        ('littleroot', 0, 9, 10, 10), ('rustboro', 0, 3, 15, 15),
                        ('center', 11, 5, 7, 8)]:
                    send(f'warp {group} {number} {x} {z}')
                    time.sleep(5)
                    snapshots = []
                    for name, mode in [('art', 0), ('sun', 1), ('restored-art', 0)]:
                        send(f'setting 18 {mode}')
                        time.sleep(2)
                        data = capture(place + '-' + name)
                        assert data['instances'][0]['world']['map'] == [group, number]
                        assert data['lighting']['requested'] == mode
                        if mode == 0 or data['instances'][0]['world']['indoor']:
                            assert not data['lighting']['active']
                        elif place in ('littleroot', 'rustboro'):
                            assert data['lighting']['active']
                        snapshots.append(data)
                    assert geometry(snapshots[0]) == geometry(snapshots[1]) == geometry(snapshots[2])
                    print('PASS: live ART / SUN / restored ART, unchanged geometry, no GL errors:',
                          place, flush=True)
                # Return outdoors with sunlight requested, then reject invalid
                # setting values without wrapping them into a valid byte.
                send('setting 18 1\nwarp 0 9 10 10')
                time.sleep(5)
                send('setting 18 256\nsetting 18 -1\nsetting 15 1\nsetting 16 3')
                time.sleep(2)
                data = capture('angle35-zoom120')
                assert data['lighting'] == {'requested': 1, 'active': True}
                assert abs(data['camera']['pitch'] - 35) < .01
                assert abs(data['camera']['distance'] - 10.0 / 1.2) < .02
                shutil.copyfile(folder / 'pokeemerald.cfg', output / 'final.cfg')
                print('PASS: live angle/zoom, invalid settings rejected, indoor/outdoor reset', flush=True)
            finally:
                os.close(fd)
                # The native debug reader waits on its FIFO; end only this
                # isolated process group after the checks, leaving user saves alone.
                if proc.poll() is None:
                    os.killpg(proc.pid, signal.SIGTERM)
                    proc.wait(timeout=10)
    print('\n'.join(line for line in log_path.read_text().splitlines()
                    if 'sunlight:' in line or 'sunlight unavailable' in line))


if __name__ == '__main__':
    main()
