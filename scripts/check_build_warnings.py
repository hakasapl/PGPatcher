#!/usr/bin/env python3
"""Fail when a build log contains a compiler, linker or C# warning.

Run by the build workflow on the console output of buildRelease.ps1, and by hand on the
output of any build locally:

    ./buildRelease.ps1 -NoZip 2>&1 | Tee-Object build.log
    python scripts/check_build_warnings.py build.log

Every tool in this build reports a warning as `warning <CODE>:` with a letter prefix and a
number: C4834 (cl), D9025 (the cl driver), LNK4098 (link), RC4005 (rc), CS8600 (csc),
MSB3277 (MSBuild), NU1603 (NuGet), NETSDK1138 (the .NET SDK). Only such lines count. CMake's
own `CMake Warning` messages (vcpkg prints one about the build path length on the CI
runners), ninja's `ninja: warning:` notes and GitHub Actions annotations carry no code and
are not warnings from the build.

A warning is only printed for a translation unit that was compiled, so an incremental build
proves nothing about the files it did not rebuild. The workflow checks the from-scratch
build it does for every pull request; locally, buildRelease.ps1 builds from scratch too.

Every distinct warning is printed once (a warning in a header repeats for each translation
unit that includes it) and, on GitHub Actions, emitted as an error annotation on the file
and line it names. The exit status is 1 when the log holds a warning.
"""

from __future__ import annotations

import argparse
import codecs
import os
import re
import sys

# `warning <CODE>:` as written by cl, link, rc, csc, MSBuild, NuGet and the .NET SDK. The cl
# driver puts a space before the colon (`warning D9025 : overriding '/W3' with '/W4'`).
WARNING = re.compile(r'\bwarning [A-Z]+\d+ ?:')

# The `path(line): ` or `path(line,column): ` location cl and csc put in front of a diagnostic.
LOCATION = re.compile(r'^(?P<file>.+?)\((?P<line>\d+)(?:,\d+)?\): ')


def read_log(path: str) -> str:
    """Read the log as text, honoring the UTF-16 byte order mark that Windows PowerShell's Tee-Object writes."""
    with open(path, 'rb') as log:
        raw = log.read()
    for bom, encoding in ((codecs.BOM_UTF16_LE, 'utf-16-le'), (codecs.BOM_UTF16_BE, 'utf-16-be')):
        if raw.startswith(bom):
            return raw[len(bom):].decode(encoding, errors='replace')
    return raw.decode('utf-8-sig', errors='replace')


def annotation(warning: str) -> str:
    """Format a warning as a GitHub Actions error, anchored to its file and line when they lie in the checkout."""
    located = LOCATION.match(warning)
    workspace = os.environ.get('GITHUB_WORKSPACE')
    if located and workspace:
        try:
            relative = os.path.relpath(located.group('file'), workspace)
        except ValueError:  # another drive
            relative = os.pardir
        if not relative.startswith(os.pardir):
            return f'::error file={relative.replace(os.sep, "/")},line={located.group("line")}::{warning}'
    return f'::error::{warning}'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', help='the console output of the build, for example build.log from Tee-Object')
    args = parser.parse_args()
    sys.stdout.reconfigure(errors='replace')

    try:
        lines = read_log(args.log).splitlines()
    except OSError as error:
        sys.exit(f'error: cannot read {args.log}: {error.strerror}')
    if not lines:
        sys.exit(f'error: {args.log} is empty; was the build output redirected into it?')

    found = [line.strip() for line in lines if WARNING.search(line)]
    if not found:
        print(f'build warnings: none in the {len(lines)} lines of {args.log}')
        return 0

    on_actions = bool(os.environ.get('GITHUB_ACTIONS'))
    distinct = list(dict.fromkeys(found))  # the first occurrence of each, in build order
    for warning in distinct:
        print(annotation(warning) if on_actions else warning)
    print(f'\nbuild warnings: {len(found)} ({len(distinct)} distinct) in {args.log}; the build has to be warning free')
    return 1


if __name__ == '__main__':
    sys.exit(main())
