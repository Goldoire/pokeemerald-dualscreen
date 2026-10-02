"""Run portable voxel tests without a ROM, GL context, Android SDK or device."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
flags = ['gcc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
         '-DPLATFORM_SDL2', '-DNATIVE_LINUX', '-ffunction-sections',
         '-Xlinker', '--gc-sections', '-Isrc/platform/voxel']
if '--sanitize' in sys.argv:
    flags += ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
tests = {
    'camera': ['tests/voxel/camera_test.c', 'src/platform/voxel/voxel_camera.c'],
    'light': ['tests/voxel/light_test.c', 'src/platform/voxel/voxel_light.c'],
    'shadow': ['tests/voxel/shadow_test.c', 'src/platform/voxel/voxel_shadow.c'],
    'caster': ['tests/voxel/caster_test.c', 'src/platform/voxel/voxel_caster.c', 'src/platform/voxel/voxel_light.c'],
    'light_grid': ['tests/voxel/light_grid_test.c', 'src/platform/voxel/voxel_light_grid.c'],
}
with tempfile.TemporaryDirectory(prefix='emerald-voxel-tests-') as temp:
    for name, sources in tests.items():
        exe = str(Path(temp) / name)
        subprocess.run(flags + sources + ['-lm', '-o', exe], cwd=root, check=True)
        subprocess.run([exe], cwd=root, check=True)
