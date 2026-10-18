#!/usr/bin/env python3
"""Fail when a change adds entries to a released section of CHANGELOG.md.

New entries go under `## UNRELEASED`. Once a release dates that section as `## [x.y.z] - <date>`,
it lists what shipped in x.y.z and gains no further entries. A pull request that branched off
before the release and appended its entries to the end of the old UNRELEASED section breaks that
without a conflict: git merges the lines into the dated section, while the pull request's diff,
taken against its merge base, still shows them under UNRELEASED.

Run by the changelog workflow on the merge commit of a pull request, and on main after each push,
with the parent commit as the base:

    python scripts/check_changelog.py HEAD^1

and by hand the same way, after merging main into a branch:

    python scripts/check_changelog.py origin/main

CHANGELOG.md on disk, or at a second revision when one is given, is compared with the one at the
base. A released section fails when it holds more entries than at the base, so editing or removing
an entry passes, and so does a release dating the UNRELEASED section, as that section is new.
Adding an entry to a released section on purpose, such as a forgotten one, fails as well. The
lines of a failing section that the base does not have are reported, as an error annotation on
GitHub Actions, and the exit status is 1.
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHANGELOG = 'CHANGELOG.md'

# The heading of a released section, `## [2.2.1] - 2026-10-15`. Older changelogs headed the next version
# `## [2.1.0] - UNRELEASED` until its release, so a section only counts as released once it is dated.
RELEASED = re.compile(r'^## \[(?P<version>[^\]]+)\] - \d{4}-\d{2}-\d{2}')


def read_changelog(revision: str | None) -> str:
    """Read the changelog at a revision, or the one on disk when there is none."""
    if revision is None:
        return (REPO_ROOT / CHANGELOG).read_text(encoding='utf-8')
    shown = subprocess.run(['git', 'show', f'{revision}:{CHANGELOG}'], cwd=REPO_ROOT, capture_output=True)
    if shown.returncode:
        raise ValueError(shown.stderr.decode(errors='replace').strip())
    return shown.stdout.decode('utf-8')


def released_entries(text: str) -> dict[str, collections.Counter[str]]:
    """Map the version of each released section to its entry lines, leaving out blank lines and subheadings."""
    sections: dict[str, collections.Counter[str]] = {}
    version = None
    for line in text.splitlines():
        if line.startswith('## '):
            heading = RELEASED.match(line)
            version = heading.group('version') if heading else None
            if version is not None:
                sections.setdefault(version, collections.Counter())
        elif version is not None and line.strip() and not line.startswith('#'):
            sections[version][line.rstrip()] += 1
    return sections


def gained(base: str, head: str) -> list[tuple[str, int, list[str]]]:
    """List the released sections with more entries in `head` than in `base`: version, count, lines `base` lacks."""
    before = released_entries(base)
    found = []
    for version, entries in released_entries(head).items():
        if version not in before:
            continue  # dated by this change
        count = sum(entries.values()) - sum(before[version].values())
        if count > 0:
            found.append((version, count, list((entries - before[version]).elements())))
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('base', help='the revision to compare with, for example HEAD^1 or origin/main')
    parser.add_argument('head', nargs='?', help='the revision to check (default: CHANGELOG.md on disk)')
    args = parser.parse_args()
    sys.stdout.reconfigure(errors='replace')

    try:
        found = gained(read_changelog(args.base), read_changelog(args.head))
    except (OSError, ValueError) as error:
        sys.exit(f'error: cannot read {CHANGELOG}: {error}')
    if not found:
        print(f'changelog: no released section gained entries over {args.base}')
        return 0

    on_actions = bool(os.environ.get('GITHUB_ACTIONS'))
    for version, count, lines in found:
        listed = ', '.join(f'"{line}"' for line in lines)
        message = (f'the released {version} section gained {count} {"entry" if count == 1 else "entries"} over '
                   f'{args.base}: {listed}; new entries go under ## UNRELEASED')
        print(f'::error file={CHANGELOG}::{message}' if on_actions else f'error: {CHANGELOG}: {message}')
    print('\nThese usually come from a branch made before the release: git merges entries appended to the end of '
          "the old UNRELEASED section into the dated section without a conflict, while the pull request's diff "
          'still shows them under UNRELEASED.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
