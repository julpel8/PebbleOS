/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! @file htop_app.c
//!
//! A watchface that shows what the system is doing: uptime, battery, heap
//! usage and the FreeRTOS task table with a per-task share of the run time.

#include "htop_app.h"

#include "applib/app.h"
#include "applib/app_timer.h"
#include "applib/battery_state_service.h"
#include "applib/fonts/fonts.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/graphics_line.h"
#include "applib/graphics/text.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/ui.h"
#include "kernel/kernel_heap.h"
#include "kernel/pbl_malloc.h"
#include "pbl/drivers/battery.h"
#include "pbl/drivers/rtc.h"
#include "pbl/util/heap.h"
#include "pbl/util/math.h"
#include "pbl/services/clock.h"
#include "process_state/app_state/app_state.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define HTOP_REFRESH_MS 1000
//! Tasks tracked at once. The system runs about a dozen.
#define HTOP_MAX_TASKS 20

typedef struct {
  char name[12];
  char state;
  uint8_t cpu_pct;
  uint16_t stack_free;
  uint32_t run_time;
  UBaseType_t number;
} HtopTask;

typedef struct {
  Window window;
  Layer canvas;
  AppTimer *timer;
  GFont font;
  GFont font_bold;
  GFont font_clock;
  TaskStatus_t *status;

  char clock_text[8];
  uint32_t uptime_s;
  uint8_t battery_pct;
  bool charging;
  bool battery_valid;
  int32_t battery_mv;
  int32_t battery_ua;
  unsigned int kernel_used;
  unsigned int kernel_size;
  unsigned int app_used;
  unsigned int app_size;

  uint8_t num_tasks;
  HtopTask tasks[HTOP_MAX_TASKS];
} HtopData;

static HtopData *s_data;

static char prv_state_char(eTaskState state) {
  switch (state) {
    case eRunning: return 'X';
    case eReady: return 'R';
    case eBlocked: return 'B';
    case eSuspended: return 'S';
    case eDeleted: return 'D';
    default: return '?';
  }
}

//! Human sizes, htop style: bytes below 1 KiB, then kilobytes.
static void prv_format_size(char *buf, size_t buf_size, unsigned int bytes) {
  if (bytes < 1024) {
    snprintf(buf, buf_size, "%u", bytes);
  } else {
    snprintf(buf, buf_size, "%uk", bytes / 1024);
  }
}

//! Look up what a task had run for at the previous sample.
//! Battery draw as milliamps with one decimal, e.g. "-12.4mA".
static void prv_format_current(char *buf, size_t size, int32_t i_ua) {
  const int32_t tenths = i_ua / 100;
  snprintf(buf, size, "%s%u.%umA", (tenths < 0) ? "-" : "", (unsigned int)(ABS(tenths) / 10),
           (unsigned int)(ABS(tenths) % 10));
}

static uint32_t prv_previous_run_time(UBaseType_t number, bool *found) {
  for (unsigned int i = 0; i < s_data->num_tasks; i++) {
    if (s_data->tasks[i].number == number) {
      *found = true;
      return s_data->tasks[i].run_time;
    }
  }
  *found = false;
  return 0;
}

static void prv_sample_tasks(void) {
  const UBaseType_t count =
      uxTaskGetSystemState(s_data->status, HTOP_MAX_TASKS, NULL);

  HtopTask tasks[HTOP_MAX_TASKS];
  uint32_t total_delta = 0;
  uint32_t deltas[HTOP_MAX_TASKS];

  for (UBaseType_t i = 0; i < count; i++) {
    const TaskStatus_t *status = &s_data->status[i];
    HtopTask *task = &tasks[i];

    strncpy(task->name, status->pcTaskName, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';
    task->state = prv_state_char(status->eCurrentState);
    task->stack_free = status->usStackHighWaterMark * sizeof(StackType_t);
    task->run_time = status->ulRunTimeCounter;
    task->number = status->xTaskNumber;

    bool found;
    const uint32_t previous = prv_previous_run_time(task->number, &found);
    deltas[i] = found ? (task->run_time - previous) : 0;
    total_delta += deltas[i];
  }

  for (UBaseType_t i = 0; i < count; i++) {
    tasks[i].cpu_pct = total_delta ? ((deltas[i] * 100) / total_delta) : 0;
  }

  memcpy(s_data->tasks, tasks, count * sizeof(HtopTask));
  s_data->num_tasks = count;
}

static void prv_sample(void) {
  s_data->uptime_s = rtc_get_ticks() / RTC_TICKS_HZ;

  struct tm now;
  clock_get_time_tm(&now);
  snprintf(s_data->clock_text, sizeof(s_data->clock_text), "%02d:%02d", now.tm_hour, now.tm_min);

  const BatteryChargeState charge = battery_state_service_peek();
  s_data->battery_pct = charge.charge_percent;
  s_data->charging = charge.is_charging || charge.is_plugged;

  BatteryConstants constants;
  s_data->battery_valid = (battery_get_constants(&constants) == 0);
  if (s_data->battery_valid) {
    s_data->battery_mv = constants.v_mv;
    s_data->battery_ua = constants.i_ua;
  }

  unsigned int used, free_bytes, max_free;
  heap_calc_totals(kernel_heap_get(), &used, &free_bytes, &max_free);
  s_data->kernel_used = used;
  s_data->kernel_size = used + free_bytes;

  heap_calc_totals(app_state_get_heap(), &used, &free_bytes, &max_free);
  s_data->app_used = used;
  s_data->app_size = used + free_bytes;

  prv_sample_tasks();
}

static void prv_timer_callback(void *unused) {
  prv_sample();
  layer_mark_dirty(&s_data->canvas);
  s_data->timer = app_timer_register(HTOP_REFRESH_MS, prv_timer_callback, NULL);
}

static void prv_draw_text(GContext *ctx, const char *text, GFont font, GRect box,
                          GTextAlignment alignment) {
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, alignment, NULL);
}

