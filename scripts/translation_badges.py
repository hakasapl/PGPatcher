#!/usr/bin/env python3
"""Track how far each translation has fallen behind the reference language.

``PGPatcher/translations/en.json`` is the benchmark: every other file in that
folder is expected to define the same keys. This script flattens each file,
diffs its key set against the benchmark and writes:

* ``.github/badges/translations/<code>.json`` - a shields.io endpoint badge
* ``.github/badges/translations/<code>.md`` - every outstanding key together with
  its English text; this is what the language's badge links to
* the badge block in ``README.md``, so a newly added language shows up on its own

Usage:
    python scripts/translation_badges.py --write     # regenerate all of the above
    python scripts/translation_badges.py --check     # fail if they are stale
    python scripts/translation_badges.py --summary   # markdown report on stdout
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from urllib.parse import quote

REPO_ROOT = Path(__file__).resolve().parent.parent
TRANSLATIONS_DIR = REPO_ROOT / "PGPatcher" / "translations"
BADGE_DIR = REPO_ROOT / ".github" / "badges" / "translations"
README = REPO_ROOT / "README.md"

REFERENCE = "en"
BADGE_RAW_URL = "https://raw.githubusercontent.com/hakasapl/PGPatcher/main/.github/badges/translations"
REPORT_LINK = "https://github.com/hakasapl/PGPatcher/blob/main/.github/badges/translations"
TRANSLATIONS_LINK = "https://github.com/hakasapl/PGPatcher/blob/main/PGPatcher/translations"

MARKER_START = "<!-- translations:start -->"
MARKER_END = "<!-- translations:end -->"

# Keys beginning with an underscore are file metadata (e.g. "_language"), not
# user-facing strings, so they do not count towards coverage.
METADATA_PREFIX = "_"


def flatten(node: dict, prefix: str = "") -> dict[str, str]:
    """Flatten nested translation objects into "a.b.c" -> value pairs."""
    flat: dict[str, str] = {}
    for key, value in node.items():
        path = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            flat.update(flatten(value, path))
        else:
            flat[path] = value
    return flat


def load(path: Path) -> dict[str, str]:
    with path.open(encoding="utf-8") as handle:
        return flatten(json.load(handle))


def is_translatable(key: str) -> bool:
    return not key.split(".")[0].startswith(METADATA_PREFIX)


def color_for(percent: int, removed: int) -> str:
    if removed:
        # Keys the reference no longer defines are dead weight regardless of coverage.
        return "orange"
    if percent == 100:
        return "brightgreen"
    if percent >= 95:
        return "green"
    if percent >= 85:
        return "yellowgreen"
    if percent >= 70:
        return "yellow"
    if percent >= 50:
        return "orange"
    return "red"


class Language:
    def __init__(self, path: Path, reference: dict[str, str]) -> None:
        self.code = path.stem
        self.path = path
        self.reference = reference
        strings = load(path)
        self.name = strings.get("_language", self.code)

        expected = [key for key in reference if is_translatable(key)]
        self.missing = [
            key for key in expected if not str(strings.get(key, "")).strip()
        ]
        self.removed = [
            key for key in strings if is_translatable(key) and key not in reference
        ]
        self.total = len(expected)
        translated = self.total - len(self.missing)

        percent = int(translated * 100 / self.total) if self.total else 100
        # Never round up to a perfect score while anything is outstanding.
        self.percent = min(percent, 99) if self.missing else percent

    @property
    def up_to_date(self) -> bool:
        return not self.missing and not self.removed

    @property
    def message(self) -> str:
        if self.up_to_date:
            return "up to date"
        parts = [f"{self.percent}%"]
        if self.missing:
            parts.append(f"{len(self.missing)} missing")
        if self.removed:
            parts.append(f"{len(self.removed)} removed")
        return " · ".join(parts)

    def badge(self) -> dict[str, object]:
        return {
            "schemaVersion": 1,
            "label": self.name,
            "message": self.message,
            "color": color_for(self.percent, len(self.removed)),
        }

    def badge_path(self) -> Path:
        return BADGE_DIR / f"{self.code}.json"

    def report_path(self) -> Path:
        return BADGE_DIR / f"{self.code}.md"

    def markdown(self) -> str:
        # The alt text deliberately carries no coverage numbers: the README block
        # then only changes when a language is added or removed, and day to day
        # drift touches nothing but the badge JSON.
        endpoint = quote(f"{BADGE_RAW_URL}/{self.code}.json", safe="")
        alt = f"{self.name} translation coverage"
        return (
            f"[![{alt}](https://img.shields.io/endpoint?url={endpoint})]"
            f"({REPORT_LINK}/{self.code}.md)"
        )


def collect() -> list[Language]:
    reference_path = TRANSLATIONS_DIR / f"{REFERENCE}.json"
    if not reference_path.is_file():
        sys.exit(f"reference translation not found: {reference_path}")
    reference = load(reference_path)

    languages = [
        Language(path, reference)
        for path in sorted(TRANSLATIONS_DIR.glob("*.json"))
        if path.stem != REFERENCE
    ]
    if not languages:
        sys.exit(f"no translations found in {TRANSLATIONS_DIR}")
    return languages


def render_readme_block(languages: list[Language]) -> str:
    # Blank lines between the badges, so each one renders on its own line rather
    # than being soft wrapped into a single paragraph.
    badges = "\n\n".join(language.markdown() for language in languages)
    return f"{MARKER_START}\n{badges}\n{MARKER_END}"


def readme_with_block(text: str, block: str) -> str:
    start = text.find(MARKER_START)
    end = text.find(MARKER_END)
    if start == -1 or end == -1:
        sys.exit(
            f"{README.name} is missing the {MARKER_START} / {MARKER_END} markers"
        )
    return text[:start] + block + text[end + len(MARKER_END) :]


def cell(value: object) -> str:
    """Render a translation string as a table cell that cannot break the table."""
    text = str(value).replace("\r\n", "\n").replace("\n", "\\n").replace("|", "\\|")
    fence = "`"
    while fence in text:
        fence += "`"
    pad = " " if text.startswith("`") or text.endswith("`") else ""
    return f"{fence}{pad}{text}{pad}{fence}"


def missing_table(language: Language) -> list[str]:
    return ["| Key | English |", "| --- | --- |"] + [
        f"| `{key}` | {cell(language.reference[key])} |" for key in language.missing
    ]


def render_language_report(language: Language) -> str:
    """The page a badge links to: what exactly is outstanding for one language."""
    total = language.total
    translated = total - len(language.missing)
    source = f"{TRANSLATIONS_LINK}/{language.code}.json"

    lines = [
        f"# {language.name}",
        "",
        "<!-- Generated by scripts/translation_badges.py - do not edit by hand. -->",
        "",
        f"[`{language.code}.json`]({source}) is **{language.percent}%** complete: "
        f"{translated} of {total} keys carried over from "
        f"[`{REFERENCE}.json`]({TRANSLATIONS_LINK}/{REFERENCE}.json), the "
        "reference language.",
    ]

    if language.up_to_date:
        lines += ["", "Nothing outstanding - this translation is up to date."]
    else:
        lines += [
            "",
            f"To help, edit [`{language.code}.json`]({source}) and fill in the "
            "entries below. Opening a pull request refreshes this page and the "
            "README badge automatically.",
        ]

        if language.missing:
            lines += ["", f"## Missing ({len(language.missing)})", ""]
            lines += missing_table(language)

        if language.removed:
            lines += [
                "",
                f"## No longer used ({len(language.removed)})",
                "",
                f"`{REFERENCE}.json` no longer defines these keys, so they can be "
                f"deleted from `{language.code}.json`.",
                "",
            ]
            lines += [f"- `{key}`" for key in language.removed]

    return "\n".join(lines) + "\n"


def render_summary(languages: list[Language]) -> str:
    """Aggregate view for the CI job summary, where no file to click exists."""
    lines = [
        "## Translation coverage",
        "",
        f"Measured against `{REFERENCE}.json` "
        f"({languages[0].total} translatable keys).",
        "",
        "| Language | Coverage | Missing | No longer used |",
        "| --- | --- | --- | --- |",
    ]
    lines += [
        f"| {language.name} (`{language.code}`) | {language.percent}% "
        f"| {len(language.missing)} | {len(language.removed)} |"
        for language in languages
    ]

    for language in languages:
        if language.up_to_date:
            continue
        lines += [
            "",
            f"<details><summary>{language.name} "
            f"(<code>{language.code}</code>)</summary>",
            "",
        ]
        if language.missing:
            lines += missing_table(language)
        if language.removed:
            lines += ["", f"No longer in `{REFERENCE}.json`:", ""]
            lines += [f"- `{key}`" for key in language.removed]
        lines += ["", "</details>"]

    return "\n".join(lines) + "\n"


def read_text(path: Path) -> str:
    """Read a file with line endings normalised to LF."""
    return path.read_text(encoding="utf-8")


def write_if_changed(path: Path, content: str, changed: list[str]) -> None:
    """Write ``content``, keeping whatever line ending the file already uses.

    Windows checkouts land as CRLF (``core.autocrlf``), CI checkouts as LF, so
    the generated files must follow the file on disk rather than flipping it.
    """
    existing = path.read_bytes() if path.is_file() else None
    if existing is not None and existing.decode("utf-8").replace("\r\n", "\n") == content:
        return

    newline = "\r\n" if existing is not None and b"\r\n" in existing else "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline=newline)
    changed.append(str(path.relative_to(REPO_ROOT)).replace("\\", "/"))


def main() -> int:
    # Language names are non-ASCII; a Windows console defaults to cp1252.
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write",
        action="store_true",
        help="regenerate badges, coverage reports and the README block",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="exit non-zero if any generated file is out of date",
    )
    parser.add_argument(
        "--summary", action="store_true", help="print a markdown report to stdout"
    )
    args = parser.parse_args()

    languages = collect()

    wanted: dict[Path, str] = {}
    for language in languages:
        wanted[language.badge_path()] = (
            json.dumps(language.badge(), indent=2, ensure_ascii=False) + "\n"
        )
        wanted[language.report_path()] = render_language_report(language)
    wanted[README] = readme_with_block(read_text(README), render_readme_block(languages))

    # A translation that was deleted or renamed leaves its badge and report behind.
    stale = [
        path
        for pattern in ("*.json", "*.md")
        for path in BADGE_DIR.glob(pattern)
        if path not in wanted
    ]

    changed: list[str] = []
    if args.write:
        for path, content in wanted.items():
            write_if_changed(path, content, changed)
        for path in changed:
            print(f"updated {path}", file=sys.stderr)
        for path in stale:
            path.unlink()
            relative = str(path.relative_to(REPO_ROOT)).replace("\\", "/")
            changed.append(relative)
            print(f"removed {relative}", file=sys.stderr)

    if args.summary:
        sys.stdout.write(render_summary(languages))

    if args.check:
        outdated = [
            str(path.relative_to(REPO_ROOT)).replace("\\", "/")
            for path, content in wanted.items()
            if not path.is_file() or read_text(path) != content
        ]
        outdated += [
            str(path.relative_to(REPO_ROOT)).replace("\\", "/") for path in stale
        ]
        if outdated:
            print(
                "translation badges are out of date: "
                + ", ".join(outdated)
                + "\nrun: python scripts/translation_badges.py --write",
                file=sys.stderr,
            )
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
