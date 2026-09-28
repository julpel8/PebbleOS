/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "syscall.h"
#include "syscall_internal.h"

#include "applib/system_metrics.h"
#include "kernel/pbl_malloc.h"
#include "pbl/drivers/battery.h"
#include "pbl/services/battery/battery_state.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

DEFINE_SYSCALL(void, sys_battery_metrics_get, BatteryMetrics *metrics) {
  if (PRIVILEGE_WAS_ELEVATED) {
    syscall_assert_userspace_buffer(metrics, sizeof(*metrics));
  }

  const BatteryChargeState charge = battery_get_charge_state();
  BatteryConstants constants;
  const bool has_electrical = (battery_get_constants(&constants) == 0);

  *metrics = (BatteryMetrics) {
    .charge_percent = charge.charge_percent,
    .is_charging = charge.is_charging,
    .is_plugged = charge.is_plugged,
    .has_electrical = has_electrical,
    .voltage_mv = has_electrical ? constants.v_mv : 0,
    .current_ua = has_electrical ? constants.i_ua : 0,
    .time_to_empty_s = battery_state_get_time_to_empty(),
  };
}

DEFINE_SYSCALL(bool, sys_cpu_metrics_get, CpuMetrics *metrics) {
  if (PRIVILEGE_WAS_ELEVATED) {
    syscall_assert_userspace_buffer(metrics, sizeof(*metrics));
  }

  // Leave room for a task created between the count and the snapshot.
  const UBaseType_t max_tasks = uxTaskGetNumberOfTasks() + 2;
  TaskStatus_t *status = kernel_malloc(max_tasks * sizeof(*status));
  if (!status) {
    return false;
  }

  uint32_t total = 0;
  const UBaseType_t count = uxTaskGetSystemState(status, max_tasks, &total);
  const TaskHandle_t idle_task = xTaskGetIdleTaskHandle();
  uint32_t idle = 0;
  for (UBaseType_t i = 0; i < count; i++) {
    if (status[i].xHandle == idle_task) {
      idle = status[i].ulRunTimeCounter;
      break;
    }
  }
  kernel_free(status);

  if (count == 0) {
    return false;
  }
  *metrics = (CpuMetrics) {
    .total_ticks = total,
    .idle_ticks = idle,
  };
  return true;
}
