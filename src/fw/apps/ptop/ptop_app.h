/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "process_management/pebble_process_md.h"

const PebbleProcessMd *ptop_app_get_app_info(void);

//! True between 08:00 and 17:00 local time. Drives both the screen palette
//! and the backlight colour.
bool ptop_is_daytime(void);
