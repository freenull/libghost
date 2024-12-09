# libghost

Secure Lua execution environment based on LuaJIT with process isolation.

Result of engineering thesis at the Wrocław University of Science and Technology [TODO :link].

## Supported platforms

Currently, only Linux is supported.

## Requirements

* Linux kernel 5.1+ (requires seccomp-bpf, memfd and `F_SEAL_FUTURE_WRITE`)
* CMake 3.27+
* Python3.11+ (for build-time tools)
* C compiler toolchain

## Building

Create build directory and prepare build with cmake:

```sh
cmake -B build
```

Build the library:

```sh
cmake --build build
```

Run tests:

```sh
make -C build test
```

> [!WARNING]
> Tests that create a sandbox will fail with the latest version of Valgrind due to a [bug](https://bugs.kde.org/show_bug.cgi?id=496353).  
> You may apply [this patch](tools/valgrind-execveat-fix.mbox) to fix the bug and run all tests. To apply it to the current Valgrind git source tree, use:
>
> ```sh
> git am < /path/to/tools/valgrind-execveat-fix.mbox
> ```

There are 4 steps to the build:

* Enums are generated from CSV files in `intermediate/`.
* `libghost.so` is built without an embedded *jail executable* (`ghost-jail`).
* `ghost-jail` is built and linked against the previously built `libghost.so`.
* `libghost.so` is rebuilt with `ghost-jail` embedded.

The final `libghost.so` is self-contained and does not need `ghost-jail` to be on disk.

## Documentation

Build documentation by running the following command in the root directory:

```sh
doxygen
```

Documentation is written into the `docs` directory.
