# Contributing To Hajimu

Thank you for your interest in Hajimu. Contributions are welcome from language
designers, educators, C developers, tooling authors, and people who simply want
programming in Japanese to feel better.

日本語での Issue / Pull Request も歓迎します。

## Good First Contributions

Great places to start:

- improve English or Japanese documentation
- add small examples under `examples/`
- add parser/runtime regression tests under `tests/`
- improve diagnostics and error messages
- report Windows packaging issues
- test `.haj` / `.hajimu` source files and English aliases
- reduce compiler warnings
- simplify confusing code paths without changing behavior

## Before Opening An Issue

For bugs, please include:

- Hajimu version or commit hash
- OS and compiler
- a minimal `.jp`, `.haj`, or `.hajimu` program
- expected behavior
- actual behavior
- terminal output or error message

For feature requests, please include:

- the problem you want to solve
- example syntax or API shape
- why it belongs in the language/runtime instead of a package

Security-sensitive issues should follow [SECURITY.md](SECURITY.md).
Community behavior should follow [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Development Setup

```bash
git clone https://github.com/ReoShiozawa/hajimu.git
cd hajimu
make
./nihongo examples/hello.jp
```

Useful build commands:

```bash
make                  # normal build
make release          # optimized build
make clean            # remove build output
make windows          # Windows cross-build, if MinGW-w64 is installed
make windows-installer
make wasm             # WebAssembly build, if Emscripten is installed
```

## Test Commands

`make test` runs every `.jp` file in `tests/`, including server-style tests.
For release-style local validation, use:

```bash
for file in tests/*.jp; do
  [ "$file" = "tests/webhook_test.jp" ] && continue
  ./nihongo "$file"
done

for file in examples/english_*.jp; do
  ./nihongo "$file"
done

tests/english_error_and_bytecode.sh
```

`tests/webhook_test.jp` starts a server and waits for an external/manual request,
so it is usually skipped in automated smoke tests.

When working on async/concurrency behavior, also run a repetition test:

```bash
for i in $(seq 1 50); do
  ./nihongo examples/english_concurrency_aliases.jp >/tmp/hajimu-async.out
done
```

## Code Style

### C

- Use 4-space indentation.
- Prefer clear, local helper functions over broad abstractions.
- Keep ownership rules explicit.
- Check allocation failures in new code when practical.
- Avoid shell command construction with untrusted input.
- Keep comments short and useful, especially around memory ownership,
  parser decisions, concurrency, and platform-specific behavior.

Example:

```c
static Value builtin_example(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_STRING) {
        return value_null();
    }

    return value_string(argv[0].string.data);
}
```

### Hajimu Code

- Keep examples small and focused.
- Prefer one concept per example.
- Use `.jp` for Japanese-first examples.
- Use `.haj` or `.hajimu` when the example is intentionally English-first.
- If a feature has both Japanese syntax and English aliases, tests should cover
  both when reasonable.

## Adding A Language Feature

Most language features touch several layers:

1. `src/lexer.c` / `src/lexer.h` for tokens or keyword aliases
2. `src/parser.c` / `src/parser.h` for grammar
3. `src/ast.c` / `src/ast.h` for AST shape, if needed
4. `src/evaluator.c` / `src/evaluator.h` for runtime behavior
5. tests under `tests/`
6. examples under `examples/`, if user-facing
7. documentation under `docs/`

For English aliases, also update:

- [docs/ENGLISH_ALIAS_POLICY.md](docs/ENGLISH_ALIAS_POLICY.md)
- [docs/ENGLISH_SYNTAX_ROADMAP.md](docs/ENGLISH_SYNTAX_ROADMAP.md)
- [docs/REFERENCE_en.md](docs/REFERENCE_en.md)
- [docs/TUTORIAL_en.md](docs/TUTORIAL_en.md)

## Adding A Built-In Function

1. Implement the function in `src/evaluator.c` or the relevant module.
2. Register it in the built-in table.
3. Add a Japanese name first when the function is part of the core language.
4. Add English aliases when they are clear and unlikely to collide.
5. Add tests.
6. Update the reference manual.

## Pull Request Checklist

Before opening a PR:

- [ ] The change is focused and explained.
- [ ] Relevant tests were added or updated.
- [ ] Documentation was updated for user-facing behavior.
- [ ] `git diff --check` passes.
- [ ] The release-style test loop passes, or the skipped tests are explained.
- [ ] No generated build directories are committed accidentally.

Do not include local generated artifacts such as:

- `build/`
- `dist/`
- `win/build/`
- `win/curl-win64/`
- temporary `.hjp` files unless the test explicitly requires them

## Commit Messages

Use a lightweight conventional format:

- `feat:` new feature
- `fix:` bug fix
- `docs:` documentation
- `test:` tests
- `refactor:` internal cleanup
- `perf:` performance improvement
- `chore:` build/release maintenance

Examples:

```text
feat: add English aliases for async helpers
fix: preserve HTTP response shape on curl failures
docs: improve English quick start
```

## Review Philosophy

Hajimu is a language project, so changes should be judged by both correctness
and user experience. A technically valid change can still be confusing if the
syntax, diagnostics, or documentation do not help learners understand what is
happening.

When in doubt, include a tiny example and a regression test. They make the
discussion concrete.
