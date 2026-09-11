/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "shell/system_app_state_machine.h"

#include "apps/core/panic_window.h"
#include "apps/ptop/ptop_app.h"
#include "apps/system_app_ids.h"
#include "kernel/panic.h"
#include "process_management/app_manager.h"

const PebbleProcessMd *system_app_state_machine_system_start(void) {
  if (launcher_panic_get_current_error() != 0) {
    return panic_app_get_app_info();
  }

  return ptop_app_get_app_info();
}

AppInstallId system_app_state_machine_get_last_registered_app(void) {
  return APP_ID_PTOP;
}

const PebbleProcessMd *system_app_state_machine_get_default_app(void) {
  return ptop_app_get_app_info();
}

void system_app_state_machine_register_app_launch(AppInstallId app_id) {
}

void system_app_state_machine_panic(void) {
  if (app_manager_is_first_app_launched()) {
    app_manager_launch_new_app(&(AppLaunchConfig) {
      .md = panic_app_get_app_info(),
    });
  }
}
