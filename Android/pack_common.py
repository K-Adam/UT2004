"""The file set and the ini overrides that the Android and web packers share."""
import os


def walk(src, dest, exts=None):
    for root, dirs, files in os.walk(src):
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if exts and ext not in exts:
                continue
            path = os.path.join(root, f)
            yield path, dest + '/' + os.path.relpath(path, src).replace(os.sep, '/')


def collect(source, scripts, prefix=''):
    """Lists (path, packed name) pairs. The scripts stay outside prefix, where Default.ini looks for them."""
    files = list(walk(os.path.join(source, 'System'), prefix + 'System'))
    files += walk(scripts, 'Scripts', {'.u', '.ucl'})
    files += walk(os.path.join(source, 'KarmaData'), prefix + 'KarmaData')
    files += walk(os.path.join(source, 'Foliage'), prefix + 'Foliage')
    files += walk(os.path.join(source, 'HDTextures'), prefix + 'HDTextures')
    for f in ('ut2004logo.bmp', 'Unreal.bmp'):
        files.append((os.path.join(source, 'Help', f), prefix + 'Help/' + f))
    return files


def read(src, rel, overrides):
    with open(src, 'rb') as f:
        data = f.read()
    for old, new in overrides.get(rel, []):
        assert old in data, (rel, old)
        data = data.replace(old, new)
    return data
