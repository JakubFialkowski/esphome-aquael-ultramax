#include "aquael_ultramax.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdio>

namespace esphome {
namespace aquael_ultramax {

static const char *const TAG = "aquael_ultramax";

static const char *const MODE_OPTIONS[] = {"continuous", "square", "sinus", "discontinuous"};

espbt::ESPBTUUID AquaelUltramax::make_uuid_(uint16_t svc_hi, uint16_t chr_lo) {
  char buf[37];
  snprintf(buf, sizeof(buf), "%04x%04x-98b5-11ed-a8fc-0242ac120002", svc_hi, chr_lo);
  return espbt::ESPBTUUID::from_raw(std::string(buf));
}

// Percent convention matches the Aquael app: percent = raw / FLOW_MAX * 100.
// The pump floor is FLOW_MIN (2048), so the usable range is 50..100%.
uint16_t AquaelUltramax::percent_to_raw(float pct) {
  if (pct < 50) pct = 50;    // app slider floor; raw cannot go below FLOW_MIN
  if (pct > 100) pct = 100;
  uint16_t raw = (uint16_t) lroundf((pct / 100.0f) * FLOW_MAX);
  if (raw < FLOW_MIN) raw = FLOW_MIN;
  if (raw > FLOW_MAX) raw = FLOW_MAX;
  return raw;
}
float AquaelUltramax::raw_to_percent(uint16_t raw) {
  if (raw > FLOW_MAX) raw = FLOW_MAX;
  return (float) raw * 100.0f / FLOW_MAX;
}

uint16_t AquaelUltramax::wave_sec_to_raw(float sec) {
  if (sec < WAVE_SEC_MIN) sec = WAVE_SEC_MIN;
  if (sec > WAVE_SEC_MAX) sec = WAVE_SEC_MAX;
  return (uint16_t) lroundf(WAVE_A * sec + WAVE_B);
}
float AquaelUltramax::wave_raw_to_sec(uint16_t raw) {
  float sec = ((float) raw - WAVE_B) / WAVE_A;
  if (sec < WAVE_SEC_MIN) sec = WAVE_SEC_MIN;
  if (sec > WAVE_SEC_MAX) sec = WAVE_SEC_MAX;
  return sec;
}
void AquaelUltramax::write_wave_seconds(float sec) {
  uint16_t raw = wave_sec_to_raw(sec);
  uint8_t p[2] = {(uint8_t)(raw), (uint8_t)(raw >> 8)};
  write_raw_(CHR_PUMP_WAVE, p, 2);
  ESP_LOGI(TAG, "wave period set to %.0f s (raw 0x%04x)", sec, raw);
}

std::string AquaelUltramax::mode_name_(uint32_t mode) {
  return mode < 4 ? MODE_OPTIONS[mode] : "unknown";
}
std::string AquaelUltramax::secs_to_hhmmss_(uint32_t secs) {
  secs %= 86400;
  char buf[9];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(secs / 3600),
           (unsigned)((secs % 3600) / 60), (unsigned)(secs % 60));
  return std::string(buf);
}

uint16_t AquaelUltramax::handle_for_(uint32_t key) {
  auto it = handles_.find(key);
  return it == handles_.end() ? 0 : it->second;
}

void AquaelUltramax::resolve_handles_() {
  static const uint32_t keys[] = {
      CHR_PUMP_FILTRATION, CHR_PUMP_MODE, CHR_PUMP_DAY_HIGH, CHR_PUMP_DAY_LOW,
      CHR_PUMP_NIGHT_HIGH, CHR_PUMP_NIGHT_LOW, CHR_PUMP_WAVE, CHR_DN_ONOFF,
      CHR_DN_SUNRISE, CHR_DN_SUNSET, CHR_DN_RAMP, CHR_CLOCK,
      CHR_STATUS, CHR_FEED_ONOFF};
  handles_.clear();
  bool all = true;
  for (uint32_t key : keys) {
    uint16_t svc_hi = key >> 16, chr_lo = key & 0xffff;
    auto *chr = this->parent()->get_characteristic(make_uuid_(svc_hi, 0x0000),
                                                   make_uuid_(svc_hi, chr_lo));
    if (chr == nullptr) {
      ESP_LOGW(TAG, "chr %04x/%04x missing", svc_hi, chr_lo);
      all = false;
      continue;
    }
    handles_[key] = chr->handle;
  }
  handles_ready_ = all;
  ESP_LOGI(TAG, "handles resolved: %d entries, ready=%d", (int) handles_.size(), all);
}

void AquaelUltramax::read_handle_(uint32_t key) {
  uint16_t h = handle_for_(key);
  if (h == 0) return;
  esp_ble_gattc_read_char(this->parent()->get_gattc_if(),
                          this->parent()->get_conn_id(), h, ESP_GATT_AUTH_REQ_NONE);
}

void AquaelUltramax::read_all_() {
  read_handle_(CHR_PUMP_FILTRATION);
  read_handle_(CHR_PUMP_MODE);
  read_handle_(CHR_PUMP_DAY_HIGH);
  read_handle_(CHR_PUMP_DAY_LOW);
  read_handle_(CHR_PUMP_NIGHT_HIGH);
  read_handle_(CHR_PUMP_NIGHT_LOW);
  read_handle_(CHR_PUMP_WAVE);
  read_handle_(CHR_DN_ONOFF);
  read_handle_(CHR_DN_SUNRISE);
  read_handle_(CHR_DN_SUNSET);
  read_handle_(CHR_DN_RAMP);
  read_handle_(CHR_CLOCK);
  read_handle_(CHR_FEED_ONOFF);
  read_handle_(CHR_STATUS);
}

void AquaelUltramax::write_raw_(uint32_t key, const uint8_t *data, uint16_t len) {
  uint16_t h = handle_for_(key);
  if (h == 0 || !handles_ready_) {
    ESP_LOGW(TAG, "write %08x skipped, not ready", (unsigned) key);
    return;
  }
  auto err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(),
                                      this->parent()->get_conn_id(), h, len,
                                      const_cast<uint8_t *>(data),
                                      ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "write %08x failed: %d", (unsigned) key, err);
  else
    ESP_LOGD(TAG, "write %08x ok (%u B)", (unsigned) key, len);
}

