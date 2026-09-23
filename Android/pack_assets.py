"""Packs the game data into APK assets as one .xz per file plus manifest.txt.

The APK zip only offers deflate, xz at its strongest preset makes the APK about a fifth smaller.
SDLLaunch unpacks the files on first run, because the engine reads them with fopen.
"""
import argparse
import hashlib
import lzma
import os
import sys
from concurrent.futures import ProcessPoolExecutor

SKIP_EXT = {'.bat', '.sh', '.pl', '.cpp', '.linux'}
SKIP_DIRS = {'EditorRes', 'UDE'}
# Phone defaults: gyro aim on, effects phone GPUs can't afford off, VSync instead of the per-frame GPU wait.
OVERRIDES = {'UT2004/System/Default.ini': [
    (b'%s=%s' % (k, old), b'%s=%s' % (k, new)) for k, old, new in (
        (b'PadGyro', b'False', b'True'),
        (b'ReduceMouseLag', b'True', b'False'),
        (b'UseVSync', b'False', b'True'),
        (b'Antialiasing', b'True', b'False'),
        (b'AmbientOcclusion', b'True', b'False'),
        (b'ContactShadows', b'True', b'False'),
        (b'ScreenSpaceReflections', b'True', b'False'),
        (b'DepthOfField', b'True', b'False'),
        (b'VolumetricFog', b'True', b'False'),
        (b'ResolutionScale', b'100', b'70'))]}


def walk(src, dest, exts=None):
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in SKIP_EXT or (exts and ext not in exts):
                continue
            path = os.path.join(root, f)
            yield path, dest + '/' + os.path.relpath(path, src).replace(os.sep, '/')


def compress(job):
    src, rel, out = job
    with open(src, 'rb') as f:
        data = f.read()
    for old, new in OVERRIDES.get(rel, []):
        assert old in data, (rel, old)
        data = data.replace(old, new)
    data = lzma.compress(data, preset=9 | lzma.PRESET_EXTREME)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out + '.tmp', 'wb') as f:
        f.write(data)
    os.replace(out + '.tmp', out)
    # The source's time, so a swapped in older file still counts as changed.
    st = os.stat(src)
    os.utime(out, ns=(st.st_atime_ns, st.st_mtime_ns))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True)
    p.add_argument('--source', required=True, help='repository root')
    p.add_argument('--scripts', required=True, help='folder with the compiled .u and .ucl files')
    p.add_argument('--assets', required=True, help='folder with Maps, Textures and the other content folders')
    a = p.parse_args()

    # Laid out like build/: Default.ini finds the packages at ../Maps and the scripts at ../../Scripts.
    files = list(walk(os.path.join(a.source, 'System'), 'UT2004/System'))
    files += walk(a.scripts, 'Scripts', {'.u', '.ucl'})
    for d in ('Animations', 'Maps', 'Music', 'Sounds', 'StaticMeshes', 'Textures'):
        files += walk(os.path.join(a.assets, d), 'UT2004/' + d)
    files += walk(os.path.join(a.source, 'KarmaData'), 'UT2004/KarmaData')
    for f in ('ut2004logo.bmp', 'Unreal.bmp'):
        files.append((os.path.join(a.source, 'Help', f), 'UT2004/Help/' + f))
    if not any(rel.startswith('Scripts/') for _, rel in files):
        sys.exit('No script packages in %s, build ucc_make with a desktop preset first' % a.scripts)

    jobs = []
    stamp = hashlib.sha256(repr(OVERRIDES).encode())
    for src, rel in files:
        st = os.stat(src)
        stamp.update(('%s %d %d\n' % (rel, st.st_size, st.st_mtime_ns)).encode())
        out = os.path.join(a.out, rel + '.xz')
        # Overridden files are small and follow edits to this script.
        if rel in OVERRIDES or not os.path.exists(out) or os.stat(out).st_mtime_ns != st.st_mtime_ns:
            jobs.append((src, rel, out))

    # Drops files that left the set, so the APK doesn't keep stale data.
    keep = {os.path.normcase(os.path.join(a.out, rel + '.xz')) for _, rel in files}
    for root, _, names in os.walk(a.out):
        for n in names:
            path = os.path.join(root, n)
            if n.endswith('.xz') and os.path.normcase(path) not in keep:
                os.remove(path)

    print('Compressing %d of %d files' % (len(jobs), len(files)), flush=True)
    with ProcessPoolExecutor() as pool:
        for i, _ in enumerate(pool.map(compress, jobs), 1):
            if i % 50 == 0 or i == len(jobs):
                print('  %d/%d' % (i, len(jobs)), flush=True)

    manifest = stamp.hexdigest() + '\n' + ''.join(rel + '\n' for _, rel in files)
    path = os.path.join(a.out, 'manifest.txt')
    if not os.path.exists(path) or open(path, newline='').read() != manifest:
        with open(path, 'w', newline='\n') as f:
            f.write(manifest)


if __name__ == '__main__':
    main()
