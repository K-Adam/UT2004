"""Runs clang-tidy on the engine's own sources. Extra arguments go to run-clang-tidy, for example a file regex.

It configures the tidy preset into build/tidy, because the checks need a compile database Clang understands.
The preset has one configuration, or every file would be listed and checked once per configuration.
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

subprocess.run(['cmake', '--preset', 'tidy'], cwd=ROOT, check=True)
with open(DATABASE) as f:
    own = [e for e in json.load(f) if not any(s in e['file'] for s in SKIP)]
with open(DATABASE, 'w') as f:
    json.dump(own, f)
sys.exit(subprocess.call(['run-clang-tidy', '-quiet', '-p', BUILD] + sys.argv[1:]))
