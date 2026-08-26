## Project Summary

Aether is a full air-quality monitor project built around an ESP32-C3, a Sensirion SEN66, a 3.7" GDEY037T03 e-paper display, a local browser UI, a WebUSB flashing page, and the supporting PCB/enclosure design files. Most agent work should focus on `firmware/`, `manifests/`, and `www/`; `hardware/` and `enclosure/` are reference/design assets and should usually only receive small, targeted documentation or file-organization updates.

## Repository Layout

```text
AGENTS.md                                  # This repo-wide guide for coding agents
README.md                                  # User-facing project overview
enclosure/                                 # SolidWorks enclosure + assembly files
firmware/
├── aether.yaml                            # Main ESPHome config
├── partitions_aether.csv                  # Custom 4 MB partition table (larger OTA slots)
├── components/
│   ├── aether_epaper/
│   │   ├── aether_epaper.h                # Display driver, render loop, mode state
│   │   ├── aether_epaper_layout.h         # Shared layout + drawing primitives
│   │   └── fonts/                         # Adafruit GFX font headers + TTF sources
│   ├── aether_homekit/
│   │   ├── __init__.py                    # ESPHome codegen + HomeSpan lib pin
│   │   ├── aether_homekit.h               # HomeSpan-free public interface (pimpl)
│   │   └── aether_homekit.cpp             # The only TU that includes HomeSpan.h
│   └── aether_web_ui/
│       ├── __init__.py                    # ESPHome codegen + HTML inlining
│       ├── aether_web_ui.h                # AsyncWebHandler + JSON/API endpoints
│       ├── aether_web_ui_html.h           # Auto-generated; do not edit directly
│       └── web/                           # Source HTML/CSS/JS for the device UI
manifests/
├── manifest.json                          # Factory flashing manifest
└── ota-manifest.json                      # OTA manifest consumed by firmware updates
www/
├── index.html                             # Combined Quick Start Guide & WebUSB flasher
├── style.css                              # Site styling
└── app.js                                 # Scroll animations and tab logic
hardware/
├── pcb/                                   # KiCad project
├── manufacturing/                         # PCB zip, BOM, centroid exports
└── datasheets/                            # Reference component datasheets
```

## Agent Priorities

- Prioritize `firmware/`, `www/`, and `manifests/`.
- Keep `hardware/` and `enclosure/` changes brief and surgical unless the user explicitly asks for CAD/PCB work.
- Do not hand-edit `firmware/components/aether_web_ui/aether_web_ui_html.h`; edit `web/index.html`, `web/style.css`, and `web/app.js` instead.

## Firmware Overview

### Core Hardware

- **MCU:** ESP32-C3 (`esp32-c3-devkitm-1`), Arduino framework
- **Sensor:** Sensirion SEN66 via I2C on SDA=10, SCL=0, address `0x6B`
- **Display:** GDEY037T03 416x240 black/white e-paper via SPI
  - MOSI=7, SCK=6, CS=5, DC=4, RST=3, BUSY=1
- **Boot button:** GPIO 9, `INPUT_PULLUP`, active low

### Main Firmware Entry Point

`firmware/aether.yaml` wires together:

- SEN66 sensor entities
- the custom `aether_web_ui` external component
- the custom `aether_homekit` external component
- the e-paper render loop
- Wi-Fi / captive portal / `improv_serial` (USB) setup - note BLE `esp32_improv` is deliberately absent, see the flash budget below
- ESPHome OTA + HTTP OTA update support
- the persisted temperature unit preference
- a custom partition table (`partitions_aether.csv`)

### Sensor Update and Render Flow

- The SEN66 platform updates every **5 seconds**.
- A YAML `interval` calls `aether::aether_epaper::tick_and_draw(...)` every **500ms**.
- Display and web UI both consume the same nine sensor values:

| YAML sensor ID | Metric      | Web UI key |
| -------------- | ----------- | ---------- |
| `co2`          | CO2         | `co2`      |
| `temp`         | Temperature | `temp`     |
| `rh`           | Humidity    | `rh`       |
| `pm1_0`        | PM1.0       | `pm1`      |
| `pm2_5`        | PM2.5       | `pm25`     |
| `pm4_0`        | PM4.0       | `pm4`      |
| `pm10_0`       | PM10        | `pm10`     |
| `voc_index`    | VOC Index   | `voc`      |
| `nox_index`    | NOx Index   | `nox`      |

When adding or renaming a metric, keep YAML IDs, `aether_web_ui` config keys, Python codegen, C++ setters/fields, display cache state, and web JS rendering aligned.

### Display Path

`firmware/components/aether_epaper/aether_epaper.h` owns display state and rendering. It has four modes:

