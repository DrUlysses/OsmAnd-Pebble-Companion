#include <pebble.h>

static Window *s_window;
static TextLayer *s_nav_layer;
static TextLayer *s_dist_layer;
static TextLayer *s_hr_layer;
static TextLayer *s_speed_layer;
static TextLayer *s_rec_layer;
static TextLayer *s_time_layer;
static TextLayer *s_rec_time_layer;
static TextLayer *s_rec_dist_layer;
static TextLayer *s_rem_dist_layer;
static TextLayer *s_rem_time_layer;
static Layer *s_arrow_layer;
static int s_nav_type = 0;
static bool s_is_recording = false;
static AppTimer *s_hr_timer = NULL;

static void prv_update_hr_ui() {
  HealthValue value = health_service_peek_current_value(HealthMetricHeartRateBPM);
  static char hr_buffer[16];
  if (value > 0) {
    snprintf(hr_buffer, sizeof(hr_buffer), "HR: %d", (int)value);
  } else {
    snprintf(hr_buffer, sizeof(hr_buffer), "HR: --");
  }
  text_layer_set_text(s_hr_layer, hr_buffer);
}

static void prv_update_heart_rate() {
  prv_update_hr_ui();
  HealthValue value = health_service_peek_current_value(HealthMetricHeartRateBPM);
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    int hr_val = (int)value;
    dict_write_int(iter, MESSAGE_KEY_HEALTH_HEART_RATE, &hr_val, sizeof(int), true);
    app_message_outbox_send();
  }
}

static void prv_hr_timer_callback(void *data) {
  prv_update_heart_rate();
  if (s_is_recording) {
    s_hr_timer = app_timer_register(10000, prv_hr_timer_callback, NULL);
  } else {
    s_hr_timer = NULL;
  }
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate) {
    prv_update_heart_rate();
  }
}

static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received AppMessage");
  Tuple *nav_tuple = dict_find(iter, MESSAGE_KEY_NAV_INSTRUCTION);
  if (nav_tuple) {
    text_layer_set_text(s_nav_layer, nav_tuple->value->cstring);
  }
  Tuple *nav_type_tuple = dict_find(iter, MESSAGE_KEY_NAV_TYPE);
  if (nav_type_tuple) {
    s_nav_type = nav_type_tuple->value->int32;
    layer_mark_dirty(s_arrow_layer);
  }
  Tuple *dist_tuple = dict_find(iter, MESSAGE_KEY_NAV_DISTANCE);
  if (dist_tuple) {
    text_layer_set_text(s_dist_layer, dist_tuple->value->cstring);
  }
  Tuple *rec_state_tuple = dict_find(iter, MESSAGE_KEY_RECORDING_STATE);
  if (rec_state_tuple) {
    int state = rec_state_tuple->value->int32;
    if (state == 1) {
      // Running
      text_layer_set_text(s_rec_layer, ">");
      text_layer_set_background_color(s_rec_layer, GColorWhite);
      text_layer_set_text_color(s_rec_layer, GColorBlack);
      s_is_recording = true;
    } else if (state == 2) {
      // Paused
      text_layer_set_text(s_rec_layer, "||");
      text_layer_set_background_color(s_rec_layer, GColorWhite);
      text_layer_set_text_color(s_rec_layer, GColorBlack);
      s_is_recording = false;
    } else {
      // Stopped
      text_layer_set_text(s_rec_layer, "");
      text_layer_set_background_color(s_rec_layer, GColorClear);
      text_layer_set_text_color(s_rec_layer, GColorBlack);
      s_is_recording = false;
    }
    if (s_is_recording) {
      if (!s_hr_timer) {
        s_hr_timer = app_timer_register(10000, prv_hr_timer_callback, NULL);
      }
    } else {
      if (s_hr_timer) {
        app_timer_cancel(s_hr_timer);
        s_hr_timer = NULL;
      }
    }
  }
  Tuple *rec_time_tuple = dict_find(iter, MESSAGE_KEY_RECORDING_TIME);
  if (rec_time_tuple) {
    static char rec_time_buffer[16];
    snprintf(rec_time_buffer, sizeof(rec_time_buffer), "%s", rec_time_tuple->value->cstring);
    text_layer_set_text(s_rec_time_layer, rec_time_buffer);
  }
  Tuple *rec_dist_tuple = dict_find(iter, MESSAGE_KEY_RECORDING_DISTANCE);
  if (rec_dist_tuple) {
    static char rec_dist_buffer[16];
    snprintf(rec_dist_buffer, sizeof(rec_dist_buffer), "%s", rec_dist_tuple->value->cstring);
    text_layer_set_text(s_rec_dist_layer, rec_dist_buffer);
  }
  Tuple *rem_dist_tuple = dict_find(iter, MESSAGE_KEY_REMAINING_DISTANCE);
  if (rem_dist_tuple) {
    static char rem_dist_buffer[16];
    snprintf(rem_dist_buffer, sizeof(rem_dist_buffer), "%s", rem_dist_tuple->value->cstring);
    text_layer_set_text(s_rem_dist_layer, rem_dist_buffer);
  }
  Tuple *rem_time_tuple = dict_find(iter, MESSAGE_KEY_REMAINING_TIME);
  if (rem_time_tuple) {
    static char rem_time_buffer[16];
    snprintf(rem_time_buffer, sizeof(rem_time_buffer), "%s", rem_time_tuple->value->cstring);
    text_layer_set_text(s_rem_time_layer, rem_time_buffer);
  }
  Tuple *speed_tuple = dict_find(iter, MESSAGE_KEY_SPEED);
  if (speed_tuple) {
    static char speed_buffer[16];
    int speed = speed_tuple->value->int32;
    snprintf(speed_buffer, sizeof(speed_buffer), "%d km/h", speed);
    text_layer_set_text(s_speed_layer, speed_buffer);
  }
}

