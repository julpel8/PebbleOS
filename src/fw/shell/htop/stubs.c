/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! @file stubs.c
//!
//! The htop firmware runs a single watchface and nothing else: no radio, no
//! filesystem, no app installs. This file fills in what the kernel and the
//! common services still reference from the subsystems that are switched off.

#include "pbl/util/uuid.h"
#include "board/board.h"
#include "kernel/events.h"
#include "popups/crashed_ui.h"
#include "popups/notifications/notification_window.h"
#include "process_management/app_install_manager.h"
#include "process_management/pebble_process_md.h"
#include "resource/resource_ids.auto.h"
#include "resource/resource_storage_file.h"
#include "pbl/services/light.h"
#include "pbl/services/notifications/do_not_disturb.h"
#include "pbl/services/notifications/alerts_private.h"
#include "pbl/services/persist.h"
#include "shell/prefs.h"
#include "shell/system_theme.h"
#include "applib/app_comm.h"
#include "comm/bt_conn_mgr.h"
#include "pbl/services/accel_manager.h"
#include "pbl/services/activity/activity.h"
#include "pbl/services/analytics/analytics.h"
#include "pbl/services/app_cache.h"
#include "pbl/services/app_glances/app_glance_service.h"
#include "pbl/services/audio_endpoint.h"
#include "pbl/services/comm_session/session.h"
#include "pbl/services/compositor/compositor.h"
#include "pbl/services/data_logging/data_logging_service.h"
#include "pbl/services/notifications/alerts.h"
#include "pbl/services/notifications/alerts_preferences_private.h"
#include "pbl/services/runlevel.h"
#include "pbl/services/vibe_pattern.h"
#include "pbl/services/vibes/vibe_client.h"
#include "pbl/services/vibes/vibe_score.h"
#include "pbl/services/vibes/vibe_score_info.h"
#include "pbl/util/attributes.h"
#include "syscall/syscall.h"

void app_fetch_binaries(const Uuid *uuid, AppInstallId app_id, bool has_worker) {
}

void app_idle_timeout_stop(void) {
}

void watchface_set_default_install_id(AppInstallId id) {
}

void watchface_handle_button_event(PebbleEvent *e) {
}

void app_idle_timeout_refresh(void) {
}

void app_idle_timeout_touch_down(void) {
}

void app_idle_timeout_touch_up(void) {
}

PebblePhoneCaller* phone_call_util_create_caller(const char *number, const char *name) {
  return NULL;
}

void alarm_set_snooze_delay(int delay_ms) {
}

const void* const g_pbl_system_tbl[] = {};


void persist_service_client_open(const Uuid *uuid) {
}

void persist_service_client_close(const Uuid *uuid) {
}

SettingsFile * persist_service_lock_and_get_store(const Uuid *uuid) {
  return NULL;
}

status_t persist_service_delete_file(const Uuid *uuid) {
  return E_INVALID_OPERATION;
}

void wakeup_enable(bool enable) {
}

bool phone_call_is_using_ANCS(void) {
  return true;
}

#include "pbl/services/blob_db/app_db.h"
#include "pbl/services/app_cache.h"
#include "pbl/services/blob_db/pin_db.h"

status_t pin_db_delete_with_parent(const TimelineItemId *parent_id) {
  return E_INVALID_OPERATION;
}

status_t app_cache_add_entry(AppInstallId app_id, uint32_t total_size) {
  return E_INVALID_OPERATION;
}

status_t app_cache_remove_entry(AppInstallId id) {
  return E_INVALID_OPERATION;
}

status_t app_db_insert(const uint8_t *key, int key_len, const uint8_t *val, int val_len) {
  return E_INVALID_OPERATION;
}

status_t app_db_delete(const uint8_t *key, int key_len) {
  return E_INVALID_OPERATION;
}

AppInstallId app_db_check_next_unique_id(void) {
  return 0;
}

void app_db_enumerate_entries(AppDBEnumerateCb cb, void *data) {
}

AppInstallId app_db_get_install_id_for_uuid(const Uuid *uuid) {
  return 0;
}

status_t app_db_get_app_entry_for_install_id(AppInstallId app_id, AppDBEntry *entry) {
  return E_INVALID_OPERATION;
}

bool app_db_exists_install_id(AppInstallId app_id) {
  return false;
}

void timeline_item_destroy(TimelineItem* item) {
}

AppInstallId worker_preferences_get_default_worker(void) {
  return INSTALL_ID_INVALID;
}

#include "process_management/process_loader.h"
void * process_loader_load(const PebbleProcessMd *app_md, PebbleTask task,
                         MemorySegment *destination) {
  return app_md->main_func;
}

#include "pbl/services/process_management/app_storage.h"
AppStorageGetAppInfoResult app_storage_get_process_info(PebbleProcessInfo* app_info,
                                                        uint8_t *build_id_out,
                                                        AppInstallId app_id,
                                                        PebbleTask task) {
  return GET_APP_INFO_COULD_NOT_READ_FORMAT;
}

void app_storage_get_file_name(char *name, size_t buf_length, AppInstallId app_id,
                               PebbleTask task) {
  // Empty string
  *name = 0;
}

