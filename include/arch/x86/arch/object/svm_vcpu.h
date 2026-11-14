/*
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * VMCB layout/intercept constants adapted from FreeBSD
 * sys/amd64/vmm/amd/vmcb.h (BSD-2-Clause).
 */

#pragma once

#include <config.h>
#include <api/failures.h>
#include <arch/object/svm_vmcb.h>

#define VCPU_VMCB_SIZE 4096
#define VCPU_IOBITMAP_SIZE 8192

#define SVASID_INVALID 0
#define SVASID_FIRST 1
#define SVASID_LAST (CONFIG_MAX_SVASIDS - 1)

typedef uint16_t svasid_t;

#ifdef CONFIG_SVM
#define SVM_TERNARY(svm, nonsvm) svm

enum vcpu_gp_register {
    VCPU_EAX = 0,
    VCPU_EBX,
    VCPU_ECX,
    VCPU_EDX,
    VCPU_ESI,
    VCPU_EDI,
    VCPU_EBP,
#ifdef CONFIG_X86_64_SVM_64BIT_GUESTS
    VCPU_R8,
    VCPU_R9,
    VCPU_R10,
    VCPU_R11,
    VCPU_R12,
    VCPU_R13,
    VCPU_R14,
    VCPU_R15,
#endif
    n_vcpu_gp_register,
    VCPU_ESP,
};

#ifdef CONFIG_X86_64_SVM_64BIT_GUESTS
enum vcpu_guest_syscall_register {
    VCPU_STAR = 0,
    VCPU_CSTAR,
    VCPU_LSTAR,
    VCPU_SYSCALL_MASK,
    n_vcpu_guest_syscall_register,
};
#endif

typedef enum vcpu_gp_register vcpu_gp_register_t;

const vcpu_gp_register_t crExitRegs[];

struct vcpu {
    char vmcb[VCPU_VMCB_SIZE];
    word_t io[VCPU_IOBITMAP_SIZE / sizeof(word_t)];

    user_fpu_state_t fpuState;

    word_t gp_registers[n_vcpu_gp_register];

    struct tcb *vcpuTCB;
    bool_t launched;
    bool_t fpu_active;

    svasid_t svasid;

    word_t cr0;
    word_t cr0_shadow;
    word_t cr0_mask;
    word_t exception_bitmap;

    word_t cached_exception_bitmap;
    word_t cached_cr0_shadow;
    word_t cached_cr0_mask;
    word_t cached_cr0;

#ifdef CONFIG_X86_64_SVM_64BIT_GUESTS
    word_t syscall_registers[n_vcpu_guest_syscall_register];
#endif

    word_t last_npt_root;

#ifndef CONFIG_KERNEL_SKIM_WINDOW
    word_t last_host_cr3;
#endif

#ifdef ENABLE_SMP_SUPPORT
    word_t last_cpu;
#endif /* ENABLE_SMP_SUPPORT */
};
typedef struct vcpu vcpu_t;

compile_assert(vcpu_size_sane, sizeof(vcpu_t) <= BIT(seL4_X86_VCPUBits))
unverified_compile_assert(vcpu_fpu_state_alignment_valid,
                          OFFSETOF(vcpu_t, fpuState) % MIN_FPU_ALIGNMENT == 0)

void vcpu_init(vcpu_t *vcpu);
void vcpu_finalise(vcpu_t *vcpu);

exception_t decodeX86VCPUInvocation(
    word_t invLabel,
    word_t length,
    cptr_t cptr,
    cte_t *slot,
    cap_t cap,
    bool_t call,
    word_t *buffer
);

void vcpu_update_state_sysvmenter(vcpu_t *vcpu);
void vcpu_sysvmenter_reply_to_user(tcb_t *tcb);

bool_t svm_init(void);
exception_t handleVmexit(void);
exception_t handleVmEntryFail(void);
void vcpu_fpu_to_guest(tcb_t *tcb, vcpu_t *vcpu);
void vcpu_fpu_to_host(tcb_t *tcb, vcpu_t *vcpu);
void restoreSVM(void);
void clearCurrentVCPU(void);

#ifdef ENABLE_SMP_SUPPORT
void VMCheckBoundNotification(tcb_t *tcb);
#endif /* ENABLE_SMP_SUPPORT */

void invnpt(npt_pml4e_t *npt_pml4);
void clearSVASIDIOPortMappings(svasid_t svasid, uint16_t first, uint16_t last);

static inline word_t vmcb_read(vcpu_t *vcpu, word_t field)
{
    if (field >= VCPU_VMCB_SIZE - sizeof(word_t)) {
        return 0;
    }
    return *(word_t *)(vcpu->vmcb + field);
}

static inline void vmcb_write(vcpu_t *vcpu, word_t field, word_t value)
{
    if (field >= VCPU_VMCB_SIZE - sizeof(word_t)) {
        return;
    }
    *(word_t *)(vcpu->vmcb + field) = value;
}

#else /* CONFIG_SVM */
#define SVM_TERNARY(svm, nonsvm) nonsvm
#endif /* CONFIG_SVM */