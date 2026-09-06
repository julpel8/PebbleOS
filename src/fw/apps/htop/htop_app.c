/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! @file htop_app.c
//!
//! A watchface that shows what the system is doing: clock, battery, heap
//! usage and the FreeRTOS task table, plus btop style history graphs.
//!
//! Up and down switch views, select changes the refresh period. The period
//! is also the averaging window for the battery draw and the time step of
//! the graphs.

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
#include "pbl/services/battery/battery_state.h"
#include "pbl/drivers/rtc.h"
#include "pbl/util/heap.h"
#include "pbl/util/math.h"
#include "pbl/util/size.h"
#include "pbl/services/clock.h"
#include "process_state/app_state/app_state.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

//! Sampling tick. The refresh period is a whole number of these.
#define HTOP_TICK_MS 1000
//! Tasks tracked at once. The system runs about a dozen.
#define HTOP_MAX_TASKS 20
//! History points kept per metric, one per refresh period.
#define HTOP_HISTORY_LEN 96
//! Smallest full scale of the current graph, in tenths of a milliamp.
#define HTOP_CURRENT_MIN_SCALE 100
//! Height of the battery graph strip under the task list, in pixels.
#define HTOP_TASKS_GRAPH_H 26

//! Daytime, local time. Inside the window the watchface is dark on white and
//! the backlight is white, outside it is light on black and the backlight is
//! orange. The screen is transflective, so white reads best in daylight.
#define HTOP_DAY_FIRST_HOUR 8
#define HTOP_DAY_LAST_HOUR 17

typedef enum {
  HtopViewTasks = 0,
  HtopViewGraphs,
  HtopViewClock,
  HtopViewCount,
} HtopView;

typedef struct {
  GColor bg;
  GColor fg;
  //! Secondary text and gauge outlines.
  GColor dim;
  //! Separators and graph frames.
  GColor rule;
  GColor ok;
  GColor warn;
  GColor bad;
} HtopPalette;

static const HtopPalette s_palette_day = {
  GColorWhite, GColorBlack, GColorDarkGray, GColorLightGray,
  GColorDarkGreen, GColorChromeYellow, GColorDarkCandyAppleRed,
};

static const HtopPalette s_palette_night = {
  GColorBlack, GColorWhite, GColorLightGray, GColorDarkGray,
  GColorGreen, GColorYellow, GColorRed,
};

//! Picked at every redraw.
static const HtopPalette *s_pal = &s_palette_night;

#define HTOP_COLOR_BG (s_pal->bg)
#define HTOP_COLOR_FG (s_pal->fg)
#define HTOP_COLOR_DIM (s_pal->dim)
#define HTOP_COLOR_RULE (s_pal->rule)
#define HTOP_COLOR_OK (s_pal->ok)
#define HTOP_COLOR_WARN (s_pal->warn)
#define HTOP_COLOR_BAD (s_pal->bad)

typedef struct {
  char name[12];
  char state;
  uint8_t cpu_pct;
  uint16_t stack_free;
  uint32_t run_time;
  UBaseType_t number;
} HtopTask;

typedef struct {
  uint8_t cpu_pct;
  uint8_t mem_pct;
  //! Battery current in tenths of a milliamp, negative while discharging.
  int16_t current;
} HtopSample;

typedef struct {
  Window window;
  Layer canvas;
  AppTimer *timer;
  GFont font;
  GFont font_bold;
  GFont font_med;
  GFont font_clock;
  TaskStatus_t *status;

  char clock_text[8];
  char date_text[16];
  int last_minute;

  uint32_t uptime_s;
  uint8_t battery_pct;
  bool charging;
  bool battery_valid;
  int32_t battery_mv;
  int32_t battery_ua;
  //! Seconds the fuel gauge thinks are left, 0 if it has no estimate.
  uint32_t battery_tte_s;
  unsigned int kernel_used;
  unsigned int kernel_size;
  unsigned int app_used;
  unsigned int app_size;
  uint8_t cpu_pct;

  //! Battery current accumulated since the last refresh.
  int64_t current_sum;
  uint16_t current_count;
  uint8_t ticks;

  HtopSample history[HTOP_HISTORY_LEN];
  uint8_t history_head;
  uint8_t history_count;

  uint8_t num_tasks;
  HtopTask tasks[HTOP_MAX_TASKS];
} HtopData;

