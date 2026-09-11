/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "shell/shell_event_loop.h"

#include "git_version.auto.h"
#include "pbl/drivers/rtc.h"

// The ptop shell has no UI of its own: the watchface is the whole shell.

void shell_event_loop_init(void) {
  // Nothing sets the clock here: no radio, and no settings app. Start from
  // the build date rather than from whatever the RTC lost.
  if (rtc_get_time() < GIT_TIMESTAMP) {
    rtc_set_time(GIT_TIMESTAMP);
  }
}

void shell_event_loop_handle_event(PebbleEvent *e) {
}
