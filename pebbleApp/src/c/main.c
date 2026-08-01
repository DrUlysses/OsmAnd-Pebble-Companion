#include <pebble.h>

static Window *s_window;
static TextLayer *s_nav_layer;
static TextLayer *s_dist_layer;
static TextLayer *s_hr_layer;
static TextLayer *s_speed_layer;
static TextLayer *s_rec_layer;
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

  Tuple *rec_tuple = dict_find(iter, MESSAGE_KEY_RECORDING_COMMAND);
  if (rec_tuple) {
    s_is_recording = (rec_tuple->value->int32 != 0);
    // Keeping s_is_recording for HR timer logic
  }

  Tuple *rec_state_tuple = dict_find(iter, MESSAGE_KEY_RECORDING_STATE);
  if (rec_state_tuple) {
    int state = rec_state_tuple->value->int32;
    if (state == 1) { // Running
      text_layer_set_text(s_rec_layer, "REC");
      text_layer_set_background_color(s_rec_layer, GColorBlack);
      text_layer_set_text_color(s_rec_layer, GColorWhite);
      s_is_recording = true;
    } else if (state == 2) { // Paused
      text_layer_set_text(s_rec_layer, "||");
      text_layer_set_background_color(s_rec_layer, GColorBlack);
      text_layer_set_text_color(s_rec_layer, GColorWhite);
      s_is_recording = false;
    } else { // Stopped
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

static void prv_arrow_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);

  if (s_nav_type == 0) return;

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

  // Recording status at top
  s_rec_layer = text_layer_create(GRect(0, 0, bounds.size.w, 20));
  text_layer_set_text(s_rec_layer, "");
  text_layer_set_text_alignment(s_rec_layer, GTextAlignmentCenter);
  text_layer_set_font(s_rec_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_rec_layer));

  // Navigation Arrow - Large Vector
  s_arrow_layer = layer_create(GRect(0, 20, bounds.size.w, 50));
  layer_set_update_proc(s_arrow_layer, prv_arrow_layer_update_proc);
  layer_add_child(window_layer, s_arrow_layer);

  // Hidden text layer for compatibility or small fallback (we keep it for now but hidden)
  s_nav_layer = text_layer_create(GRect(0, 0, 0, 0));

  // Distance - Medium
  s_dist_layer = text_layer_create(GRect(0, 75, bounds.size.w, 40));
  text_layer_set_text(s_dist_layer, "");
  text_layer_set_text_alignment(s_dist_layer, GTextAlignmentCenter);
  text_layer_set_font(s_dist_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  layer_add_child(window_layer, text_layer_get_layer(s_dist_layer));

  // Heart Rate and Speed at bottom
  int bottom_y = bounds.size.h - 35;
  s_hr_layer = text_layer_create(GRect(0, bottom_y, bounds.size.w / 2, 25));
  text_layer_set_text(s_hr_layer, "HR: --");
  text_layer_set_text_alignment(s_hr_layer, GTextAlignmentCenter);
  text_layer_set_font(s_hr_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_hr_layer));

  s_speed_layer = text_layer_create(GRect(bounds.size.w / 2, bottom_y, bounds.size.w / 2, 25));
  text_layer_set_text(s_speed_layer, "0 km/h");
  text_layer_set_text_alignment(s_speed_layer, GTextAlignmentCenter);
  text_layer_set_font(s_speed_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_speed_layer));
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_arrow_layer);
  text_layer_destroy(s_nav_layer);
  text_layer_destroy(s_dist_layer);
  text_layer_destroy(s_hr_layer);
  text_layer_destroy(s_speed_layer);
  text_layer_destroy(s_rec_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
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

  // Instant HR
  prv_update_heart_rate();
}

static void prv_deinit(void) {
  window_destroy(s_window);
  health_service_events_unsubscribe();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
