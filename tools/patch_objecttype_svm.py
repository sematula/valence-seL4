#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Append CONFIG_SVM mirrors of CONFIG_VTX blocks in x86 objecttype.c."""

import pathlib
import re

REPL = [
    ("CONFIG_VTX", "CONFIG_SVM"),
    ("cap_ept_", "cap_npt_"),
    ("EPT ", "NPT "),
    ("EPT", "NPT"),
    ("VPID_INVALID", "SVASID_INVALID"),
    ("decodeX86EPT", "decodeX86NPT"),
    ("seL4_X86_EPT", "seL4_X86_NPT"),
]


def svmify(block: str) -> str:
    out = block.replace("#ifdef CONFIG_VTX", "#ifdef CONFIG_SVM", 1)
    for a, b in REPL[1:]:
        out = out.replace(a, b)
    return out


def main() -> None:
    path = pathlib.Path(__file__).resolve().parents[1] / "src/arch/x86/object/objecttype.c"
    text = path.read_text()
    parts = []
    pos = 0
    for m in re.finditer(r"#ifdef CONFIG_VTX\n", text):
        start = m.start()
        parts.append(text[pos:start])
        # find matching endif at same indent level (simple: next '\n#endif' after block)
        end = text.find("\n#endif", m.end())
        if end == -1:
            parts.append(text[start:])
            pos = len(text)
            break
        end += len("\n#endif")
        block = text[start:end]
        parts.append(block)
        if "#ifdef CONFIG_SVM" not in text[start:end + 200]:
            parts.append("\n" + svmify(block) + "\n")
        pos = end
    parts.append(text[pos:])
    new_text = "".join(parts)
    path.write_text(new_text)
    print("patched", path)


if __name__ == "__main__":
    main()