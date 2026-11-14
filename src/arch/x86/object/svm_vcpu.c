/*
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * VMCB layout adapted from FreeBSD sys/amd64/vmm/amd/vmcb.h (BSD-2-Clause).
 */

#include <config.h>

#ifdef CONFIG_SVM

#include <types.h>
#include <machine/io.h>
#include <api/failures.h>
#include <api/syscall.h>
#include <kernel/thread.h>
#include <object/objecttype.h>
#include <arch/machine/cpu_registers.h>
#include <arch/model/statedata.h>
#include <arch/object/vcpu.h>
#include <arch/object/ioport.h>
#include <arch/kernel/npt.h>
#include <util.h>
#include <sel4/arch/vmenter.h>

#define MSR_SVM               0xc0000080
#define EFER_SVME             BIT(12)
#define MSR_VM_CR             0xc0010114
#define VM_CR_SVMDIS          BIT(4)

const vcpu_gp_register_t crExitRegs[] = {
    VCPU_EAX, VCPU_ECX, VCPU_EDX, VCPU_EBX, VCPU_ESP, VCPU_EBP, VCPU_ESI, VCPU_EDI,
#ifdef CONFIG_X86_64_SVM_64BIT_GUESTS
    VCPU_R8, VCPU_R9, VCPU_R10, VCPU_R11, VCPU_R12, VCPU_R13, VCPU_R14, VCPU_R15,
#endif
};

static char null_npt_space[BIT(seL4_PageBits)] ALIGN(BIT(seL4_PageBits));

static vcpu_t *x86KSSVASIDTable[SVASID_LAST + 1];
static svasid_t x86KSNextSVASID = SVASID_FIRST;

static void releaseSVASID(svasid_t svasid);

void clearCurrentVCPU(void)
{
    vcpu_t *vcpu = ARCH_NODE_STATE(x86KSCurrentVCPU);
    if (vcpu) {
        vcpu->launched = false;
        ARCH_NODE_STATE(x86KSCurrentVCPU) = NULL;
        if (vcpu->fpu_active && vcpu->vcpuTCB) {
            fpuRelease(vcpu->vcpuTCB);
            vcpu->fpu_active = false;
        }
    }
}

static void switchVCPU(vcpu_t *vcpu)
{
#ifdef ENABLE_SMP_SUPPORT
    if (vcpu->last_cpu != getCurrentCPUIndex() && ARCH_NODE_STATE_ON_CORE(x86KSCurrentVCPU, vcpu->last_cpu) == vcpu) {
        doRemoteClearCurrentVCPU(vcpu->last_cpu);
    }
#endif
    clearCurrentVCPU();
#ifdef ENABLE_SMP_SUPPORT
    vcpu->last_cpu = getCurrentCPUIndex();
#endif
    ARCH_NODE_STATE(x86KSCurrentVCPU) = vcpu;
}

static void set_default_intercepts(vcpu_t *vcpu)
{
    word_t i1 = vmcb_read(vcpu, VMCB_CONTROL_INTERCEPT_INSTRUCTION1);
    i1 |= VMCB_INTERCEPT_VMRUN | VMCB_INTERCEPT_CPUID | VMCB_INTERCEPT_IO | VMCB_INTERCEPT_MSR;
    vmcb_write(vcpu, VMCB_CONTROL_INTERCEPT_INSTRUCTION1, i1);
}

