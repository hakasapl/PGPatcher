#!/usr/bin/env python3
"""Run clang-tidy over every project translation unit and fail on any diagnostic.

Run by the build workflow once it has a build tree, and by hand for the same check
locally. Not a pre-commit hook: clang-tidy replays the real compile commands, so it needs
a configured *and* built tree, which pre-commit.ci does not have.

    python scripts/run_clang_tidy.py [--build-dir DIR] [--jobs N] [files...]

Two quirks of this project are handled here:

* `--config-file` is passed explicitly. Without it clang-tidy walks up from each source
  file looking for a .clang-tidy, and for anything that pulls in nifly it finds
  external/nifly/.clang-tidy, which sets the long-removed `AnalyzeTemporaryDtors` key and
  makes clang-tidy abort with "invalid argument".

* Diagnostics from outside the project are dropped. fmt 11.0.2 fails its own consteval
  format-string check under clang 19 and newer, so every TU that logs anything reports
  errors in fmt/spdlog headers. Those are not actionable here, and the header filter does
  not apply to compiler diagnostics.
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

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Directories holding first-party code. Everything else (external/, the build tree,
# vcpkg_installed/) is somebody else's problem.
PROJECT_DIRS = ('PGLib', 'PGPatcher', 'PGMutagen', 'PGTools')

# Where a configured build tree usually lives, best first.
BUILD_DIR_CANDIDATES = (
    'buildRelease',
    'build',
    os.path.join('build', 'VS2026-VCPKG', 'RelWithDebInfo'),
    os.path.join('out', 'build'),
)

HEADER_FILTER = r'(' + '|'.join(PROJECT_DIRS) + r')[/\\](include|src)[/\\]'
DIAG_LINE = re.compile(
    r'^(?P<path>[A-Za-z]:[\\/].+?|/.+?):(\d+):(\d+):\s+(?:fatal error|error|warning):')

# clang-tidy's own failures: a bad config, an unusable compilation database, a crash. These
# carry no file position, or point at a .clang-tidy rather than at our code, so the path
# filter below would silently swallow them. "Error while processing <file>." is excluded on
# purpose: every clean TU already emits it because of the fmt/spdlog errors.
TOOL_FAILURE = re.compile(r'^Error(?! while processing\b)')


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
    units = {os.path.normpath(e['file']) for e in entries if is_project_file(e['file'])}
    return sorted(units)


def run_one(clang_tidy: str, build_dir: str, unit: str) -> tuple[str, list[str]]:
    result = subprocess.run(
        [clang_tidy, '-p', build_dir,
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
        elif line.startswith('Error while processing') or not line.strip():
            keeping = False
        if keeping:
            kept.append(line)

    # clang-tidy exits 1 for the fmt/spdlog errors on every clean TU, so a non-zero status
    # on its own means nothing here. Anything above 1 is a crash rather than diagnostics.
    if result.returncode > 1:
        failures.append(f'clang-tidy exited {result.returncode}')
    if failures and not kept:
        kept = [f'{unit}: clang-tidy could not complete:'] + [f'  {f}' for f in failures]
    return unit, kept


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', help='directory containing compile_commands.json')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--clang-tidy', default=os.environ.get('CLANG_TIDY', 'clang-tidy'))
    parser.add_argument('files', nargs='*', help='ignored; every project TU is always checked')
    args = parser.parse_args()

    clang_tidy = shutil.which(args.clang_tidy)
    if not clang_tidy:
        sys.exit(f'error: {args.clang_tidy} not found on PATH')

    compile_commands = find_compile_commands(args.build_dir)
    build_dir = os.path.dirname(compile_commands)
    units = translation_units(compile_commands)
    if not units:
        sys.exit(f'error: {compile_commands} lists no project sources; is the build tree stale?')

    version = subprocess.run([clang_tidy, '--version'], capture_output=True, text=True,
                             check=False).stdout.strip().splitlines()
    print(f'clang-tidy: {clang_tidy}')
    for line in version[:2]:
        print(f'  {line.strip()}')
    print(f'build tree: {build_dir}')
    print(f'checking {len(units)} translation units on {args.jobs} jobs\n', flush=True)

    # A redrawn counter is only useful on a terminal; in a CI log it would be one long line.
    progress = sys.stdout.isatty()

    failed: dict[str, list[str]] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, clang_tidy, build_dir, unit) for unit in units]
        for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
            unit, diagnostics = future.result()
            if diagnostics:
                failed[unit] = diagnostics
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
