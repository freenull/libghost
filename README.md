# libghost

Secure Lua execution environment based on LuaJIT with process isolation.

Result of engineering thesis at the Wrocław University of Science and Technology [Link will be available in repository].

## Supported platforms

Only Linux is currently supported.

## Requirements

* Linux kernel 5.1+ (requires seccomp-bpf, memfd and `F_SEAL_FUTURE_WRITE`)
* CMake 3.27+
* Python3.11+ (for build-time tools)
* C compiler toolchain
* Valgrind (for running tests, at least 3.23.0 or some tests may fail)
* Headers for OpenSSL (you may need to install a package like `libssl-dev`)

## Building

Make sure that submodules are up to date:

```sh
git submodule update --init --recursive
```

Create build directory and prepare build with cmake (`-DEXAMPLES=ON` enables building example programs):

```sh
cmake -B build -DEXAMPLES=ON
```

Build the library and examples:

```sh
cmake --build build
```

Run tests:

```sh
make -C build test
```

There are 4 steps to the build:

* Enums are generated from CSV files in `intermediate/`.
* `libghost.so` is built without an embedded *jail executable* (`ghost-jail`).
* `ghost-jail` is built and linked against the previously built `libghost.so`.
* `libghost.so` is rebuilt with `ghost-jail` embedded.

The final `libghost.so` is self-contained and does not need `ghost-jail` to be on disk.

The directory `build/examples` contains the example programs [`filesize`](examples/filesize/README.md) and [`prep`](examples/prep/README.md).

## Documentation

Build documentation by running the following command in the root directory:

```sh
doxygen
```

Documentation is written into the `docs` directory.