bool shell_prefs_get_clock_24h_style(void) {
  return true;
}

void shell_prefs_set_clock_24h_style(bool is_24h_style) {
}

bool shell_prefs_is_timezone_source_manual(void) {
  return false;
}

void shell_prefs_set_timezone_source_manual(bool manual) {
}

void shell_prefs_set_automatic_timezone_id(int16_t timezone_id) {
}

int16_t shell_prefs_get_automatic_timezone_id(void) {
  return -1;
}

bool shell_prefs_can_coredump_on_request() {
  // it would be good to have a core dump escape hatch in PRF
  return true;
}

// PRF has no preference storage, so the content size is fixed at the runtime platform default.
void system_theme_set_content_size(PreferredContentSize content_size) {
}

PreferredContentSize system_theme_get_content_size(void) {
  return system_theme_get_default_content_size_for_runtime_platform();
}

AlertMask alerts_get_mask(void) {
  return AlertMaskAllOff;
}

bool do_not_disturb_is_active(void) {
  return true;
}

BacklightBehaviour backlight_get_behaviour(void) {
  return BacklightBehaviour_On;
}

bool backlight_is_enabled(void) {
  return true;
}

bool backlight_is_ambient_sensor_enabled(void) {
  return false;
}

bool backlight_is_motion_enabled(void) {
  return false;
}

BacklightTouchWake backlight_get_touch_wake(void) {
  return BacklightTouchWake_Off;
}

void backlight_set_touch_wake(BacklightTouchWake wake) {
}

bool touch_is_globally_enabled(void) {
  return true;
}

void touch_set_globally_enabled(bool enable) {
}


bool touch_navigation_menu_is_enabled(void) {
  return false;
}

void touch_set_navigation_menu_enabled(bool enable) {
}

bool bt_persistent_storage_get_airplane_mode_enabled(void) {
  return false;
}

void bt_persistent_storage_set_airplane_mode_enabled(bool *state) {
}

uint32_t backlight_get_timeout_ms(void) {
  return DEFAULT_BACKLIGHT_TIMEOUT_MS;
}

uint8_t backlight_get_intensity(void) {
  return 100U;
}

#ifdef CONFIG_BACKLIGHT_HAS_COLOR
uint32_t backlight_get_default_color(void) {
  return BOARD_CONFIG.backlight_default_color;
}

void backlight_set_default_color(uint32_t rgb_color) {
}
#endif

bool shell_prefs_get_language_english(void) {
  return true;
}
void shell_prefs_set_language_english(bool english) {
}
void shell_prefs_toggle_language_english(void) {
}
ShellLanguage shell_prefs_get_language(void) {
  return ShellLanguageEnglish;
}
uint32_t shell_prefs_get_language_resource_id(void) {
  return RESOURCE_ID_INVALID;
}
void shell_prefs_set_language(ShellLanguage language) {
}



void pbl_analytics_external_collect_pfs_stats(void) {
}

void pbl_analytics_external_collect_settings(void) {
}


// ---------------------------------------------------------------------------
// Services switched off in this firmware.

void services_normal_early_init(void) {
}

void services_normal_init(void) {
}

void services_normal_set_runlevel(RunLevel runlevel) {
}

void boot_splash_start(void) {
}

void boot_splash_stop(void) {
}

void check_prf_update(void) {
}

bool is_unread_coredump_available(void) {
  return false;
}

void language_ui_display_changed(const char *lang_name) {
}

void app_glance_service_init_glance(AppGlance *glance) {
}

void dls_inactivate_sessions(PebbleTask task) {
}

void speaker_service_stop_for_task(PebbleTask task) {
}

void audio_endpoint_cancel_transfer(AudioEndpointSessionId session_id) {
}

// ---------------------------------------------------------------------------
// Bluetooth and the phone connection.

void bt_lock_init(void) {
}

void bt_ctl_init(void) {
}

void bt_ctl_set_enabled(bool enabled) {
}

void bt_persistent_storage_init(void) {
}

void gatt_service_changed_server_handle_fw_update(void) {
}

void kernel_le_client_handle_event(const PebbleEvent *event) {
}

void comm_session_init(void) {
}

void comm_default_kernel_sender_init(void) {
}

CommSession *comm_session_get_system_session(void) {
  return NULL;
}

bool comm_session_has_capability(CommSession *session, CommSessionCapability capability) {
  return false;
}

bool comm_session_send_data(CommSession *session, uint16_t endpoint_id, const uint8_t *data,
                            size_t length, uint32_t timeout_ms) {
  return false;
}

void comm_session_set_responsiveness(CommSession *session, BtConsumer consumer,
                                     ResponseTimeState state, uint16_t max_period_secs) {
}

void comm_session_app_session_capabilities_init(void) {
}

void comm_session_app_session_capabilities_evict(const Uuid *app_uuid) {
}

void debounced_connection_service_init(void) {
}

void debounced_connection_service_handle_event(PebbleCommSessionEvent *e) {
}

void poll_remote_init(void) {
}

void shared_prf_storage_init(void) {
}

