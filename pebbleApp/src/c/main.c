#include <pebble.h>

static Window *s_window;
static TextLayer *s_nav_layer;
static TextLayer *s_dist_layer;
static TextLayer *s_hr_layer;
static TextLayer *s_rec_layer;
static bool s_is_recording = false;
static AppTimer *s_hr_timer = NULL;

static void prv_update_heart_rate() {
  HealthValue value = health_service_peek_current_value(HealthMetricHeartRateBPM);
  static char hr_buffer[16];
  if (value > 0) {
    snprintf(hr_buffer, sizeof(hr_buffer), "HR: %d", (int)value);
  } else {
    snprintf(hr_buffer, sizeof(hr_buffer), "HR: --");
  }
  text_layer_set_text(s_hr_layer, hr_buffer);

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

  Tuple *dist_tuple = dict_find(iter, MESSAGE_KEY_NAV_DISTANCE);
  if (dist_tuple) {
    text_layer_set_text(s_dist_layer, dist_tuple->value->cstring);
  }

  Tuple *rec_tuple = dict_find(iter, MESSAGE_KEY_RECORDING_COMMAND);
  if (rec_tuple) {
    s_is_recording = (rec_tuple->value->int32 != 0);
    text_layer_set_text(s_rec_layer, s_is_recording ? "REC" : "");
    text_layer_set_background_color(s_rec_layer, s_is_recording ? GColorBlack : GColorClear);
    text_layer_set_text_color(s_rec_layer, s_is_recording ? GColorWhite : GColorBlack);
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

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_nav_layer = text_layer_create(GRect(0, 20, bounds.size.w, 40));
  text_layer_set_text(s_nav_layer, "Waiting for Nav...");
  text_layer_set_text_alignment(s_nav_layer, GTextAlignmentCenter);
  text_layer_set_font(s_nav_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_nav_layer));

  s_dist_layer = text_layer_create(GRect(0, 70, bounds.size.w, 30));
  text_layer_set_text(s_dist_layer, "");
  text_layer_set_text_alignment(s_dist_layer, GTextAlignmentCenter);
  text_layer_set_font(s_dist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_dist_layer));

  s_hr_layer = text_layer_create(GRect(0, 120, bounds.size.w, 20));
  text_layer_set_text(s_hr_layer, "HR: --");
  text_layer_set_text_alignment(s_hr_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_hr_layer));

  s_rec_layer = text_layer_create(GRect(0, 0, bounds.size.w, 20));
  text_layer_set_text(s_rec_layer, "");
  text_layer_set_text_alignment(s_rec_layer, GTextAlignmentCenter);
  text_layer_set_font(s_rec_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_rec_layer));
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_nav_layer);
  text_layer_destroy(s_dist_layer);
  text_layer_destroy(s_hr_layer);
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
