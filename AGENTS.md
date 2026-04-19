## Project Summary

Aether is an ESPHome firmware project for an ESP32-C3 air quality monitor with a 3.7" e-paper display and a local web dashboard. The device reads air quality metrics from a Sensirion SEN66 sensor and presents them on both the e-paper screen and a browser-based UI served from the device itself.

## Repository Layout

```
firmware/
├── aether.yaml                          # Core ESPHome config (pins, sensors, components, intervals)
├── .gitignore                           # Ignores .esphome/ and secrets.yaml
├── components/
│   ├── aether_epaper/
│   │   ├── aether_epaper.h             # E-paper driver, display modes, button handlers
│   │   ├── aether_epaper_layout.h      # Shared layout rendering (sketch layout, drawing primitives)
│   │   └── fonts/                       # GFX font headers (.h) and source TTFs
│   └── aether_web_ui/
│       ├── __init__.py                  # ESPHome codegen (CONFIG_SCHEMA + to_code)
│       ├── aether_web_ui.h              # Web UI component (AetherWebUI class, API handlers)
│       ├── aether_web_ui_html.h         # Auto-generated: inlined HTML/CSS/JS (do not hand-edit)
│       └── web/
│           ├── index.html               # Web UI HTML source
│           ├── style.css                # Web UI CSS source
│           └── app.js                   # Web UI JavaScript source
ota-manifest.json                        # OTA update manifest (version + binary URL)
```

## Hardware & Pin Map

- **MCU:** ESP32-C3 (`esp32-c3-devkitm-1`), Arduino framework
- **Sensor:** Sensirion SEN66 via I2C (SDA=10, SCL=0, address 0x6B)
- **Display:** GDEY037T03 3.7" e-paper (416×240, black/white) via SPI
  - SPI pins: MOSI=7, SCK=6, CS=5, DC=4, RST=3, BUSY=1
- **Boot button:** GPIO 9 (INPUT_PULLUP, active low) — short press toggles info screen, long press triggers factory reset flow
- **Libraries** (PlatformIO): GxEPD2 ≥1.6.4, Adafruit GFX ≥1.12.3, Adafruit BusIO ≥1.15.0, QRCode ≥0.0.1

## Architecture & Data Flow

### Sensor → State

The SEN66 sensor is declared in `aether.yaml` with a 10-second update interval. It produces nine metric IDs:

| Sensor ID    | Metric        | Unit      |
|-------------|---------------|-----------|
| `co2`       | CO₂           | ppm       |
| `temp`      | Temperature   | °C        |
| `rh`        | Humidity      | %RH       |
| `pm1_0`     | PM1.0         | µg/m³     |
| `pm2_5`     | PM2.5         | µg/m³     |
| `pm4_0`     | PM4.0         | µg/m³     |
| `pm10_0`    | PM10          | µg/m³     |
| `voc_index` | VOC Index     | (index)   |
| `nox_index` | NOx Index     | (index)   |

### Display Path

A YAML `interval` block calls `aether::aether_epaper::tick_and_draw(...)` every **500ms**, passing all nine sensor values plus the temperature unit flag (`!id(temp_unit_c_switch).state` — true = Fahrenheit).

`aether_epaper.h` manages four display modes:
- **MODE_BOOT** — animated "Syntropy // Labs" wordmark with slash animation; transitions to normal when all metrics are valid.
- **MODE_NORMAL** — main air quality dashboard rendered by `aether_epaper_layout::render_sketch_layout()`.
- **MODE_INFO** — device information screen with QR codes. Shows hostname/IP/dashboard link when WiFi is connected; shows setup AP name and reboot instructions when disconnected.
- **MODE_RESET** — factory reset confirmation screen.

### Web UI Path

