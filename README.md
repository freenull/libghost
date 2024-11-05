# libghost

Secure Lua execution environment based on LuaJIT with process isolation.

Result of engineering thesis at the Wrocław University of Science and Technology [link todo].

## Supported platforms

Currently, only Linux is supported.

## Requirements

* Linux kernel 3.5+ (with seccomp-bpf)
* CMake 3.27+
* Python3.11+ (for build-time tools)
* C compiler toolchain
* xxd (comes with Vim, alternative implementation is okay as long as it supports the `-i` option)

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

There are 4 steps to the build:

* Enums are generated from CSV files in `intermediate/`.
* `libghost.so` is built without an embedded *jail executable* (`ghost-jail`).
* `ghost-jail` is built and linked against the previously built `libghost.so`.
* `libghost.so` is rebuilt with `ghost-jail` embedded.

The final `libghost.so` is self-contained and does not need `ghost-jail` to be on disk.

## Design

The API is designed around objects called "sandboxes". A sandbox is a process running `ghost-jail` and a full LuaJIT JIT compiler with a socket for IPC.

TODO
