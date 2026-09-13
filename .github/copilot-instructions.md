# PGPatcher review instructions

C++20, MSVC. All C++ follows the [WebKit code style](https://webkit.org/code-style-guidelines/).
When reviewing, check the rules below — they are the ones no tool in this repo can catch.

## Already enforced automatically — do not raise these

Flagging these wastes a review cycle, because CI already fails the PR if they are wrong.

- **Formatting** — clang-format (pinned in `.pre-commit-config.yaml`) runs on pre-commit.ci and
  pushes fixes to the branch. Never comment on indentation, spacing, brace placement, line
  breaking, or `#include` sorting.
- **clang-tidy** — the `Build PGPatcher` workflow runs `scripts/run_clang_tidy.py` after the
  build. It covers identifier naming, explicit constructors, `override`/`final`, private data
  members, redundant lambda parentheses, `nullptr`, and else-after-return.

## Check these — no tool enforces them

1. **Tests use no equality comparison.** `if (ptr)`, `if (!count)`, `if (flags & Mask)` — not
   `if (ptr != nullptr)`, `if (count == 0)`.
   *Only inside a condition.* Outside one — `return ptr != nullptr;`, `bool x = (p != nullptr);`,
   `attrs.isWeighted = (flags & 1U) != 0U;` — the explicit comparison is deliberate and correct;
   see the false positives below.
2. **Getters are bare words.** `count()`, not `getCount()`. Setters take `set`. The `get` prefix
   is kept only for out-argument getters, e.g. `bool getDDS(path, ScratchImage& out)`.
3. **Trailing return types only when they omit redundant information.** `auto Foo::bar() -> Baz`
   where `Baz` is scoped to `Foo` is right; `auto f() -> int` is wrong, write `int f()`.
4. **Enum constants are InterCaps with an initial capital.** `AnimatedObject`, `None`, `Failure`.
   Acronyms stay upper case: `BSA`, `GOG`, `RMAOS`, `TruePBR`.
5. **Booleans are preceded by a state word** — `is`, `did`, `should`, `has`.
   `isGenerated`, `didLanguageChange`, `shouldMultithread`.
6. **`unsigned`, never `unsigned int`.** Never the `signed` modifier.
7. **No redundant float suffixes.** `1`, not `1.0F` — unless the suffix is needed to force the
   math (`10.0 / 4.0`, `std::clamp(v, 0.0F, 1.0F)`, `auto` deduction).
8. **Comments read as sentences** — capital letter, terminating period. End-of-line comments are
   exempt. Use `FIXME:` without attribution, never `TODO:`.
9. **No file-scope `using namespace` in implementation files.** Qualify at the point of use.
10. **`#pragma once`**, never include guards.
11. **Constructors initialize every member in the initializer list**, in declaration order, not by
    assignment in the body.
12. **Omit empty lambda parentheses** — `[this] { ... }`, not `[this]() { ... }`.
13. **Out-arguments are references**, pointers only when the argument is optional.
14. **Prefer an index or a range-for over iterators** when only reading a container. Erase-while-
    iterating loops legitimately need iterators.

## Known false positives — do not report these

These have each been checked and are correct as written.

- **`constexpr` constants take no `s_` prefix.** clang-tidy classifies them as *constexpr
  variable* (`camelBack`), not *class constant* or *global constant*. `ClassConstantPrefix: s_`
  and `GlobalConstantPrefix: s_` bind only non-`constexpr` `const` statics such as
  `s_losingModColor`. `fullPercentage`, `defaultWidth` and `numTextureSlots` are correct.
- **A clean clang-tidy run does not prove rule 4.** Its `CamelCase` check accepts an all-caps
  single word, so `NONE` or `FAILURE` passes clang-tidy while still violating WebKit. Enum
  constants must be read by eye.
- **fmt/spdlog `consteval` errors in build logs are third-party noise** — fmt 11.0.2 fails its own
  compile-time format-string check under clang 19 and newer. `scripts/run_clang_tidy.py` filters
  them; they are not actionable.
- **`external/` is out of scope.** nifly is a submodule; its `.clang-tidy` sets a key clang-tidy
  removed years ago, which is why the runner passes `--config-file` explicitly.
- **Config and JSON keys are lower case strings** (`"shaderpatcher"`, `"parallax"`) and must never
  be renamed to match a C++ identifier. Renaming a field is fine; renaming its serialized key is a
  breaking change to users' config files.