`aether_web_ui` is a custom ESPHome component that registers an `AsyncWebHandler` on port 80. It serves:
- `GET /` — the full dashboard UI (single-page app inlined from `web/` sources)
- `GET /api/state` — JSON payload with all metrics, firmware version, temp unit, and OTA status
- `POST /api/perform_update` — triggers OTA firmware update
- `POST /api/temp_unit?unit=C|F` — toggles temperature display unit

The web dashboard has two tabs: **Environment** (live sensor grid, polls `/api/state` every 5s) and **Firmware & Updates** (version info, one-click OTA).

### Web UI Build Pipeline

`__init__.py` runs `_generate_html_header()` at codegen time. This reads `web/index.html`, `web/style.css`, and `web/app.js`, inlines CSS/JS into the HTML, and writes the result to `aether_web_ui_html.h` as a C++ raw string literal (`INDEX_HTML`). **Do not hand-edit `aether_web_ui_html.h`** — edit the source files in `web/` instead.

### Temperature Unit Preference

Temperature unit is managed via an ESPHome template switch (`temp_unit_c_switch`) with `restore_mode: RESTORE_DEFAULT_OFF`:
- Switch ON = Celsius, Switch OFF = Fahrenheit (default)
- The switch state persists across reboots via ESPHome's built-in restore mechanism
- The display reads the switch state via the interval lambda: `!id(temp_unit_c_switch).state`
- The web API reads it in `handle_state_()` and toggles it via `handle_temp_unit_()`
- Temperatures are always stored/cached in Celsius internally; Fahrenheit conversion happens only at render time

### OTA Update Path

- **ESPHome native OTA** (`ota: platform: esphome`) for local development
- **HTTP OTA** (`update: platform: http_request`) checks `ota-manifest.json` every 24h
  - Manifest URL: `https://enriqueneyra.github.io/Aether/ota-manifest.json`
  - Binary URL: `https://github.com/enriqueneyra/Aether/releases/latest/download/aether.bin`
- Web UI triggers updates via `POST /api/perform_update` → `fw_update_->perform(false)`

### Network & Boot Behavior

- WiFi AP activates on boot with a 1-minute timeout for captive portal setup
- `on_boot` (priority -100): after 10 minutes, if WiFi is still not connected, AP is disabled and WiFi is fully turned off to enter **Offline Mode**
- `api.reboot_timeout: 0s` and `wifi.reboot_timeout: 0s` prevent unwanted reboots when disconnected
- `captive_portal`, `improv_serial`, and `esp32_improv` are enabled for initial device setup
- mDNS advertises `_http._tcp` on port 80

### Button Interactions

The boot button (GPIO 9) supports two gestures via `on_multi_click`:
- **Short press** (50ms–1s): calls `aether_epaper::on_short_press()` — toggles between MODE_NORMAL and MODE_INFO
- **Long press** (≥3s): calls `aether_epaper::on_long_press()`:
  - From MODE_NORMAL → enters MODE_RESET (shows confirmation screen)
  - From MODE_RESET → returns `true`, triggering `aether_factory_reset` button press
  - From MODE_INFO (disconnected) → triggers `safe_reboot()`
  - From MODE_BOOT → ignored

## Sensor ID Wiring Map

Sensor IDs must match exactly across these four touchpoints:

| YAML sensor ID | `aether_web_ui:` key | `__init__.py` conf key | C++ setter          | Display lambda arg |
|---------------|-------------------|----------------------|--------------------|--------------------|
| `co2`         | `co2`             | `CONF_CO2`           | `set_co2()`        | `id(co2).state`    |
| `pm2_5`       | `pm25`            | `CONF_PM25`          | `set_pm25()`       | `id(pm2_5).state`  |
| `temp`        | `temp`            | `CONF_TEMP`          | `set_temp()`       | `id(temp).state`   |
| `rh`          | `rh`              | `CONF_RH`            | `set_rh()`         | `id(rh).state`     |
| `pm1_0`       | `pm1`             | `CONF_PM1`           | `set_pm1()`        | `id(pm1_0).state`  |
| `pm4_0`       | `pm4`             | `CONF_PM4`           | `set_pm4()`        | `id(pm4_0).state`  |
| `pm10_0`      | `pm10`            | `CONF_PM10`          | `set_pm10()`       | `id(pm10_0).state` |
| `voc_index`   | `voc`             | `CONF_VOC`           | `set_voc()`        | `id(voc_index).state` |
| `nox_index`   | `nox`             | `CONF_NOX`           | `set_nox()`        | `id(nox_index).state` |

