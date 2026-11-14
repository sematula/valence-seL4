/*
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * VMCB layout constants adapted from FreeBSD sys/amd64/vmm/amd/vmcb.h (BSD-2-Clause).
 */

#pragma once

#include <config.h>

#ifdef CONFIG_SVM

/* Guest and host control fields (bytes from VMCB base). */
#define VMCB_CONTROL_INTERCEPT_INSTRUCTION1 0x00C
#define VMCB_CONTROL_INTERCEPT_INSTRUCTION2 0x010
#define VMCB_CONTROL_INTERCEPT_INSTRUCTION3 0x014
#define VMCB_CONTROL_GUEST_ASID             0x058
#define VMCB_CONTROL_TLB_CONTROL            0x05C
#define VMCB_CONTROL_NP_ENABLE              0x090
#define VMCB_CONTROL_NCR3                   0x0B0
#define VMCB_CONTROL_HOST_CR3               0x0D0
#define VMCB_CONTROL_HOST_RIP               0x0D8
#define VMCB_CONTROL_HOST_RSP               0x0DC

/* State save area begins at 0x400. */
#define VMCB_STATE_SAVE_BASE                0x400
#define VMCB_STATE_EXITCODE                 0x440
#define VMCB_STATE_EXITINFO1                0x448
#define VMCB_STATE_EXITINFO2                0x450
#define VMCB_STATE_EXITINTINFO               0x458
#define VMCB_STATE_GUEST_RIP                0x568
#define VMCB_STATE_GUEST_RSP                0x5D8
#define VMCB_STATE_GUEST_RFLAGS             0x570
#define VMCB_STATE_GUEST_CR3                0x5B0
#define VMCB_STATE_GUEST_CR0                0x548
#define VMCB_STATE_GUEST_CR4                0x558

/* Intercept bits (instruction intercept word 1). */
#define VMCB_INTERCEPT_CPUID                BIT(10)
#define VMCB_INTERCEPT_IO                   BIT(27)
#define VMCB_INTERCEPT_MSR                  BIT(28)
#define VMCB_INTERCEPT_VMRUN                BIT(4)

/* SVM exit codes (low 64 bits of exitcode). */
#define SVM_EXIT_CPUID          0x072ULL
#define SVM_EXIT_IO             0x07BULL
#define SVM_EXIT_MSR            0x07CULL
#define SVM_EXIT_VMRUN          0x080ULL
#define SVM_EXIT_NPF            0x0400ULL

#endif /* CONFIG_SVM */