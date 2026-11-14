/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <config.h>

#ifdef CONFIG_SVM

#include <model/statedata.h>
#include <arch/kernel/npt.h>
#include <arch/api/invocation.h>

struct lookupNPTPDPTSlot_ret {
    exception_t status;
    npt_pdpte_t *pdptSlot;
};
typedef struct lookupNPTPDPTSlot_ret lookupNPTPDPTSlot_ret_t;

struct lookupNPTPDSlot_ret {
    exception_t status;
    npt_pde_t  *pdSlot;
};
typedef struct lookupNPTPDSlot_ret lookupNPTPDSlot_ret_t;

struct lookupNPTPTSlot_ret {
    exception_t status;
    npt_pte_t  *ptSlot;
};
typedef struct lookupNPTPTSlot_ret lookupNPTPTSlot_ret_t;

enum npt_cache_options {
    NPTUncacheable = 0,
    NPTWriteCombining = 1,
    NPTWriteThrough = 4,
    NPTWriteProtected = 5,
    NPTWriteBack = 6
};
typedef enum npt_cache_options npt_cache_options_t;

void deleteNPTASID(asid_t asid, npt_pml4e_t *npt)
{
    asid_pool_t *poolPtr;

    poolPtr = x86KSASIDTable[ASID_HIGH(asid)];
    if (poolPtr != NULL) {
        asid_map_t asid_map = poolPtr->array[ASID_LOW(asid)];
        if (asid_map_get_type(asid_map) == asid_map_asid_map_npt &&
            (npt_pml4e_t *)asid_map_asid_map_npt_get_npt_root(asid_map) == npt) {
            poolPtr->array[ASID_LOW(asid)] = asid_map_asid_map_none_new();
        }
    }
}

exception_t performX86NPTPageInvocationUnmap(cap_t cap, cte_t *ctSlot)
{
    unmapNPTPage(
        cap_frame_cap_get_capFSize(cap),
        cap_frame_cap_get_capFMappedASID(cap),
        cap_frame_cap_get_capFMappedAddress(cap),
        (void *)cap_frame_cap_get_capFBasePtr(cap)
    );

    cap_frame_cap_ptr_set_capFMappedAddress(&ctSlot->cap, 0);
    cap_frame_cap_ptr_set_capFMappedASID(&ctSlot->cap, asidInvalid);
    cap_frame_cap_ptr_set_capFMapType(&ctSlot->cap, X86_MappingNone);

    return EXCEPTION_NONE;
}

