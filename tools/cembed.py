#!/usr/bin/env python3

import argparse
import os
import sys

parser = argparse.ArgumentParser()
parser.add_argument("file")
parser.add_argument("-n", "--name", required = True)
parser.add_argument("-e", "--extern", action = "store_true")
parser.add_argument("-c", "--columns", default = "16")
parser.add_argument("-o", "--output")
args = parser.parse_args()

output = sys.stdout
if args.output is not None:
    os.makedirs(os.path.dirname(args.output), exist_ok = True)
    output = open(args.output, "w")
try:

    columns = int(args.columns)

    var_data = args.name
    var_len = f"{var_data}_len"

    print("#include <stddef.h>", file = output)

    if args.extern:
        print(f"extern char {var_data}[];", file = output)
        print(f"extern size_t {var_len};", file = output)

    print(f"char {var_data}[] = {{", file = output)

    size = 0
    with open(args.file, "rb") as f:
        col = 0

        print("    ", end = "", file = output)
        while b := f.read(1):
            size += 1
            if col >= columns:
                print(file = output)
                print("    ", end = "", file = output)
                col = 0

            print("0x{:02x}".format(int(b[0])) + ", ", end = "", file = output)

            col += 1

    print(file = output)
    print(f"}};", file = output)

    print(f"size_t {var_len} = {size};", file = output)
finally:
    if output != sys.stdout:
        output.close()
