## Project Summary

Aether is a full air-quality monitor project built around an ESP32-C3, a Sensirion SEN66, a 3.7" GDEY037T03 e-paper display, a local browser UI, a WebUSB flashing page, and the supporting PCB/enclosure design files. Most agent work should focus on `firmware/` and `flash/`; `hardware/` and `enclosure/` are reference/design assets and should usually only receive small, targeted documentation or file-organization updates.

## Repository Layout

```text
AGENTS.md                                  # This repo-wide guide for coding agents
README.md                                  # User-facing project overview
enclosure/                                 # SolidWorks enclosure + assembly files
firmware/
├── aether.yaml                            # Main ESPHome config
├── components/
│   ├── aether_epaper/
│   │   ├── aether_epaper.h                # Display driver, render loop, mode state
│   │   ├── aether_epaper_layout.h         # Shared layout + drawing primitives
│   │   └── fonts/                         # Adafruit GFX font headers + TTF sources
│   └── aether_web_ui/
│       ├── __init__.py                    # ESPHome codegen + HTML inlining
│       ├── aether_web_ui.h                # AsyncWebHandler + JSON/API endpoints
│       ├── aether_web_ui_html.h           # Auto-generated; do not edit directly
│       └── web/                           # Source HTML/CSS/JS for the device UI
flash/
├── index.html                             # WebUSB / ESP Web Tools flashing page
├── manifest.json                          # Factory flashing manifest
└── ota-manifest.json                      # OTA manifest consumed by firmware updates
hardware/
├── pcb/                                   # KiCad project
├── manufacturing/                         # PCB zip, BOM, centroid exports
└── datasheets/                            # Reference component datasheets
```

## Agent Priorities

- Prioritize `firmware/` and `flash/`.
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
- the e-paper render loop
- Wi-Fi / captive portal / improv setup
- ESPHome OTA + HTTP OTA update support
- the persisted temperature unit preference

### Sensor Update and Render Flow

- The SEN66 platform updates every **5 seconds**.
- A YAML `interval` calls `aether::aether_epaper::tick_and_draw(...)` every **500ms**.
- Display and web UI both consume the same nine sensor values:

| YAML sensor ID | Metric | Web UI key |
| --- | --- | --- |
| `co2` | CO2 | `co2` |
| `temp` | Temperature | `temp` |
| `rh` | Humidity | `rh` |
| `pm1_0` | PM1.0 | `pm1` |
| `pm2_5` | PM2.5 | `pm25` |
| `pm4_0` | PM4.0 | `pm4` |
| `pm10_0` | PM10 | `pm10` |
| `voc_index` | VOC Index | `voc` |
| `nox_index` | NOx Index | `nox` |

When adding or renaming a metric, keep YAML IDs, `aether_web_ui` config keys, Python codegen, C++ setters/fields, display cache state, and web JS rendering aligned.

### Display Path

`firmware/components/aether_epaper/aether_epaper.h` owns display state and rendering. It has four modes:

- `MODE_BOOT` - boot wordmark animation
- `MODE_NORMAL` - main dashboard
- `MODE_INFO` - device information / QR screen
- `MODE_RESET` - factory reset confirmation

The shared layout math lives in `aether_epaper_layout.h`.

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
- `GET /api/state` - JSON state for metrics, firmware version, temp unit, and update status
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

The firmware exposes two update mechanisms:

- `ota: platform: esphome` for normal ESPHome development flashing
- `update: platform: http_request` for release OTA checks and installation

Current OTA source in firmware:

- Manifest URL: `https://enriqueneyra.github.io/aether/flash/ota-manifest.json`
- Release binary URL inside the manifest: GitHub Releases `aether.bin`

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

## Flash Folder

`flash/` contains the browser-based flashing flow:

- `index.html` is the WebUSB / ESP Web Tools page used for factory flashing and recovery
- `manifest.json` points to the factory image (`aether-factory.bin`)
- `ota-manifest.json` points to the OTA image (`aether.bin`) and its MD5

If you change release artifact names or URLs, update both the firmware OTA source and the manifests in `flash/`.

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
- The UI is a small single-page app with two tabs: Environment and Firmware & Updates.
- The frontend expects the `/api/state` schema to stay stable unless the user explicitly wants an API change.

## Safe Change Rules

- Prefer small, wiring-consistent changes over broad rewrites.
- Preserve stable IDs, config keys, API keys, and route names unless migration is part of the task.
- When editing firmware features that span YAML, Python, C++, and browser JS, update every touchpoint together.
- When changing OTA behavior, keep `firmware/` and `flash/` aligned.

## Maintenance Rule

Keep this file in sync with the actual repo. Update it when repository structure, firmware wiring, or OTA/flash workflows materially change.
