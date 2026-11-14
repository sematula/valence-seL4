/*
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <config.h>
#include <object/structures.h>

#ifdef CONFIG_SVM

struct NPTPDPTMapped_ret {
    npt_pml4e_t *pml4;
    npt_pml4e_t *pml4Slot;
    exception_t status;
};
typedef struct NPTPDPTMapped_ret NPTPDPTMapped_ret_t;

struct NPTPageDirectoryMapped_ret {
    npt_pml4e_t *pml4;
    npt_pdpte_t *pdptSlot;
    exception_t status;
};
typedef struct NPTPageDirectoryMapped_ret NPTPageDirectoryMapped_ret_t;

struct NPTPageTableMapped_ret {
    npt_pml4e_t *pml4;
    npt_pde_t *pdSlot;
    exception_t status;
};
typedef struct NPTPageTableMapped_ret NPTPageTableMapped_ret_t;

struct findNPTForASID_ret {
    exception_t status;
    npt_pml4e_t *npt;
};
typedef struct findNPTForASID_ret findNPTForASID_ret_t;

NPTPDPTMapped_ret_t NPTPDPTMapped(asid_t asid, vptr_t vptr, npt_pdpte_t *pdpt);
NPTPageDirectoryMapped_ret_t NPTPageDirectoryMapped(asid_t asid, vptr_t vaddr, npt_pde_t *pd);
NPTPageTableMapped_ret_t NPTPageTableMapped(asid_t asid, vptr_t vaddr, npt_pte_t *pt);
findNPTForASID_ret_t findNPTForASID(asid_t asid);

void deleteNPTASID(asid_t asid, npt_pml4e_t *npt);
exception_t decodeX86NPTInvocation(word_t invLabel, word_t length, cptr_t cptr, cte_t *cte, cap_t cap,
                                   word_t *buffer);
exception_t decodeX86NPTPDInvocation(word_t invLabel, word_t length, cte_t *cte, cap_t cap, word_t *buffer);
exception_t decodeX86NPTPTInvocation(word_t invLabel, word_t length, cte_t *cte, cap_t cap, word_t *buffer);
exception_t decodeX86NPTPageMap(word_t invLabel, word_t length, cte_t *cte, cap_t cap, word_t *buffer);
exception_t performX86NPTPageInvocationUnmap(cap_t cap, cte_t *ctSlot);
void unmapNPTPDPT(asid_t asid, vptr_t vaddr, npt_pdpte_t *pdpt);
void unmapNPTPageDirectory(asid_t asid, vptr_t vaddr, npt_pde_t *pd);
void unmapNPTPageTable(asid_t asid, vptr_t vaddr, npt_pte_t *pt);
void unmapNPTPage(vm_page_size_t page_size, asid_t asid, vptr_t vptr, void *pptr);

#endif /* CONFIG_SVM */