static void prv_inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped! Reason: %d", reason);
}

static void prv_outbox_sent_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Outbox sent success");
}

static void prv_outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed! Reason: %d", reason);
}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  // Toggle GPX Recording
  uint8_t command = 1; // 1 for Toggle/Start
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int(iter, MESSAGE_KEY_RECORDING_COMMAND, &command, sizeof(uint8_t), true);
    app_message_outbox_send();
  }
}

static void prv_refresh_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_update_hr_ui();
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    // Send HR
    HealthValue value = health_service_peek_current_value(HealthMetricHeartRateBPM);
    int hr_val = (int)value;
    dict_write_int(iter, MESSAGE_KEY_HEALTH_HEART_RATE, &hr_val, sizeof(int), true);

    // Send Refresh Command
    uint8_t command = 1;
    dict_write_int(iter, MESSAGE_KEY_REFRESH_COMMAND, &command, sizeof(uint8_t), true);
    app_message_outbox_send();
  }
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, prv_refresh_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_refresh_click_handler);
}

static void prv_update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time();
}

static void prv_arrow_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);
  if (s_nav_type == 0)
    return;

  // Simple drawing: lines and arrows
  // 1:Straight, 2:Left, 3:Slight L, 4:Sharp L, 5:Right, 6:Slight R, 7:Sharp R,
  // 8:Keep L, 9:Keep R, 10:U-Turn, 11:U-Turn R, 12:Off, 13:Rndb, 14:Rndb L

  int x = center.x;
  int y = center.y;
  int size = 18;
  switch (s_nav_type) {
  case 1: // Straight
    graphics_draw_line(ctx, GPoint(x, y + size), GPoint(x, y - size));
    graphics_draw_line(ctx, GPoint(x, y - size), GPoint(x - 8, y - size + 8));
    graphics_draw_line(ctx, GPoint(x, y - size), GPoint(x + 8, y - size + 8));
    break;
  case 2: // Left
    graphics_draw_line(ctx, GPoint(x + size, y), GPoint(x - size, y));
    graphics_draw_line(ctx, GPoint(x - size, y), GPoint(x - size + 8, y - 8));
    graphics_draw_line(ctx, GPoint(x - size, y), GPoint(x - size + 8, y + 8));
    break;
  case 5: // Right
    graphics_draw_line(ctx, GPoint(x - size, y), GPoint(x + size, y));
    graphics_draw_line(ctx, GPoint(x + size, y), GPoint(x + size - 8, y - 8));
    graphics_draw_line(ctx, GPoint(x + size, y), GPoint(x + size - 8, y + 8));
    break;
  case 3: // Slight Left
  case 8: // Keep Left
    graphics_draw_line(ctx, GPoint(x + 10, y + 15), GPoint(x - 10, y - 15));
    graphics_draw_line(ctx, GPoint(x - 10, y - 15), GPoint(x - 10, y - 5));
    graphics_draw_line(ctx, GPoint(x - 10, y - 15), GPoint(x, y - 15));
    break;
  case 6: // Slight Right
  case 9: // Keep Right
    graphics_draw_line(ctx, GPoint(x - 10, y + 15), GPoint(x + 10, y - 15));
    graphics_draw_line(ctx, GPoint(x + 10, y - 15), GPoint(x + 10, y - 5));
    graphics_draw_line(ctx, GPoint(x + 10, y - 15), GPoint(x, y - 15));
    break;
  case 4: // Sharp Left
    graphics_draw_line(ctx, GPoint(x + 15, y + 5), GPoint(x - 5, y + 5));
    graphics_draw_line(ctx, GPoint(x - 5, y + 5), GPoint(x - 15, y - 15));
    graphics_draw_line(ctx, GPoint(x - 15, y - 15), GPoint(x - 15, y - 5));
    graphics_draw_line(ctx, GPoint(x - 15, y - 15), GPoint(x - 5, y - 15));
    break;
  case 7: // Sharp Right
    graphics_draw_line(ctx, GPoint(x - 15, y + 5), GPoint(x + 5, y + 5));
    graphics_draw_line(ctx, GPoint(x + 5, y + 5), GPoint(x + 15, y - 15));
    graphics_draw_line(ctx, GPoint(x + 15, y - 15), GPoint(x + 15, y - 5));
    graphics_draw_line(ctx, GPoint(x + 15, y - 15), GPoint(x + 5, y - 15));
    break;
  case 10: // U-Turn
  case 11:
    graphics_draw_circle(ctx, GPoint(x, y + 5), 12); // full circle for simplicity
    graphics_draw_line(ctx, GPoint(x - 12, y + 5), GPoint(x - 12, y - 10));
    graphics_draw_line(ctx, GPoint(x - 12, y - 10), GPoint(x - 18, y - 4));
    graphics_draw_line(ctx, GPoint(x - 12, y - 10), GPoint(x - 6, y - 4));
    break;
  case 13: // Roundabout
  case 14:
    graphics_draw_circle(ctx, center, 15);
    graphics_draw_line(ctx, GPoint(x + 15, y), GPoint(x + 15, y - 8));
    graphics_draw_line(ctx, GPoint(x + 15, y - 8), GPoint(x + 10, y - 3));
    graphics_draw_line(ctx, GPoint(x + 15, y - 8), GPoint(x + 20, y - 3));
    break;
  case 12: // Off route
    graphics_draw_line(ctx, GPoint(x - 15, y - 15), GPoint(x + 15, y + 15));
    graphics_draw_line(ctx, GPoint(x + 15, y - 15), GPoint(x - 15, y + 15));
    break;
  default:
    break;
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Current Time at Top Right
  s_time_layer = text_layer_create(GRect(80, 0, bounds.size.w - 87, 20));
  text_layer_set_text(s_time_layer, "00:00");
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentRight);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // Recording status at Top Left
  s_rec_layer = text_layer_create(GRect(2, 5, 15, 18));
  text_layer_set_text(s_rec_layer, "");
  text_layer_set_text_alignment(s_rec_layer, GTextAlignmentCenter);
  text_layer_set_font(s_rec_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_rec_layer));

  // Recording Time at Top Left
  s_rec_time_layer = text_layer_create(GRect(15, 2, 68, 18));
  text_layer_set_text(s_rec_time_layer, "");
  text_layer_set_text_alignment(s_rec_time_layer, GTextAlignmentLeft);
  text_layer_set_font(s_rec_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(window_layer, text_layer_get_layer(s_rec_time_layer));

  // Recording Distance below Rec Time
  s_rec_dist_layer = text_layer_create(GRect(15, 20, 68, 18));
  text_layer_set_text(s_rec_dist_layer, "");
  text_layer_set_text_alignment(s_rec_dist_layer, GTextAlignmentLeft);
  text_layer_set_font(s_rec_dist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(window_layer, text_layer_get_layer(s_rec_dist_layer));

  // Navigation Arrow
  s_arrow_layer = layer_create(GRect(0, 35, bounds.size.w, 40));
  layer_set_update_proc(s_arrow_layer, prv_arrow_layer_update_proc);
  layer_add_child(window_layer, s_arrow_layer);

  // Hidden text layer for compatibility
  s_nav_layer = text_layer_create(GRect(0, 0, 0, 0));

  // Main Distance - Medium
  s_dist_layer = text_layer_create(GRect(0, 75, bounds.size.w, 44));
  text_layer_set_text(s_dist_layer, "");
  text_layer_set_text_alignment(s_dist_layer, GTextAlignmentCenter);
  text_layer_set_font(s_dist_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS));
  layer_add_child(window_layer, text_layer_get_layer(s_dist_layer));

  // Remaining Distance below main distance
  s_rem_dist_layer = text_layer_create(GRect(0, 115, bounds.size.w, 24));
  text_layer_set_text(s_rem_dist_layer, "");
  text_layer_set_text_alignment(s_rem_dist_layer, GTextAlignmentCenter);
  text_layer_set_font(s_rem_dist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  layer_add_child(window_layer, text_layer_get_layer(s_rem_dist_layer));

  // Remaining Time below remaining distance
  s_rem_time_layer = text_layer_create(GRect(0, 140, bounds.size.w, 24));
  text_layer_set_text(s_rem_time_layer, "");
  text_layer_set_text_alignment(s_rem_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_rem_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  layer_add_child(window_layer, text_layer_get_layer(s_rem_time_layer));

  // Heart Rate and Speed at bottom
  int bottom_y = bounds.size.h - 25;
  s_hr_layer = text_layer_create(GRect(0, bottom_y, bounds.size.w / 3, 25));
  text_layer_set_text(s_hr_layer, "HR: --");
  text_layer_set_text_alignment(s_hr_layer, GTextAlignmentCenter);
  text_layer_set_font(s_hr_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_hr_layer));
  s_speed_layer = text_layer_create(GRect(bounds.size.w / 2, bottom_y, bounds.size.w / 1.5, 25));
  text_layer_set_text(s_speed_layer, "0 km/h");
  text_layer_set_text_alignment(s_speed_layer, GTextAlignmentCenter);
  text_layer_set_font(s_speed_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_speed_layer));
  prv_update_time();
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_arrow_layer);
  text_layer_destroy(s_nav_layer);
  text_layer_destroy(s_dist_layer);
  text_layer_destroy(s_hr_layer);
  text_layer_destroy(s_speed_layer);
  text_layer_destroy(s_rec_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_rec_time_layer);
  text_layer_destroy(s_rec_dist_layer);
  text_layer_destroy(s_rem_dist_layer);
  text_layer_destroy(s_rem_time_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = prv_window_load,
                                           .unload = prv_window_unload,
                                       });
  window_stack_push(s_window, true);

  // AppMessage
  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_register_inbox_dropped(prv_inbox_dropped_handler);
  app_message_register_outbox_sent(prv_outbox_sent_handler);
  app_message_register_outbox_failed(prv_outbox_failed_handler);
  app_message_open(256, 256);

  // Health
  if (health_service_events_subscribe(prv_health_handler, NULL)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Subscribed to health events");
  }

  // Time
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);

  // Instant HR
  prv_update_heart_rate();
}

static void prv_deinit(void) {
  window_destroy(s_window);
  health_service_events_unsubscribe();
  tick_timer_service_unsubscribe();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