//! On a round display the drawable width shrinks away from the middle, so
//! every row asks for its own left and right edge.
static void prv_row_bounds(const Layer *layer, int16_t y, int16_t h, int16_t *left,
                           int16_t *right) {
#if PBL_ROUND
  const int32_t radius = layer->bounds.size.w / 2;
  const int32_t center_y = layer->bounds.size.h / 2;
  const int32_t dy = MAX(ABS(y - center_y), ABS(y + h - center_y));
  const int32_t half = (dy >= radius) ? 0 : integer_sqrt(radius * radius - dy * dy);
  *left = radius - half + 2;
  *right = radius + half - 2;
#else
  *left = 2;
  *right = layer->bounds.size.w - 2;
#endif
}

//! A labelled usage bar: "krn [####    ] 33k/64k".
static int16_t prv_draw_heap(GContext *ctx, const Layer *layer, int16_t y, const char *label,
                             unsigned int used, unsigned int size) {
  const int16_t row_h = fonts_get_font_height(s_data->font);
  const int16_t label_w = 28;
  const int16_t value_w = 62;
  int16_t left, right;
  prv_row_bounds(layer, y, row_h, &left, &right);

  graphics_context_set_text_color(ctx, GColorWhite);
  prv_draw_text(ctx, label, s_data->font, GRect(left, y, label_w, row_h), GTextAlignmentLeft);

  const GRect bar = GRect(left + label_w, y + 4, right - left - label_w - value_w, row_h - 8);
  if (bar.size.w > 0) {
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_rect(ctx, &bar);
    if (size > 0) {
      GRect fill = grect_inset(bar, GEdgeInsets(1));
      fill.size.w = (fill.size.w * used) / size;
      graphics_context_set_fill_color(ctx, GColorGreen);
      graphics_fill_rect(ctx, &fill);
    }
  }

  char used_text[12];
  char size_text[12];
  char value[28];
  prv_format_size(used_text, sizeof(used_text), used);
  prv_format_size(size_text, sizeof(size_text), size);
  snprintf(value, sizeof(value), "%s/%s", used_text, size_text);
  graphics_context_set_text_color(ctx, GColorWhite);
  prv_draw_text(ctx, value, s_data->font, GRect(right - value_w, y, value_w, row_h),
                GTextAlignmentRight);

  return y + row_h;
}

