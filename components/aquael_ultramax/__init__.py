"""ESPHome external component for the Aquael Ultramax BT filter — stage 2a.

Adds the CO2 daily profile (day/night service 19b2) and the full pump level
set (19b1), on top of the working first cut. Single-characteristic reads
(no SUM offset parsing). Entities:
  sensors:      flow % (day high), mode, device clock, filtration (binary)
  controls:     mode (select), filtration (switch), D&N (switch),
                day_high/day_low/night_high/night_low (number %),
                sunrise/sunset (datetime time), refresh (button)

Protocol facts (RE + confirmed live):
- services 19bX-98b5-11ed-a8fc-0242ac120002, no pairing
- level uint16 LE linear 2048..4096 (0..100%)
- mode uint32 LE enum 0..3
- sunrise/sunset/clock = seconds-from-midnight uint32 LE
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    ble_client, sensor, number, text_sensor, binary_sensor,
    button, switch, select, datetime, time,
)
from esphome.const import (
    CONF_ID, CONF_TIME_ID, CONF_MODE, UNIT_PERCENT,
    ENTITY_CATEGORY_CONFIG, ENTITY_CATEGORY_DIAGNOSTIC,
)

CODEOWNERS = ["@jakub"]
DEPENDENCIES = ["ble_client", "time"]
AUTO_LOAD = [
    "sensor", "number", "text_sensor", "binary_sensor",
    "button", "switch", "select", "datetime",
]

# config keys
CONF_FLOW_PERCENT = "flow_percent_day"
CONF_FILTRATION = "filtration"
CONF_CLOCK = "device_clock"
CONF_REFRESH = "refresh"
CONF_DAY_HIGH = "day_high"
CONF_DAY_LOW = "day_low"
CONF_NIGHT_HIGH = "night_high"
CONF_NIGHT_LOW = "night_low"
CONF_DN = "day_night"
CONF_SUNRISE = "sunrise"
CONF_SUNSET = "sunset"
CONF_STATUS = "status"
CONF_PUMP_FAULT = "pump_fault"
CONF_FEEDING = "feeding"
CONF_WAVE = "wave_period"

aquael_ns = cg.esphome_ns.namespace("aquael_ultramax")
AquaelUltramax = aquael_ns.class_("AquaelUltramax", cg.Component, ble_client.BLEClientNode)
AquaelLevelNumber = aquael_ns.class_("AquaelLevelNumber", number.Number)
AquaelModeSelect = aquael_ns.class_("AquaelModeSelect", select.Select)
AquaelFiltrationSwitch = aquael_ns.class_("AquaelFiltrationSwitch", switch.Switch)
AquaelDNSwitch = aquael_ns.class_("AquaelDNSwitch", switch.Switch)
AquaelFeedingSwitch = aquael_ns.class_("AquaelFeedingSwitch", switch.Switch)
AquaelWaveNumber = aquael_ns.class_("AquaelWaveNumber", number.Number)
AquaelTime = aquael_ns.class_("AquaelTime", datetime.TimeEntity)
AquaelRefreshButton = aquael_ns.class_("AquaelRefreshButton", button.Button)

MODE_OPTIONS = ["continuous", "square", "sinus", "discontinuous"]

# ChrKey values (must match the C++ enum)
KEY_DAY_HIGH = (0x19b1 << 16) | 0x0003
KEY_DAY_LOW = (0x19b1 << 16) | 0x0004
KEY_NIGHT_HIGH = (0x19b1 << 16) | 0x0005
KEY_NIGHT_LOW = (0x19b1 << 16) | 0x0007
KEY_SUNRISE = (0x19b2 << 16) | 0x0002
KEY_SUNSET = (0x19b2 << 16) | 0x0003


def _level_schema():
    return number.number_schema(
        AquaelLevelNumber, entity_category=ENTITY_CATEGORY_CONFIG,
        unit_of_measurement=UNIT_PERCENT,
    )


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AquaelUltramax),
            cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            # sensors
            cv.Optional(CONF_FLOW_PERCENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT, accuracy_decimals=0),
            cv.Optional(CONF_MODE): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
            cv.Optional(CONF_CLOCK): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
            cv.Optional(CONF_FILTRATION): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
            # controls
            cv.Optional("mode_select"): select.select_schema(
                AquaelModeSelect, entity_category=ENTITY_CATEGORY_CONFIG),
            cv.Optional("filtration_switch"): switch.switch_schema(
                AquaelFiltrationSwitch),
            cv.Optional(CONF_DN): switch.switch_schema(AquaelDNSwitch),
            cv.Optional(CONF_DAY_HIGH): _level_schema(),
            cv.Optional(CONF_DAY_LOW): _level_schema(),
            cv.Optional(CONF_NIGHT_HIGH): _level_schema(),
            cv.Optional(CONF_NIGHT_LOW): _level_schema(),
            cv.Optional(CONF_SUNRISE): datetime.time_schema(AquaelTime),
            cv.Optional(CONF_SUNSET): datetime.time_schema(AquaelTime),
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
            cv.Optional(CONF_PUMP_FAULT): binary_sensor.binary_sensor_schema(
                device_class="problem", entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
            cv.Optional(CONF_FEEDING): switch.switch_schema(AquaelFeedingSwitch),
            cv.Optional(CONF_WAVE): number.number_schema(
                AquaelWaveNumber, entity_category=ENTITY_CATEGORY_CONFIG,
                unit_of_measurement="s"),
            cv.Optional(CONF_REFRESH): button.button_schema(
                AquaelRefreshButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def _new_level(config, key, hub):
    # usable pump range is 50..100% (raw 2048..4096); app uses the same floor
    num = await number.new_number(config, min_value=50, max_value=100, step=1)
    await cg.register_parented(num, hub)
    cg.add(num.set_key(key))
    cg.add(hub.register_level_number(key, num))


async def _new_time(config, key, hub):
    t = await datetime.new_datetime(config)
    await cg.register_parented(t, hub)
    cg.add(t.set_key(key))
    cg.add(hub.register_time_entity(key, t))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    rtc = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(rtc))

    if CONF_FLOW_PERCENT in config:
        cg.add(var.set_flow_percent_sensor(await sensor.new_sensor(config[CONF_FLOW_PERCENT])))
    if CONF_MODE in config:
        cg.add(var.set_mode_sensor(await text_sensor.new_text_sensor(config[CONF_MODE])))
    if CONF_CLOCK in config:
        cg.add(var.set_clock_sensor(await text_sensor.new_text_sensor(config[CONF_CLOCK])))
    if CONF_FILTRATION in config:
        cg.add(var.set_filtration_bsensor(await binary_sensor.new_binary_sensor(config[CONF_FILTRATION])))

    if "mode_select" in config:
        sel = await select.new_select(config["mode_select"], options=MODE_OPTIONS)
        await cg.register_parented(sel, var)
        cg.add(var.set_mode_select(sel))
    if "filtration_switch" in config:
        sw = await switch.new_switch(config["filtration_switch"])
        await cg.register_parented(sw, var)
        cg.add(var.set_filtration_switch(sw))
    if CONF_DN in config:
        sw = await switch.new_switch(config[CONF_DN])
        await cg.register_parented(sw, var)
        cg.add(var.set_dn_switch(sw))

    if CONF_DAY_HIGH in config:
        await _new_level(config[CONF_DAY_HIGH], KEY_DAY_HIGH, var)
    if CONF_DAY_LOW in config:
        await _new_level(config[CONF_DAY_LOW], KEY_DAY_LOW, var)
    if CONF_NIGHT_HIGH in config:
        await _new_level(config[CONF_NIGHT_HIGH], KEY_NIGHT_HIGH, var)
    if CONF_NIGHT_LOW in config:
        await _new_level(config[CONF_NIGHT_LOW], KEY_NIGHT_LOW, var)

    if CONF_SUNRISE in config:
        await _new_time(config[CONF_SUNRISE], KEY_SUNRISE, var)
    if CONF_SUNSET in config:
        await _new_time(config[CONF_SUNSET], KEY_SUNSET, var)

    if CONF_STATUS in config:
        cg.add(var.set_status_sensor(await text_sensor.new_text_sensor(config[CONF_STATUS])))
    if CONF_PUMP_FAULT in config:
        cg.add(var.set_pump_fault_bsensor(await binary_sensor.new_binary_sensor(config[CONF_PUMP_FAULT])))
    if CONF_FEEDING in config:
        sw = await switch.new_switch(config[CONF_FEEDING])
        await cg.register_parented(sw, var)
        cg.add(var.set_feeding_switch(sw))
    if CONF_WAVE in config:
        num = await number.new_number(config[CONF_WAVE], min_value=4, max_value=60, step=1)
        await cg.register_parented(num, var)
        cg.add(var.set_wave_number(num))

    if CONF_REFRESH in config:
        btn = await button.new_button(config[CONF_REFRESH])
        await cg.register_parented(btn, var)