void vcpu_init(vcpu_t *vcpu)
{
    vcpu->vcpuTCB = NULL;
    vcpu->launched = false;
    vcpu->fpu_active = false;
    vcpu->fpuState = x86KSnullFpuState;
    memset(vcpu->vmcb, 0, VCPU_VMCB_SIZE);
    vcpu->cr0 = read_cr0();
    vcpu->cr0_shadow = 0;
    vcpu->cr0_mask = 0;
    vcpu->exception_bitmap = 0;
    vcpu->svasid = SVASID_INVALID;
#ifdef ENABLE_SMP_SUPPORT
    vcpu->last_cpu = getCurrentCPUIndex();
#endif

    switchVCPU(vcpu);

    vmcb_write(vcpu, VMCB_CONTROL_HOST_RIP, (word_t)&handle_vmexit);
    vmcb_write(vcpu, VMCB_CONTROL_HOST_RSP, (word_t)&vcpu->gp_registers[n_vcpu_gp_register]);
#ifndef CONFIG_KERNEL_SKIM_WINDOW
    vmcb_write(vcpu, VMCB_CONTROL_HOST_CR3, getCurrentCR3().words[0]);
#endif

    vmcb_write(vcpu, VMCB_CONTROL_NP_ENABLE, 1);
    vmcb_write(vcpu, VMCB_STATE_GUEST_CR0, vcpu->cr0);
    vmcb_write(vcpu, VMCB_STATE_GUEST_CR4, read_cr4());

    set_default_intercepts(vcpu);
    memset(vcpu->io, ~(word_t)0, VCPU_IOBITMAP_SIZE);
}

static void dissociateVcpuTcb(tcb_t *tcb, vcpu_t *vcpu)
{
    assert(tcb->tcbArch.tcbVCPU == vcpu);
    assert(vcpu->vcpuTCB == tcb);
    fpuRelease(tcb);
    vcpu->fpu_active = false;
    tcb->tcbArch.tcbVCPU = NULL;
    vcpu->vcpuTCB = NULL;
}

void vcpu_finalise(vcpu_t *vcpu)
{
    if (ARCH_NODE_STATE_ON_CORE(x86KSCurrentVCPU, vcpu->last_cpu) == vcpu) {
#ifdef ENABLE_SMP_SUPPORT
        if (vcpu->last_cpu != getCurrentCPUIndex()) {
            doRemoteClearCurrentVCPU(vcpu->last_cpu);
        } else
#endif
        {
            clearCurrentVCPU();
        }
    }
    if (vcpu->vcpuTCB) {
        dissociateVcpuTcb(vcpu->vcpuTCB, vcpu);
    }
    if (vcpu->svasid != SVASID_INVALID) {
        releaseSVASID(vcpu->svasid);
    }
}

static void associateVcpuTcb(tcb_t *tcb, vcpu_t *vcpu)
{
    if (tcb->tcbArch.tcbVCPU) {
        dissociateVcpuTcb(tcb, tcb->tcbArch.tcbVCPU);
    }
    if (vcpu->vcpuTCB) {
        dissociateVcpuTcb(vcpu->vcpuTCB, vcpu);
    }
    vcpu->vcpuTCB = tcb;
    tcb->tcbArch.tcbVCPU = vcpu;
}

static exception_t invokeVCPUWriteRegisters(vcpu_t *vcpu, word_t *buffer)
{
    int i;
    for (i = 0; i < n_vcpu_gp_register; i++) {
        vcpu->gp_registers[i] = getSyscallArg(i, buffer);
    }
    setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
    return EXCEPTION_NONE;
}

