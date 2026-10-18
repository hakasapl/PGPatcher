#!/usr/bin/env python3
"""Turn the UNRELEASED section of CHANGELOG.md into the section of a release.

Run by the release workflow for a stable release, with the version it calculated, and by
hand the same way:

    python scripts/release_changelog.py 2.2.2

The `## UNRELEASED` heading becomes `## [2.2.2] - <date>`, so the entries under it belong
to that release, and an empty `## UNRELEASED` section opens above it for the next version.
The date is today in UTC unless --date gives another. Nothing else in the file changes, and
it keeps its line endings. With --check the file is only checked, which the workflow does
before it builds the release.

The file is left untouched and the exit status is 1 when it has no `## UNRELEASED` heading
or more than one, when that section has no entries, or when it already has a section for
the version.
"""

from __future__ import annotations

import argparse
import datetime
import os
import sys
from pathlib import Path

CHANGELOG = Path(__file__).resolve().parent.parent / 'CHANGELOG.md'
UNRELEASED = '## UNRELEASED'


def release(text: str, version: str, date: str) -> str:
    """Return the changelog with its UNRELEASED section headed as `version`, below a new empty UNRELEASED section."""
    lines = text.split('\n')
    headings = [index for index, line in enumerate(lines) if line.rstrip() == UNRELEASED]
    if len(headings) != 1:
        raise ValueError(f'expected one "{UNRELEASED}" heading, found {len(headings)}')

    # The section runs up to the next level two heading.
    start = headings[0]
    end = next((index for index in range(start + 1, len(lines)) if lines[index].startswith('## ')), len(lines))
    if not any(line.strip() for line in lines[start + 1:end]):
        raise ValueError(f'the "{UNRELEASED}" section has no entries, so the release would have nothing to list')
    if any(line.startswith(f'## [{version}]') for line in lines):
        raise ValueError(f'there already is a section for {version}')

    lines[start:start + 1] = [UNRELEASED, '', f'## [{version}] - {date}']
    return '\n'.join(lines)


def fail(message: str) -> int:
    """Print an error, as an annotation on GitHub Actions, and return the failing exit status."""
    print(f'::error::{message}' if os.environ.get('GITHUB_ACTIONS') else f'error: {message}', file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('version', help='the version being released, for example 2.2.2')
    parser.add_argument('--date', type=datetime.date.fromisoformat,
                        default=datetime.datetime.now(datetime.timezone.utc).date(),
                        help='the release date as YYYY-MM-DD (default: today in UTC)')
    parser.add_argument('--changelog', type=Path, default=CHANGELOG,
                        help='the changelog to edit (default: CHANGELOG.md in the repository root)')
    parser.add_argument('--check', action='store_true',
                        help='only check that the changelog can be released as the version, leaving it unchanged')
    args = parser.parse_args()

    try:
        raw = args.changelog.read_bytes()
    except OSError as error:
        return fail(f'cannot read {args.changelog}: {error.strerror}')

    # Windows checkouts are CRLF (core.autocrlf), so the edited file follows whatever is on disk.
    newline = '\r\n' if b'\r\n' in raw else '\n'
    try:
        text = release(raw.decode('utf-8').replace('\r\n', '\n'), args.version, args.date.isoformat())
    except ValueError as error:
        return fail(f'{args.changelog.name}: {error}')
    if args.check:
        print(f'{args.changelog.name}: the {UNRELEASED} entries can be released as {args.version}')
        return 0

    args.changelog.write_bytes(text.replace('\n', newline).encode('utf-8'))
    print(f'{args.changelog.name}: the {UNRELEASED} entries are now "## [{args.version}] - {args.date}", '
          f'below a new empty {UNRELEASED} section')
    return 0


if __name__ == '__main__':
    sys.exit(main())
