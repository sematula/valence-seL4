#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Patch x86 structures.bf for CONFIG_SVM (one-off helper)."""

import pathlib
import re

CAP_UNION_OLD = """#ifdef CONFIG_VTX
    tag vcpu_cap            21
    tag ept_pt_cap          23
    tag ept_pd_cap          25
    tag ept_pdpt_cap        27
    tag ept_pml4_cap        29
#endif"""

CAP_UNION_NEW = """#if defined(CONFIG_VTX) || defined(CONFIG_SVM)
    tag vcpu_cap            21
#endif
#ifdef CONFIG_VTX
    tag ept_pt_cap          23
    tag ept_pd_cap          25
    tag ept_pdpt_cap        27
    tag ept_pml4_cap        29
#endif
#ifdef CONFIG_SVM
    tag npt_pt_cap          23
    tag npt_pd_cap          25
    tag npt_pdpt_cap        27
    tag npt_pml4_cap        29
#endif"""

ASID_MAP_OLD = """#ifdef CONFIG_VTX
block asid_map_ept {
    field_high ept_root             48
    padding                         14
    field type                      2
}
#endif

tagged_union asid_map type {
    tag asid_map_none 0
    tag asid_map_vspace 1
#ifdef CONFIG_VTX
    tag asid_map_ept 2
#endif
}"""

ASID_MAP_NEW = """#ifdef CONFIG_VTX
block asid_map_ept {
    field_high ept_root             48
    padding                         14
    field type                      2
}
#endif
#ifdef CONFIG_SVM
block asid_map_npt {
    field_high npt_root             48
    padding                         14
    field type                      2
}
#endif

tagged_union asid_map type {
    tag asid_map_none 0
    tag asid_map_vspace 1
#ifdef CONFIG_VTX
    tag asid_map_ept 2
#endif
#ifdef CONFIG_SVM
    tag asid_map_npt 2
#endif
}"""


def duplicate_ept_caps(text: str) -> str:
    pattern = re.compile(
        r"#ifdef CONFIG_VTX\n\nblock ept_pt_cap.*?\nblock ept_pml4_cap.*?\n}\n\n#endif",
        re.S,
    )
    m = pattern.search(text)
    if not m:
        return text
    vtx_block = m.group(0)
    if "#ifdef CONFIG_SVM" in text and "block npt_pml4_cap" in text:
        return text
    svm_block = vtx_block.replace("#ifdef CONFIG_VTX", "#ifdef CONFIG_SVM", 1)
    svm_block = re.sub(r"\bept_", "npt_", svm_block)
    return text.replace(vtx_block, vtx_block + "\n" + svm_block)


def duplicate_pte_blocks(text: str) -> str:
    pattern = re.compile(
        r"#ifdef CONFIG_VTX\n\nblock ept_pml4e.*?block vmx_eptp.*?\n}\n\n#endif",
        re.S,
    )
    m = pattern.search(text)
    if not m:
        return text
    vtx_block = m.group(0)
    if "block npt_pml4e" in text:
        return text
    svm_block = vtx_block.replace("#ifdef CONFIG_VTX", "#ifdef CONFIG_SVM", 1)
    svm_block = re.sub(r"\bept_", "npt_", svm_block)
    svm_block = re.sub(r"block vmx_nptp \{.*?\n\}\n\n", "", svm_block, flags=re.S)
    return text.replace(vtx_block, vtx_block + "\n" + svm_block)


def patch_bf(path: pathlib.Path) -> None:
    text = path.read_text()
    text = text.replace("#ifdef CONFIG_VTX\n\nblock vcpu_cap {",
                        "#if defined(CONFIG_VTX) || defined(CONFIG_SVM)\n\nblock vcpu_cap {")
    text = text.replace(CAP_UNION_OLD, CAP_UNION_NEW)
    text = text.replace(ASID_MAP_OLD, ASID_MAP_NEW)
    text = text.replace(
        "#ifdef CONFIG_VTX\n    field   capIOPortVPID      16\n#else",
        "#if defined(CONFIG_VTX) || defined(CONFIG_SVM)\n    field   capIOPortVPID      16\n#else",
    )
    text = duplicate_ept_caps(text)
    text = duplicate_pte_blocks(text)
    path.write_text(text)
    print(f"patched {path}")


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    for rel in (
        "include/arch/x86/arch/64/mode/object/structures.bf",
        "include/arch/x86/arch/32/mode/object/structures.bf",
    ):
        patch_bf(root / rel)


if __name__ == "__main__":
    main()