static exception_t decodeVCPUWriteRegisters(cap_t cap, word_t length, word_t *buffer)
{
#ifdef CONFIG_X86_64_SVM_64BIT_GUESTS
    if (length < 15) {
#else
    if (length < 7) {
#endif
        userError("VCPU WriteRegisters: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    return invokeVCPUWriteRegisters(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)), buffer);
}

static exception_t decodeSetTCB(cap_t cap, word_t length, word_t *buffer)
{
    tcb_t *tcb;
    vcpu_t *vcpu;
    if (length < 1) {
        userError("VCPU SetTCB: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    if (current_extra_caps.excaprefs[0] == NULL) {
        userError("VCPU SetTCB: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    if (cap_get_capType(current_extra_caps.excaprefs[0]->cap) != cap_thread_cap) {
        userError("VCPU SetTCB: TCB cap is not a TCB cap.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
    tcb = TCB_PTR(cap_thread_cap_get_capTCBPtr(current_extra_caps.excaprefs[0]->cap));
    vcpu = VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap));
    associateVcpuTcb(tcb, vcpu);
    setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
    return EXCEPTION_NONE;
}

static exception_t invokeReadVMCB(vcpu_t *vcpu, word_t field, bool_t call, word_t *buffer)
{
    word_t value = vmcb_read(vcpu, field);
    if (call) {
        setMR(NODE_STATE(ksCurThread), buffer, 0, value);
        setRegister(NODE_STATE(ksCurThread), msgInfoRegister,
                    wordFromMessageInfo(seL4_MessageInfo_new(0, 0, 0, 1)));
    }
    setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
    return EXCEPTION_NONE;
}

static exception_t decodeReadVMCB(cap_t cap, word_t length, bool_t call, word_t *buffer)
{
    word_t field;
    if (length < 1) {
        userError("VCPU ReadVMCB: Not enough arguments.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    field = getSyscallArg(0, buffer);
    if (field >= VCPU_VMCB_SIZE) {
        userError("VCPU ReadVMCB: Invalid field %lx.", (long)field);
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
    return invokeReadVMCB(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)), field, call, buffer);
}

static exception_t invokeWriteVMCB(vcpu_t *vcpu, word_t field, word_t value, bool_t call, word_t *buffer)
{
    if (field < VCPU_VMCB_SIZE) {
        vmcb_write(vcpu, field, value);
    }
    if (call) {
        setMR(NODE_STATE(ksCurThread), buffer, 0, vmcb_read(vcpu, field));
        setRegister(NODE_STATE(ksCurThread), msgInfoRegister,
                    wordFromMessageInfo(seL4_MessageInfo_new(0, 0, 0, 1)));
    }
    setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
    return EXCEPTION_NONE;
}

static exception_t decodeWriteVMCB(cap_t cap, word_t length, bool_t call, word_t *buffer)
{
    word_t field, value;
    if (length < 2) {
        userError("VCPU WriteVMCB: Not enough arguments.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    field = getSyscallArg(0, buffer);
    value = getSyscallArg(1, buffer);
    if (field >= VCPU_VMCB_SIZE) {
        userError("VCPU WriteVMCB: Invalid field %lx.", (long)field);
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
    return invokeWriteVMCB(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)), field, value, call, buffer);
}

static exception_t decodeEnableIOPort(cap_t cap, word_t length, word_t *buffer)
{
    (void)cap;
    (void)length;
    (void)buffer;
    userError("VCPU EnableIOPort: Unimplemented.");
    current_syscall_error.type = seL4_IllegalOperation;
    return EXCEPTION_SYSCALL_ERROR;
}

static exception_t decodeDisableIOPort(cap_t cap, word_t length, word_t *buffer)
{
    (void)cap;
    (void)length;
    (void)buffer;
    userError("VCPU DisableIOPort: Unimplemented.");
    current_syscall_error.type = seL4_IllegalOperation;
    return EXCEPTION_SYSCALL_ERROR;
}

exception_t decodeX86VCPUInvocation(
    word_t invLabel,
    word_t length,
    cptr_t cptr,
    cte_t *slot,
    cap_t cap,
    bool_t call,
    word_t *buffer
)
{
    (void)cptr;
    (void)slot;
    switch (invLabel) {
    case X86VCPUSetTCB:
        return decodeSetTCB(cap, length, buffer);
    case X86VCPUReadVMCB:
        return decodeReadVMCB(cap, length, call, buffer);
    case X86VCPUWriteVMCB:
        return decodeWriteVMCB(cap, length, call, buffer);
    case X86VCPUEnableIOPort:
        return decodeEnableIOPort(cap, length, buffer);
    case X86VCPUDisableIOPort:
        return decodeDisableIOPort(cap, length, buffer);
    case X86VCPUWriteRegisters:
        return decodeVCPUWriteRegisters(cap, length, buffer);
    default:
        userError("VCPU: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
}

void vcpu_update_state_sysvmenter(vcpu_t *vcpu)
{
    word_t *buffer = lookupIPCBuffer(true, NODE_STATE(ksCurThread));
    vmcb_write(vcpu, VMCB_STATE_GUEST_RIP, getSyscallArg(0, buffer));
    vmcb_write(vcpu, VMCB_STATE_GUEST_RFLAGS, getSyscallArg(2, buffer));
}

void vcpu_sysvmenter_reply_to_user(tcb_t *tcb)
{
    word_t *buffer;
    vcpu_t *vcpu;

    buffer = lookupIPCBuffer(true, tcb);
    vcpu = tcb->tcbArch.tcbVCPU;
    assert(vcpu);

    if (ARCH_NODE_STATE(x86KSCurrentVCPU) != vcpu) {
        switchVCPU(vcpu);
    }

    setMR(tcb, buffer, SEL4_VMENTER_CALL_EIP_MR, vmcb_read(vcpu, VMCB_STATE_GUEST_RIP));
    setMR(tcb, buffer, SEL4_VMENTER_CALL_CONTROL_PPC_MR, vmcb_read(vcpu, VMCB_CONTROL_INTERCEPT_INSTRUCTION1));
    setMR(tcb, buffer, SEL4_VMENTER_CALL_INTERRUPT_INFO_MR, 0);
    setRegister(tcb, msgInfoRegister, 0);
}

static bool_t is_svm_supported(void)
{
    if (!(x86_cpuid_ecx(0x80000001, 0) & BIT(2))) {
        return false;
    }
    if (!(x86_cpuid_eax(0x8000000a, 0) & MASK(32))) {
        return false;
    }
    return true;
}

BOOT_CODE bool_t svm_init(void)
{
    word_t efer;
    word_t vm_cr;

    if (!is_svm_supported()) {
        printf("svm: not supported\n");
        return false;
    }

    vm_cr = x86_rdmsr(MSR_VM_CR);
    if (vm_cr & VM_CR_SVMDIS) {
        printf("svm: disabled in VM_CR\n");
        return false;
    }

    efer = x86_rdmsr(MSR_SVM);
    if (!(efer & EFER_SVME)) {
        efer |= EFER_SVME;
        x86_wrmsr(MSR_SVM, efer);
    }

    memset(&ARCH_NODE_STATE(x86KSSVMHostSave), 0, sizeof(ARCH_NODE_STATE(x86KSSVMHostSave)));
    x86_wrmsr(0xC0010117, kpptr_to_paddr(&ARCH_NODE_STATE(x86KSSVMHostSave)));
    return true;
}

static void setMRs_vmexit(uint32_t reason, word_t qualification)
{
    word_t *buffer;
    int i;
    vcpu_t *vcpu = ARCH_NODE_STATE(x86KSCurrentVCPU);

    buffer = lookupIPCBuffer(true, NODE_STATE(ksCurThread));

    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_CALL_EIP_MR, vmcb_read(vcpu, VMCB_STATE_GUEST_RIP));
    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_CALL_CONTROL_PPC_MR,
          vmcb_read(vcpu, VMCB_CONTROL_INTERCEPT_INSTRUCTION1));
    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_CALL_INTERRUPT_INFO_MR, 0);
    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_FAULT_REASON_MR, reason);
    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_FAULT_QUALIFICATION_MR, qualification);

    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_FAULT_INSTRUCTION_LEN_MR, 0);
    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_FAULT_GUEST_PHYSICAL_MR, vmcb_read(vcpu, VMCB_STATE_EXITINFO2));
    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_FAULT_RFLAGS_MR, vmcb_read(vcpu, VMCB_STATE_GUEST_RFLAGS));
    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_FAULT_GUEST_INT_MR, 0);
    setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_FAULT_CR3_MR, vmcb_read(vcpu, VMCB_STATE_GUEST_CR3));

    for (i = 0; i < n_vcpu_gp_register; i++) {
        setMR(NODE_STATE(ksCurThread), buffer, SEL4_VMENTER_FAULT_EAX + i,
              NODE_STATE(ksCurThread)->tcbArch.tcbVCPU->gp_registers[i]);
    }
}

