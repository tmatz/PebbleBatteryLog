#include <pebble.h>

#define PERSIST_KEY_LOG_COUNT 0x00000000
#define PERSIST_KEY_LOG_INDEX 0x00000001
#define PERSIST_KEY_LOG_BASE 0x00010000

#define MAX_LOG_COUNT 20
#define WAKEUP_INTERVAL (3 * 60 * 60) // seconds

static Window *s_main_window;
static TextLayer *s_battery_layer;
static Layer *s_graph_layer;
static WakeupId s_wakeup_id = -1;
  
typedef struct ChargeLog {
  time_t time;
  BatteryChargeState charge_state;
} ChargeLog;

bool get_last_charge_log(ChargeLog* charge_log)
{
  if (!persist_exists(PERSIST_KEY_LOG_COUNT) || !persist_exists(PERSIST_KEY_LOG_INDEX)) {
    return false;
  }
  
  int32_t log_count = persist_read_int(PERSIST_KEY_LOG_COUNT);
  int32_t log_index = persist_read_int(PERSIST_KEY_LOG_INDEX);
  
  if (log_count == 0) {
    return false;
  }
    
  uint32_t key_log = PERSIST_KEY_LOG_BASE + (log_index + log_count - 1) % MAX_LOG_COUNT;
  persist_read_data(key_log, charge_log, sizeof(*charge_log));
  
  return true;
}

void save_charge_log(ChargeLog* charge_log)
{
  int32_t log_count = 0;
  int32_t log_index = 0;
  
  if (persist_exists(PERSIST_KEY_LOG_COUNT)) {
    log_count = persist_read_int(PERSIST_KEY_LOG_COUNT);
  }
      
  if (persist_exists(PERSIST_KEY_LOG_INDEX)) {
    log_index = persist_read_int(PERSIST_KEY_LOG_INDEX);
  }
  
  log_count++;
  
  uint32_t key_log = PERSIST_KEY_LOG_BASE + (log_index + log_count - 1) % MAX_LOG_COUNT;
  
  if (log_count > MAX_LOG_COUNT) {
    log_count--;
    log_index++;
  }
  
  persist_write_int(PERSIST_KEY_LOG_COUNT, log_count);
  persist_write_int(PERSIST_KEY_LOG_INDEX, log_index);
  persist_write_data(key_log, charge_log, sizeof(*charge_log));
  
  if (launch_reason() != APP_LAUNCH_WAKEUP) {
    layer_mark_dirty(s_graph_layer);
  }
}

static void show_charge_log()
{
  if (!persist_exists(PERSIST_KEY_LOG_COUNT) || !persist_exists(PERSIST_KEY_LOG_INDEX)) {
    return;
  }
  
  int log_count = persist_read_int(PERSIST_KEY_LOG_COUNT);
  int log_index = persist_read_int(PERSIST_KEY_LOG_INDEX);
  
  if (log_count == 0) {
    return;
  }
  
  time_t now = time(NULL);
  for (int i = 0; i < log_count; ++i) {
    uint32_t key_log = PERSIST_KEY_LOG_BASE + (log_index + i) % MAX_LOG_COUNT;
    
    ChargeLog charge_log;
    persist_read_data(key_log, &charge_log, sizeof(charge_log));
    
    static char buff[] = "999 4294967296 %100";
    snprintf(buff, sizeof(buff), "%d %u %d%%", i, (unsigned)difftime(now, charge_log.time), charge_log.charge_state.charge_percent);
    APP_LOG(APP_LOG_LEVEL_DEBUG, buff);
  }
}

static void save_charge_state(BatteryChargeState* charge_state)
{
  ChargeLog charge_log;
  if (!get_last_charge_log(&charge_log) ||
      charge_log.charge_state.charge_percent != charge_state->charge_percent)
  {
    charge_log.time = time(NULL);
    charge_log.charge_state = *charge_state;
    save_charge_log(&charge_log);
  }
}

static void show_charge_state(BatteryChargeState* charge_state)
{
  save_charge_state(charge_state);
  show_charge_log();
  
  static char battery_text[] = "100% charging";

  if (charge_state->is_charging) {
    snprintf(battery_text, sizeof(battery_text), "%d%% charging", charge_state->charge_percent);
  } else {
    snprintf(battery_text, sizeof(battery_text), "%d%% charged", charge_state->charge_percent);
  }
  
  text_layer_set_text(s_battery_layer, battery_text);
}

