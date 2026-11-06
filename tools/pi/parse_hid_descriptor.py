#!/usr/bin/env python3
"""Extract a byte initializer from the firmware descriptor source."""

import re
import sys
from pathlib import Path


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", source)


def initializer(source: str, symbol: str) -> str:
    match = re.search(
        rf"\b{re.escape(symbol)}\s*\[\s*\]\s*=\s*\{{",
        source,
    )
    if not match:
        raise ValueError(f"descriptor {symbol} not found")
    start = match.end()
    depth = 1
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    raise ValueError(f"descriptor {symbol} initializer is unterminated")


def expressions(body: str) -> list[str]:
    result: list[str] = []
    start = 0
    depth = 0
    for index, character in enumerate(body):
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == "," and depth == 0:
            result.append(body[start:index].strip())
            start = index + 1
    tail = body[start:].strip()
    if tail:
        result.append(tail)
    return result


def parse_value(expression: str) -> list[int]:
    expression = expression.strip()
    # U16_TO_U8S_LE is the only multi-byte descriptor macro used by this
    # firmware. Expand it as the two bytes its C definition emits.
    macro = re.fullmatch(r"U16_TO_U8S_LE\s*\(\s*(0x[0-9a-fA-F]+|\d+)\s*\)", expression)
    if macro:
        value = int(macro.group(1), 0)
        if not 0 <= value <= 0xFFFF:
            raise ValueError(f"16-bit macro value out of range: {expression}")
        return [value & 0xFF, value >> 8]

    numeric = re.fullmatch(r"(?:0x[0-9a-fA-F]+|\d+)(?:[uUlL]*)", expression)
    if not numeric:
        raise ValueError(f"unsupported descriptor initializer: {expression}")
    value = int(re.match(r"(?:0x[0-9a-fA-F]+|\d+)", expression).group(0), 0)
    if not 0 <= value <= 0xFF:
        raise ValueError(
            f"descriptor value is not one byte (use an explicit byte macro): {expression}"
        )
    return [value]


def parse_descriptor(path: Path, symbol: str) -> bytes:
    source = strip_comments(path.read_text(encoding="utf-8"))
    body = initializer(source, symbol)
    values: list[int] = []
    for expression in expressions(body):
        values.extend(parse_value(expression))
    return bytes(values)


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} SOURCE SYMBOL OUTPUT", file=sys.stderr)
        return 2
    source, symbol, output = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    try:
        descriptor = parse_descriptor(source, symbol)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    output.write_bytes(descriptor)
    print(f"REPORT_DESC: {symbol}, {len(descriptor)} bytes from {source}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
