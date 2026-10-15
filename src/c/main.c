// SPDX-FileCopyrightText: 2026 MrReSc
// SPDX-License-Identifier: Apache-2.0
// Modified 2026 by Dallin Sumpter: mm/dd/yy date, one "next sun event" cell
// (sunrise or sunset, whichever is next) and a density altitude cell.

#include <pebble.h>

#include <ctype.h>
#include <locale.h>
#include <string.h>

#define OUTER_MARGIN 4
#define CONTENT_MARGIN 10
#define STATUS_HEIGHT 32
#define HEADER_SEPARATOR_Y (OUTER_MARGIN + STATUS_HEIGHT + 1)
#define INFO_HEIGHT STATUS_HEIGHT
#define CELL_GAP 10
#define ICON_SIZE 12
#define INFO_VALUE_X_OFFSET 22
// Narrower than the original 11 px: slashes make mm/dd/yy wider than dd.mm.yy,
// and the extra room keeps "100%" from running into the weekday.
#define DATE_VALUE_GAP 5
#define PIXEL_GLYPH_WIDTH 5
#define PIXEL_GLYPH_HEIGHT 7
#define PIXEL_GLYPH_PITCH 6
#define TIME_DIGIT_PITCH 44
#define FRAME_RADIUS 8
#define COLON_SIZE 6

#define PERSIST_KEY_SUN_DATE 1
#define PERSIST_KEY_SUNRISE_MINUTES 2
#define PERSIST_KEY_SUNSET_MINUTES 3
#define PERSIST_KEY_LAST_SUN_REQUEST 4
#define PERSIST_KEY_NEXT_SUNRISE_MINUTES 5
#define PERSIST_KEY_NEXT_SUN_DATE 6
#define PERSIST_KEY_DENSITY_ALTITUDE_FT 7
#define PERSIST_KEY_DENSITY_ALTITUDE_TIME 8
#define SUN_RETRY_INTERVAL_SECONDS (6 * 60 * 60)
// Fetch weather when the last reading is this old...
#define WEATHER_REFRESH_SECONDS (30 * 60)
// ...but don't ask the phone more often than this if requests keep failing.
#define WEATHER_RETRY_SECONDS (10 * 60)
// Older readings are replaced with dashes rather than shown as current.
#define DENSITY_ALTITUDE_MAX_AGE_SECONDS (2 * 60 * 60)
#define DENSITY_ALTITUDE_MIN_FT (-5000)
#define DENSITY_ALTITUDE_MAX_FT 30000

typedef enum {
  InfoIconSteps,
  InfoIconHeart,
  InfoIconSunrise,
  InfoIconSunset,
  InfoIconDensityAltitude
} InfoIcon;

static Window *s_main_window;
static Layer *s_canvas_layer;
static GFont s_time_font;
static GFont s_header_font;
static GFont s_info_font;
static GFont s_info_font_large;

static char s_time_buffer[5] = "0000";
static char s_weekday_buffer[12] = "";
static char s_date_buffer[10] = "--/--/--";
static char s_ampm_buffer[8] = "";
static char s_steps_buffer[12] = "--";
static char s_heart_buffer[12] = "--";
static char s_sun_event_buffer[8] = "--:--";
static bool s_sun_event_is_sunrise = true;
static char s_density_altitude_buffer[8] = "--";
static char s_battery_buffer[6] = "--%";

static int32_t s_sun_date;
static int32_t s_sunrise_minutes = -1;
static int32_t s_sunset_minutes = -1;
static int32_t s_next_sun_date;
static int32_t s_next_sunrise_minutes = -1;
static int32_t s_current_date;
static int32_t s_current_minutes;
static time_t s_last_sun_request_time;
static int32_t s_density_altitude_ft;
static time_t s_density_altitude_time;
static time_t s_last_weather_request_time;
static uint8_t s_battery_percent;
static bool s_health_subscribed;
static bool s_outbox_busy;