Additional non-sensor wirings:
- `aether_fw_update` → `fw_update` → `CONF_FW_UPDATE` → `set_fw_update()`
- `temp_unit_c_switch` → `temp_unit_switch` → `CONF_TEMP_UNIT_SWITCH` → `set_temp_unit_switch()`

## Web API JSON Schema (`/api/state`)

```json
{
  "co2": 450.0,
  "temp": 22.50,
  "rh": 45.00,
  "pm1": 3.0,
  "pm25": 5.5,
  "pm4": 6.0,
  "pm10": 8.0,
  "voc": 120.0,
  "nox": 15.0,
  "fw_version": "1.0.0",
  "temp_unit": "F",
  "has_update": false,
  "latest_version": "1.0.0",
  "update_state": "no_update",
  "update_progress": 0.00
}
```

`update_state` values: `"no_update"`, `"available"`, `"installing"`, `"unknown"`

## Font System

Fonts are pre-converted Adafruit GFX font headers in `firmware/components/aether_epaper/fonts/`. Each `.h` file is declared in `esphome.includes` in `aether.yaml` and referenced via `extern const GFXfont` in `aether_epaper_layout.h`.

Active fonts used in code:
- `Inter_Bold9pt7b` — small labels, secondary text
- `Inter_Bold12pt7b` — info screen text, meta rows
- `Inter_Bold18pt7b` — CO₂ fallback, info footer ("S//L")
- `Inter_Bold_Tabular18pt7b` — metric values (monospaced digits)
- `Inter_Bold_Tabular24pt7b` — large CO₂ value (monospaced digits)
- `Satoshi_800_Device_Information_Subset18pt7b` — "Device Information" title
- `Satoshi_800_Factory_Reset_Subset22pt7b` — "Factory Reset" title
- `Satoshi_800_Logo_Subset22pt7b` — boot wordmark ("Syntropy // Labs")

When adding a new font: create the `.h` header with `#pragma once`, add it to `esphome.includes` in `aether.yaml`, and `#include` it in `aether_epaper.h`.

## Display Layout Details (aether_epaper_layout.h)

The layout file is templated on `Display` type so it can be used both by the firmware (GxEPD2) and by other rendering contexts. Key structures and functions:

- **`Metrics` struct** — holds all nine sensor values (temp always in °C)
- **`render_sketch_layout()`** — main normal-mode layout: four corner PM sections, center CO₂ with ppm label, temp/humidity readings, VOC/NOx index sections, decorative corner braces and dividers
- **`render_disconnected_info_layout()`** — disconnected info screen with QR code and setup instructions
- **Alert thresholds** (caution diamond icon shown when exceeded): PM1 >15, PM2.5 >15, PM4 >30, PM10 >50, CO₂ >1000, VOC >250, NOx >100
- **Drawing primitives**: `print_left/centered/right()`, `draw_stroked_line()`, `draw_stroked_quadratic()`, `draw_divider()`, `draw_corner_brace()`, `draw_caution_icon()`, `format_metric()`

## Adding a New Sensor Metric

Update all touchpoints together:

