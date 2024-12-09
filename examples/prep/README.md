# `libghost` example: Tiny extensible preprocessor

This program is a small plaintext document preprocessor that supports custom plugins written in Lua, running in a sandbox under `libghost`.

## Usage

```sh
prep input [-p plugin.lua [-p plugin2.lua [...]]]
```

Each `-p plugin.lua` option points to a L

## Preprocessor

Macros are written in the form:  
```
#name(parameter that may contain spaces, commas and (inner parentheses), ends at right parenthesis)
```

`name` is the name of the macro, and text inside the parentheses is treated as a single string argument. All whitespace in the parameter is preserved.

Note: Without plugins, the preprocessor doesn't offer any macros.

## Plugins

Plugins can define macros using the global `macro` function:

```lua
macro("name", function(param)
    write(trim(param))
end)
```

The function passed as the second argument is called when the macro is used, passing the contents of the parameter.

Printing output is done by simply writing into stanard output. The global `write` function can be used to write to stdout (without adding a newline).
The global `trim` function can be used to trim whitespace at the beginning and end of strings.

## Example

Check out `examples/documents/plugindocs/`. If you have buit `libghost` with `-DEXAMPLES=ON`, you can just use the Makefile inside that directory.
Otherwise, set the `PREP` environment variable to point to the built `prep` executable.

Use `make -B` if you want to generate the output document again (for example, to check if permissions were saved correctly).

## Purpose

This program is a proper demonstration of implementing safe, sandboxed extensions without limiting the range of possibilities.

Example plugins inside `examples/plugins/` do things such as run external commands and access files. When generating the output document, the application will ask you for permission for each file or command. You can answer 'Y' to remember the permissions in a special `.ghperm` file that is placed next to the plugin. The next time you run the program, the permissions are loaded back in.

All code that touches `libghost` resides in `plugin.c`.
