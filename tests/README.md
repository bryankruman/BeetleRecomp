# Focused mod-enable regression

Use an existing C++20 compiler; no package installation or full application build
is needed. From the repository root:

```sh
python scripts/test-mod-enable.py --cxx g++
```

On a configured MSVC developer shell, `--cxx clang-cl` or `--cxx cl` selects MSVC
flags automatically. `--compiler-style gnu|msvc` makes the choice explicit.
Compiler paths are passed as one argument; shell fragments and embedded flags are
not accepted. `--compile-timeout` defaults to 60 seconds and `--test-timeout` to
10 seconds, each bounded to at most 300 seconds.

The runner loads `scripts/generate-mod-enable-regression.py` and
`tests/mod_enable_regression.cpp.in`. The generator inserts the complete current
production `ModContext` enable/status/deprecation methods and `Version` definition
into in-memory state scaffolding. Keep these two maintainable assets in source
control; generated copies of production methods are temporary build outputs.

The assertions cover installed versus requested dependency versions, deprecated
installed versions, recursive/optional/shared dependencies, callback handler and
handle identity, repeated enable, recomputation, orphan disable and missing IDs.
`--mods-source /absolute/path/to/preview.cpp` tests a detached production preview
without changing the dependency checkout.

Compilation, execution and logs use one temporary workspace, removed on success
or failure. On timeout, POSIX uses the owned process group; Windows uses a
kill-on-close job. The Windows helper joins that job before spawning the compiler,
and refuses to start the compiler if job assignment fails.
The POSIX runner also checks the owned group after the immediate child exits;
unexpected surviving descendants are stopped and make the test fail, even when
the compiler itself returned zero.

This test validates the production methods against a hermetic state fixture. It
does not validate full `ModContext` layout, background threads, archive loading,
application startup or gameplay. Existing parser/config tests remain separate.
