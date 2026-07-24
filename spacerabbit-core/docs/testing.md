# Testing

SpaceRabbit uses GoogleTest for in-tree unit tests and CTest for test orchestration.
The CMake build produces an internal static runtime target for tests and examples;
it does not install or export a standalone library package.

## Debug Test Workflow

```sh
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

Makefile equivalent:

```sh
make test
```

Useful focused runs:

```sh
ctest --test-dir build/ninja-debug -L unit --output-on-failure
```

## Coverage Workflow

Coverage uses LLVM source-based instrumentation on macOS through the Xcode toolchain.

```sh
cmake --preset ninja-coverage
cmake --build --preset ninja-coverage
ctest --preset ninja-coverage
cmake --build build/ninja-coverage --target coverage
```

Makefile equivalent:

```sh
make coverage
```

Coverage artifacts are written under `build/ninja-coverage/coverage/`:

- `spacerabbit.profdata`
- `spacerabbit.lcov`
- raw profiles under `profiles/`

The coverage target runs only `unit`-labeled tests and excludes vendored GoogleTest
sources plus the test sources themselves from the final report.

## Makefile Wrapper

The repo-root `Makefile` is a thin wrapper around the Ninja-based CMake presets only.
It does not replace `CMakePresets.json` and does not expose the `xcode-debug` preset.

Useful commands:

```sh
make
make release
make build CONFIG=coverage
make test CONFIG=release
make clean CONFIG=debug
```