static const uint8_t s_refresh_periods_s[] = {1, 2, 5, 10, 30, 60};

//! Kept out of HtopData so the settings survive an app restart.
static uint8_t s_view = HtopViewTasks;
static uint8_t s_period_index;

static HtopData *s_data;

static uint8_t prv_period_s(void) { return s_refresh_periods_s[s_period_index]; }

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

//! Seconds as "2d 4h" or "3h20".
static void prv_format_duration(char *buf, size_t size, uint32_t seconds) {
  const unsigned int hours = seconds / 3600U;
  if (hours >= 48U) {
    snprintf(buf, size, "%ud %uh", hours / 24U, hours % 24U);
  } else {
    snprintf(buf, size, "%uh%02u", hours, (unsigned int)((seconds / 60U) % 60U));
  }
}

//! Tenths of a milliamp as "-12.4mA".
static void prv_format_current(char *buf, size_t size, int32_t tenths) {
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

//! Busiest first, like htop sorted on CPU.
static void prv_sort_tasks(HtopTask *tasks, uint8_t count) {
  for (uint8_t i = 1; i < count; i++) {
    const HtopTask task = tasks[i];
    int8_t j = i - 1;
    while (j >= 0 && tasks[j].cpu_pct < task.cpu_pct) {
      tasks[j + 1] = tasks[j];
      j--;
    }
    tasks[j + 1] = task;
  }
}

static void prv_sample_tasks(void) {
  const UBaseType_t count = uxTaskGetSystemState(s_data->status, HTOP_MAX_TASKS, NULL);

  HtopTask tasks[HTOP_MAX_TASKS];
  uint32_t total_delta = 0;
  uint32_t idle_delta = 0;
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
    if (strncmp(task->name, "IDLE", 4) == 0) {
      idle_delta += deltas[i];
    }
  }

  for (UBaseType_t i = 0; i < count; i++) {
    tasks[i].cpu_pct = total_delta ? ((deltas[i] * 100) / total_delta) : 0;
  }
  s_data->cpu_pct = total_delta ? (((total_delta - idle_delta) * 100) / total_delta) : 0;

  prv_sort_tasks(tasks, count);
  memcpy(s_data->tasks, tasks, count * sizeof(HtopTask));
  s_data->num_tasks = count;
}

bool htop_is_daytime(void) {
  struct tm now;
  clock_get_time_tm(&now);
  return (now.tm_hour >= HTOP_DAY_FIRST_HOUR) && (now.tm_hour < HTOP_DAY_LAST_HOUR);
}

