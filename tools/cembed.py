#!/usr/bin/env python3

import argparse

parser = argparse.ArgumentParser()
parser.add_argument("file")
parser.add_argument("-n", "--name", required = True)
parser.add_argument("-e", "--extern", action = "store_true")
parser.add_argument("-c", "--columns", default = "16")
args = parser.parse_args()

columns = int(args.columns)

var_data = args.name
var_len = f"{var_data}_len"

print("#include <stddef.h>")

if args.extern:
    print(f"extern char {var_data}[];")
    print(f"extern size_t {var_len};")

print(f"char {var_data}[] = {{")

size = 0
with open(args.file, "rb") as f:
    col = 0

    print("    ", end = "")
    while b := f.read(1):
        size += 1
        if col >= columns:
            print()
            print("    ", end = "")
            col = 0

        print("0x{:02x}".format(int(b[0])) + ", ", end = "")

        col += 1

print()
print(f"}};")

print(f"size_t {var_len} = {size};")
