/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system_metrics.h"

#include "syscall/syscall.h"

void battery_metrics_get(BatteryMetrics *metrics) {
  sys_battery_metrics_get(metrics);
}

bool cpu_metrics_get(CpuMetrics *metrics) {
  return sys_cpu_metrics_get(metrics);
}

uint8_t cpu_metrics_load_percent(const CpuMetrics *before, const CpuMetrics *after) {
  if (!before || !after) {
    return 0;
  }
  // Unsigned subtraction survives the counters wrapping.
  const uint32_t total = after->total_ticks - before->total_ticks;
  const uint32_t idle = after->idle_ticks - before->idle_ticks;
  if (total == 0 || idle > total) {
    return 0;
  }
  return (uint8_t)(((uint64_t)(total - idle) * 100) / total);
}
