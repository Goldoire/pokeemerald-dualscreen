"""Run portable voxel tests without a ROM, GL context, Android SDK or device."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
flags = ['gcc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
         '-DPLATFORM_SDL2', '-DNATIVE_LINUX', '-ffunction-sections',
         '-Xlinker', '--gc-sections', '-Isrc/platform/voxel']
tests = {'camera': ['tests/voxel/camera_test.c', 'src/platform/voxel/voxel_camera.c']}
with tempfile.TemporaryDirectory(prefix='emerald-voxel-tests-') as temp:
    for name, sources in tests.items():
        exe = str(Path(temp) / name)
        subprocess.run(flags + sources + ['-lm', '-o', exe], cwd=root, check=True)
        subprocess.run([exe], cwd=root, check=True)
