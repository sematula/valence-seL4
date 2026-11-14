/*
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <config.h>

#if defined(CONFIG_VTX) || defined(CONFIG_SVM)
#define CONFIG_HARDWARE_VIRT 1
#endif