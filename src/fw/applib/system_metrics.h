/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

//! @addtogroup Foundation
//! @{
//!   @addtogroup SystemMetrics
//! \brief Read battery and CPU load figures
//!
//! The SystemMetrics API gives apps the raw figures a system monitor needs:
//! battery voltage, current and estimated time left, and the CPU time spent
//! idle. The CPU figures are counters: take two samples some time apart and
//! pass them to \ref cpu_metrics_load_percent.
//! @{

//! Battery readings.
typedef struct {
  //! State of charge, 0 to 100.
  uint8_t charge_percent;
  //! True if the battery is charging.
  bool is_charging;
  //! True if the charger is plugged in.
  bool is_plugged;
  //! True if voltage_mv and current_ua hold a reading.
  bool has_electrical;
  //! Battery voltage in millivolts.
  int32_t voltage_mv;
  //! Battery current in microamps, as reported by the power chip.
  int32_t current_ua;
  //! Estimated seconds before the battery is empty, 0 if unknown.
  uint32_t time_to_empty_s;
} BatteryMetrics;

//! CPU time counters, in RTC ticks since boot.
typedef struct {
  //! Time counted for every task, idle included.
  uint32_t total_ticks;
  //! Time spent in the idle task.
  uint32_t idle_ticks;
} CpuMetrics;

//! Read the battery.
//! @param[out] metrics Filled with the current readings.
void battery_metrics_get(BatteryMetrics *metrics);

//! Read the CPU time counters.
//! @param[out] metrics Filled with the current counters.
//! @return false if the counters could not be read.
bool cpu_metrics_get(CpuMetrics *metrics);

//! CPU load between two samples.
//! @param before The older sample.
//! @param after The newer sample.
//! @return Share of the time spent outside the idle task, 0 to 100.
uint8_t cpu_metrics_load_percent(const CpuMetrics *before, const CpuMetrics *after);

//!   @} // end addtogroup SystemMetrics
//! @} // end addtogroup Foundation
