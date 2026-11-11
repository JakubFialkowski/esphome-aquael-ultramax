#pragma once

#include "esphome/core/component.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include "esphome/components/datetime/time_entity.h"

#include <esp_gattc_api.h>
#include <map>

namespace esphome {
namespace aquael_ultramax {

namespace espbt = esphome::esp32_ble_tracker;

// --- Reverse-engineered protocol --------------------------------------------
// Service family: 19bX-98b5-11ed-a8fc-0242ac120002
// Flow level: uint16 LE, linear, 0%=2048, 100%=4096.
// Mode: uint32 LE enum 0..3.
// All times (sunrise/sunset/clock) = seconds-from-midnight uint32 LE; ramp too.
static const uint16_t FLOW_MAX = 4096;
static const uint16_t FLOW_MIN = 2048;

// Wave period: its OWN small scale, raw = 0.9*sec + 46 (confirmed from
// 30s->73, 40s->82). raw 50..100 maps to 4..60 seconds.
static const float WAVE_A = 0.9f;    // raw per second
static const float WAVE_B = 46.0f;   // offset
static const float WAVE_SEC_MIN = 4.0f;
static const float WAVE_SEC_MAX = 60.0f;

// Characteristic keys: (service_hi << 16) | char_lo
enum ChrKey : uint32_t {
  CHR_PUMP_FILTRATION = (0x19b1u << 16) | 0x0001,  // uint8 on/off
  CHR_PUMP_MODE       = (0x19b1u << 16) | 0x0002,  // uint32 enum
  CHR_PUMP_DAY_HIGH   = (0x19b1u << 16) | 0x0003,  // uint16
  CHR_PUMP_DAY_LOW    = (0x19b1u << 16) | 0x0004,  // uint16
  CHR_PUMP_NIGHT_HIGH = (0x19b1u << 16) | 0x0005,  // uint16 level
  CHR_PUMP_NIGHT_LOW  = (0x19b1u << 16) | 0x0006,  // uint16 level (was mis-mapped to 0007)
  CHR_PUMP_WAVE       = (0x19b1u << 16) | 0x0007,  // wave period, raw = 0.9*sec + 46
  CHR_DN_ONOFF        = (0x19b2u << 16) | 0x0001,  // uint8 on/off
  CHR_DN_SUNRISE      = (0x19b2u << 16) | 0x0002,  // uint32 sec-from-midnight
  CHR_DN_SUNSET       = (0x19b2u << 16) | 0x0003,  // uint32 sec-from-midnight
  CHR_DN_RAMP         = (0x19b2u << 16) | 0x0004,  // uint16 seconds
  CHR_CLOCK           = (0x19b0u << 16) | 0x0001,  // uint32 sec-from-midnight
  CHR_STATUS          = (0x19b0u << 16) | 0x0003,  // NOTIFY status code
  CHR_FEED_ONOFF      = (0x19b3u << 16) | 0x0001,  // uint8 feeding on/off
};

class AquaelUltramax;

// ---- entity subclasses ------------------------------------------------------
class AquaelLevelNumber : public number::Number, public Parented<AquaelUltramax> {
 public:
  void set_key(uint32_t key) { key_ = key; }
  void control(float value) override;
 protected:
  uint32_t key_{0};
};

class AquaelModeSelect : public select::Select, public Parented<AquaelUltramax> {
 public:
  void control(const std::string &value) override;
};

class AquaelWaveNumber : public number::Number, public Parented<AquaelUltramax> {
 public:
  void control(float value) override;
};

class AquaelFiltrationSwitch : public switch_::Switch, public Parented<AquaelUltramax> {
 public:
  void write_state(bool state) override;
};

class AquaelDNSwitch : public switch_::Switch, public Parented<AquaelUltramax> {
 public:
  void write_state(bool state) override;
};

class AquaelFeedingSwitch : public switch_::Switch, public Parented<AquaelUltramax> {
 public:
  void write_state(bool state) override;
};

class AquaelTime : public datetime::TimeEntity, public Parented<AquaelUltramax> {
 public:
  void set_key(uint32_t key) { key_ = key; }
  void control(const datetime::TimeCall &call) override;
  // publish a state read back from the device, without re-entering control()
  void publish_from_secs(uint32_t secs) {
    secs %= 86400;
    this->hour_ = secs / 3600;
    this->minute_ = (secs % 3600) / 60;
    this->second_ = secs % 60;
    this->publish_state();
  }
 protected:
  uint32_t key_{0};
};

class AquaelRefreshButton : public button::Button, public Parented<AquaelUltramax> {
 public:
  void press_action() override;
};

// ---- hub --------------------------------------------------------------------
class AquaelUltramax : public Component, public ble_client::BLEClientNode {
 public:
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_time(time::RealTimeClock *rtc) { rtc_ = rtc; }
  // sensors
  void set_flow_percent_sensor(sensor::Sensor *s) { flow_percent_sensor_ = s; }
  void set_mode_sensor(text_sensor::TextSensor *s) { mode_sensor_ = s; }
  void set_clock_sensor(text_sensor::TextSensor *s) { clock_sensor_ = s; }
  void set_filtration_bsensor(binary_sensor::BinarySensor *s) { filtration_bsensor_ = s; }
  void set_status_sensor(text_sensor::TextSensor *s) { status_sensor_ = s; }
  void set_pump_fault_bsensor(binary_sensor::BinarySensor *s) { pump_fault_bsensor_ = s; }
  // controls
  void set_mode_select(AquaelModeSelect *s) { mode_select_ = s; }
  void set_filtration_switch(AquaelFiltrationSwitch *s) { filtration_switch_ = s; }
  void set_dn_switch(AquaelDNSwitch *s) { dn_switch_ = s; }
  void set_feeding_switch(AquaelFeedingSwitch *s) { feeding_switch_ = s; }
  void set_wave_number(AquaelWaveNumber *n) { wave_number_ = n; }
  void register_level_number(uint32_t key, AquaelLevelNumber *n) { level_numbers_[key] = n; }
  void register_time_entity(uint32_t key, AquaelTime *t) { time_entities_[key] = t; }

