"""Packs System, the script packages, KarmaData and the splash bitmaps into an Emscripten preload package.

The files keep the layout of a build folder (/System, /Scripts, ...), so Default.ini's paths work unchanged.
The content folders are not packed, the page downloads them.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Android'))
from pack_common import collect, read  # noqa: E402

# Browsers only enter fullscreen from a click or key press.
OVERRIDES = {'System/Default.ini': [(b'StartupFullscreen=True', b'StartupFullscreen=False')]}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True, help='folder for system.data and system.js')
    p.add_argument('--source', required=True, help='repository root')
    p.add_argument('--scripts', required=True, help='folder with the compiled .u and .ucl files')
    p.add_argument('--packager', required=True, help="Emscripten's tools/file_packager.py")
    a = p.parse_args()

    files = collect(a.source, a.scripts)

    os.makedirs(a.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as stage:
        for src, rel in files:
            dest = os.path.join(stage, rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            if rel in OVERRIDES:
                with open(dest, 'wb') as f:
                    f.write(read(src, rel, OVERRIDES))
            else:
                shutil.copyfile(src, dest)
        # The loader embeds the target name as given, so it runs from the output folder with a bare name.
        subprocess.run([sys.executable, a.packager, 'system.data', '--preload', stage.replace('@', '@@') + '@/',
                        '--use-preload-cache', '--js-output=system.js', '--no-node'], cwd=a.out, check=True)


if __name__ == '__main__':
    main()