//! One line of the task table, header included.
static void prv_draw_task_row(GContext *ctx, const Layer *layer, int16_t y, GFont font,
                              const char *name, const char *state, const char *cpu,
                              const char *stack) {
  const int16_t row_h = fonts_get_font_height(s_data->font);
  const int16_t stack_w = 40;
  const int16_t cpu_w = 38;
  const int16_t state_w = 14;
  int16_t left, right;
  prv_row_bounds(layer, y, row_h, &left, &right);

  const int16_t stack_x = right - stack_w;
  const int16_t cpu_x = stack_x - cpu_w - 4;
  const int16_t state_x = cpu_x - state_w - 4;

  if (state_x - left < 24) {
    return;
  }

  prv_draw_text(ctx, name, font, GRect(left, y, state_x - left - 2, row_h), GTextAlignmentLeft);
  prv_draw_text(ctx, state, font, GRect(state_x, y, state_w, row_h), GTextAlignmentLeft);
  prv_draw_text(ctx, cpu, font, GRect(cpu_x, y, cpu_w, row_h), GTextAlignmentRight);
  prv_draw_text(ctx, stack, font, GRect(stack_x, y, stack_w, row_h), GTextAlignmentRight);
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
  const int16_t height = layer->bounds.size.h;
  const int16_t row_h = fonts_get_font_height(s_data->font);
  char text[24];
  char cpu_text[8];
  char stack_text[12];

  graphics_context_set_text_color(ctx, GColorWhite);

  const int16_t clock_h = fonts_get_font_height(s_data->font_clock);

  int16_t y = PBL_IF_ROUND_ELSE(20, 2);
  int16_t left, right;
  prv_row_bounds(layer, y, clock_h, &left, &right);

  prv_draw_text(ctx, s_data->clock_text, s_data->font_clock,
                GRect(left, y, (right - left) / 2, clock_h), GTextAlignmentLeft);

  snprintf(text, sizeof(text), "%u%%%s", s_data->battery_pct, s_data->charging ? "+" : "");
  prv_draw_text(ctx, text, s_data->font_clock,
                GRect((left + right) / 2, y, (right - left) / 2, clock_h), GTextAlignmentRight);
  y += clock_h;

  prv_row_bounds(layer, y, row_h, &left, &right);

  const uint32_t uptime = s_data->uptime_s;
  snprintf(text, sizeof(text), "up %u:%02u:%02u", (unsigned int)(uptime / 3600),
           (unsigned int)((uptime / 60) % 60), (unsigned int)(uptime % 60));
  prv_draw_text(ctx, text, s_data->font_bold, GRect(left, y, (right - left) / 2, row_h),
                GTextAlignmentLeft);

  if (s_data->battery_valid) {
    char current_text[16];
    prv_format_current(current_text, sizeof(current_text), s_data->battery_ua);
    snprintf(text, sizeof(text), "%u.%02uV %s", (unsigned int)(s_data->battery_mv / 1000),
             (unsigned int)((s_data->battery_mv % 1000) / 10), current_text);
    prv_draw_text(ctx, text, s_data->font_bold,
                  GRect((left + right) / 2, y, (right - left) / 2, row_h), GTextAlignmentRight);
  }
  y += row_h;

  y = prv_draw_heap(ctx, layer, y, "krn", s_data->kernel_used, s_data->kernel_size);
  y = prv_draw_heap(ctx, layer, y, "app", s_data->app_used, s_data->app_size);

  y += 2;
  prv_row_bounds(layer, y, 1, &left, &right);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_line(ctx, GPoint(left, y), GPoint(right, y));
  y += 3;

  graphics_context_set_text_color(ctx, GColorWhite);
  prv_draw_task_row(ctx, layer, y, s_data->font_bold, "task", "s", "cpu", "stk");
  y += row_h;

  for (unsigned int i = 0; i < s_data->num_tasks && y + row_h <= height; i++) {
    const HtopTask *task = &s_data->tasks[i];

    snprintf(text, sizeof(text), "%c", task->state);
    snprintf(cpu_text, sizeof(cpu_text), "%u%%", task->cpu_pct);
    prv_format_size(stack_text, sizeof(stack_text), task->stack_free);
    prv_draw_task_row(ctx, layer, y, s_data->font, task->name, text, cpu_text, stack_text);

    y += row_h;
  }
}

static void prv_init(void) {
  s_data = app_zalloc_check(sizeof(*s_data));
  s_data->status = app_zalloc_check(HTOP_MAX_TASKS * sizeof(TaskStatus_t));
  s_data->font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_data->font_bold = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_data->font_clock = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

  window_init(&s_data->window, "Htop");
  window_set_background_color(&s_data->window, GColorBlack);

  layer_init(&s_data->canvas, &s_data->window.layer.bounds);
  layer_set_update_proc(&s_data->canvas, prv_update_proc);
  layer_add_child(&s_data->window.layer, &s_data->canvas);

  app_window_stack_push(&s_data->window, false /* not animated */);

  prv_sample();
  s_data->timer = app_timer_register(HTOP_REFRESH_MS, prv_timer_callback, NULL);
}

static void prv_deinit(void) {
  app_timer_cancel(s_data->timer);
  layer_deinit(&s_data->canvas);
  app_free(s_data->status);
  app_free(s_data);
  s_data = NULL;
}

static void prv_main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

const PebbleProcessMd *htop_app_get_app_info(void) {
  static const PebbleProcessMdSystem s_app_md = {
    .common = {
      // UUID: 2b5f0dc3-fd9a-4a2f-96f7-1a7c1b4f7bd0
      .uuid = {0x2b, 0x5f, 0x0d, 0xc3, 0xfd, 0x9a, 0x4a, 0x2f,
               0x96, 0xf7, 0x1a, 0x7c, 0x1b, 0x4f, 0x7b, 0xd0},
      .main_func = prv_main,
      .process_type = ProcessTypeWatchface,
    },
    .name = "Htop",
  };
  return (const PebbleProcessMd *)&s_app_md;
}