1. `firmware/aether.yaml` — add sensor declaration under the `sen6x` platform with a unique `id` and `name`
2. `firmware/aether.yaml` — add the ID mapping under `aether_web_ui:`
3. `firmware/aether.yaml` — add `id(new_sensor).state` to the `interval` lambda's `tick_and_draw()` call
4. `firmware/components/aether_web_ui/__init__.py` — add `CONF_*` constant, add to `CONFIG_SCHEMA`, add to the `to_code` wiring loop
5. `firmware/components/aether_web_ui/aether_web_ui.h` — add `set_*()` setter, private `Sensor*` field, include in `handle_state_()` JSON output
6. `firmware/components/aether_epaper/aether_epaper.h` — add cached `g_*` variable, update `tick_and_draw()` signature, update `all_metrics_ready()`
7. `firmware/components/aether_epaper/aether_epaper_layout.h` — add field to `Metrics` struct, add rendering in `render_sketch_layout()`
8. `firmware/components/aether_web_ui/web/app.js` — add to the `items` array in `renderMetrics()`

## Adding a New Persisted Preference

1. `firmware/aether.yaml` — add a `switch: platform: template` with `restore_mode` set appropriately
2. `firmware/aether.yaml` — add the switch ID to `aether_web_ui:` mapping if the web UI needs access
3. `firmware/components/aether_web_ui/__init__.py` — add config key, schema entry, and `to_code` wiring
4. `firmware/components/aether_web_ui/aether_web_ui.h` — add setter, field, API handler if needed
5. Pass to display lambda if the display needs to read it

## ESPHome External Components

```yaml
external_components:
  - source: { type: local, path: components }
    components: [aether_web_ui]
  - source: { type: git, url: https://github.com/esphome/esphome, ref: dev }
    components: [sen6x, sensirion_common]
```

- `aether_web_ui` is loaded from the local `components/` directory
- `sen6x` and `sensirion_common` are pulled from ESPHome's dev branch (SEN66 support is not yet in stable)
- Do not remove or restructure this wiring unless explicitly requested

## Build & Validation

```bash
# Validate YAML config (fast, no compilation)
esphome config firmware/aether.yaml

# Full firmware compilation
esphome compile firmware/aether.yaml
```

## C++ Conventions

- All custom code lives under `namespace aether { ... }`
- `aether_epaper` uses free functions in a nested namespace (not a class)
- `aether_web_ui` uses the `AetherWebUI` class extending `esphome::Component` and `AsyncWebHandler`
- `aether_epaper_layout` uses templated free functions for display-type independence
- Avoid unnecessary dynamic allocation; keep code lightweight for embedded
- Use `inline` for all functions in header-only components
- Feed the watchdog (`esphome::App.feed_wdt()`) during long paged rendering loops

## Python Codegen Conventions (`__init__.py`)

- `AUTO_LOAD = ["web_server_base", "update"]` — keep these intact
- `CONFIG_SCHEMA` and `to_code` must stay strictly aligned with the C++ `AetherWebUI` API
- For each config field: schema validation entry + variable fetch in `to_code` + matching C++ setter call
- `_generate_html_header()` runs at import time before codegen to inline web sources into `aether_web_ui_html.h`

## Web UI Conventions

- Source files live in `web/` (HTML, CSS, JS); they are inlined into `aether_web_ui_html.h` at compile time
- Dark theme, responsive (mobile + desktop), lightweight single-page app
- Polls `/api/state` every 5 seconds
- Temperature unit toggle sends `POST /api/temp_unit?unit=C|F` and refreshes state
- OTA update button sends `POST /api/perform_update`
- Do not hand-edit `aether_web_ui_html.h`

## Primary Change Goals

- Prefer small, safe, incremental edits
- Preserve existing behavior unless the task explicitly asks for behavior changes
- Keep IDs, schema keys, and YAML/C++/Python wiring stable and consistent

## Non-Goals

- No broad architecture rewrites unless explicitly requested
- Do not rename stable entity IDs without clear migration reason
- Do not alter OTA source URLs or update strategy unless explicitly requested

## Maintenance Rule

Keep this document accurate as the project evolves. When foundational architecture, dependencies, or wiring patterns change, update this file.
