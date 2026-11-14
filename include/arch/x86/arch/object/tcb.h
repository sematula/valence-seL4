/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <config.h>
#include <arch/virt.h>
#include <types.h>
#include <object/structures.h>

#ifdef CONFIG_HARDWARE_VIRT
void Arch_leaveVMAsyncTransfer(tcb_t *tcb);
#endif
#ifdef CONFIG_VTX
exception_t decodeSetEPTRoot(cap_t cap);
#endif
#ifdef CONFIG_SVM
exception_t decodeSetNPTRoot(cap_t cap);
#endif