void AquaelUltramax::write_u8(uint32_t key, uint8_t v) {
  write_raw_(key, &v, 1);
}
void AquaelUltramax::write_secs(uint32_t key, uint32_t secs) {
  uint8_t p[4] = {(uint8_t)(secs), (uint8_t)(secs >> 8), (uint8_t)(secs >> 16),
                  (uint8_t)(secs >> 24)};
  write_raw_(key, p, 4);
}
void AquaelUltramax::write_mode(uint32_t mode) {
  uint8_t p[4] = {(uint8_t)(mode), (uint8_t)(mode >> 8), (uint8_t)(mode >> 16),
                  (uint8_t)(mode >> 24)};
  write_raw_(CHR_PUMP_MODE, p, 4);
  ESP_LOGI(TAG, "mode set to %s", mode_name_(mode).c_str());
}
void AquaelUltramax::write_level_percent(uint32_t key, float pct) {
  uint16_t raw = percent_to_raw(pct);
  uint8_t p[2] = {(uint8_t)(raw), (uint8_t)(raw >> 8)};
  write_raw_(key, p, 2);
  ESP_LOGI(TAG, "level %08x set to %.0f%% (raw 0x%04x)", (unsigned) key, pct, raw);
}

void AquaelUltramax::subscribe_status_() {
  uint16_t h = handle_for_(CHR_STATUS);
  if (h == 0) return;
  auto status = esp_ble_gattc_register_for_notify(
      this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), h);
  if (status != ESP_OK) {
    ESP_LOGW(TAG, "register_for_notify failed: %d", status);
    return;
  }
  ESP_LOGI(TAG, "subscribed to status NOTIFY (handle 0x%04x)", h);
}

void AquaelUltramax::handle_status_(uint8_t code) {
  // 0xA0 = ok/day, 0xA1 = night active, 0xAE = night inactive; others unknown.
  const char *name;
  switch (code) {
    case 0xA0: name = "ok"; break;
    case 0xA1: name = "night_active"; break;
    case 0xAE: name = "night_inactive"; break;
    default:   name = "unknown"; break;
  }
  ESP_LOGI(TAG, "status notify: 0x%02X (%s)", code, name);
  if (status_sensor_) status_sensor_->publish_state(name);
  // treat any code we don't recognise as a potential fault flag
  if (pump_fault_bsensor_)
    pump_fault_bsensor_->publish_state(!(code == 0xA0 || code == 0xA1 || code == 0xAE));
}