static void handleSvmFault(uint32_t reason, word_t qualification)
{
    setRegister(NODE_STATE(ksCurThread), msgInfoRegister, SEL4_VMENTER_RESULT_FAULT);
    setMRs_vmexit(reason, qualification);
    setThreadState(NODE_STATE(ksCurThread), ThreadState_Running);
    activateThread();
}

exception_t handleVmexit(void)
{
    vcpu_t *vcpu = ARCH_NODE_STATE(x86KSCurrentVCPU);
    uint32_t reason;
    word_t qualification;

    assert(vcpu == NODE_STATE(ksCurThread)->tcbArch.tcbVCPU);
    vcpu->launched = true;
    vcpu->cached_cr0 = vmcb_read(vcpu, VMCB_STATE_GUEST_CR0);
    vcpu->cr0 = vcpu->cached_cr0;

    NODE_LOCK_SYS;

    reason = (uint32_t)vmcb_read(vcpu, VMCB_STATE_EXITCODE);
    qualification = vmcb_read(vcpu, VMCB_STATE_EXITINFO1);
    handleSvmFault(reason, qualification);

    return EXCEPTION_NONE;
}

exception_t handleVmEntryFail(void)
{
    handleSvmFault((uint32_t)-1, (word_t)-1);
    return EXCEPTION_NONE;
}

