/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/rtc.h>
#include <pbl/logging/logging.h>
#include "pbl/services/new_timer/new_timer.h"
#include "system/passert.h"

#include "bf0_hal.h"

PBL_LOG_MODULE_DECLARE(driver_rtc_sf32lb, CONFIG_DRIVER_RTC_LOG_LEVEL);

#define RC10K_DEFAULT_FREQ_HZ 10000UL
#define RC10K_CAL_PERIOD_MS 15000U

static TimerID s_rc10k_cal_timer;

static void prv_rc10k_cal_timer_cb(void *data) {
  uint8_t lp_cycle;

  lp_cycle = HAL_RC_CAL_GetLPCycle();
  // A dropped sample leaves the RTC running on a stale reference; the LCPU also
  // drives RC calibration here, so losing the mailbox is expected but must not
  // be silent - a run of these means the clock is drifting uncorrected.
  const int rv = HAL_RC_CAL_update_reference_cycle_on_48M(lp_cycle);
  if (rv != 0) {
    PBL_LOG_WRN("RC10K calibration failed: %d", rv);
  }
}

#if !defined(CONFIG_BT_FW_NIMBLE)
// Calibration runs in the LCPU power domain, so the core must be up. With a
// Bluetooth stack the transport does this; without one, nobody else will.
extern void lcpu_custom_nvds_config(void);
extern uint8_t lcpu_power_on(void);
#endif

void rc10k_init(void) {
#if !defined(CONFIG_BT_FW_NIMBLE)
  lcpu_custom_nvds_config();
  lcpu_power_on();
#endif

  prv_rc10k_cal_timer_cb(NULL);

  s_rc10k_cal_timer = new_timer_create();
  PBL_ASSERTN(s_rc10k_cal_timer != TIMER_INVALID_ID);

  bool success = new_timer_start(s_rc10k_cal_timer, RC10K_CAL_PERIOD_MS, prv_rc10k_cal_timer_cb,
                                 NULL, TIMER_START_FLAG_REPEATING);
  PBL_ASSERTN(success);
}

uint32_t rc10k_get_freq_hz(void) {
  uint32_t hxt48_cyc;
  
  hxt48_cyc = HAL_RC_CAL_get_average_cycle_on_48M();
  if (hxt48_cyc == 0UL) {
    return RC10K_DEFAULT_FREQ_HZ;
  } else {
    return (48000000ULL * HAL_RC_CAL_GetLPCycle()) / hxt48_cyc;
  }
}

uint32_t rc10k_cyc_to_milli_ticks(uint32_t rc10k_cyc) {
  uint32_t hxt48_cyc;

  hxt48_cyc = HAL_RC_CAL_get_average_cycle_on_48M();
  if (hxt48_cyc == 0UL) {
    return (1000ULL * RTC_TICKS_HZ * rc10k_cyc) / RC10K_DEFAULT_FREQ_HZ;
  } else {
    return (1000ULL * RTC_TICKS_HZ * rc10k_cyc * hxt48_cyc) / (48000000ULL * HAL_RC_CAL_GetLPCycle());
  }
}