static bool schedule_wakeup_measure_battery_state()
{
  s_wakeup_id = wakeup_schedule(time(NULL) + WAKEUP_INTERVAL, 0 /*cookie*/, true /*notify_if_missed*/);
  return s_wakeup_id >= 0;
}

static void handle_wakeup(WakeupId wakeup_id, int32_t cookie)
{
  APP_LOG(APP_LOG_LEVEL_DEBUG, "handle_wakeup");
  BatteryChargeState charge_state = battery_state_service_peek();
  save_charge_state(&charge_state);
  show_charge_log();
  
  schedule_wakeup_measure_battery_state();
}
  
static void handle_battery(BatteryChargeState charge_state) {
  show_charge_state(&charge_state);
}

static void update_graph_layer(Layer *layer, GContext *ctx) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  GRect bounds = layer_get_bounds(layer);
  int16_t w = bounds.size.w;
  int16_t h = bounds.size.h;
  
  graphics_draw_rect(ctx, bounds); 
  
  int32_t count = persist_read_int(PERSIST_KEY_LOG_COUNT);
  if (count == 0) {
    return;
  }
  
  int32_t index = persist_read_int(PERSIST_KEY_LOG_INDEX);
  
  time_t now = time(NULL);
  unsigned max_time = 10 * 24 * 60 * 60; // 10days
  int16_t x0 = -1;
  int16_t y0 = -1;
  for (int i = 0; i < count; ++i) {
    uint32_t key = PERSIST_KEY_LOG_BASE + (index + i) % MAX_LOG_COUNT;
    ChargeLog log;
    persist_read_data(key, &log, sizeof(log));
    unsigned t = (unsigned)difftime(now, log.time);
    int16_t x = -1;
    if (t <= max_time) {
      x = w - w * t / max_time;
    }
    int16_t y = h - h * log.charge_state.charge_percent / 100;
    if (0 <= x) {
      graphics_fill_circle(ctx, GPoint(x, y), 2);
      
      if (0 <= x0) {
        graphics_draw_line(ctx, GPoint(x0, y0), GPoint(x, y0));
        graphics_draw_line(ctx, GPoint(x, y0), GPoint(x, y));
      }
    }
    x0 = x;
    y0 = y;
  }
  
  if (0 <= y0) {
    graphics_draw_line(ctx, GPoint(x0, y0), GPoint(w, y0));
  }
}

static void handle_init(void) {
  s_main_window = window_create();
  
  Layer *window_layer = window_get_root_layer(s_main_window);
  GRect bounds = layer_get_frame(window_layer);

  s_battery_layer = text_layer_create(GRect(0, 0, 144, 20));
  window_stack_push(s_main_window, true);
  
  s_graph_layer = layer_create(grect_crop(GRect(0, 0, bounds.size.w, bounds.size.h), 10));
  layer_set_update_proc(s_graph_layer, update_graph_layer);
  
  s_battery_layer = text_layer_create(GRect(0, 120, bounds.size.w, 34));
  // text_layer_set_text_color(s_battery_layer, GColorWhite);
  // text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentCenter);
  text_layer_set_text(s_battery_layer, "100% charged");
  
  layer_add_child(window_layer, s_graph_layer);
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));
  
  battery_state_service_subscribe(handle_battery);
  
  wakeup_service_subscribe(handle_wakeup);
  
  if (s_wakeup_id >= 0 && wakeup_query(s_wakeup_id, NULL)) {
    wakeup_cancel(s_wakeup_id);
  }
  schedule_wakeup_measure_battery_state();
  
  BatteryChargeState charge_state = battery_state_service_peek();
  show_charge_state(&charge_state);
}

static void handle_deinit(void) {
  text_layer_destroy(s_battery_layer);
  layer_destroy(s_graph_layer);
  window_destroy(s_main_window);
}

int main(void) {
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "launch wakeup");
    schedule_wakeup_measure_battery_state();
    show_charge_log();
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "launch other");
    handle_init();
    app_event_loop();
    handle_deinit();
  }
}