void vcpu_fpu_to_guest(tcb_t *tcb, vcpu_t *vcpu)
{
    if (nativeThreadUsingFPU(tcb)) {
        enableFpu();
        if (!vcpu->fpu_active) {
            saveFpuState(tcb);
            vcpu->fpu_active = true;
            loadFpuState(tcb);
        }
    } else {
        vcpu->fpu_active = true;
        switchLocalFpuOwner(tcb);
    }
}

void vcpu_fpu_to_host(tcb_t *tcb, vcpu_t *vcpu)
{
    if (tcb->tcbFlags & seL4_TCBFlag_fpuDisabled) {
        disableFpu();
    } else if (nativeThreadUsingFPU(tcb) && vcpu->fpu_active) {
        saveFpuState(tcb);
        vcpu->fpu_active = false;
        loadFpuState(tcb);
    } else if (!nativeThreadUsingFPU(tcb)) {
        switchLocalFpuOwner(tcb);
    }
}

static void flush_guest_tlb(svasid_t svasid)
{
    asm volatile("invlpga (%0), %1" :: "r"(0), "r"((word_t)svasid) : "memory");
}

static void setNPTRoot(cap_t nptSpace, vcpu_t *vcpu)
{
    paddr_t npt_root;
    if (cap_get_capType(nptSpace) != cap_npt_pml4_cap ||
        !cap_npt_pml4_cap_get_capPML4IsMapped(nptSpace)) {
        npt_root = kpptr_to_paddr(null_npt_space);
    } else {
        findNPTForASID_ret_t find_ret;
        npt_pml4e_t *pml4;

        pml4 = (npt_pml4e_t *)cap_npt_pml4_cap_get_capPML4BasePtr(nptSpace);
        find_ret = findNPTForASID(cap_npt_pml4_cap_get_capPML4MappedASID(nptSpace));
        if (find_ret.status != EXCEPTION_NONE || find_ret.npt != pml4) {
            npt_root = kpptr_to_paddr(null_npt_space);
        } else {
            npt_root = pptr_to_paddr(pml4);
        }
    }
    if (npt_root != vcpu->last_npt_root) {
        vcpu->last_npt_root = npt_root;
        vmcb_write(vcpu, VMCB_CONTROL_NCR3, npt_root);
        if (vcpu->svasid != SVASID_INVALID) {
            flush_guest_tlb(vcpu->svasid);
        }
    }
}

static svasid_t nextSVASID(svasid_t svasid)
{
    if (svasid == SVASID_LAST) {
        return SVASID_FIRST;
    }
    return svasid + 1;
}