void put_bytes_init(void) {
}

void put_bytes_handle_comm_session_event(const PebbleCommSessionEvent *app_event) {
}

void firmware_update_init(void) {
}

bool firmware_update_is_in_progress(void) {
  return false;
}

void firmware_update_event_handler(PebbleSystemMessageEvent *event) {
}

void firmware_update_pb_event_handler(PebblePutBytesEvent *event) {
}

void app_fetch_put_bytes_event_handler(PebblePutBytesEvent *pb_event) {
}

void sys_app_comm_set_responsiveness(SniffInterval interval) {
}

void app_inbox_service_unregister_all(void) {
}

uint32_t sys_app_inbox_service_unregister(uint8_t *storage) {
  return 0;
}

void app_outbox_service_cleanup_all_pending_messages(void) {
}

void app_outbox_service_cleanup_event(PebbleEvent *event) {
}

// ---------------------------------------------------------------------------
// Analytics.

void pbl_analytics_init(void) {
}

void sys_pbl_analytics_add(enum pbl_analytics_key key, int32_t amount) {
}

void sys_pbl_analytics_set_string(enum pbl_analytics_key key, const char *value) {
}

void sys_pbl_analytics_timer_start(enum pbl_analytics_key key) {
}

void sys_pbl_analytics_timer_stop(enum pbl_analytics_key key) {
}

// ---------------------------------------------------------------------------
// Sensors and haptics.

bool sys_activity_get_metric(ActivityMetric metric, uint32_t history_len, int32_t *history) {
  return false;
}

void vibes_init(void) {
}

void vibe_service_set_enabled(bool enable) {
}

void vibe_pattern_clear_for_owner(VibePatternOwner owner) {
}

void sys_vibe_history_stop_collecting(void) {
}

void sys_vibe_history_start_collecting(void) {
}

bool sys_vibe_history_was_vibrating(uint64_t time_search) {
  return false;
}

int32_t sys_vibe_get_vibe_strength(void) {
  return 0;
}

int32_t vibes_get_vibe_strength(void) {
  return 0;
}

uint32_t vibes_get_time_since_last_vibe_ms(void) {
  return UINT32_MAX;
}

bool shell_prefs_get_accel_shake_log_info_enabled(void) {
  return false;
}

uint8_t shell_prefs_get_motion_sensitivity(void) {
  // Same default as the normal shell.
  return 55;
}

bool sys_vibe_pattern_enqueue_step(uint32_t step_duration_ms, bool on) {
  return false;
}

void sys_vibe_pattern_trigger_start(void) {
}

VibeScore *vibe_score_create_with_resource_system(ResAppNum app_num, uint32_t resource_id) {
  return NULL;
}

void vibe_score_do_vibe(VibeScore *score) {
}

void vibe_score_destroy(VibeScore *score) {
}

uint32_t vibe_score_info_get_resource_id(VibeScoreId id) {
  return 0;
}

bool alerts_should_vibrate_for_type(AlertType type) {
  return false;
}

VibeScoreId alerts_preferences_get_vibe_score_for_client(VibeClient client) {
  return VibeScoreId_Invalid;
}

bool alerts_preferences_dnd_get_motion_backlight(void) {
  return false;
}

bool alerts_preferences_dnd_get_touch_backlight(void) {
  return false;
}

// ---------------------------------------------------------------------------
// Time-driven services with nothing to reschedule.

void alarm_handle_clock_change(void) {
}

void cron_service_init(void) {
}

void cron_service_handle_clock_change(PebbleSetTimeEvent *set_time_info) {
}

void do_not_disturb_handle_clock_change(void) {
}

void wakeup_handle_clock_change(void) {
}

void wakeup_handle_significant_clock_change(void) {
}

// ---------------------------------------------------------------------------
// App installs: the watchface is compiled in, so there is nothing to launch,
// cache or time out.

bool app_cache_entry_exists(AppInstallId app_id) {
  return false;
}

status_t app_cache_app_launched(AppInstallId app_id) {
  return S_SUCCESS;
}

void app_idle_timeout_pause(void) {
}

void app_idle_timeout_resume(void) {
}

AppInstallId watchface_get_default_install_id(void) {
  return INSTALL_ID_INVALID;
}

void watchface_launch_default(const CompositorTransition *animation) {
}

void watchface_reset_click_manager(void) {
}

void worker_preferences_set_default_worker(AppInstallId id) {
}

LegacyAppRenderMode shell_prefs_get_legacy_app_render_mode(void) {
  return LegacyAppRenderMode_Bezel;
}

int16_t timeline_peek_get_origin_y(void) {
  return DISP_ROWS;
}

int16_t timeline_peek_get_obstruction_origin_y(void) {
  return DISP_ROWS;
}

void timeline_peek_handle_process_start(void) { }

void timeline_peek_handle_process_kill(void) { }

#if TIMELINE_PEEK_WATCHFACE_FIT_SUPPORTED
TimelinePeekUnsupportedFaceMode timeline_peek_prefs_get_unsupported_face_mode(void) {
  return TimelinePeekUnsupportedFaceMode_None;
}
#endif