static void prv_mark_dirty(void) {
  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

static int32_t prv_date_code(const struct tm *local_time) {
  return (local_time->tm_year + 1900) * 10000 +
      (local_time->tm_mon + 1) * 100 + local_time->tm_mday;
}

static void prv_uppercase_ascii(char *text) {
  for (size_t i = 0; text[i] != '\0'; ++i) {
    const unsigned char value = (unsigned char)text[i];
    if (value < 128) {
      text[i] = (char)toupper(value);
    }
  }
}

static void prv_format_sun_time(char *buffer, size_t size, int32_t minutes) {
  if (size < 6 || minutes < 0 || minutes >= 24 * 60) {
    snprintf(buffer, size, "--:--");
    return;
  }

  int hour = minutes / 60;
  if (!clock_is_24h_style()) {
    hour %= 12;
    if (hour == 0) {
      hour = 12;
    }
  }
  snprintf(buffer, size, "%02d:%02ld", hour, (long)(minutes % 60));
}

static bool prv_is_minute_of_day(int32_t minutes) {
  return minutes >= 0 && minutes < 24 * 60;
}

// Picks whichever sun event comes next and formats it into one buffer:
//   before today's sunrise  -> today's sunrise
//   before today's sunset   -> today's sunset
//   after today's sunset    -> tomorrow's sunrise
// Just after midnight, before the phone has sent the new day's times,
// yesterday's "tomorrow sunrise" is today's sunrise and is used instead.
static void prv_refresh_sun_buffers(void) {
  int32_t event_minutes = -1;
  bool is_sunrise = true;
  const int32_t now = s_current_minutes;
  const bool today_valid = s_sun_date == s_current_date &&
      prv_is_minute_of_day(s_sunrise_minutes) &&
      prv_is_minute_of_day(s_sunset_minutes);
  if (today_valid) {
    if (now < s_sunrise_minutes) {
      event_minutes = s_sunrise_minutes;
    } else if (now < s_sunset_minutes) {
      event_minutes = s_sunset_minutes;
      is_sunrise = false;
    } else if (s_next_sun_date != 0) {
      event_minutes = s_next_sunrise_minutes;
    }
  } else if (s_next_sun_date == s_current_date &&
             prv_is_minute_of_day(s_next_sunrise_minutes) &&
             now < s_next_sunrise_minutes) {
    event_minutes = s_next_sunrise_minutes;
  }
  s_sun_event_is_sunrise = is_sunrise;
  prv_format_sun_time(s_sun_event_buffer, sizeof(s_sun_event_buffer),
                      event_minutes);
}

static bool prv_has_sun_result_for_current_date(void) {
  // Data saved by the original watchface has no tomorrow sunrise; treat it
  // as missing so the phone is asked again.
  if (s_sun_date != s_current_date || s_next_sun_date == 0) {
    return false;
  }
  const bool has_times = prv_is_minute_of_day(s_sunrise_minutes) &&
      prv_is_minute_of_day(s_sunset_minutes);
  const bool has_no_events = s_sunrise_minutes == -1 &&
      s_sunset_minutes == -1;
  return has_times || has_no_events;
}

static time_t prv_density_altitude_age(time_t now) {
  if (s_density_altitude_time <= 0 || now < s_density_altitude_time) {
    return (time_t)0x7fffffff;
  }
  return now - s_density_altitude_time;
}

static void prv_refresh_density_altitude_buffer(void) {
  if (prv_density_altitude_age(time(NULL)) <= DENSITY_ALTITUDE_MAX_AGE_SECONDS) {
    snprintf(s_density_altitude_buffer, sizeof(s_density_altitude_buffer),
             "%ld", (long)s_density_altitude_ft);
  } else {
    snprintf(s_density_altitude_buffer, sizeof(s_density_altitude_buffer),
             "--");
  }
}

static bool prv_weather_is_stale(void) {
  return prv_density_altitude_age(time(NULL)) >= WEATHER_REFRESH_SECONDS;
}

static bool prv_weather_request_is_due(void) {
  if (!prv_weather_is_stale()) {
    return false;
  }
  const time_t now = time(NULL);
  return s_last_weather_request_time <= 0 ||
      now < s_last_weather_request_time ||
      now - s_last_weather_request_time >= WEATHER_RETRY_SECONDS;
}

static void prv_update_clock_state(const struct tm *local_time) {
  int hour = local_time->tm_hour;
  if (!clock_is_24h_style()) {
    hour %= 12;
    if (hour == 0) {
      hour = 12;
    }
    snprintf(s_ampm_buffer, sizeof(s_ampm_buffer),
             local_time->tm_hour < 12 ? "AM" : "PM");
  } else {
    s_ampm_buffer[0] = '\0';
  }

  snprintf(s_time_buffer, sizeof(s_time_buffer), "%02d%02d", hour,
           local_time->tm_min);
  if (strftime(s_weekday_buffer, sizeof(s_weekday_buffer), "%a", local_time) == 0) {
    s_weekday_buffer[0] = '\0';
  }
  prv_uppercase_ascii(s_weekday_buffer);
  if (strftime(s_date_buffer, sizeof(s_date_buffer), "%m/%d/%y", local_time) == 0) {
    snprintf(s_date_buffer, sizeof(s_date_buffer), "--/--/--");
  }
  s_current_date = prv_date_code(local_time);
  s_current_minutes = local_time->tm_hour * 60 + local_time->tm_min;
  prv_refresh_sun_buffers();
  prv_refresh_density_altitude_buffer();
}

static void prv_update_battery_state(BatteryChargeState state) {
  s_battery_percent = state.charge_percent > 100 ? 100 : state.charge_percent;
  snprintf(s_battery_buffer, sizeof(s_battery_buffer), "%u%%",
           (unsigned int)s_battery_percent);
}

static void prv_update_health_state(void) {
  snprintf(s_steps_buffer, sizeof(s_steps_buffer), "--");
  snprintf(s_heart_buffer, sizeof(s_heart_buffer), "--");
#ifdef PBL_HEALTH
  const time_t now = time(NULL);
  const time_t today = time_start_of_today();
  const HealthServiceAccessibilityMask steps_access =
      health_service_metric_accessible(HealthMetricStepCount, today, now);
  if ((steps_access & HealthServiceAccessibilityMaskAvailable) != 0) {
    const int32_t steps = health_service_sum_today(HealthMetricStepCount);
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%ld", (long)steps);
  }

  const HealthServiceAccessibilityMask heart_access =
      health_service_metric_accessible(HealthMetricHeartRateBPM, now, now);
  const int32_t heart_rate =
      health_service_peek_current_value(HealthMetricHeartRateBPM);
  if ((heart_access & HealthServiceAccessibilityMaskAvailable) != 0 &&
      heart_rate > 0) {
    snprintf(s_heart_buffer, sizeof(s_heart_buffer), "%ld", (long)heart_rate);
  }
#endif
}

typedef struct {
  char character;
  uint8_t rows[PIXEL_GLYPH_HEIGHT];
} PixelGlyph;

static const PixelGlyph PIXEL_GLYPHS[] = {
  {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
  {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
  {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
  {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
  {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}},
  {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
  {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
  {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
  {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}},
  {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
  {'I', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f}},
  {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c}},
  {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
  {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
  {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
  {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
  {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
  {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
  {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
  {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
  {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
  {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
  {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
  {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
  {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
  {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
  {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
  {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
};

static const uint8_t *prv_pixel_glyph_rows(char character) {
  for (size_t i = 0; i < ARRAY_LENGTH(PIXEL_GLYPHS); ++i) {
    if (PIXEL_GLYPHS[i].character == character) {
      return PIXEL_GLYPHS[i].rows;
    }
  }
  return NULL;
}

static int prv_pixel_text_width(const char *text) {
  const size_t length = strlen(text);
  return length == 0 ? 0 : (int)length * PIXEL_GLYPH_PITCH - 1;
}

static int prv_bold_pixel_text_width(const char *text) {
  const size_t length = strlen(text);
  return length == 0 ? 0 : (int)length * (PIXEL_GLYPH_PITCH + 1) - 1;
}

static void prv_draw_pixel_text(GContext *ctx, const char *text, GPoint origin,
                                GColor color) {
  graphics_context_set_fill_color(ctx, color);
  for (size_t index = 0; text[index] != '\0'; ++index) {
    const uint8_t *rows = prv_pixel_glyph_rows(text[index]);
    if (!rows) {
      continue;
    }
    for (int row = 0; row < PIXEL_GLYPH_HEIGHT; ++row) {
      for (int column = 0; column < PIXEL_GLYPH_WIDTH; ++column) {
        if ((rows[row] & (1 << (PIXEL_GLYPH_WIDTH - 1 - column))) != 0) {
          graphics_fill_rect(ctx,
              GRect(origin.x + (int)index * PIXEL_GLYPH_PITCH + column,
                    origin.y + row, 1, 1), 0, GCornerNone);
        }
      }
    }
  }
}

static void prv_draw_bold_pixel_text(GContext *ctx, const char *text,
                                     GPoint origin, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  for (size_t index = 0; text[index] != '\0'; ++index) {
    const uint8_t *rows = prv_pixel_glyph_rows(text[index]);
    if (!rows) {
      continue;
    }
    for (int row = 0; row < PIXEL_GLYPH_HEIGHT; ++row) {
      for (int column = 0; column < PIXEL_GLYPH_WIDTH; ++column) {
        if ((rows[row] & (1 << (PIXEL_GLYPH_WIDTH - 1 - column))) != 0) {
          graphics_fill_rect(
              ctx,
              GRect(origin.x + (int)index * (PIXEL_GLYPH_PITCH + 1) + column,
                    origin.y + row, 2, 1),
              0, GCornerNone);
        }
      }
    }
  }
}

static void prv_draw_battery_icon(GContext *ctx, GPoint origin) {
  const GRect body = GRect(origin.x, origin.y, 16, 10);
  const GRect interior = GRect(origin.x + 2, origin.y + 2, 12, 6);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, body);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(origin.x + 16, origin.y + 3, 2, 4), 0,
                     GCornerNone);
  const int fill_width = (interior.size.w * s_battery_percent) / 100;
  if (fill_width > 0) {
    graphics_fill_rect(ctx,
                       GRect(interior.origin.x, interior.origin.y,
                             fill_width, interior.size.h),
                       0, GCornerNone);
  }
}

static void prv_draw_steps_icon(GContext *ctx, GPoint origin) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(origin.x + 1, origin.y + 3, 3, 5), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 3, origin.y + 5, 4, 3), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 6, origin.y + 6, 4, 2), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 1, origin.y + 9, 10, 2), 0,
                     GCornerNone);
}

static void prv_draw_heart_icon(GContext *ctx, GPoint origin) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(origin.x + 2, origin.y + 1, 3, 2), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 7, origin.y + 1, 3, 2), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 1, origin.y + 3, 10, 3), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 2, origin.y + 6, 8, 2), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 4, origin.y + 8, 4, 2), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 5, origin.y + 10, 2, 1), 0,
                     GCornerNone);
}

static void prv_draw_sun_icon(GContext *ctx, GPoint origin, bool sunrise) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(origin.x + 7, origin.y + 5, 3, 1), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 6, origin.y + 6, 5, 3), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(origin.x + 5, origin.y + 10, 7, 1), 0,
                     GCornerNone);
  if (sunrise) {
    graphics_fill_rect(ctx, GRect(origin.x + 2, origin.y, 1, 2), 0,
                       GCornerNone);
    graphics_fill_rect(ctx, GRect(origin.x + 1, origin.y + 2, 3, 1), 0,
                       GCornerNone);
    graphics_fill_rect(ctx, GRect(origin.x + 2, origin.y + 3, 1, 6), 0,
                       GCornerNone);
  } else {
    graphics_fill_rect(ctx, GRect(origin.x + 2, origin.y, 1, 6), 0,
                       GCornerNone);
    graphics_fill_rect(ctx, GRect(origin.x + 1, origin.y + 6, 3, 1), 0,
                       GCornerNone);
    graphics_fill_rect(ctx, GRect(origin.x + 2, origin.y + 7, 1, 2), 0,
                       GCornerNone);
  }
}

static void prv_draw_mountain_icon(GContext *ctx, GPoint origin) {
  // A tall peak with a smaller one behind it. Each row is {y, x_from, x_to},
  // inclusive; the base sits on y+10 like the other icons.
  static const int8_t spans[][3] = {
    {3, 3, 4}, {4, 2, 5}, {5, 1, 6}, {6, 0, 7}, {6, 9, 10},
    {7, 0, 11}, {8, 0, 11}, {9, 0, 11}, {10, 0, 11},
  };
  graphics_context_set_fill_color(ctx, GColorBlack);
  for (size_t i = 0; i < ARRAY_LENGTH(spans); ++i) {
    graphics_fill_rect(ctx,
                       GRect(origin.x + spans[i][1], origin.y + spans[i][0],
                             spans[i][2] - spans[i][1] + 1, 1),
                       0, GCornerNone);
  }
}

static void prv_draw_info_icon(GContext *ctx, InfoIcon icon, GPoint origin) {
  switch (icon) {
    case InfoIconSteps:
      prv_draw_steps_icon(ctx, origin);
      break;
    case InfoIconHeart:
      prv_draw_heart_icon(ctx, origin);
      break;
    case InfoIconSunrise:
      prv_draw_sun_icon(ctx, origin, true);
      break;
    case InfoIconSunset:
      prv_draw_sun_icon(ctx, origin, false);
      break;
    case InfoIconDensityAltitude:
      prv_draw_mountain_icon(ctx, origin);
      break;
  }
}

static void prv_draw_info_cell(GContext *ctx, GRect cell, InfoIcon icon,
                               const char *label, const char *value) {
  const int value_width = cell.size.w - INFO_VALUE_X_OFFSET;
  GFont font = s_info_font_large;
  const GSize large_text_size = graphics_text_layout_get_content_size(
      value, font, GRect(0, 0, 200, cell.size.h),
      GTextOverflowModeFill, GTextAlignmentLeft);
  if (large_text_size.w > value_width) {
    font = s_info_font;
  }
  prv_draw_pixel_text(ctx, label, GPoint(cell.origin.x, cell.origin.y + 5),
                      GColorBlack);
  // Every icon uses y+10 as its lowest ink row. Its bottom stays aligned with
  // the visible baseline of the adjacent status value.
  const GPoint icon_origin = GPoint(cell.origin.x, cell.origin.y + 15);
  prv_draw_info_icon(ctx, icon, icon_origin);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, value, font,
                     GRect(cell.origin.x + INFO_VALUE_X_OFFSET,
                           cell.origin.y + 2, value_width,
                           cell.size.h - 2),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

static void prv_draw_header(GContext *ctx, int width) {
  const GRect header_cell = GRect(CONTENT_MARGIN, OUTER_MARGIN + 1,
                                  width - 2 * CONTENT_MARGIN, STATUS_HEIGHT);
  const GSize battery_size = graphics_text_layout_get_content_size(
      s_battery_buffer, s_header_font, GRect(0, 0, 70, header_cell.size.h),
      GTextOverflowModeFill, GTextAlignmentLeft);
  const int battery_y = header_cell.origin.y + 16;
  prv_draw_pixel_text(ctx, "BAT", GPoint(header_cell.origin.x,
                                          header_cell.origin.y + 5),
                      GColorBlack);
  prv_draw_battery_icon(ctx, GPoint(header_cell.origin.x, battery_y));
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_battery_buffer, s_header_font,
                     GRect(header_cell.origin.x + 22,
                           header_cell.origin.y + 2,
                           battery_size.w + 2, battery_size.h + 2),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  const int weekday_width = prv_bold_pixel_text_width(s_weekday_buffer);
  const GSize date_size = graphics_text_layout_get_content_size(
      s_date_buffer, s_header_font, GRect(0, 0, 90, header_cell.size.h),
      GTextOverflowModeFill, GTextAlignmentLeft);
  const int date_group_width = weekday_width + DATE_VALUE_GAP + date_size.w;
  const int date_x = header_cell.origin.x + header_cell.size.w - date_group_width;
  prv_draw_pixel_text(ctx, "DATE", GPoint(date_x, header_cell.origin.y + 5),
                      GColorBlack);
  // The seven raster rows end at y+25, matching the date digits' ink bottom.
  prv_draw_bold_pixel_text(ctx, s_weekday_buffer,
                           GPoint(date_x, header_cell.origin.y + 19),
                           GColorBlack);
  graphics_draw_text(ctx, s_date_buffer, s_header_font,
                     GRect(date_x + weekday_width + DATE_VALUE_GAP,
                           header_cell.origin.y + 2,
                           date_size.w + 2, date_size.h + 2),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

static void prv_draw_time(GContext *ctx, int width, int center_y) {
  // Pack the visible segments instead of using the font's wide 58px advance.
  // The largest four-digit combination occupies 181px including bold overdraw.
  const int digit_origins[4] = {0, TIME_DIGIT_PITCH, 98, 98 + TIME_DIGIT_PITCH};
  const int colon_origin = 96;
  char glyph[2] = {'\0', '\0'};

  // This font deliberately keeps every digit on the same advance width, but
  // the visible strokes of “1” occupy only its right edge. Center the ink,
  // rather than the invisible advance boxes, so every displayed time looks
  // balanced. Values are the unchanged font's monochrome 72px raster extents.
  static const uint8_t ink_left[10] = {10, 39, 11, 13, 10, 10, 10, 14, 10, 10};
  static const uint8_t ink_right[10] = {47, 47, 48, 47, 47, 47, 47, 48, 47, 47};
  const int first_digit = s_time_buffer[0] - '0';
  const int last_digit = s_time_buffer[3] - '0';
  const int visible_left = ink_left[first_digit];
  const int visible_right = digit_origins[3] + ink_right[last_digit];
  // A second pass one pixel to the right strengthens the existing segments.
  const int visible_width = visible_right - visible_left + 1;
  const int x = (width - visible_width) / 2 - visible_left;
  // The 72px raster occupies y+14 through y+86 for full-height digits.
  const int text_y = center_y - 50;
  graphics_context_set_text_color(ctx, GColorBlack);
  for (int i = 0; i < 4; ++i) {
    glyph[0] = s_time_buffer[i];
    graphics_draw_text(ctx, glyph, s_time_font,
                       GRect(x + digit_origins[i], text_y,
                             60, 96),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, glyph, s_time_font,
                       GRect(x + digit_origins[i] + 1, text_y,
                             60, 96),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    if (i == 1) {
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, GRect(x + colon_origin, center_y - 20,
                                    COLON_SIZE, COLON_SIZE), 0,
                         GCornerNone);
      graphics_fill_rect(ctx, GRect(x + colon_origin, center_y + 14,
                                    COLON_SIZE, COLON_SIZE), 0,
                         GCornerNone);
    }
  }

  const char *indicator = s_ampm_buffer[0] == '\0' ? "24H" : s_ampm_buffer;
  const int indicator_width = prv_pixel_text_width(indicator) + 4;
  const GRect indicator_frame = GRect(width - CONTENT_MARGIN - indicator_width,
                                      HEADER_SEPARATOR_Y + 5,
                                      indicator_width, 11);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, indicator_frame, 0, GCornerNone);
  prv_draw_pixel_text(ctx, indicator,
                      GPoint(indicator_frame.origin.x + 2,
                             indicator_frame.origin.y + 2), GColorWhite);
}

static void prv_canvas_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  const int width = bounds.size.w;
  const int height = bounds.size.h;
  const int content_width = width - 2 * CONTENT_MARGIN;
  const int cell_width = (content_width - CELL_GAP) / 2;
  const int second_info_y = height - OUTER_MARGIN - 1 - STATUS_HEIGHT;
  const int first_info_y = second_info_y - 1 - STATUS_HEIGHT;
  const int time_center_y = (HEADER_SEPARATOR_Y + first_info_y - 1) / 2;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_antialiased(ctx, false);
  graphics_draw_round_rect(ctx, GRect(OUTER_MARGIN, OUTER_MARGIN,
                                width - 2 * OUTER_MARGIN,
                                height - 2 * OUTER_MARGIN), FRAME_RADIUS);
  graphics_draw_line(ctx, GPoint(OUTER_MARGIN, HEADER_SEPARATOR_Y),
                     GPoint(width - OUTER_MARGIN - 1, HEADER_SEPARATOR_Y));
  // Each shared boundary is drawn once, using the outer frame's coordinates.
  graphics_draw_line(ctx, GPoint(OUTER_MARGIN, first_info_y - 1),
                     GPoint(width - OUTER_MARGIN - 1, first_info_y - 1));
  graphics_draw_line(ctx, GPoint(OUTER_MARGIN, second_info_y - 1),
                     GPoint(width - OUTER_MARGIN - 1, second_info_y - 1));
  graphics_draw_line(ctx, GPoint(width / 2, first_info_y - 1),
                     GPoint(width / 2, height - OUTER_MARGIN - 1));

  prv_draw_header(ctx, width);
  prv_draw_time(ctx, width, time_center_y);
  prv_draw_info_cell(ctx,
                     GRect(CONTENT_MARGIN, first_info_y,
                           cell_width, INFO_HEIGHT),
                     InfoIconSteps, "STEP", s_steps_buffer);
  prv_draw_info_cell(ctx,
                     GRect(CONTENT_MARGIN + cell_width + CELL_GAP,
                           first_info_y, cell_width, INFO_HEIGHT),
                     InfoIconHeart, "BPM", s_heart_buffer);
  prv_draw_info_cell(ctx,
                     GRect(CONTENT_MARGIN, second_info_y,
                           cell_width, INFO_HEIGHT),
                     s_sun_event_is_sunrise ? InfoIconSunrise : InfoIconSunset,
                     s_sun_event_is_sunrise ? "RISE" : "SET",
                     s_sun_event_buffer);
  prv_draw_info_cell(ctx,
                     GRect(CONTENT_MARGIN + cell_width + CELL_GAP,
                           second_info_y, cell_width, INFO_HEIGHT),
                     InfoIconDensityAltitude, "DA FT",
                     s_density_altitude_buffer);
}

// Sun and weather requests share one message so neither waits on the other.
static void prv_send_requests(bool sun, bool weather) {
  if ((!sun && !weather) || s_outbox_busy) {
    return;
  }
  const time_t now = time(NULL);
  if (sun) {
    s_last_sun_request_time = now;
    persist_write_int(PERSIST_KEY_LAST_SUN_REQUEST,
                      (int32_t)s_last_sun_request_time);
  }
  if (weather) {
    s_last_weather_request_time = now;
  }

  DictionaryIterator *iterator;
  AppMessageResult result = app_message_outbox_begin(&iterator);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Request begin failed: %d", (int)result);
    return;
  }
  if (sun) {
    dict_write_uint8(iterator, MESSAGE_KEY_REQUEST_SUN, 1);
  }
  if (weather) {
    dict_write_uint8(iterator, MESSAGE_KEY_REQUEST_WEATHER, 1);
  }
  result = app_message_outbox_send();
  if (result == APP_MSG_OK) {
    s_outbox_busy = true;
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Request send failed: %d", (int)result);
  }
}

static bool prv_sun_retry_is_due(void) {
  if (prv_has_sun_result_for_current_date()) {
    return false;
  }
  const time_t now = time(NULL);
  return s_last_sun_request_time <= 0 || now < s_last_sun_request_time ||
      now - s_last_sun_request_time >= SUN_RETRY_INTERVAL_SECONDS;
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  const int32_t previous_date = s_current_date;
  prv_update_clock_state(tick_time);
  prv_update_health_state();
  prv_mark_dirty();
  const bool day_changed = previous_date != s_current_date;
  const bool retry_boundary = tick_time->tm_min == 0 &&
      tick_time->tm_hour % 6 == 0;
  const bool sun_due =
      (day_changed && !prv_has_sun_result_for_current_date()) ||
      (retry_boundary && prv_sun_retry_is_due());
  prv_send_requests(sun_due, prv_weather_request_is_due());
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate ||
      event == HealthEventMovementUpdate ||
      event == HealthEventHeartRateUpdate) {
    prv_update_health_state();
    prv_mark_dirty();
  }
}

static void prv_battery_handler(BatteryChargeState state) {
  prv_update_battery_state(state);
  prv_mark_dirty();
}

static void prv_receive_sun_times(DictionaryIterator *iterator) {
  Tuple *sunrise_tuple = dict_find(iterator, MESSAGE_KEY_SUNRISE_MINUTES);
  Tuple *sunset_tuple = dict_find(iterator, MESSAGE_KEY_SUNSET_MINUTES);
  Tuple *date_tuple = dict_find(iterator, MESSAGE_KEY_SUN_DATE);
  Tuple *next_sunrise_tuple =
      dict_find(iterator, MESSAGE_KEY_NEXT_SUNRISE_MINUTES);
  Tuple *next_date_tuple = dict_find(iterator, MESSAGE_KEY_NEXT_SUN_DATE);
  if (!sunrise_tuple || !sunset_tuple || !date_tuple ||
      !next_sunrise_tuple || !next_date_tuple) {
    return;
  }

  const int32_t sunrise = sunrise_tuple->value->int32;
  const int32_t sunset = sunset_tuple->value->int32;
  const int32_t date = date_tuple->value->int32;
  const int32_t next_sunrise = next_sunrise_tuple->value->int32;
  const int32_t next_date = next_date_tuple->value->int32;
  const bool has_times = prv_is_minute_of_day(sunrise) &&
      prv_is_minute_of_day(sunset);
  const bool has_no_events = sunrise == -1 && sunset == -1;
  const bool next_valid = (prv_is_minute_of_day(next_sunrise) ||
      next_sunrise == -1) && next_date > date;
  if ((!has_times && !has_no_events) || date <= 0 || !next_valid) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Invalid sun data");
    return;
  }

  s_sunrise_minutes = sunrise;
  s_sunset_minutes = sunset;
  s_sun_date = date;
  s_next_sunrise_minutes = next_sunrise;
  s_next_sun_date = next_date;
  persist_write_int(PERSIST_KEY_SUNRISE_MINUTES, sunrise);
  persist_write_int(PERSIST_KEY_SUNSET_MINUTES, sunset);
  persist_write_int(PERSIST_KEY_SUN_DATE, date);
  persist_write_int(PERSIST_KEY_NEXT_SUNRISE_MINUTES, next_sunrise);
  persist_write_int(PERSIST_KEY_NEXT_SUN_DATE, next_date);
  prv_refresh_sun_buffers();
  prv_mark_dirty();
}

static void prv_receive_density_altitude(DictionaryIterator *iterator) {
  Tuple *tuple = dict_find(iterator, MESSAGE_KEY_DENSITY_ALTITUDE_FT);
  if (!tuple) {
    return;
  }
  const int32_t feet = tuple->value->int32;
  if (feet < DENSITY_ALTITUDE_MIN_FT || feet > DENSITY_ALTITUDE_MAX_FT) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Invalid density altitude: %ld",
            (long)feet);
    return;
  }
  s_density_altitude_ft = feet;
  s_density_altitude_time = time(NULL);
  persist_write_int(PERSIST_KEY_DENSITY_ALTITUDE_FT, feet);
  persist_write_int(PERSIST_KEY_DENSITY_ALTITUDE_TIME,
                    (int32_t)s_density_altitude_time);
  prv_refresh_density_altitude_buffer();
  prv_mark_dirty();
}

