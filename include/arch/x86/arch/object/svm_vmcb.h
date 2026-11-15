/*
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * VMCB field offsets and intercept/exit constants adapted from FreeBSD
 * sys/amd64/vmm/amd/vmcb.h (BSD-2-Clause). See AMD APM Vol 2 App. B.
 */

#pragma once

#include <config.h>

#ifdef CONFIG_SVM

/* Control area (from VMCB base). */
#define VMCB_CONTROL_INTERCEPT_CR           0x000
#define VMCB_CONTROL_INTERCEPT_DR           0x004
#define VMCB_CONTROL_INTERCEPT_EXC          0x008
#define VMCB_CONTROL_INTERCEPT_INSTRUCTION1 0x00C
#define VMCB_CONTROL_INTERCEPT_INSTRUCTION2 0x010
#define VMCB_CONTROL_IOPM_BASE_PA           0x040
#define VMCB_CONTROL_MSRPM_BASE_PA          0x048
#define VMCB_CONTROL_TSC_OFFSET             0x050
#define VMCB_CONTROL_GUEST_ASID             0x058
#define VMCB_CONTROL_TLB_CONTROL            0x05C
#define VMCB_CONTROL_EXITCODE               0x070
#define VMCB_CONTROL_EXITINFO1              0x078
#define VMCB_CONTROL_EXITINFO2              0x080
#define VMCB_CONTROL_EXITINTINFO            0x088
#define VMCB_CONTROL_NP_ENABLE              0x090
#define VMCB_CONTROL_EVENTINJ               0x0A8
#define VMCB_CONTROL_NCR3                   0x0B0

/* State save area begins at 0x400 (offsets absolute from VMCB base).
 * Each segment is 16 bytes: selector, attrib, limit, base@+8. */
#define VMCB_STATE_SAVE_BASE                0x400
#define VMCB_STATE_FS                       0x440
#define VMCB_STATE_GS                       0x450
#define VMCB_STATE_GUEST_EFER               0x4D0
#define VMCB_STATE_GUEST_CR4                0x548
#define VMCB_STATE_GUEST_CR3                0x550
#define VMCB_STATE_GUEST_CR0                0x558
#define VMCB_STATE_GUEST_DR7                0x560
#define VMCB_STATE_GUEST_DR6                0x568
#define VMCB_STATE_GUEST_RFLAGS             0x570
#define VMCB_STATE_GUEST_RIP                0x578
#define VMCB_STATE_GUEST_RSP                0x5D8
#define VMCB_STATE_GUEST_RAX                0x5F8

/* intercept[INSTRUCTION1] bits (FreeBSD VMCB_INTCPT_*). */
#define VMCB_INTERCEPT_CPUID                BIT(18)
#define VMCB_INTERCEPT_HLT                  BIT(24)
#define VMCB_INTERCEPT_IO                   BIT(27)
#define VMCB_INTERCEPT_MSR                  BIT(28)
#define VMCB_INTERCEPT_SHUTDOWN             BIT(31)

/* intercept[INSTRUCTION2] bits — VMRUN must stay intercepted (no Nested SVM). */
#define VMCB_INTERCEPT_VMRUN                BIT(0)
#define VMCB_INTERCEPT_VMMCALL              BIT(1)

/* TLB_CONTROL values. */
#define VMCB_TLB_FLUSH_ALL                  1

/* Exit codes. */
#define SVM_EXIT_CPUID                      0x072ULL
#define SVM_EXIT_HLT                        0x078ULL
#define SVM_EXIT_IO                         0x07BULL
#define SVM_EXIT_MSR                        0x07CULL
#define SVM_EXIT_VMRUN                      0x080ULL
#define SVM_EXIT_NPF                        0x0400ULL

#endif /* CONFIG_SVM */