static void invalidateSVASID(svasid_t svasid)
{
    vcpu_t *vcpu = x86KSSVASIDTable[svasid];
    memset(vcpu->io, ~0, sizeof(vcpu->io));
    flush_guest_tlb(svasid);
}

static void releaseSVASID(svasid_t svasid)
{
    vcpu_t *vcpu = x86KSSVASIDTable[svasid];
    invalidateSVASID(svasid);
    vcpu->svasid = SVASID_INVALID;
    x86KSSVASIDTable[svasid] = NULL;
}

static svasid_t findFreeSVASID(void)
{
    svasid_t svasid = x86KSNextSVASID;
    do {
        if (x86KSSVASIDTable[svasid] == NULL) {
            return svasid;
        }
        svasid = nextSVASID(svasid);
    } while (svasid != x86KSNextSVASID);

    svasid = x86KSNextSVASID;
    releaseSVASID(svasid);
    x86KSNextSVASID = nextSVASID(x86KSNextSVASID);
    return svasid;
}

static void storeSVASID(vcpu_t *vcpu, svasid_t svasid)
{
    assert(x86KSSVASIDTable[svasid] == NULL);
    assert(vcpu->svasid == SVASID_INVALID);
    x86KSSVASIDTable[svasid] = vcpu;
    vcpu->svasid = svasid;
    vmcb_write(vcpu, VMCB_CONTROL_GUEST_ASID, svasid);
}

void restoreSVM(void)
{
    tcb_t *cur_thread = NODE_STATE(ksCurThread);
    vcpu_t *expected = cur_thread->tcbArch.tcbVCPU;

    if (ARCH_NODE_STATE(x86KSCurrentVCPU) != expected) {
        switchVCPU(expected);
    }

#ifndef CONFIG_KERNEL_SKIM_WINDOW
    if (getCurrentCR3().words[0] != expected->last_host_cr3) {
        expected->last_host_cr3 = getCurrentCR3().words[0];
        vmcb_write(expected, VMCB_CONTROL_HOST_CR3, getCurrentCR3().words[0]);
    }
#endif

    if (expected->svasid == SVASID_INVALID) {
        svasid_t svasid = findFreeSVASID();
        storeSVASID(expected, svasid);
    }
    setNPTRoot(TCB_PTR_CTE_PTR(cur_thread, tcbArchNPTRoot)->cap, expected);
}

void invnpt(npt_pml4e_t *npt_pml4)
{
    vcpu_t *vcpu = ARCH_NODE_STATE(x86KSCurrentVCPU);
    (void)npt_pml4;
    if (vcpu && vcpu->svasid != SVASID_INVALID) {
        flush_guest_tlb(vcpu->svasid);
    }
}

void clearSVASIDIOPortMappings(svasid_t svasid, uint16_t first, uint16_t last)
{
  (void)svasid;
  (void)first;
  (void)last;
}

#ifdef ENABLE_SMP_SUPPORT
void VMCheckBoundNotification(tcb_t *tcb)
{
    assert(tcb->tcbAffinity == getCurrentCPUIndex());
    notification_t *ntfnPtr = tcb->tcbBoundNotification;
    if (thread_state_ptr_get_tsType(&tcb->tcbState) == ThreadState_RunningVM
        && ntfnPtr && notification_ptr_get_state(ntfnPtr) == NtfnState_Active) {

        word_t badge = notification_ptr_get_ntfnMsgIdentifier(ntfnPtr);
        notification_ptr_set_state(ntfnPtr, NtfnState_Idle);
        setThreadState(tcb, ThreadState_Running);
        setRegister(tcb, badgeRegister, badge);
        Arch_leaveVMAsyncTransfer(tcb);
        if (tcb != NODE_STATE(ksCurThread)) {
            possibleSwitchTo(tcb);
        }
    }
}
#endif /* ENABLE_SMP_SUPPORT */

#endif /* CONFIG_SVM */