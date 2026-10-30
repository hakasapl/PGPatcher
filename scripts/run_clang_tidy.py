#!/usr/bin/env python3
"""Run clang-tidy over every project translation unit and fail on any diagnostic.

Run by the build workflow once it has a build tree, and by hand for the same check
locally. Not a pre-commit hook: clang-tidy replays the real compile commands, so it needs
a configured *and* built tree, which pre-commit.ci does not have.

    python scripts/run_clang_tidy.py [--build-dir DIR] [--jobs N]

The clang-tidy version is pinned in scripts/clang-tidy-version.txt. CI downloads that LLVM
release from llvm-project and puts it first on PATH; locally, install the same release and
do the same. Whatever `clang-tidy` is found on PATH must match the pin (the Visual Studio
developer environment puts its own, older LLVM there), or the runner refuses to continue.

Three quirks of this project are handled here:

* `--config-file` is passed explicitly. Without it clang-tidy walks up from each source
  file looking for a .clang-tidy, and for anything that pulls in nifly it finds
  external/nifly/.clang-tidy, which sets the long-removed `AnalyzeTemporaryDtors` key and
  makes clang-tidy abort with "invalid argument".

* Diagnostics from outside the project are dropped. fmt 11.0.2 fails its own consteval
  format-string check under clang 19 and newer, so every TU that logs anything reports
  errors in fmt/spdlog headers. Those are not actionable here, and the header filter does
  not apply to compiler diagnostics.

* MSVC precompiled headers are left out. PGLite builds with `target_precompile_headers`, so
  its compile commands name an MSVC .pch with /Yu and /Fp, which clang cannot read: every
  such TU failed with "is not a valid PCH file" and was analyzed no further. A tree with
  those flags is checked against a copy of its compile commands without them; the /FI of
  the precompiled header stays, so each TU parses that header itself.

A clang-tidy error that names no file, or a fatal error anywhere, fails the run: either one
means the TU was not analyzed in full, which must not pass as a clean result.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION_FILE = os.path.join(REPO_ROOT, 'scripts', 'clang-tidy-version.txt')

# Directories holding first-party code. Everything else (external/, the build tree,
# vcpkg_installed/) is somebody else's problem.
PROJECT_DIRS = ('PGLib', 'PGPatcher', 'PGMutagen', 'PGTools', 'PGTests', 'PGLite')

# PGLite is a second configure (buildRelease.ps1 puts it under the root build tree); its
# compile commands are checked alongside the root ones when that tree exists.
SECONDARY_BUILD_DIRS = ('PGLite',)

# The translation units clang-tidy can check.
SOURCE_EXTENSIONS = ('.c', '.cc', '.cpp', '.cxx')

# Where a configured build tree usually lives, best first.
BUILD_DIR_CANDIDATES = (
    'buildRelease',
    'build',
    os.path.join('out', 'build'),
)

HEADER_FILTER = r'(' + '|'.join(PROJECT_DIRS) + r')[/\\](include|src)[/\\]'
DIAG_LINE = re.compile(
    r'^(?P<path>[A-Za-z]:[\\/].+?|/.+?):(\d+):(\d+):\s+(?P<severity>fatal error|error|warning):')

# clang-tidy's own failures: a bad config, an unusable compilation database, a crash, a
# precompiled header it cannot read. These carry no file position, or point at a .clang-tidy
# rather than at our code, so the path filter below would silently swallow them. "Error while
# processing <file>." is excluded on purpose: every clean TU already emits it because of the
# fmt/spdlog errors.
TOOL_FAILURE = re.compile(r'^(?:Error(?! while processing\b)|(?:fatal )?error: )')

# The MSVC precompiled header options of a compile command: /Yc and /Yu name the header, /Fp
# the .pch file. A token may be quoted as a whole or in its path.
PCH_OPTION = re.compile(r'(?<!\S)(?:"[/-](?:Yc|Yu|Fp)[^"]*"|[/-](?:Yc|Yu|Fp)(?:"[^"]*"|\S)*)')


def pinned_version() -> str:
    try:
        with open(VERSION_FILE, encoding='utf-8') as handle:
            version = handle.read().strip()
    except FileNotFoundError:
        sys.exit(f'error: {VERSION_FILE} not found')
    if not version:
        sys.exit(f'error: {VERSION_FILE} is empty')
    return version


def find_clang_tidy(explicit: str | None, expected: str) -> str:
    path = shutil.which(explicit or 'clang-tidy')
    if not path:
        sys.exit(f'error: {explicit or "clang-tidy"} not found; install LLVM {expected} and put its bin directory on PATH')
    return path


def secondary_compile_commands(compile_commands: str) -> list[str]:
    """The compile commands of the secondary configures under the same build tree, when they exist."""
    build_dir = os.path.dirname(compile_commands)
    found = []
    for name in SECONDARY_BUILD_DIRS:
        path = os.path.join(build_dir, name, 'compile_commands.json')
        if os.path.isfile(path):
            found.append(path)
    return found


def find_compile_commands(explicit: str | None) -> str:
    if explicit:
        path = explicit if explicit.endswith('.json') else os.path.join(explicit, 'compile_commands.json')
        if not os.path.isfile(path):
            sys.exit(f'error: no compile_commands.json at {path}')
        return path
    for candidate in BUILD_DIR_CANDIDATES:
        path = os.path.join(REPO_ROOT, candidate, 'compile_commands.json')
        if os.path.isfile(path):
            return path
    # last resort: anything one level deep under build/ or out/
    for root in ('build', 'out'):
        for dirpath, _dirnames, filenames in os.walk(os.path.join(REPO_ROOT, root)):
            if 'compile_commands.json' in filenames:
                return os.path.join(dirpath, 'compile_commands.json')
    sys.exit(
        'error: no compile_commands.json found.\n'
        '  Configure a build first, for example:\n'
        '      cmake -B buildRelease -S . -G Ninja '
        '-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake\n'
        '  then build it once so generated headers exist, or pass --build-dir.')


def is_project_file(path: str) -> bool:
    try:
        rel = os.path.relpath(path, REPO_ROOT)
    except ValueError:
        return False            # different drive
    if rel.startswith('..'):
        return False
    parts = rel.replace('\\', '/').split('/')
    return parts[0] in PROJECT_DIRS


def translation_units(compile_commands: str) -> list[str]:
    with open(compile_commands, encoding='utf-8') as handle:
        entries = json.load(handle)
    # The compile commands list the resource scripts as well (PGPatcher's icon.rc), which clang cannot compile.
    units = {os.path.normpath(e['file']) for e in entries
             if is_project_file(e['file']) and e['file'].lower().endswith(SOURCE_EXTENSIONS)}
    return sorted(units)


def without_precompiled_headers(compile_commands: str, scratch: str) -> str | None:
    """Writes the compile commands without their MSVC precompiled header options into scratch.

    Returns the directory to pass to clang-tidy's -p, or None when no command has those options
    and the tree's own compile commands serve as they are.
    """
    with open(compile_commands, encoding='utf-8') as handle:
        entries = json.load(handle)

    changed = False
    for entry in entries:
        if 'command' in entry:
            command = PCH_OPTION.sub('', entry['command'])
            changed |= command != entry['command']
            entry['command'] = command
        if 'arguments' in entry:
            arguments = [a for a in entry['arguments'] if not PCH_OPTION.fullmatch(a)]
            changed |= len(arguments) != len(entry['arguments'])
            entry['arguments'] = arguments
    if not changed:
        return None

    os.makedirs(scratch, exist_ok=True)
    with open(os.path.join(scratch, 'compile_commands.json'), 'w', encoding='utf-8') as handle:
        json.dump(entries, handle)
    return scratch


def run_one(clang_tidy: str, database_dir: str, unit: str) -> tuple[str, list[str]]:
    result = subprocess.run(
        [clang_tidy, '-p', database_dir,
         '--config-file', os.path.join(REPO_ROOT, '.clang-tidy'),
         '--header-filter', HEADER_FILTER,
         '--quiet', unit],
        capture_output=True, text=True, errors='replace', check=False)

    # Keep a diagnostic and its following context lines only when it points at our code.
    kept: list[str] = []
    keeping = False
    failures: list[str] = []
    for line in (result.stdout + result.stderr).splitlines():
        if TOOL_FAILURE.match(line):
            failures.append(line)
        match = DIAG_LINE.match(line)
        if match:
            keeping = is_project_file(match.group('path'))
            # A fatal error ends the parse, so the rest of the TU went unchecked, wherever it is.
            if match.group('severity') == 'fatal error' and not keeping:
                failures.append(line)
        elif line.startswith('Error while processing') or not line.strip():
            keeping = False
        if keeping:
            kept.append(line)

    # clang-tidy exits 1 for the fmt/spdlog errors on every clean TU, so a non-zero status
    # on its own means nothing here. Anything above 1 is a crash rather than diagnostics.
    if result.returncode > 1:
        failures.append(f'clang-tidy exited {result.returncode}')
    if failures:
        kept += [f'{unit}: clang-tidy could not complete:'] + [f'  {f}' for f in failures]
    return unit, kept


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', help='directory containing compile_commands.json')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--clang-tidy', default=os.environ.get('CLANG_TIDY'),
                        help='binary to run instead of the one on PATH (must still match the pin)')
    args = parser.parse_args()

    expected = pinned_version()
    clang_tidy = find_clang_tidy(args.clang_tidy, expected)

    compile_commands = find_compile_commands(args.build_dir)
    # Every translation unit is checked against the build tree whose compile commands list it.
    units_by_build_dir: dict[str, list[str]] = {}
    commands_by_build_dir: dict[str, str] = {}
    for commands in [compile_commands] + secondary_compile_commands(compile_commands):
        found = translation_units(commands)
        if found:
            units_by_build_dir[os.path.dirname(commands)] = found
            commands_by_build_dir[os.path.dirname(commands)] = commands
    if not units_by_build_dir:
        sys.exit(f'error: {compile_commands} lists no project sources; is the build tree stale?')
    units = [unit for found in units_by_build_dir.values() for unit in found]

    version = subprocess.run([clang_tidy, '--version'], capture_output=True, text=True,
                             check=False).stdout.strip().splitlines()
    print(f'clang-tidy: {clang_tidy}')
    for line in version[:2]:
        print(f'  {line.strip()}')
    found = re.search(r'LLVM version (\S+)', '\n'.join(version))
    if not found or found.group(1) != expected:
        sys.exit(f'error: clang-tidy {found.group(1) if found else "(unknown version)"} does not match the pinned '
                 f'{expected}; install that LLVM release and put its bin directory first on PATH')
    # A redrawn counter is only useful on a terminal; in a CI log it would be one long line.
    progress = sys.stdout.isatty()

    # Keyed by build tree and unit: a PGLib unit is checked once per tree and may fail in one tree only.
    failed: dict[tuple[str, str], list[str]] = {}
    # The compile commands rewritten without precompiled headers live for the run.
    with tempfile.TemporaryDirectory(prefix='clang-tidy-') as scratch:
        database_dirs: dict[str, str] = {}
        for index, (tree, found) in enumerate(units_by_build_dir.items()):
            rewritten = without_precompiled_headers(commands_by_build_dir[tree], os.path.join(scratch, str(index)))
            database_dirs[tree] = rewritten or tree
            note = ', precompiled header options left out' if rewritten else ''
            print(f'build tree: {tree} ({len(found)} translation units{note})')
        print(f'checking {len(units)} translation units on {args.jobs} jobs\n', flush=True)

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(run_one, clang_tidy, database_dirs[tree], unit): tree
                       for tree, found in units_by_build_dir.items() for unit in found}
            for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
                unit, diagnostics = future.result()
                if diagnostics:
                    failed[(futures[future], unit)] = diagnostics
                if progress:
                    print(f'\r  {done}/{len(units)}', end='', flush=True)
    if progress:
        print('\r' + ' ' * 24 + '\r', end='')

    if not failed:
        print(f'clang-tidy: no diagnostics in {len(units)} translation units')
        return 0

    total = 0
    for unit in sorted(failed):
        for line in failed[unit]:
            if DIAG_LINE.match(line):
                total += 1
            print(line)
    print(f'\nclang-tidy: {total} diagnostic(s) in {len(failed)} translation unit(s)')
    return 1


if __name__ == '__main__':
    sys.exit(main())