findNPTForASID_ret_t findNPTForASID(asid_t asid)
{
    findNPTForASID_ret_t ret;
    asid_map_t asid_map;

    asid_map = findMapForASID(asid);
    if (asid_map_get_type(asid_map) != asid_map_asid_map_npt) {
        current_lookup_fault = lookup_fault_invalid_root_new();

        ret.npt = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    ret.npt = (npt_pml4e_t *)asid_map_asid_map_npt_get_npt_root(asid_map);
    ret.status = EXCEPTION_NONE;
    return ret;
}

static npt_pml4e_t *CONST lookupNPTPML4Slot(npt_pml4e_t *pml4, vptr_t vptr)
{
    return pml4 + GET_NPT_PML4_INDEX(vptr);
}

static lookupNPTPDPTSlot_ret_t CONST lookupNPTPDPTSlot(npt_pml4e_t *pml4, vptr_t vptr)
{
    lookupNPTPDPTSlot_ret_t ret;
    npt_pml4e_t *pml4Slot;

    pml4Slot = lookupNPTPML4Slot(pml4, vptr);

    if (!npt_pml4e_ptr_get_read(pml4Slot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(NPT_PML4_INDEX_OFFSET);

        ret.pdptSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    npt_pdpte_t *pdpt = paddr_to_pptr(npt_pml4e_ptr_get_pdpt_base_address(pml4Slot));
    uint32_t index = GET_NPT_PDPT_INDEX(vptr);
    ret.pdptSlot = pdpt + index;
    ret.status = EXCEPTION_NONE;
    return ret;
}

static lookupNPTPDSlot_ret_t lookupNPTPDSlot(npt_pml4e_t *pml4, vptr_t vptr)
{
    lookupNPTPDSlot_ret_t ret;
    lookupNPTPDPTSlot_ret_t lu_ret;

    lu_ret = lookupNPTPDPTSlot(pml4, vptr);
    if (lu_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;
        /* current_lookup_fault will have been set by lookupNPTPDPTSlot */
        ret.pdSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    if (!npt_pdpte_ptr_get_read(lu_ret.pdptSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(NPT_PDPT_INDEX_OFFSET);

        ret.pdSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    npt_pde_t *pd = paddr_to_pptr(npt_pdpte_ptr_get_pd_base_address(lu_ret.pdptSlot));
    uint32_t index = GET_NPT_PD_INDEX(vptr);
    ret.pdSlot = pd + index;
    ret.status = EXCEPTION_NONE;
    return ret;
}

static lookupNPTPTSlot_ret_t lookupNPTPTSlot(npt_pml4e_t *pml4, vptr_t vptr)
{
    lookupNPTPTSlot_ret_t ret;
    lookupNPTPDSlot_ret_t lu_ret;

    lu_ret = lookupNPTPDSlot(pml4, vptr);
    if (lu_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;
        /* current_lookup_fault will have been set by lookupNPTPDSlot */
        ret.ptSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    if ((npt_pde_ptr_get_page_size(lu_ret.pdSlot) != npt_pde_npt_pde_pt) ||
        !npt_pde_npt_pde_pt_ptr_get_read(lu_ret.pdSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(NPT_PD_INDEX_OFFSET);

        ret.ptSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    npt_pte_t *pt = paddr_to_pptr(npt_pde_npt_pde_pt_ptr_get_pt_base_address(lu_ret.pdSlot));
    uint32_t index = GET_NPT_PT_INDEX(vptr);

    ret.ptSlot = pt + index;
    ret.status = EXCEPTION_NONE;
    return ret;
}

static npt_cache_options_t nptCacheFromVmAttr(vm_attributes_t vmAttr)
{
    /* Need to sanitise user input, vmAttr will not have been verified at this point. */
    npt_cache_options_t option = vmAttr.words[0];
    if (option != NPTUncacheable &&
        option != NPTWriteCombining &&
        option != NPTWriteThrough &&
        option != NPTWriteBack) {
        option = NPTWriteBack;
    }
    return option;
}

static inline npt_pte_t CONST makeNPTPTEInvalid(void)
{
    return npt_pte_new(0, 0, 0, 0, 0, 0, 0);
}

static inline npt_pte_t CONST makeNPTPTE(paddr_t paddr, vm_attributes_t vm_attr, vm_rights_t vm_rights)
{
    /* NPT has no user/supervisor distinction, so kernel-only means no guest access */
    if (vm_rights == VMKernelOnly) {
        return makeNPTPTEInvalid();
    }
    return npt_pte_new(
               paddr,                                          /* page_base_address    */
               0,                                              /* avl_cte_dnpth        */
               0,                                              /* ignore_pat           */
               nptCacheFromVmAttr(vm_attr),                    /* type                 */
               1,                                              /* execute              */
               WritableFromVMRights(vm_rights),                /* write                */
               1                                               /* read                 */
           );
}

static inline npt_pde_t CONST makeNPTPDEInvalid(void)
{
    return npt_pde_npt_pde_2m_new(0, 0, 0, 0, 0, 0, 0);
}

static inline npt_pde_t CONST makeNPTPDELargePage(paddr_t paddr, vm_attributes_t vm_attr, vm_rights_t vm_rights)
{
    /* NPT has no user/supervisor distinction, so kernel-only means no guest access */
    if (vm_rights == VMKernelOnly) {
        return makeNPTPDEInvalid();
    }
    return npt_pde_npt_pde_2m_new(
               paddr,                                          /* page_base_address    */
               0,                                              /* avl_cte_dnpth        */
               0,                                              /* ignore_pat           */
               nptCacheFromVmAttr(vm_attr),                    /* type                 */
               1,                                              /* execute              */
               WritableFromVMRights(vm_rights),                /* write                */
               1                                               /* read                 */
           );
}

NPTPDPTMapped_ret_t NPTPDPTMapped(asid_t asid, vptr_t vptr, npt_pdpte_t *pdpt)
{
    NPTPDPTMapped_ret_t ret;
    findNPTForASID_ret_t asid_ret;
    npt_pml4e_t *pml4Slot;

    asid_ret = findNPTForASID(asid);
    if (asid_ret.status != EXCEPTION_NONE) {
        ret.pml4 = NULL;
        ret.pml4Slot = NULL;
        ret.status = asid_ret.status;
        return ret;
    }

    pml4Slot = lookupNPTPML4Slot(asid_ret.npt, vptr);

    if (npt_pml4e_ptr_get_read(pml4Slot)
        && ptrFromPAddr(npt_pml4e_ptr_get_pdpt_base_address(pml4Slot)) == pdpt) {
        ret.pml4 = asid_ret.npt;
        ret.pml4Slot = pml4Slot;
        ret.status = EXCEPTION_NONE;
        return ret;
    } else {
        ret.pml4 = NULL;
        ret.pml4Slot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }
}

void unmapNPTPDPT(asid_t asid, vptr_t vaddr, npt_pdpte_t *pdpt)
{
    NPTPDPTMapped_ret_t lu_ret;

    lu_ret = NPTPDPTMapped(asid, vaddr, pdpt);

    if (lu_ret.status == EXCEPTION_NONE) {
        *lu_ret.pml4Slot = npt_pml4e_new(0, 0, 0, 0);
        invnpt(lu_ret.pml4);
    }
}

static exception_t performNPTPDPTInvocationUnmap(cap_t cap, cte_t *cte)
{
    if (cap_npt_pdpt_cap_get_capPDPTIsMapped(cap)) {
        npt_pdpte_t *pdpt = (npt_pdpte_t *)cap_npt_pdpt_cap_get_capPDPTBasePtr(cap);
        unmapNPTPDPT(
            cap_npt_pdpt_cap_get_capPDPTMappedASID(cap),
            cap_npt_pdpt_cap_get_capPDPTMappedAddress(cap),
            pdpt);
        clearMemory((void *)pdpt, cap_get_capSizeBits(cap));
    }
    cap_npt_pdpt_cap_ptr_set_capPDPTIsMapped(&(cte->cap), 0);

    return EXCEPTION_NONE;
}

static exception_t performNPTPDPTInvocationMap(cap_t cap, cte_t *cte, npt_pml4e_t pml4e, npt_pml4e_t *pml4Slot,
                                               npt_pml4e_t *pml4)
{
    cte->cap = cap;
    *pml4Slot = pml4e;
    invnpt(pml4);

    return EXCEPTION_NONE;
}

static exception_t decodeX86NPTPDPTInvocation(
    word_t invLabel,
    word_t length,
    cte_t *cte,
    cap_t cap,
    word_t *buffer
)
{
    word_t          vaddr;
    cap_t           pml4Cap;
    npt_pml4e_t    *pml4;
    npt_pml4e_t     pml4e;
    paddr_t         paddr;
    asid_t          asid;
    findNPTForASID_ret_t find_ret;
    npt_pml4e_t    *pml4Slot;

    if (invLabel == X86NPTPDPTUnmap) {
        if (!isFinalCapability(cte)) {
            current_syscall_error.type = seL4_RevokeFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }
        setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
        return performNPTPDPTInvocationUnmap(cap, cte);
    }

    if (invLabel != X86NPTPDPTMap) {
        userError("X86NPTPDPT Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || current_extra_caps.excaprefs[0] == NULL) {
        userError("X86NPTPDPTMap: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_npt_pdpt_cap_get_capPDPTIsMapped(cap)) {
        userError("X86NPTPDPTMap: NPT PDPT is already mapped to a PML4.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer);
    /* cannot use ~MASK(NPT_PML4_INDEX_OFFSET) because on 32-bit compilations
     * this results in an error shifting by greater than 31 bits, so we manually
     * force a 64-bit variable to do the shifting with */
    vaddr = vaddr & ~(((uint64_t)1 << NPT_PML4_INDEX_OFFSET) - 1);
    pml4Cap = current_extra_caps.excaprefs[0]->cap;

    if (cap_get_capType(pml4Cap) != cap_npt_pml4_cap) {
        userError("X86NPTPDPTMap: Not a valid NPT PML4.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pml4 = (npt_pml4e_t *)cap_npt_pml4_cap_get_capPML4BasePtr(pml4Cap);
    asid = cap_npt_pml4_cap_get_capPML4MappedASID(pml4Cap);

    find_ret = findNPTForASID(asid);
    if (find_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;

        return EXCEPTION_SYSCALL_ERROR;
    }

    if (find_ret.npt != pml4) {
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pml4Slot = lookupNPTPML4Slot(pml4, vaddr);

    if (npt_pml4e_ptr_get_read(pml4Slot)) {
        userError("X86NPTPDPTMap: PDPT already mapped here.");
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr((void *)cap_npt_pdpt_cap_get_capPDPTBasePtr(cap));
    pml4e = npt_pml4e_new(
                paddr,
                1,
                1,
                1
            );

    cap = cap_npt_pdpt_cap_set_capPDPTIsMapped(cap, 1);
    cap = cap_npt_pdpt_cap_set_capPDPTMappedASID(cap, asid);
    cap = cap_npt_pdpt_cap_set_capPDPTMappedAddress(cap, vaddr);

    setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
    return performNPTPDPTInvocationMap(cap, cte, pml4e, pml4Slot, pml4);
}

exception_t decodeX86NPTInvocation(
    word_t invLabel,
    word_t length,
    cptr_t cptr,
    cte_t *cte,
    cap_t cap,
    word_t *buffer
)
{
    switch (cap_get_capType(cap)) {
    case cap_npt_pdpt_cap:
        return decodeX86NPTPDPTInvocation(invLabel, length, cte, cap, buffer);
    case cap_npt_pd_cap:
        return decodeX86NPTPDInvocation(invLabel, length, cte, cap, buffer);
    case cap_npt_pt_cap:
        return decodeX86NPTPTInvocation(invLabel, length, cte, cap, buffer);
    default:
        fail("Invalid cap type");
    }
}

NPTPageDirectoryMapped_ret_t NPTPageDirectoryMapped(asid_t asid, vptr_t vaddr, npt_pde_t *pd)
{
    NPTPageDirectoryMapped_ret_t ret;
    lookupNPTPDPTSlot_ret_t find_ret;
    findNPTForASID_ret_t asid_ret;

    asid_ret = findNPTForASID(asid);
    if (asid_ret.status != EXCEPTION_NONE) {
        ret.pml4 = NULL;
        ret.pdptSlot = NULL;
        ret.status = asid_ret.status;
        return ret;
    }

    find_ret = lookupNPTPDPTSlot(asid_ret.npt, vaddr);
    if (find_ret.status != EXCEPTION_NONE) {
        ret.pml4 = NULL;
        ret.pdptSlot = NULL;
        ret.status = find_ret.status;
        return ret;
    }

    if (npt_pdpte_ptr_get_read(find_ret.pdptSlot)
        && ptrFromPAddr(npt_pdpte_ptr_get_pd_base_address(find_ret.pdptSlot)) == pd) {
        ret.pml4 = asid_ret.npt;
        ret.pdptSlot = find_ret.pdptSlot;
        ret.status = EXCEPTION_NONE;
        return ret;
    } else {
        ret.pml4 = NULL;
        ret.pdptSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }
}

void unmapNPTPageDirectory(asid_t asid, vptr_t vaddr, npt_pde_t *pd)
{
    NPTPageDirectoryMapped_ret_t lu_ret;

    lu_ret = NPTPageDirectoryMapped(asid, vaddr, pd);

    if (lu_ret.status == EXCEPTION_NONE) {
        *lu_ret.pdptSlot = npt_pdpte_new(
                               0,  /* pd_base_address  */
                               0,  /* avl_cte_dnpth    */
                               0,  /* execute          */
                               0,  /* write            */
                               0   /* read             */
                           );
        invnpt(lu_ret.pml4);
    }
}

static exception_t performNPTPDInvocationUnmap(cap_t cap, cte_t *cte)
{
    if (cap_npt_pd_cap_get_capPDIsMapped(cap)) {
        npt_pde_t *pd = (npt_pde_t *)cap_npt_pd_cap_get_capPDBasePtr(cap);
        unmapNPTPageDirectory(
            cap_npt_pd_cap_get_capPDMappedASID(cap),
            cap_npt_pd_cap_get_capPDMappedAddress(cap),
            pd);
        clearMemory((void *)pd, cap_get_capSizeBits(cap));
    }
    cap_npt_pd_cap_ptr_set_capPDIsMapped(&(cte->cap), 0);

    return EXCEPTION_NONE;
}

static exception_t performNPTPDInvocationMap(cap_t cap, cte_t *cte, npt_pdpte_t pdpte, npt_pdpte_t *pdptSlot,
                                             npt_pml4e_t *pml4)
{
    cte->cap = cap;
    *pdptSlot = pdpte;
    invnpt(pml4);

    return EXCEPTION_NONE;
}

exception_t decodeX86NPTPDInvocation(
    word_t invLabel,
    word_t length,
    cte_t *cte,
    cap_t cap,
    word_t *buffer
)
{
    word_t          vaddr;
    cap_t           pml4Cap;
    npt_pml4e_t    *pml4;
    npt_pdpte_t     pdpte;
    paddr_t         paddr;
    asid_t          asid;
    findNPTForASID_ret_t find_ret;
    lookupNPTPDPTSlot_ret_t lu_ret;

    if (invLabel == X86NPTPDUnmap) {
        if (!isFinalCapability(cte)) {
            current_syscall_error.type = seL4_RevokeFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }
        setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
        return performNPTPDInvocationUnmap(cap, cte);
    }

    if (invLabel != X86NPTPDMap) {
        userError("X86NPTPD Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || current_extra_caps.excaprefs[0] == NULL) {
        userError("X86NPTPDMap: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_npt_pd_cap_get_capPDIsMapped(cap)) {
        userError("X86NPTPDMap: NPT Page directory is already mapped to a PDPT.");
        current_syscall_error.type =
            seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer);
    vaddr = vaddr & ~MASK(NPT_PDPT_INDEX_OFFSET);
    pml4Cap = current_extra_caps.excaprefs[0]->cap;

    if (cap_get_capType(pml4Cap) != cap_npt_pml4_cap) {
        userError("X86NPTPDMap: Not a valid NPT pml4.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pml4 = (npt_pml4e_t *)cap_npt_pml4_cap_get_capPML4BasePtr(pml4Cap);
    asid = cap_npt_pml4_cap_get_capPML4MappedASID(pml4Cap);

    find_ret = findNPTForASID(asid);
    if (find_ret.status != EXCEPTION_NONE) {
        userError("X86NPTPDMap: NPT PML4 is not mapped.");
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;

        return EXCEPTION_SYSCALL_ERROR;
    }

    if (find_ret.npt != pml4) {
        userError("X86NPTPDMap: NPT PML4 asid is invalid.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    lu_ret = lookupNPTPDPTSlot(pml4, vaddr);
    if (lu_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (npt_pdpte_ptr_get_read(lu_ret.pdptSlot)) {
        userError("X86NPTPDMap: Page directory already mapped here.");
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr((void *)(cap_npt_pd_cap_get_capPDBasePtr(cap)));
    pdpte = npt_pdpte_new(
                paddr,  /* pd_base_address  */
                0,      /* avl_cte_dnpth    */
                1,      /* execute          */
                1,      /* write            */
                1       /* read             */
            );

    cap = cap_npt_pd_cap_set_capPDIsMapped(cap, 1);
    cap = cap_npt_pd_cap_set_capPDMappedASID(cap, asid);
    cap = cap_npt_pd_cap_set_capPDMappedAddress(cap, vaddr);

    setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
    return performNPTPDInvocationMap(cap, cte, pdpte, lu_ret.pdptSlot, pml4);
}

NPTPageTableMapped_ret_t NPTPageTableMapped(asid_t asid, vptr_t vaddr, npt_pte_t *pt)
{
    NPTPageTableMapped_ret_t ret;
    lookupNPTPDSlot_ret_t find_ret;
    findNPTForASID_ret_t asid_ret;

    asid_ret = findNPTForASID(asid);
    if (asid_ret.status != EXCEPTION_NONE) {
        ret.pml4 = NULL;
        ret.pdSlot = NULL;
        ret.status = asid_ret.status;
        return ret;
    }

    find_ret = lookupNPTPDSlot(asid_ret.npt, vaddr);
    if (find_ret.status != EXCEPTION_NONE) {
        ret.pml4 = NULL;
        ret.pdSlot = NULL;
        ret.status = find_ret.status;
        return ret;
    }

    if (npt_pde_ptr_get_page_size(find_ret.pdSlot) == npt_pde_npt_pde_pt
        && ptrFromPAddr(npt_pde_npt_pde_pt_ptr_get_pt_base_address(find_ret.pdSlot)) == pt) {
        ret.pml4 = asid_ret.npt;
        ret.pdSlot = find_ret.pdSlot;
        ret.status = EXCEPTION_NONE;
        return ret;
    } else {
        ret.pml4 = NULL;
        ret.pdSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }
}

void unmapNPTPageTable(asid_t asid, vptr_t vaddr, npt_pte_t *pt)
{
    NPTPageTableMapped_ret_t lu_ret;

    lu_ret = NPTPageTableMapped(asid, vaddr, pt);

    if (lu_ret.status == EXCEPTION_NONE) {
        *lu_ret.pdSlot = npt_pde_npt_pde_pt_new(
                             0,  /* pt_base_address  */
                             0,  /* avl_cte_dnpth    */
                             0,  /* execute          */
                             0,  /* write            */
                             0   /* read             */
                         );
        invnpt(lu_ret.pml4);
    }
}

static exception_t performNPTPTInvocationUnmap(cap_t cap, cte_t *cte)
{
    if (cap_npt_pt_cap_get_capPTIsMapped(cap)) {
        npt_pte_t *pt = (npt_pte_t *)cap_npt_pt_cap_get_capPTBasePtr(cap);
        unmapNPTPageTable(
            cap_npt_pt_cap_get_capPTMappedASID(cap),
            cap_npt_pt_cap_get_capPTMappedAddress(cap),
            pt);
        clearMemory((void *)pt, cap_get_capSizeBits(cap));
    }
    cap_npt_pt_cap_ptr_set_capPTIsMapped(&(cte->cap), 0);

    return EXCEPTION_NONE;
}

static exception_t performNPTPTInvocationMap(cap_t cap, cte_t *cte, npt_pde_t pde, npt_pde_t *pdSlot, npt_pml4e_t *pml4)
{
    cte->cap = cap;
    *pdSlot = pde;
    invnpt(pml4);

    return EXCEPTION_NONE;
}

exception_t decodeX86NPTPTInvocation(
    word_t invLabel,
    word_t length,
    cte_t *cte,
    cap_t cap,
    word_t *buffer
)
{
    word_t          vaddr;
    cap_t           pml4Cap;
    npt_pml4e_t    *pml4;
    npt_pde_t       pde;
    paddr_t         paddr;
    asid_t          asid;
    findNPTForASID_ret_t find_ret;
    lookupNPTPDSlot_ret_t lu_ret;

    if (invLabel == X86NPTPTUnmap) {
        if (!isFinalCapability(cte)) {
            current_syscall_error.type = seL4_RevokeFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }
        setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
        return performNPTPTInvocationUnmap(cap, cte);
    }

    if (invLabel != X86NPTPTMap) {
        userError("X86NPTPT Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || current_extra_caps.excaprefs[0] == NULL) {
        userError("X86NPTPT: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_npt_pt_cap_get_capPTIsMapped(cap)) {
        userError("X86NPTPT NPT Page table is already mapped to an NPT page directory.");
        current_syscall_error.type =
            seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer);
    vaddr = vaddr & ~MASK(NPT_PD_INDEX_OFFSET);
    pml4Cap = current_extra_caps.excaprefs[0]->cap;

    if (cap_get_capType(pml4Cap) != cap_npt_pml4_cap ||
        !cap_npt_pml4_cap_get_capPML4IsMapped(pml4Cap)) {
        userError("X86NPTPTMap: Not a valid NPT pml4.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pml4 = (npt_pml4e_t *)(cap_npt_pml4_cap_get_capPML4BasePtr(pml4Cap));
    asid = cap_npt_pml4_cap_get_capPML4MappedASID(pml4Cap);

    find_ret = findNPTForASID(asid);
    if (find_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;

        return EXCEPTION_SYSCALL_ERROR;
    }

    if (find_ret.npt != pml4) {
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    lu_ret = lookupNPTPDSlot(pml4, vaddr);
    if (lu_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;
        /* current_lookup_fault will have been set by lookupPTSlot */
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (((npt_pde_ptr_get_page_size(lu_ret.pdSlot) == npt_pde_npt_pde_pt) &&
         npt_pde_npt_pde_pt_ptr_get_read(lu_ret.pdSlot)) ||
        ((npt_pde_ptr_get_page_size(lu_ret.pdSlot) == npt_pde_npt_pde_2m) &&
         npt_pde_npt_pde_2m_ptr_get_read(lu_ret.pdSlot))) {
        userError("X86NPTPTMap: Page table already mapped here");
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr((void *)(cap_npt_pt_cap_get_capPTBasePtr(cap)));
    pde = npt_pde_npt_pde_pt_new(
              paddr,/* pt_base_address  */
              0,    /* avl_cte_dnpth    */
              1,    /* execute          */
              1,    /* write            */
              1     /* read             */
          );

    cap = cap_npt_pt_cap_set_capPTIsMapped(cap, 1);
    cap = cap_npt_pt_cap_set_capPTMappedASID(cap, asid);
    cap = cap_npt_pt_cap_set_capPTMappedAddress(cap, vaddr);

    setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
    return performNPTPTInvocationMap(cap, cte, pde, lu_ret.pdSlot, pml4);
}

static exception_t performNPTPageMapPTE(cap_t cap, cte_t *cte, npt_pte_t *ptSlot, npt_pte_t pte, npt_pml4e_t *pml4)
{
    *ptSlot = pte;
    cte->cap = cap;
    invnpt(pml4);

    return EXCEPTION_NONE;
}

static exception_t performNPTPageMapPDE(cap_t cap, cte_t *cte, npt_pde_t *pdSlot, npt_pde_t pde1, npt_pde_t pde2,
                                        npt_pml4e_t *pml4)
{
    pdSlot[0] = pde1;
    if (LARGE_PAGE_BITS == 22) {
        pdSlot[1] = pde2;
    }
    cte->cap = cap;
    invnpt(pml4);

    return EXCEPTION_NONE;
}

exception_t decodeX86NPTPageMap(
    word_t invLabel,
    word_t length,
    cte_t *cte,
    cap_t cap,
    word_t *buffer)
{
    word_t          vaddr;
    word_t          w_rightsMask;
    paddr_t         paddr;
    cap_t           pml4Cap;
    npt_pml4e_t    *pml4;
    vm_rights_t     capVMRights;
    vm_rights_t     vmRights;
    vm_attributes_t vmAttr;
    vm_page_size_t  frameSize;
    asid_t          asid;

    frameSize = cap_frame_cap_get_capFSize(cap);
    vaddr = getSyscallArg(0, buffer);
    vaddr = vaddr & ~MASK(NPT_PT_INDEX_OFFSET);
    w_rightsMask = getSyscallArg(1, buffer);
    vmAttr = vmAttributesFromWord(getSyscallArg(2, buffer));
    pml4Cap = current_extra_caps.excaprefs[0]->cap;

    capVMRights = cap_frame_cap_get_capFVMRights(cap);

    if (cap_frame_cap_get_capFMappedASID(cap) != asidInvalid) {
        userError("X86NPTPageMap: Frame already mapped.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    assert(cap_frame_cap_get_capFMapType(cap) == X86_MappingNone);

    if (cap_get_capType(pml4Cap) != cap_npt_pml4_cap ||
        !cap_npt_pml4_cap_get_capPML4IsMapped(pml4Cap)) {
        userError("X86NPTPageMap: Attempting to map frame into invalid npt pml4.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pml4 = (npt_pml4e_t *)(cap_npt_pml4_cap_get_capPML4BasePtr(pml4Cap));
    asid = cap_npt_pml4_cap_get_capPML4MappedASID(pml4Cap);

    findNPTForASID_ret_t find_ret = findNPTForASID(asid);
    if (find_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;

        return EXCEPTION_SYSCALL_ERROR;
    }

    if (find_ret.npt != pml4) {
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }


    vmRights = maskVMRights(capVMRights, rightsFromWord(w_rightsMask));

    if (!checkVPAlignment(frameSize, vaddr)) {
        current_syscall_error.type = seL4_AlignmentError;

        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr((void *)cap_frame_cap_get_capFBasePtr(cap));

    cap = cap_frame_cap_set_capFMappedASID(cap, asid);
    cap = cap_frame_cap_set_capFMappedAddress(cap, vaddr);
    cap = cap_frame_cap_set_capFMapType(cap, X86_MappingNPT);

    switch (frameSize) {
    /* PTE mappings */
    case X86_SmallPage: {
        lookupNPTPTSlot_ret_t lu_ret;
        npt_pte_t pte;

        lu_ret = lookupNPTPTSlot(pml4, vaddr);
        if (lu_ret.status != EXCEPTION_NONE) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            /* current_lookup_fault will have been set by lookupNPTPTSlot */
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (npt_pte_ptr_get_read(lu_ret.ptSlot)) {
            userError("X86NPTPageMap: Mapping already present.");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }

        pte = makeNPTPTE(paddr, vmAttr, vmRights);

        setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
        return performNPTPageMapPTE(cap, cte, lu_ret.ptSlot, pte, pml4);
    }

    /* PDE mappings */
    case X86_LargePage: {
        lookupNPTPDSlot_ret_t lu_ret;

        lu_ret = lookupNPTPDSlot(pml4, vaddr);
        if (lu_ret.status != EXCEPTION_NONE) {
            userError("X86NPTPageMap: Need a page directory first.");
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            /* current_lookup_fault will have been set by lookupNPTPDSlot */
            return EXCEPTION_SYSCALL_ERROR;
        }


        if ((npt_pde_ptr_get_page_size(lu_ret.pdSlot) == npt_pde_npt_pde_pt) &&
            npt_pde_npt_pde_pt_ptr_get_read(lu_ret.pdSlot)) {
            userError("X86NPTPageMap: Page table already present.");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }
        if (LARGE_PAGE_BITS != NPT_PD_INDEX_OFFSET &&
            (npt_pde_ptr_get_page_size(lu_ret.pdSlot + 1) == npt_pde_npt_pde_pt) &&
            npt_pde_npt_pde_pt_ptr_get_read(lu_ret.pdSlot + 1)) {
            userError("X86NPTPageMap: Page table already present.");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }
        if ((npt_pde_ptr_get_page_size(lu_ret.pdSlot) == npt_pde_npt_pde_2m) &&
            npt_pde_npt_pde_2m_ptr_get_read(lu_ret.pdSlot)) {
            userError("X86NPTPageMap: Mapping already present.");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }

        npt_pde_t pde1 = makeNPTPDELargePage(paddr, vmAttr, vmRights);
        npt_pde_t pde2 = makeNPTPDELargePage(paddr + BIT(NPT_PD_INDEX_OFFSET), vmAttr, vmRights);

        setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
        return performNPTPageMapPDE(cap, cte, lu_ret.pdSlot, pde1, pde2, pml4);
    }

    default:
        /* When initializing NPT we only checked for support for 4K and 2M
         * pages, so we must disallow attempting to use any other */
        userError("X86NPTPageMap: Attempted to map unsupported page size.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }
}

void unmapNPTPage(vm_page_size_t page_size, asid_t asid, vptr_t vptr, void *pptr)
{
    findNPTForASID_ret_t find_ret;
    paddr_t addr = addrFromPPtr(pptr);

    find_ret = findNPTForASID(asid);
    if (find_ret.status != EXCEPTION_NONE) {
        return;
    }

    switch (page_size) {
    case X86_SmallPage: {
        lookupNPTPTSlot_ret_t lu_ret;

        lu_ret = lookupNPTPTSlot(find_ret.npt, vptr);
        if (lu_ret.status != EXCEPTION_NONE) {
            return;
        }
        if (!npt_pte_ptr_get_read(lu_ret.ptSlot)) {
            return;
        }
        if (npt_pte_ptr_get_page_base_address(lu_ret.ptSlot) != addr) {
            return;
        }

        *lu_ret.ptSlot = makeNPTPTEInvalid();
        break;
    }
    case X86_LargePage: {
        lookupNPTPDSlot_ret_t lu_ret;

        lu_ret = lookupNPTPDSlot(find_ret.npt, vptr);
        if (lu_ret.status != EXCEPTION_NONE) {
            return;
        }
        if (npt_pde_ptr_get_page_size(lu_ret.pdSlot) != npt_pde_npt_pde_2m) {
            return;
        }
        if (!npt_pde_npt_pde_2m_ptr_get_read(lu_ret.pdSlot)) {
            return;
        }
        if (npt_pde_npt_pde_2m_ptr_get_page_base_address(lu_ret.pdSlot) != addr) {
            return;
        }

        lu_ret.pdSlot[0] = makeNPTPDEInvalid();

        if (LARGE_PAGE_BITS != NPT_PD_INDEX_OFFSET) {
            assert(npt_pde_ptr_get_page_size(lu_ret.pdSlot + 1) == npt_pde_npt_pde_2m);
            assert(npt_pde_npt_pde_2m_ptr_get_read(lu_ret.pdSlot + 1));
            assert(npt_pde_npt_pde_2m_ptr_get_page_base_address(lu_ret.pdSlot + 1) == addr + BIT(21));

            lu_ret.pdSlot[1] = makeNPTPDEInvalid();
        }
        break;
    }
    default:
        /* we did not allow mapping additional page sizes into NPT objects,
         * so this should not happen. As we have no way to return an error
         * all we can do is assert */
        assert(!"Invalid page size for unmap");
    }
}

#endif /* CONFIG_SVM */