static void prv_update_clock(void) {
  struct tm now;
  clock_get_time_tm(&now);

  strftime(s_data->clock_text, sizeof(s_data->clock_text),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", &now);
  strftime(s_data->date_text, sizeof(s_data->date_text), "%a %d %b", &now);
  s_data->last_minute = now.tm_min;
}

//! Read the battery. Called every tick so the draw can be averaged.
static void prv_sample_current(void) {
  const BatteryChargeState charge = battery_state_service_peek();
  s_data->battery_pct = charge.charge_percent;
  s_data->battery_tte_s = battery_state_get_time_to_empty();
  s_data->charging = charge.is_charging || charge.is_plugged;

  BatteryConstants constants;
  s_data->battery_valid = (battery_get_constants(&constants) == 0);
  if (s_data->battery_valid) {
    s_data->battery_mv = constants.v_mv;
    s_data->current_sum += constants.i_ua / 100;
    s_data->current_count++;
  }
}

static void prv_history_push(void) {
  HtopSample *sample = &s_data->history[s_data->history_head];

  sample->cpu_pct = s_data->cpu_pct;
  sample->mem_pct =
      s_data->kernel_size ? ((s_data->kernel_used * 100) / s_data->kernel_size) : 0;
  sample->current = s_data->battery_ua;

  s_data->history_head = (s_data->history_head + 1) % HTOP_HISTORY_LEN;
  if (s_data->history_count < HTOP_HISTORY_LEN) {
    s_data->history_count++;
  }
}

//! Everything that only needs refreshing once per period.
static void prv_sample(void) {
  s_data->uptime_s = rtc_get_ticks() / RTC_TICKS_HZ;

  if (s_data->current_count) {
    s_data->battery_ua = s_data->current_sum / s_data->current_count;
  }
  s_data->current_sum = 0;
  s_data->current_count = 0;

  unsigned int used, free_bytes, max_free;
  heap_calc_totals(kernel_heap_get(), &used, &free_bytes, &max_free);
  s_data->kernel_used = used;
  s_data->kernel_size = used + free_bytes;

  heap_calc_totals(app_state_get_heap(), &used, &free_bytes, &max_free);
  s_data->app_used = used;
  s_data->app_size = used + free_bytes;

  prv_sample_tasks();
  prv_history_push();
}

static void prv_timer_callback(void *unused) {
  bool redraw = false;

  prv_sample_current();

  const int last_minute = s_data->last_minute;
  prv_update_clock();
  if (s_data->last_minute != last_minute) {
    redraw = true;
  }

  if (++s_data->ticks >= prv_period_s()) {
    s_data->ticks = 0;
    prv_sample();
    redraw = true;
  }

  if (redraw) {
    layer_mark_dirty(&s_data->canvas);
  }
  s_data->timer = app_timer_register(HTOP_TICK_MS, prv_timer_callback, NULL);
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
static int16_t prv_draw_bar(GContext *ctx, const Layer *layer, int16_t y, const char *label,
                            unsigned int used, unsigned int size, const char *value) {
  const int16_t row_h = fonts_get_font_height(s_data->font);
  const int16_t label_w = 28;
  const int16_t value_w = 62;
  int16_t left, right;
  prv_row_bounds(layer, y, row_h, &left, &right);

  graphics_context_set_text_color(ctx, HTOP_COLOR_FG);
  prv_draw_text(ctx, label, s_data->font, GRect(left, y, label_w, row_h), GTextAlignmentLeft);

  const GRect bar = GRect(left + label_w, y + 4, right - left - label_w - value_w, row_h - 8);
  if (bar.size.w > 0) {
    graphics_context_set_stroke_color(ctx, HTOP_COLOR_DIM);
    graphics_draw_rect(ctx, &bar);
    if (size > 0) {
      GRect fill = grect_inset(bar, GEdgeInsets(1));
      fill.size.w = (fill.size.w * used) / size;
      graphics_context_set_fill_color(ctx, HTOP_COLOR_OK);
      graphics_fill_rect(ctx, &fill);
    }
  }

  graphics_context_set_text_color(ctx, HTOP_COLOR_FG);
  prv_draw_text(ctx, value, s_data->font, GRect(right - value_w, y, value_w, row_h),
                GTextAlignmentRight);

  return y + row_h;
}

//! A usage bar labelled with the heap totals, as "33k/64k".
static int16_t prv_draw_heap(GContext *ctx, const Layer *layer, int16_t y, const char *label,
                             unsigned int used, unsigned int size) {
  char used_text[12];
  char size_text[12];
  char value[28];

  prv_format_size(used_text, sizeof(used_text), used);
  prv_format_size(size_text, sizeof(size_text), size);
  snprintf(value, sizeof(value), "%s/%s", used_text, size_text);

  return prv_draw_bar(ctx, layer, y, label, used, size, value);
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

typedef GColor (*HtopColorizer)(int16_t value, int16_t scale);

static GColor prv_load_color(int16_t value, int16_t scale) {
  if (value * 2 < scale) {
    return HTOP_COLOR_OK;
  } else if (value * 5 < scale * 4) {
    return HTOP_COLOR_WARN;
  }
  return HTOP_COLOR_BAD;
}

static GColor prv_current_color(int16_t value, int16_t scale) {
  return (value >= 0) ? HTOP_COLOR_OK : HTOP_COLOR_WARN;
}

//! History as a filled area chart, oldest on the left.
static void prv_draw_graph(GContext *ctx, GRect box, const int16_t *values, uint8_t count,
                           int16_t scale, HtopColorizer colorize) {
  graphics_context_set_stroke_color(ctx, HTOP_COLOR_RULE);
  graphics_draw_rect(ctx, &box);

  if (count == 0 || scale <= 0 || box.size.w <= 2 || box.size.h <= 2) {
    return;
  }

  const int16_t inner_h = box.size.h - 2;
  const int16_t bottom = box.origin.y + box.size.h - 1;

  for (int16_t x = 1; x < box.size.w - 1; x++) {
    const uint8_t index = ((uint32_t)(x - 1) * count) / (box.size.w - 2);
    const int16_t value = values[index];
    int16_t h = ((int32_t)ABS(value) * inner_h) / scale;

    if (h <= 0) {
      h = 1;
    } else if (h > inner_h) {
      h = inner_h;
    }

    graphics_context_set_fill_color(ctx, colorize(value, scale));
    graphics_fill_rect(ctx, &GRect(box.origin.x + x, bottom - h, 1, h));
  }
}

//! Copy the history of one metric into values[], oldest first.
static uint8_t prv_history_series(int16_t *values, uint8_t metric) {
  const uint8_t count = s_data->history_count;
  const uint8_t start = (s_data->history_head + HTOP_HISTORY_LEN - count) % HTOP_HISTORY_LEN;

  for (uint8_t i = 0; i < count; i++) {
    const HtopSample *sample = &s_data->history[(start + i) % HTOP_HISTORY_LEN];
    switch (metric) {
      case 0: values[i] = sample->cpu_pct; break;
      case 1: values[i] = sample->mem_pct; break;
      default: values[i] = sample->current; break;
    }
  }
  return count;
}

//! Clock, date, battery and the current settings. Shared by every view.
static int16_t prv_draw_header(GContext *ctx, const Layer *layer) {
  static const char *const s_view_names[HtopViewCount] = {"tasks", "graphs", "clock"};
  const int16_t clock_h = fonts_get_font_height(s_data->font_clock);
  const int16_t date_h = fonts_get_font_height(s_data->font_med);
  const int16_t battery_w = 76;
  char text[24];
  int16_t left, right;

  int16_t y = PBL_IF_ROUND_ELSE(14, 0);
  prv_row_bounds(layer, y, clock_h, &left, &right);

  graphics_context_set_text_color(ctx, HTOP_COLOR_FG);
  prv_draw_text(ctx, s_data->clock_text, s_data->font_clock,
                GRect(left, y, right - left - battery_w, clock_h), GTextAlignmentLeft);

  const int16_t small_h = fonts_get_font_height(s_data->font);
  int16_t battery_y = y + 2;

  snprintf(text, sizeof(text), "%u%%%s", s_data->battery_pct, s_data->charging ? "+" : "");
  prv_draw_text(ctx, text, s_data->font_med, GRect(right - battery_w, battery_y, battery_w, date_h),
                GTextAlignmentRight);
  battery_y += date_h + 1;

  const GRect gauge = GRect(right - battery_w, battery_y, battery_w, 4);
  graphics_context_set_stroke_color(ctx, HTOP_COLOR_DIM);
  graphics_draw_rect(ctx, &gauge);
  GRect gauge_fill = grect_inset(gauge, GEdgeInsets(1));
  gauge_fill.size.w = (gauge_fill.size.w * s_data->battery_pct) / 100;
  graphics_context_set_fill_color(ctx, HTOP_COLOR_OK);
  graphics_fill_rect(ctx, &gauge_fill);
  battery_y += gauge.size.h;

  if (s_data->battery_tte_s != 0U) {
    char left_text[16];
    prv_format_duration(left_text, sizeof(left_text), s_data->battery_tte_s);
    graphics_context_set_text_color(ctx, HTOP_COLOR_DIM);
    prv_draw_text(ctx, left_text, s_data->font, GRect(right - battery_w, battery_y, battery_w,
                                                     small_h),
                  GTextAlignmentRight);
  }
  y += clock_h;

  prv_row_bounds(layer, y, date_h, &left, &right);
  graphics_context_set_text_color(ctx, HTOP_COLOR_DIM);
  prv_draw_text(ctx, s_data->date_text, s_data->font_med, GRect(left, y, right - left - 80, date_h),
                GTextAlignmentLeft);

  snprintf(text, sizeof(text), "%s %us", s_view_names[s_view], prv_period_s());
  prv_draw_text(ctx, text, s_data->font, GRect(right - 80, y + 4, 80, date_h),
                GTextAlignmentRight);
  y += date_h + 2;

  prv_row_bounds(layer, y, 1, &left, &right);
  graphics_context_set_stroke_color(ctx, HTOP_COLOR_RULE);
  graphics_draw_line(ctx, GPoint(left, y), GPoint(right, y));

  return y + 3;
}

//! The battery draw over the whole history, scaled to its own peak.
static void prv_draw_current_graph(GContext *ctx, GRect box) {
  int16_t values[HTOP_HISTORY_LEN];
  const uint8_t count = prv_history_series(values, 2);
  int16_t scale = HTOP_CURRENT_MIN_SCALE;

  for (uint8_t i = 0; i < count; i++) {
    if (ABS(values[i]) > scale) {
      scale = ABS(values[i]);
    }
  }
  prv_draw_graph(ctx, box, values, count, scale, prv_current_color);
}

static void prv_draw_tasks_view(GContext *ctx, const Layer *layer, int16_t y) {
  const int16_t height = layer->bounds.size.h - HTOP_TASKS_GRAPH_H - 4;
  const int16_t row_h = fonts_get_font_height(s_data->font);
  char text[24];
  char cpu_text[8];
  char stack_text[12];
  int16_t left, right;

  prv_row_bounds(layer, y, row_h, &left, &right);
  graphics_context_set_text_color(ctx, HTOP_COLOR_FG);

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

  graphics_context_set_text_color(ctx, HTOP_COLOR_FG);
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

  y = layer->bounds.size.h - HTOP_TASKS_GRAPH_H - 2;
  prv_row_bounds(layer, y, HTOP_TASKS_GRAPH_H, &left, &right);
  prv_draw_current_graph(ctx, GRect(left, y, right - left, HTOP_TASKS_GRAPH_H));
}

//! One graph with its label row above it.
static int16_t prv_draw_block(GContext *ctx, const Layer *layer, int16_t y, int16_t block_h,
                              const char *label, const char *value, const int16_t *values,
                              uint8_t count, int16_t scale, HtopColorizer colorize) {
  const int16_t row_h = fonts_get_font_height(s_data->font);
  int16_t left, right;
  prv_row_bounds(layer, y, block_h, &left, &right);

  graphics_context_set_text_color(ctx, HTOP_COLOR_FG);
  prv_draw_text(ctx, label, s_data->font_bold, GRect(left, y, 60, row_h), GTextAlignmentLeft);
  prv_draw_text(ctx, value, s_data->font, GRect(right - 100, y, 100, row_h), GTextAlignmentRight);

  const int16_t graph_h = block_h - row_h - 4;
  if (graph_h > 4) {
    prv_draw_graph(ctx, GRect(left, y + row_h, right - left, graph_h), values, count, scale,
                   colorize);
  }
  return y + block_h;
}

static void prv_draw_graphs_view(GContext *ctx, const Layer *layer, int16_t y) {
  int16_t values[HTOP_HISTORY_LEN];
  char value[24];
  uint8_t count;

  const int16_t block_h = (layer->bounds.size.h - y - 2) / 3;

  count = prv_history_series(values, 0);
  snprintf(value, sizeof(value), "%u%%", s_data->cpu_pct);
  y = prv_draw_block(ctx, layer, y, block_h, "cpu", value, values, count, 100, prv_load_color);

  count = prv_history_series(values, 1);
  snprintf(value, sizeof(value), "%uk/%uk", s_data->kernel_used / 1024,
           s_data->kernel_size / 1024);
  y = prv_draw_block(ctx, layer, y, block_h, "krn", value, values, count, 100, prv_load_color);

  count = prv_history_series(values, 2);
  int16_t scale = HTOP_CURRENT_MIN_SCALE;
  for (uint8_t i = 0; i < count; i++) {
    if (ABS(values[i]) > scale) {
      scale = ABS(values[i]);
    }
  }
  prv_format_current(value, sizeof(value), s_data->battery_ua);
  prv_draw_block(ctx, layer, y, block_h, "bat", value, values, count, scale, prv_current_color);
}

static void prv_draw_clock_view(GContext *ctx, const Layer *layer, int16_t y) {
  char value[24];
  int16_t left, right;
  const int16_t row_h = fonts_get_font_height(s_data->font);

  snprintf(value, sizeof(value), "%u%%%s", s_data->battery_pct, s_data->charging ? " chg" : "");
  y = prv_draw_bar(ctx, layer, y + 4, "bat", s_data->battery_pct, 100, value);

  if (s_data->battery_valid) {
    prv_row_bounds(layer, y, row_h, &left, &right);
    snprintf(value, sizeof(value), "%u.%02uV", (unsigned int)(s_data->battery_mv / 1000),
             (unsigned int)((s_data->battery_mv % 1000) / 10));
    graphics_context_set_text_color(ctx, HTOP_COLOR_FG);
    prv_draw_text(ctx, value, s_data->font_bold, GRect(left, y, (right - left) / 2, row_h),
                  GTextAlignmentLeft);

    prv_format_current(value, sizeof(value), s_data->battery_ua);
    prv_draw_text(ctx, value, s_data->font_bold,
                  GRect((left + right) / 2, y, (right - left) / 2, row_h), GTextAlignmentRight);
    y += row_h;
  }

  if (s_data->battery_tte_s != 0U) {
    // The gauge reads the current on a 200 mA scale, so its steps are worth
    // 0.2 mA. This estimate is what the fuel gauge makes of the whole history.
    char left_text[16];
    prv_format_duration(left_text, sizeof(left_text), s_data->battery_tte_s);
    snprintf(value, sizeof(value), "left %s", left_text);
    prv_row_bounds(layer, y, row_h, &left, &right);
    graphics_context_set_text_color(ctx, HTOP_COLOR_FG);
    prv_draw_text(ctx, value, s_data->font_bold, GRect(left, y, right - left, row_h),
                  GTextAlignmentLeft);
    y += row_h;
  }

  prv_row_bounds(layer, y, 1, &left, &right);
  prv_draw_current_graph(ctx, GRect(left, y + 4, right - left, layer->bounds.size.h - y - 8));
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
  s_pal = htop_is_daytime() ? &s_palette_day : &s_palette_night;
  graphics_context_set_fill_color(ctx, HTOP_COLOR_BG);
  graphics_fill_rect(ctx, &layer->bounds);

  const int16_t y = prv_draw_header(ctx, layer);

  switch (s_view) {
    case HtopViewGraphs:
      prv_draw_graphs_view(ctx, layer, y);
      break;
    case HtopViewClock:
      prv_draw_clock_view(ctx, layer, y);
      break;
    default:
      prv_draw_tasks_view(ctx, layer, y);
      break;
  }
}

static void prv_up_click(ClickRecognizerRef recognizer, void *context) {
  s_view = (s_view + HtopViewCount - 1) % HtopViewCount;
  layer_mark_dirty(&s_data->canvas);
}

static void prv_down_click(ClickRecognizerRef recognizer, void *context) {
  s_view = (s_view + 1) % HtopViewCount;
  layer_mark_dirty(&s_data->canvas);
}

//! The period sets both the averaging window and the graph time step, so a
//! change makes the history meaningless.
static void prv_select_click(ClickRecognizerRef recognizer, void *context) {
  s_period_index = (s_period_index + 1) % ARRAY_LENGTH(s_refresh_periods_s);
  s_data->ticks = 0;
  s_data->history_head = 0;
  s_data->history_count = 0;
  layer_mark_dirty(&s_data->canvas);
}

//! The htop shell has nowhere to go back to, and leaving the watchface would
//! leave the watch with no running app at all.
static void prv_back_click(ClickRecognizerRef recognizer, void *context) {}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
}

static void prv_init(void) {
  s_data = app_zalloc_check(sizeof(*s_data));
  s_data->status = app_zalloc_check(HTOP_MAX_TASKS * sizeof(TaskStatus_t));
  s_data->font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_data->font_bold = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_data->font_med = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_data->font_clock = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);

  window_init(&s_data->window, "Htop");
  window_set_background_color(&s_data->window, GColorBlack);
  window_set_click_config_provider(&s_data->window, prv_click_config_provider);

  layer_init(&s_data->canvas, &s_data->window.layer.bounds);
  layer_set_update_proc(&s_data->canvas, prv_update_proc);
  layer_add_child(&s_data->window.layer, &s_data->canvas);

  app_window_stack_push(&s_data->window, false /* not animated */);

  prv_update_clock();
  prv_sample_current();
  prv_sample();
  s_data->timer = app_timer_register(HTOP_TICK_MS, prv_timer_callback, NULL);
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
