"""Migrate legacy generated font headers without rerasterizing their glyph data.

The legacy dense-kerning layout predates split class maps and sparse kerning.
Designated initializers leave those optional representations zero-initialized.
Run with one or more generated header paths; already migrated files are unchanged.
"""

import argparse
from pathlib import Path
import re

FIELDS = (
    "bitmap", "glyph", "intervals", "intervalCount", "advanceY", "ascender",
    "descender", "is2Bit", "groups", "groupCount", "glyphToGroup",
    "kernLeftClasses", "kernRightClasses", "kernMatrix", "kernLeftEntryCount",
    "kernRightEntryCount", "kernLeftClassCount", "kernRightClassCount",
    "ligaturePairs", "ligaturePairCount",
)


def migrate(path):
    source = path.read_text(encoding="utf-8")
    pattern = r"(static const EpdFontData \w+ = \{\n)(.*?)(\n\};)"
    match = re.search(pattern, source, re.S)
    if not match:
        raise ValueError(f"{path}: missing font initializer")
    if re.search(r"\.bitmap\s*=", match[2]):
        return
    values = [value.strip() for value in match[2].split(",") if value.strip()]
    if len(values) != len(FIELDS):
        raise ValueError(f"{path}: expected legacy 20-field initializer, got {len(values)}")
    replacement = "\n".join(f"    .{field} = {value}," for field, value in zip(FIELDS, values))
    result = source[:match.start(2)] + replacement + source[match.end(2):]
    path.write_text(result, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("headers", nargs="+", type=Path)
    for header in parser.parse_args().headers:
        migrate(header)
