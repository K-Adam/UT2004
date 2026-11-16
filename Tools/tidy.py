"""Runs clang-tidy on the engine's own sources. Extra arguments go to run-clang-tidy, for example a file regex.

It configures the linux64 preset with Clang into build/tidy, because the checks need a compile database Clang understands.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'tidy')
DATABASE = os.path.join(BUILD, 'compile_commands.json')
# Fetched dependencies, the physics submodule and vendored code are not ours to fix.
SKIP = ('/_deps/', '/metoolkit-karma/', '/Tools/NVTriStrip/')

# One configuration, or every file is listed and checked once per configuration.
subprocess.run(['cmake', '--preset', 'linux64', '-B', BUILD, '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
                '-DCMAKE_CONFIGURATION_TYPES=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], cwd=ROOT, check=True)
with open(DATABASE) as f:
    own = [e for e in json.load(f) if not any(s in e['file'] for s in SKIP)]
with open(DATABASE, 'w') as f:
    json.dump(own, f)
sys.exit(subprocess.call(['run-clang-tidy', '-quiet', '-p', BUILD] + sys.argv[1:]))
