# `libghost` example: File size counter

This program uses `libghost` to execute the file [filesize.lua](filesize.lua) in a sandboxed environment, passing on commandline arguments.

The `filesize.lua` script sums up the size in bytes of each file passed on the commandline. It does this by opening each file, seeking to the end and reading the offset.

## Purpose

Although this example is very much synthetic, the program primarily serves as an example of how to implement a custom prompter into `libghost`, going as far as to allow any external program to act as a prompter.

## External prompter

The source code of the program implements a `gh_permprompter` interface based around executing an external program. A sample `prompter.py` is provided, which is a Python script fulfilling the following interface:

* The first argument (`argv[0]`) is the program name, as is standard.
* The second argument (`argv[1]`) is the source of the permission request (the `gh_thread`'s safe identifier).
* The third argument (`argv[2]`) is the name of the permission group (e.g. `filesystem`)
* The third argument (`argv[3]`) is the name of the permission resource (e.g. `node`)
* Up to 16 following parameters (`argv[4]` to `argv[19]` inclusive) contain request fields in the format `key=value`

The field `hint` is interpreted as in the `simpletui` prompter. If the value is equal to `future`, the button for accepting the request just once is not displayed.

The field `description` is parsed according to the de-facto standard rules:

- The set of characters `$$` separates "entries", which are displayed on separate lines.
- `${KEY}` is replaced by the value of the field `KEY`.