static void prv_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *ready_tuple = dict_find(iterator, MESSAGE_KEY_JS_READY);
  if (ready_tuple) {
    // The phone side just started, so any earlier request was lost: ask for
    // stale weather right away instead of waiting out the retry interval.
    prv_send_requests(prv_sun_retry_is_due(), prv_weather_is_stale());
  }
  prv_receive_sun_times(iterator);
  prv_receive_density_altitude(iterator);
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Inbox dropped: %d", (int)reason);
}

static void prv_outbox_sent(DictionaryIterator *iterator, void *context) {
  s_outbox_busy = false;
}

static void prv_outbox_failed(DictionaryIterator *iterator, AppMessageResult reason,
                              void *context) {
  s_outbox_busy = false;
  APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox failed: %d", (int)reason);
}

static void prv_load_persisted_sun(void) {
  if (persist_exists(PERSIST_KEY_SUN_DATE) &&
      persist_exists(PERSIST_KEY_SUNRISE_MINUTES) &&
      persist_exists(PERSIST_KEY_SUNSET_MINUTES)) {
    s_sun_date = persist_read_int(PERSIST_KEY_SUN_DATE);
    s_sunrise_minutes = persist_read_int(PERSIST_KEY_SUNRISE_MINUTES);
    s_sunset_minutes = persist_read_int(PERSIST_KEY_SUNSET_MINUTES);
  }
  if (persist_exists(PERSIST_KEY_NEXT_SUN_DATE) &&
      persist_exists(PERSIST_KEY_NEXT_SUNRISE_MINUTES)) {
    s_next_sun_date = persist_read_int(PERSIST_KEY_NEXT_SUN_DATE);
    s_next_sunrise_minutes =
        persist_read_int(PERSIST_KEY_NEXT_SUNRISE_MINUTES);
  }
  if (persist_exists(PERSIST_KEY_LAST_SUN_REQUEST)) {
    s_last_sun_request_time =
        (time_t)persist_read_int(PERSIST_KEY_LAST_SUN_REQUEST);
  }
  if (persist_exists(PERSIST_KEY_DENSITY_ALTITUDE_FT) &&
      persist_exists(PERSIST_KEY_DENSITY_ALTITUDE_TIME)) {
    s_density_altitude_ft = persist_read_int(PERSIST_KEY_DENSITY_ALTITUDE_FT);
    s_density_altitude_time =
        (time_t)persist_read_int(PERSIST_KEY_DENSITY_ALTITUDE_TIME);
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(window_layer);
  s_time_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_DIGITAL_72));
  s_header_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_info_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_info_font_large = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_canvas_layer = layer_create(bounds);
  if (s_canvas_layer) {
    layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);
  }
}

