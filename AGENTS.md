## Project Summary

Aether is a full air-quality monitor project built around an ESP32-C3, a Sensirion SEN66, a 3.7" GDEY037T03 e-paper display, a local browser UI, a USB (Web Serial) flashing page, and the supporting PCB/enclosure design files. Most agent work should focus on `firmware/`, `manifests/`, and `www/`; `hardware/` and `enclosure/` are reference/design assets and should usually only receive small, targeted documentation or file-organization updates.

## Repository Layout

```text
AGENTS.md                                  # This repo-wide guide for coding agents
README.md                                  # User-facing project overview
LICENSE                                    # Per-directory license scope + CC BY-NC-SA 4.0 text
display-compatibility.md                   # Notes on alternative e-paper panels/ICs
.github/workflows/
├── build.yml                              # PR + weekly compile against latest ESPHome
├── release.yml                            # Firmware release on firmware/ pushes to main
└── pages.yml                              # Deploys www/ + manifests + release binaries
docs/images/                               # README photos (resized for the web; ~1400 px wide)
enclosure/                                 # SolidWorks enclosure + assembly files
firmware/
├── LICENSE                                # GPLv3 (covers everything under firmware/)
├── aether.yaml                            # Main (release) ESPHome config
├── aether-dev.yaml                        # Release config + ESPHome network OTA, for development
├── components/
│   ├── aether_epaper/
│   │   ├── aether_epaper.h                # Display driver, render loop, mode state
│   │   ├── aether_epaper_layout.h         # Shared layout + drawing primitives
│   │   └── fonts/                         # Adafruit GFX font headers, Inter TTFs, font licenses (README.md)
│   ├── aether_homekit/
│   │   ├── __init__.py                    # ESPHome codegen + HomeSpan lib pin
│   │   ├── aether_homekit.h               # HomeSpan-free public interface (pimpl)
│   │   └── aether_homekit.cpp             # The only TU that includes HomeSpan.h
│   └── aether_web_ui/
│       ├── __init__.py                    # ESPHome codegen + HTML inlining
│       ├── aether_web_ui.h                # AsyncWebHandler + JSON/API endpoints
│       ├── aether_web_ui_html.h           # Generated at build time (gitignored); do not edit
│       └── web/                           # Source HTML/CSS/JS for the device UI
manifests/
├── manifest.json                          # Factory flashing manifest
└── ota-manifest.json                      # OTA manifest consumed by firmware updates
www/
├── index.html                             # Combined Quick Start Guide & USB (Web Serial) flasher
├── style.css                              # Site styling
├── app.js                                 # Scroll animations, tab logic, Web Serial check
├── assets/                                # Site images
└── vendor/                                # Self-hosted ESP Web Tools and Inter font (with licenses)
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
- Wi-Fi / captive portal / `improv_serial` (USB) setup - BLE `improv_ble` is deliberately absent, see the flash budget below
- HTTP OTA update support (no ESPHome network OTA in release builds; see OTA / Update Path)
- the native API with keyless encryption (`api: encryption:` with no key; a client such as Home Assistant provisions one)
- the persisted temperature unit preference
- the ESP32 build toolchain, selected by the `toolchain` substitution (see ESPHome Version Policy below)

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

Display behavior worth knowing before changing it:

- The main dashboard only refreshes when what it would draw changes, based on `format_dashboard_key()`, which formats values exactly as drawn plus alert flags. It also does a full refresh every 24 h to clear ghosting. Keep `format_dashboard_key()` in sync with `render_sketch_layout()`, and use the shared alert thresholds and `display_temperature()`.
- GxEPD2's BUSY wait (up to 10 s per phase) runs a busy callback that feeds the watchdog. A refresh slower than 8 s marks the panel faulted. Rendering then stops blocking the main loop and retries every 5 minutes, so a missing or broken panel can't boot-loop the device (issue #5). Each refresh logs its duration at DEBUG level (`Display refresh took N ms`). On a working GDEY037T03 a partial refresh takes about 0.4-0.5 s and a full one about 2.3 s.
- `MODE_RESET` cancels itself back to `MODE_NORMAL` after 30 s.

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

NVS (Wi-Fi credentials, settings and HomeKit pairings) sits at the end of flash in ESPHome's stock layout, past the firmware image, so flashing over USB (`esphome run`, `firmware.factory.bin`, or the flasher's Update) keeps it. Only a full erase or a factory reset clears it.

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

- `GET /` or `/index.html` - the inlined device dashboard (yielded to the captive portal's Wi-Fi form while the setup AP's portal is active)
- `GET /api/state` - JSON state for metrics, firmware version, temp unit, update status, and (when built with HomeKit) a `homekit` object of `{enabled, paired, code, payload}`
- `POST /api/perform_update` - starts the HTTP update flow
- `POST /api/check_update` - re-checks the OTA manifest
- `POST /api/temp_unit?unit=C|F` - changes the temperature unit select

Requests are served on the ESP-IDF httpd task, not the main loop:

- Handlers never change entities directly. Selects, update installs and checks go through `this->defer(...)`, as ESPHome's own `web_server` does.
- `/api/state` reads update and unit status from a snapshot that `loop()` refreshes under a mutex. Sensor floats are read directly.
- `/api/*` requests with an `Origin` that doesn't match `Host` get `400` (`forbidden_origin`), the same rule ESPHome's `web_server` uses. Errors use 400 rather than 403/405 because ESPHome's ESP32 server sends any status other than 200, 204, 400, 401, 404, 409 or 422 as 500.
- `POST /api/perform_update` returns `409` when no update is available, and `/api/state` includes `install_failed` after an install drops back to AVAILABLE.
- Paths are read with `url_to()` into a stack buffer; `AsyncWebServerRequest::url()` was removed in ESPHome 2026.9.0.
- The component declares `DEPENDENCIES = ["web_server"]`, because `web_server` is what starts the HTTP listener.
 The browser app in `firmware/components/aether_web_ui/web/` polls `/api/state` every **5 seconds** and loads no external resources (no web fonts or CDNs), so it works on a LAN without internet.

### Web UI Build Pipeline

`firmware/components/aether_web_ui/__init__.py` runs `_generate_html_header()` at import/codegen time:

1. Reads `web/index.html`, `web/style.css`, and `web/app.js`
2. Inlines CSS and JS into the HTML
3. Writes `aether_web_ui_html.h` as a raw string literal

Always edit the `web/` source files, never the generated header.

### Flash budget (this is tight - measure before adding anything)

Measured with ESPHome 2026.10.1 (PlatformIO toolchain) on ESP32-C3, against ESPHome's stock 4 MB layout (1,835,008-byte app slots):

| Build | Flash | Notes |
| ----- | ----- | ----- |
| `main` 1.0.15, with `improv_ble` | 1,710,282 (93.2%) | |
| HomeKit, no `improv_ble` | 1,743,424 (95.0%) | this branch |

HomeKit (HomeSpan plus the promoted arduino WiFi/Ethernet/OTA/mDNS libraries) and `improv_ble`'s Bluedroid stack each cost roughly 450 KB, so they don't fit together. Onboarding is unaffected in practice: the USB flashing page provisions over `improv_serial`, and the captive portal AP still works. **Do not re-add `improv_ble` without removing something else of similar size.**

Keep ESPHome's stock partition layout. OTA updates can't change a partition table, and a USB update that does moves NVS, which wipes Wi-Fi credentials and settings.

### OTA / Update Path

The release firmware (`aether.yaml`) installs updates only through `update: platform: http_request`, from the official manifest over verified HTTPS, or over USB.

- There is deliberately no `ota: platform: esphome` in `aether.yaml`. It would let anyone on the LAN flash firmware, and a password would be readable in the public binary.
- `firmware/aether-dev.yaml` includes `aether.yaml` as a package and adds `ota: platform: esphome` for development flashing over Wi-Fi. Never release it.
- `web_server: ota: false` keeps `POST /update` closed. The captive portal still accepts uploads while the setup AP is active, as ESPHome's recovery path.

Current OTA source in firmware:

- Manifest URL: `https://aether.syntropylabs.io/ota-manifest.json`
- Binary URL inside the manifest: `https://aether.syntropylabs.io/aether-ota.bin`, a GitHub Pages mirror of that version's GitHub Release asset (copied by `pages.yml`, which checks it against the manifest MD5)
- `http_request: verify_ssl: true` - the TLS certificate is checked against ESP-IDF's common CA bundle. GitHub Pages serves a Let's Encrypt chain that is cross-signed by ISRG Root X1, which is in that bundle. Releases pick up newer bundles, so keep shipping them regularly.

The web UI triggers installation through `fw_update_->perform(false)`.

### Network and Boot Behavior

- The device starts with a temporary AP for captive-portal onboarding.
- After 10 minutes, if Wi-Fi is still unconfigured, firmware disables Wi-Fi and enters offline mode.
- `api.reboot_timeout: 0s` and `wifi.reboot_timeout: 0s` avoid reboot loops while disconnected.
- `web_server` runs on port 80 with local access enabled, `ota: false`, and without `include_internal`.

### Button Behavior

The boot button supports two gestures via `on_multi_click`:

- Short press (50ms-1s): toggle normal/info screen
- Long press (>=3s): enter reset flow, confirm reset, or reboot from disconnected info mode depending on the current display mode. An unconfirmed reset screen times out after 30 s.

The button has a 10 ms `delayed_on_off` debounce filter.

## Web and Manifests

`www/` contains the combined Quick Start Guide and USB flashing flow. It uses ESP Web Tools over Web Serial, self-hosted from `www/vendor/` at a pinned version, and loads no third-party scripts. Fontshare's CSS for the Satoshi logo font is the only external resource.
`manifests/` contains the JSON definitions used by both the flasher and firmware updates:

- `manifest.json` points to the factory image (`aether-factory.bin`) for ESP Web Tools. Its `name` must stay equal to the ESPHome `project.name` (`syntropylabs.aether`): Improv reports that name, and ESP Web Tools uses the match to offer a settings-preserving update instead of an erase-and-install.
- `ota-manifest.json` points to the OTA image (`aether-ota.bin`) and its MD5. It is for the firmware's `http_request` update only; it has no `parts`, so ESP Web Tools cannot use it.

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

To try the native ESP-IDF toolchain without editing the YAML:

```bash
esphome -s toolchain esp-idf compile aether.yaml
```

To flash a development build over Wi-Fi, use the dev config. The device must already run a dev build; release builds only accept USB or the official update:

```bash
esphome run aether-dev.yaml --device <device-ip>
```

Build output lands in `.esphome/build/aether/.pioenvs/aether/` (platformio) or `.esphome/build/aether/build/` (esp-idf); `release.yml` searches for `firmware.factory.bin` / `firmware.ota.bin` in either.

### ESPHome Version Policy

This project tracks the **latest ESPHome release**. `release.yml` installs the newest ESPHome on every release, so a new ESPHome version can break `main` with no repo change.

- `build.yml` compiles every PR, and runs weekly, against the latest stable ESPHome. Its "Release config (ESPHome stable)" job builds exactly what `release.yml` builds; make it the required status check on `main`. Two early-warning jobs report failures as annotations without failing the check: "esp-idf toolchain (ESPHome stable)" and "Release config (ESPHome beta)". The beta job pins the newest pre-release of ESPHome only, not of its dependencies.
- GitHub disables scheduled workflows after 60 days without repository activity. If the weekly run stops, re-enable it in the Actions tab.
- Treat ESPHome deprecation warnings as must-fix. They name the release that removes the feature, and `build.yml` surfaces them as annotations and in the job summary.
- Build locally with the newest ESPHome before changing firmware. The PlatformIO toolchain needs Python 3.10-3.14; ESPHome itself supports 3.12-3.14.
- Toolchain migration: ESPHome removes `toolchain: platformio` in 2027.2.0. On ESPHome 2026.10.0 the esp-idf toolchain fails: Adafruit GFX's `library.properties` dependency on Adafruit BusIO isn't resolved. The fix (esphome/esphome#21317) is milestoned for 2026.10.1, and this project builds with esp-idf on ESPHome `dev` with that fix. Once the esp-idf CI job is green on a stable release, set the `toolchain` substitution default in `aether.yaml` to `esp-idf`, and drop `platformio_options.lib_ldf_mode`; the native toolchain ignores it and warns. The gating job then builds esp-idf automatically. Point the early-warning job at `platformio`, or drop it.

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
- Pushes to `main` that touch `firmware/` cut a release that is offered to every device over OTA. For firmware changes that should not ship yet, put `[skip release]` in the pushed commit's message. When merging a PR, that means the merge commit's subject or the PR title, since GitHub puts the PR title in the merge commit. Changes to `www/` and `manifests/` only redeploy the site.
- `release.yml` rebases its version-bump commit onto `main` if `main` moved during the build, and tags the commit that was pushed.

## Licensing

- `firmware/` is GPLv3 (`firmware/LICENSE`) because the firmware is built on GPLv3 code (ESPHome's C++ runtime, GxEPD2). Anything added to the firmware build must be GPLv3-compatible (e.g. MIT, BSD, Apache-2.0, LGPL, GPLv3). Never add non-commercial or proprietary code to it.
- Everything else (hardware, enclosure, `www/`, manifests, docs) is CC BY-NC-SA 4.0; see the root `LICENSE`.
- Third-party assets keep their own licenses: the Inter (SIL OFL 1.1) and Satoshi (Indian Type Foundry, Fontshare terms) fonts in `firmware/components/aether_epaper/fonts/` (see its README), and ESP Web Tools (Apache-2.0) and Inter in `www/vendor/`. Don't commit Satoshi font files; only the generated logo-subset header is in the repo.

## Maintenance Rule

Keep this file, as well as any other markdown documentation, in sync with the actual repo. Update it when repository structure, firmware wiring, or OTA/flash workflows materially change.

README images live in `docs/images/` and are linked by relative path. Resize photos before adding them: about 1400 px wide for images shown at 700 px, 1600 px for the top banner, as progressive sRGB JPEGs of roughly 50-100 KB each. Full-size camera photos (3-4 MB each) make the README slow to load. The same applies to `www/assets/`: the site's device photo is an 880 px WebP (about 60 KB), since the page shows it at most 440 px wide.
