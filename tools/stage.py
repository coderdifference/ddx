#!/usr/bin/env python3
"""Stage a DDX resource package or generate a client mod manifest."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def manifest(folder, name):
    output = folder / f'{name}_modfiles.txt'
    paths = sorted(p.relative_to(folder).as_posix() for p in folder.rglob('*')
                   if p.is_file() and p != output and p.name != 'project.xml'
                   and not any(part.startswith('.') for part in p.relative_to(folder).parts))
    output.write_text('\n'.join(paths) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['macos', 'windows'])
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest-only', type=Path, metavar='MOD_FOLDER')
    args = parser.parse_args()
    if args.manifest_only:
        folder = args.manifest_only.resolve()
        if not folder.is_dir():
            parser.error('mod folder does not exist')
        manifest(folder, folder.name)
        return
    if not args.platform or not args.output:
        parser.error('--platform and --output are required for staging')
    artifacts = ([ROOT / 'engine_hooks/build/ddx.dylib'] if args.platform == 'macos' else
                 [ROOT / 'engine_hooks/build/win/ddx.dll', ROOT / 'engine_hooks/build/win/ddxlaunch.exe'])
    for artifact in artifacts:
        if not artifact.is_file():
            parser.error(f'build artifact missing: {artifact}')
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('output must be absent or an empty directory')
    shutil.copytree(ROOT / 'engine_hooks/mod', output, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('*.loc2', '*_modfiles.txt', 'bin', '.DS_Store'))
    (output / 'bin').mkdir()
    for artifact in artifacts:
        shutil.copy2(artifact, output / 'bin' / artifact.name)
    subprocess.run([sys.executable, str(ROOT / 'tools/localization.py'),
                    str(output / 'localization/DarkestDungeonExtender.string_table.xml')], check=True)
    manifest(output, 'DarkestDungeonExtender')
    print(f'Staged {args.platform} package: {output}')


if __name__ == '__main__':
    main()