static void prv_window_unload(Window *window) {
  if (s_canvas_layer) {
    layer_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
  }
  if (s_time_font) {
    fonts_unload_custom_font(s_time_font);
    s_time_font = NULL;
  }
  s_info_font = NULL;
  s_info_font_large = NULL;
  s_header_font = NULL;
}

static void prv_init(void) {
  setlocale(LC_ALL, "");
  prv_load_persisted_sun();
  time_t now = time(NULL);
  struct tm *local_time = localtime(&now);
  if (local_time) {
    prv_update_clock_state(local_time);
  }
  prv_update_health_state();
  prv_update_battery_state(battery_state_service_peek());

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorWhite);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload
  });
  window_stack_push(s_main_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  battery_state_service_subscribe(prv_battery_handler);
#ifdef PBL_HEALTH
  s_health_subscribed = health_service_events_subscribe(prv_health_handler, NULL);
#endif
  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_register_outbox_sent(prv_outbox_sent);
  app_message_register_outbox_failed(prv_outbox_failed);
  const AppMessageResult result = app_message_open(128, 64);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage open failed: %d", (int)result);
  }
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
#ifdef PBL_HEALTH
  if (s_health_subscribed) {
    health_service_events_unsubscribe();
  }
#endif
  app_message_deregister_callbacks();
  window_destroy(s_main_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
  return 0;
}
