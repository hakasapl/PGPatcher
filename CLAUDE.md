# PGPatcher

C++20 on the MSVC toolchain, CMake + vcpkg. Four modules: `PGLib` (the shared library holding
most logic), `PGPatcher` (wxWidgets GUI), `PGTools` (CLI), `PGMutagen` (C#/.NET plugin access via
a C++ wrapper). `external/` holds submodules and is never edited or linted.

There are no unit tests, by design — see `CONTRIBUTING.md`.

## Code style

All C++ follows the [WebKit code style](https://webkit.org/code-style-guidelines/). The review
checklist, the rules no tool enforces, and the known false positives are in
@.github/copilot-instructions.md — read it before writing or reviewing C++ here, and apply it to
new code as well as to review comments.

Two things that follow from it and are easy to get wrong:

- A clean clang-tidy run does **not** prove style compliance. clang-tidy cannot see most of the
  WebKit rules — bare-word getters, enum InterCaps, `if (ptr)` over `if (ptr != nullptr)`,
  trailing return types, comment sentences. Check those by reading.
- Never rename a serialized string. Renaming a C++ field is fine; the JSON/config key next to it
  is user-facing. After any rename, diff the string literals to prove none changed.

## Verifying a change

```
./buildRelease.ps1 -NoZip                 # needs VCPKG_ROOT, flatc 25.2.10, a VS dev shell
pre-commit run --all-files                # clang-format and the generic hooks
python scripts/run_clang_tidy.py          # needs a configured *and built* tree
```

`run_clang_tidy.py` finds `buildRelease/` or `build/` on its own; pass `--build-dir` otherwise. It
needs the LLVM release pinned in `scripts/clang-tidy-version.txt` first on PATH (CI downloads it
from llvm-project) and refuses any other version. Its docstring explains why a plain
`clang-tidy` invocation is not enough here.

A style-only change still has to build. Renames in particular surface shadowing that the compiler
catches and a regex does not.