void AquaelUltramax::sync_clock_() {
  if (rtc_ == nullptr) return;
  auto now = rtc_->now();
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "time not valid, skip clock sync");
    return;
  }
  uint32_t secs = now.hour * 3600UL + now.minute * 60UL + now.second;
  write_secs(CHR_CLOCK, secs);
  ESP_LOGI(TAG, "clock synced to %s", secs_to_hhmmss_(secs).c_str());
}

void AquaelUltramax::on_read_(uint16_t handle, const uint8_t *data, uint16_t len) {
  // reverse-lookup key from handle
  uint32_t key = 0;
  for (auto &kv : handles_)
    if (kv.second == handle) { key = kv.first; break; }
  if (key == 0) return;

  // raw hex dump for diagnosing the level mapping
  char hex[48] = {0};
  for (uint16_t i = 0; i < len && i < 12; i++)
    snprintf(hex + i * 3, 4, "%02X ", data[i]);
  ESP_LOGI(TAG, "read %08x (%04x/%04x) len=%u: %s", (unsigned) key,
           (unsigned)(key >> 16), (unsigned)(key & 0xffff), len, hex);

  auto u16 = [&]() -> uint16_t { return len >= 2 ? (data[0] | (data[1] << 8)) : 0; };
  auto u32 = [&]() -> uint32_t {
    return len >= 4 ? (data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24)) : 0;
  };

  switch (key) {
    case CHR_PUMP_FILTRATION:
      if (filtration_bsensor_) filtration_bsensor_->publish_state(data[0] != 0);
      if (filtration_switch_) filtration_switch_->publish_state(data[0] != 0);
      break;
    case CHR_PUMP_MODE: {
      uint32_t m = u32();
      if (mode_sensor_) mode_sensor_->publish_state(mode_name_(m));
      if (mode_select_ && m < 4) mode_select_->publish_state(MODE_OPTIONS[m]);
      break;
    }
    case CHR_PUMP_DAY_HIGH: {
      float pct = roundf(raw_to_percent(u16()));
      if (flow_percent_sensor_) flow_percent_sensor_->publish_state(pct);
      auto it = level_numbers_.find(key);
      if (it != level_numbers_.end()) it->second->publish_state(pct);
      break;
    }
    case CHR_PUMP_DAY_LOW:
    case CHR_PUMP_NIGHT_HIGH:
    case CHR_PUMP_NIGHT_LOW: {
      float pct = roundf(raw_to_percent(u16()));
      auto it = level_numbers_.find(key);
      if (it != level_numbers_.end()) it->second->publish_state(pct);
      break;
    }
    case CHR_DN_ONOFF:
      if (dn_switch_) dn_switch_->publish_state(data[0] != 0);
      break;
    case CHR_DN_SUNRISE:
    case CHR_DN_SUNSET: {
      uint32_t secs = u32() % 86400;
      auto it = time_entities_.find(key);
      if (it != time_entities_.end())
        it->second->publish_from_secs(secs);
      break;
    }
    case CHR_CLOCK:
      if (clock_sensor_) clock_sensor_->publish_state(secs_to_hhmmss_(u32()));
      break;
    case CHR_PUMP_WAVE: {
      uint16_t raw = u16();
      float sec = roundf(wave_raw_to_sec(raw));
      ESP_LOGI(TAG, "wave period raw=%u (0x%04x) -> %.0f s", (unsigned) raw,
               (unsigned) raw, sec);
      if (wave_number_) wave_number_->publish_state(sec);
      break;
    }
    case CHR_FEED_ONOFF:
      if (feeding_switch_) feeding_switch_->publish_state(data[0] != 0);
      break;
    case CHR_STATUS:
      this->handle_status_(data[0]);
      break;
    default:
      break;
  }
}

