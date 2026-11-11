# ESPHome Aquael Ultramax (BLE)

ESPHome external component for controlling Aquael Ultramax filters over Bluetooth LE.
Native Home Assistant entities — no cloud, no Aquael app.

## Features
- Flow % (day), mode, filtration on/off, day/night switching
- Day/night high/low flow, sunrise/sunset times
- Feeding mode, wave period
- Pump fault + status sensors, device clock sync from HA time

## Requirements
- ESP32 / ESP32-S3, ESP-IDF framework
- `esp32_ble_tracker`, `ble_client`, `time` (homeassistant)

## Installation
```yaml
external_components:
  - source: github://JakubFialkowski/esphome-aquael-ultramax@v0.1.0
    components: [aquael_ultramax]
```

## Configuration
See [example.yaml](example.yaml).

## Notes
- The filter accepts a single BLE connection — close the Aquael app first.
- Unofficial, reverse-engineered protocol. Not affiliated with Aquael. Use at your own risk.

## License
MIT
