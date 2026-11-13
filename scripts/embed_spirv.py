#!/usr/bin/env python3
"""Встраивает SPIR-V бинарник в C++-заголовок как массив байт."""
import sys


def main() -> int:
    if len(sys.argv) < 4:
        print("usage: embed_spirv.py <input.spv> <symbol> <output.h> [--append]", file=sys.stderr)
        return 2

    input_path, symbol, output_path = sys.argv[1], sys.argv[2], sys.argv[3]
    append = len(sys.argv) > 4 and sys.argv[4] == "--append"

    with open(input_path, "rb") as handle:
        data = handle.read()

    mode = "a" if append else "w"
    with open(output_path, mode, encoding="utf-8") as out:
        if not append:
            out.write("// Сгенерировано scripts/embed_spirv.py — не редактировать.\n")
            out.write("#pragma once\n\n")
        out.write(f"static const unsigned char {symbol}[] = {{\n")
        for offset in range(0, len(data), 16):
            chunk = ",".join(f"0x{byte:02x}" for byte in data[offset:offset + 16])
            out.write(f"    {chunk},\n")
        out.write("};\n")
        out.write(f"static const unsigned int {symbol}_len = {len(data)};\n\n")

    print(f"{symbol}: {len(data)} байт -> {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