  void write_wave_seconds(float sec);
  static uint16_t wave_sec_to_raw(float sec);
  static float wave_raw_to_sec(uint16_t raw);

  // generic writers used by entity subclasses
  void write_level_percent(uint32_t key, float pct);
  void write_mode(uint32_t mode);
  void write_u8(uint32_t key, uint8_t v);
  void write_secs(uint32_t key, uint32_t secs);
  void refresh();

  static uint16_t percent_to_raw(float pct);
  static float raw_to_percent(uint16_t raw);

 protected:
  time::RealTimeClock *rtc_{nullptr};
  sensor::Sensor *flow_percent_sensor_{nullptr};
  text_sensor::TextSensor *mode_sensor_{nullptr};
  text_sensor::TextSensor *clock_sensor_{nullptr};
  binary_sensor::BinarySensor *filtration_bsensor_{nullptr};
  text_sensor::TextSensor *status_sensor_{nullptr};
  binary_sensor::BinarySensor *pump_fault_bsensor_{nullptr};
  AquaelModeSelect *mode_select_{nullptr};
  AquaelFiltrationSwitch *filtration_switch_{nullptr};
  AquaelDNSwitch *dn_switch_{nullptr};
  AquaelFeedingSwitch *feeding_switch_{nullptr};
  AquaelWaveNumber *wave_number_{nullptr};
  std::map<uint32_t, AquaelLevelNumber *> level_numbers_;
  std::map<uint32_t, AquaelTime *> time_entities_;

  std::map<uint32_t, uint16_t> handles_;  // ChrKey -> GATT handle
  bool handles_ready_{false};

  void resolve_handles_();
  void sync_clock_();
  void read_all_();
  void read_handle_(uint32_t key);
  void write_raw_(uint32_t key, const uint8_t *data, uint16_t len);
  void subscribe_status_();
  void handle_status_(uint8_t code);

  uint16_t handle_for_(uint32_t key);
  void on_read_(uint16_t handle, const uint8_t *data, uint16_t len);

  static espbt::ESPBTUUID make_uuid_(uint16_t svc_hi, uint16_t chr_lo);
  static std::string mode_name_(uint32_t mode);
  static std::string secs_to_hhmmss_(uint32_t secs);
};

}  // namespace aquael_ultramax
}  // namespace esphome