void AquaelUltramax::gattc_event_handler(esp_gattc_cb_event_t event,
                                         esp_gatt_if_t gattc_if,
                                         esp_ble_gattc_cb_param_t *param) {
  ESP_LOGD(TAG, "gattc event %d node_state=%d", (int) event, (int) this->node_state);
  switch (event) {
    case ESP_GATTC_SEARCH_CMPL_EVT:
      ESP_LOGI(TAG, "SEARCH_CMPL, resolving");
      this->resolve_handles_();
      if (handles_ready_) {
        this->sync_clock_();
        this->read_all_();
        this->subscribe_status_();
      }
      break;
    case ESP_GATTC_READ_CHAR_EVT:
      if (param->read.status == ESP_GATT_OK)
        this->on_read_(param->read.handle, param->read.value, param->read.value_len);
      break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      // enable notifications: write 0x0001 to the CCCD of the status char
      if (param->reg_for_notify.status != ESP_GATT_OK) break;
      uint16_t chr_h = handle_for_(CHR_STATUS);
      auto *chr = this->parent()->get_characteristic(make_uuid_(0x19b0, 0x0000),
                                                     make_uuid_(0x19b0, 0x0003));
      if (chr == nullptr) break;
      auto *descr = chr->get_descriptor(espbt::ESPBTUUID::from_uint16(0x2902));
      if (descr == nullptr) { ESP_LOGW(TAG, "status CCCD not found"); break; }
      uint16_t notify_en = 1;
      esp_ble_gattc_write_char_descr(
          this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
          descr->handle, sizeof(notify_en), (uint8_t *) &notify_en,
          ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
      ESP_LOGI(TAG, "status CCCD enabled (chr 0x%04x)", chr_h);
      break;
    }
    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == handle_for_(CHR_STATUS) && param->notify.value_len >= 1)
        this->handle_status_(param->notify.value[0]);
      break;
    case ESP_GATTC_DISCONNECT_EVT:
      handles_ready_ = false;
      break;
    default:
      break;
  }
}

void AquaelUltramax::loop() {
  static uint32_t last_try = 0;
  if (handles_ready_) return;
  uint32_t now = millis();
  if (now - last_try < 2000) return;
  last_try = now;
  this->resolve_handles_();
  if (handles_ready_) { this->sync_clock_(); this->read_all_(); }
}

void AquaelUltramax::refresh() {
  if (handles_ready_) this->read_all_();
}

void AquaelUltramax::dump_config() {
  ESP_LOGCONFIG(TAG, "Aquael Ultramax BT bridge (stage 2a)");
  ESP_LOGCONFIG(TAG, "  flow scale: %u..%u (0..100%%)", FLOW_MIN, FLOW_MAX);
}

// ---- entity subclass method bodies -----------------------------------------
void AquaelLevelNumber::control(float value) {
  this->publish_state(value);
  this->parent_->write_level_percent(key_, value);
}
void AquaelModeSelect::control(const std::string &value) {
  for (uint32_t i = 0; i < 4; i++) {
    if (value == MODE_OPTIONS[i]) {
      this->publish_state(value);
      this->parent_->write_mode(i);
      return;
    }
  }
}
void AquaelFiltrationSwitch::write_state(bool state) {
  this->publish_state(state);
  this->parent_->write_u8(CHR_PUMP_FILTRATION, state ? 1 : 0);
}
void AquaelDNSwitch::write_state(bool state) {
  this->publish_state(state);
  this->parent_->write_u8(CHR_DN_ONOFF, state ? 1 : 0);
}
void AquaelFeedingSwitch::write_state(bool state) {
  this->publish_state(state);
  this->parent_->write_u8(CHR_FEED_ONOFF, state ? 1 : 0);
}
void AquaelWaveNumber::control(float value) {
  this->publish_state(value);
  this->parent_->write_wave_seconds(value);
}
void AquaelTime::control(const datetime::TimeCall &call) {
  uint8_t h = call.get_hour().value_or(this->hour);
  uint8_t m = call.get_minute().value_or(this->minute);
  uint8_t s = call.get_second().value_or(0);
  uint32_t secs = h * 3600UL + m * 60UL + s;
  // publish directly (NOT make_call().perform(), which re-enters control -> stack overflow)
  this->publish_from_secs(secs);
  this->parent_->write_secs(key_, secs);
}
void AquaelRefreshButton::press_action() { this->parent_->refresh(); }

}  // namespace aquael_ultramax
}  // namespace esphome