- `MODE_BOOT` - boot wordmark animation
- `MODE_NORMAL` - main dashboard
- `MODE_INFO` - device information / QR screen
- `MODE_RESET` - factory reset confirmation

When `USE_AETHER_HOMEKIT` is defined there is a fifth mode, `MODE_HOMEKIT`, showing the HomeKit setup QR and pairing code. Short press cycles `NORMAL -> INFO -> HOMEKIT -> NORMAL`; `MODE_INFO` and `MODE_HOMEKIT` both ignore long presses so the reset flow can't be entered from them.

The shared layout math lives in `aether_epaper_layout.h`.

`aether_epaper.h` must never include the HomeKit component header. It receives pairing state as plain strings/bools via `set_homekit_state()`, called from the YAML render interval. See the HomeKit section below for why.

## HomeKit Path

`aether_homekit` embeds [HomeSpan](https://github.com/HomeSpan/HomeSpan) (pinned to 2.1.8) so the device speaks HAP natively - no bridge and no Home Assistant.

### Build plumbing (fragile - read before changing)

Two non-obvious things are required to make HomeSpan build inside ESPHome. Both live in `aether_homekit/__init__.py`.

**1. libsodium.** HomeSpan's `HAP.cpp` needs `sodium.h` for Ed25519/Curve25519/ChaCha20-Poly1305. ESP-IDF 5.5 dropped libsodium as a built-in component, so it is pulled from the Espressif registry via `add_idf_component(name="espressif/libsodium", ref="^1.0.20")`.

**2. arduino library include paths.** ESPHome builds arduino-esp32 as an IDF component, so the bundled arduino libraries that `HomeSpan.h` reaches (`WiFi.h`, `ETH.h`, `ArduinoOTA.h`, `ESPmDNS.h`, ...) get promoted to PlatformIO *project* libraries. PlatformIO then compiles each one without its siblings' include paths, producing a cascade of failures - `WiFi` cannot find `Network.h`, `Ethernet` cannot find `SPI.h`, and so on. `_ARDUINO_LIB_INCLUDES` puts those paths back with explicit `-I` flags. Without HomeSpan none of these libraries appear in the dependency graph at all, which is why the stock build never needed this.

### The pimpl rule

`aether_homekit.h` is kept completely free of HomeSpan types (they live in `struct Impl` in the `.cpp`), and `aether_homekit.cpp` is the only file allowed to `#include <HomeSpan.h>`. ESPHome pulls every component header into `esphome.h` and therefore into every translation unit; keeping HomeSpan's heavy include tree and its `LOG0`/`VERSION`/`REQUIRED` macros out of that path keeps compiles fast and avoids macro collisions. `aether_epaper.h` must likewise never include the HomeKit header - it takes pairing state as plain strings via `set_homekit_state()`.

### Coexistence with ESPHome

HomeSpan overrides arduino-esp32's weak `init()` hook, which runs *before* ESPHome's `setup()`. `Span::init()` calls `WiFi.mode(WIFI_STA)`, bringing up the default event loop, both default WiFi netifs and `esp_wifi`. ESPHome's `wifi_pre_setup_()` then gets `ESP_ERR_INVALID_STATE` from `esp_event_loop_create_default()` and **returns early**, so it never registers its event handlers, never creates its netifs and never calls `esp_wifi_init()`. The symptom is a device with no setup AP and no STA connection at all.

That ordering cannot be changed, so `reclaim_wifi_stack_()` undoes arduino's half of it (wifi → netifs → event loop, in that order) and hands ESPHome a clean stack. It must run before the WiFi component, which is why `get_setup_priority()` returns `WIFI + 1` rather than `AFTER_WIFI`.

A consequence: arduino's WiFi event translation layer is never registered, so HomeSpan would never see the `GOT_IP` that triggers `configureNetwork()` (mDNS advertising + HAP server). `bridge_wifi_state_()` posts ESPHome's connection state into arduino's event queue instead. It posts the **ETH** events, not the WiFi ones — they drive the identical path in `Span::networkCallback()` but read `ETH.localIP()` rather than `WiFi.localIP()`, whose netif pointer dangles after the teardown.

Beyond that, ESPHome owns WiFi, mDNS, the serial port, and port 80. The component defers to it:

- `setPortNum(1201)` - port 80 belongs to `web_server`
- `setSerialInputDisable(true)` + `setLogLevel(-1)` - the logger owns UART0
- `setWifiBegin(no-op)` - HomeSpan must never call `WiFi.begin()`; it still learns it is online from the arduino `GOT_IP` event and starts its HAP server from there
- `setHostNameSuffix("")` with `App.get_name()` - one mDNS identity for both stacks
- `autoPoll()` on its own task - pairing runs SRP-3072 and would otherwise starve the render loop and trip the watchdog
- writes from ESPHome's `loop()` are made under `homeSpanPAUSE`

### Reflashing during development

`firmware.factory.bin` written at `0x0` spans the NVS region, so it **erases stored Wi-Fi credentials and HomeKit pairings** every time. Once a device already has the current partition table, flash the app only and leave NVS alone:

```bash
# preserves Wi-Fi credentials and HomeKit pairing
esptool --chip esp32c3 --port <port> write-flash 0x10000 firmware.bin
```

Use the full factory image only for a first flash, a partition table change, or a deliberate wipe.

### Bridge topology (do not collapse back into one accessory)

The component publishes `Category::Bridges` with five accessories: the bridge itself, then one accessory each for temperature, humidity, CO2 and air quality. HomeSpan infers bridge mode from accessory 1 containing nothing but `AccessoryInformation`, so that accessory must stay empty of sensor services.

This was originally one accessory carrying four sensor services, which is what HAP nominally allows. The Home app collapses that into a single tile showing only the highest-priority state — the red CO2 alert — and hides every numeric reading. Splitting into bridged accessories is what makes each reading show as its own tile and become selectable in the Home widget.

Every bridged accessory needs its own `AccessoryInformation` service, defined *first* on that accessory (`add_accessory_info_()`), or HAP validation fails.

### Sensor mapping

| Metric | HAP characteristic |
| ------ | ------------------ |
| Temperature | `CurrentTemperature` (range widened to -40..100) |
| Humidity | `CurrentRelativeHumidity` |
| CO2 | `CarbonDioxideLevel` + `CarbonDioxideDetected` |
| PM2.5 | `PM25Density` |
| PM10 | `PM10Density` |
| VOC Index | `VOCDensity` (unit mismatch: index is unitless 1-500) |
| NOx Index | `NitrogenDioxideDensity` (same unit mismatch) |
| PM1.0, PM4.0 | **none - HAP defines no characteristic**; display/web/API only |

`AirQuality` (1-5) is derived on-device from the worst of PM2.5, VOC Index, and CO2.

### Pairing code

`setPairingCode()` regenerates SRP verification data every call, which costs seconds. The component hashes the configured code into ESPHome preferences and only re-applies it when it actually changes. Disallowed codes are rejected at config-validation time rather than at runtime, where HomeSpan would halt the program.

### Temperature Unit Preference

Temperature unit is currently a persisted **ESPHome template select**, not a switch:

- Entity ID: `temp_unit_select`
- Options: `"Fahrenheit"` and `"Celsius"`
- Default initial option: Fahrenheit
- `restore_value: true`

Internally, temperatures stay in Celsius. The display converts at render time, and the web UI toggles units through `POST /api/temp_unit?unit=C|F`.

### Web UI Path

`aether_web_ui` registers an `AsyncWebHandler` on the ESPHome web server and serves:

- `GET /` or `/index.html` - the inlined device dashboard
- `GET /api/state` - JSON state for metrics, firmware version, temp unit, update status, and (when built with HomeKit) a `homekit` object of `{enabled, paired, code, payload}`
- `POST /api/perform_update` - starts the HTTP update flow
- `POST /api/temp_unit?unit=C|F` - changes the temperature unit select

The browser app in `firmware/components/aether_web_ui/web/` polls `/api/state` every **5 seconds**.

### Web UI Build Pipeline

`firmware/components/aether_web_ui/__init__.py` runs `_generate_html_header()` at import/codegen time:

1. Reads `web/index.html`, `web/style.css`, and `web/app.js`
2. Inlines CSS and JS into the HTML
3. Writes `aether_web_ui_html.h` as a raw string literal

Always edit the `web/` source files, never the generated header.

### OTA / Update Path

### Partition table

`firmware/partitions_aether.csv` replaces ESPHome's stock 4 MB Arduino layout. The stock layout gives each OTA slot `0x1C0000` and leaves `0x60000` (384 KB) of flash unallocated; the custom table splits that dead space across `app0`/`app1`, taking each to `0x1F0000`. Without it the HomeKit build overflows the app partition.

### Flash budget (this is tight - measure before adding anything)

Measured with esphome 2026.2.4 on ESP32-C3:

| Build | Flash | App slot | Notes |
| ----- | ----- | -------- | ----- |
| Stock 1.0.12 | 1,758,516 | 1,835,008 (95.8%) | almost full already |
| + HomeKit, bigger partitions | 2,217,340 | 2,031,616 (109.1%) | **overflows** |
| + HomeKit, no `esp32_improv` | 1,785,294 | 2,031,616 (87.9%) | ships |

HomeKit costs ~448 KB (HomeSpan + libsodium + the promoted arduino WiFi/Ethernet/OTA/mDNS libraries). Paying for it meant dropping `esp32_improv`, whose Bluedroid BT stack was ~432 KB. Onboarding is unaffected in practice: the WebUSB flashing page provisions over USB via `improv_serial`, and the captive portal AP still works. **Do not re-add `esp32_improv` without removing something else of similar size.**

**A partition table change cannot be delivered over OTA.** Devices on 1.0.12 or earlier must be re-flashed once over USB with the factory image. Every release after that OTAs normally again. If you ever change partition sizes again, the same one-time USB re-flash applies.

### Update mechanisms

The firmware exposes two update mechanisms:

- `ota: platform: esphome` for normal ESPHome development flashing
- `update: platform: http_request` for release OTA checks and installation

Current OTA source in firmware:

- Manifest URL: `https://aether.syntropylabs.io/ota-manifest.json`
- Release binary URL inside the manifest: GitHub Releases `aether-ota.bin`

The web UI triggers installation through `fw_update_->perform(false)`.

### Network and Boot Behavior

- The device starts with a temporary AP for captive-portal onboarding.
- After 10 minutes, if Wi-Fi is still unconfigured, firmware disables Wi-Fi and enters offline mode.
- `api.reboot_timeout: 0s` and `wifi.reboot_timeout: 0s` avoid reboot loops while disconnected.
- `web_server` runs on port 80 with local access enabled.

### Button Behavior

The boot button supports two gestures via `on_multi_click`:

- Short press (50ms-1s): toggle normal/info screen
- Long press (>=3s): enter reset flow, confirm reset, or reboot from disconnected info mode depending on the current display mode

## Web and Manifests

`www/` contains the combined Quick Start Guide and WebUSB flashing flow.
`manifests/` contains the JSON definitions used by both the flasher and firmware updates:

- `manifest.json` points to the factory image (`aether-factory.bin`)
- `ota-manifest.json` points to the OTA image (`aether-ota.bin`) and its MD5

If you change release artifact names or URLs, update both the firmware OTA source and the manifests in `manifests/`.

## Hardware and Enclosure

Keep work here brief unless the user explicitly asks for PCB or CAD changes.

- `hardware/pcb/` contains the KiCad project (`Aether.kicad_pro`, `.sch`, `.kicad_pcb`)
- `hardware/manufacturing/` contains generated fabrication outputs, BOM, and centroid files
- `hardware/datasheets/` contains reference PDFs used during design
- `enclosure/` contains SolidWorks part and assembly files for the enclosure and display stack

Do not casually rename or reorganize CAD, KiCad, or manufacturing artifacts; these files are tooling-sensitive and often referenced outside the repo.

## Build and Validation

### Firmware

From the repo root:

```bash
esphome config firmware/aether.yaml
esphome compile firmware/aether.yaml
```

From inside `firmware/`:

```bash
esphome config aether.yaml
esphome compile aether.yaml
```

## Codebase Conventions

### C++ / Firmware

- Custom code lives under `namespace aether`.
- `aether_epaper` is header-only and organized around free functions/state in a nested namespace.
- `aether_web_ui` uses the `AetherWebUI` class extending `esphome::Component` and `AsyncWebHandler`.
- `aether_epaper_layout.h` is templated for display-type-independent layout rendering.
- Prefer lightweight/static patterns over heap-heavy abstractions.
- Feed the watchdog during paged display rendering loops.

### Python Codegen

- Keep `CONFIG_SCHEMA`, `to_code()`, and the C++ API in sync.
- `AUTO_LOAD = ["web_server_base", "update"]` is required and should remain intact.
- The current temperature-unit dependency is `select.Select`, not `switch.Switch`.

### Web UI

- Device UI sources live in `firmware/components/aether_web_ui/web/`.
- The UI is a small single-page app with three tabs: Environment, HomeKit, and Firmware & Updates.
- The frontend expects the `/api/state` schema to stay stable unless the user explicitly wants an API change.

## Safe Change Rules

- Prefer small, wiring-consistent changes over broad rewrites.
- Preserve stable IDs, config keys, API keys, and route names unless migration is part of the task.
- When editing firmware features that span YAML, Python, C++, and browser JS, update every touchpoint together.
- When changing OTA behavior, keep `firmware/` and `manifests/` aligned.

## Maintenance Rule

Keep this file, as well as any other markdown documentation, in sync with the actual repo. Update it when repository structure, firmware wiring, or OTA/flash workflows materially change.
