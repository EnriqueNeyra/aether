# Copilot Instructions for Aether

## Project Summary

- Aether is an ESPHome firmware project for an ESP32-C3 air quality monitor.
- Core runtime configuration is in `firmware/aether.yaml`.
- Custom component code lives in:
  - `firmware/components/aether_ui/` (web UI handler + API + update actions)
  - `firmware/components/aether_display/aether_display.h` (e-paper rendering)

## Architecture Snapshot

- Data source: SEN66 (`sen6x`) sensor over I2C (0x6B), with metrics exposed as ESPHome sensor IDs.
- Display path: YAML `interval` lambda calls `aether::aether_display::tick_and_draw(...)` every 5s.
- Web path: `aether_ui` custom component serves local dashboard + `/api/state`.
- OTA path: HTTP update entity (`update.http_request`) wired to firmware manifest URL.
- Boot/network behavior: AP fallback is intentionally time-limited; offline mode behavior is deliberate.

## Primary Change Goals

- Prefer small, safe, incremental edits.
- Preserve existing behavior unless the task explicitly asks for behavior changes.
- Keep IDs, schema keys, and YAML/C++/Python wiring stable and consistent.

## Cross-File Consistency Rules

- Sensor IDs declared in `firmware/aether.yaml` must exactly match IDs consumed by:
  - `aether_ui:` mapping in `firmware/aether.yaml`
  - display lambda arguments in `interval` block
  - C++ setters/fields in `aether_ui` component
- When adding a new metric, update all relevant touchpoints together:
  - sensor declaration in `firmware/aether.yaml`
  - `aether_ui:` mapping in `firmware/aether.yaml`
  - schema and `to_code` wiring in `firmware/components/aether_ui/__init__.py`
  - render/cache logic in `firmware/components/aether_ui/aether_ui.h` and/or `firmware/components/aether_display/aether_display.h`

## ESPHome and YAML Conventions

- Do not remove or restructure `external_components` wiring unless explicitly requested.
- Preserve comments and intent around:
  - boot priority and delayed AP disable flow
  - OTA/update source configuration
  - network fallback behavior
- Keep `on_boot` sequencing robust; avoid edits that break offline operation.

## C++ Component Conventions

- Keep existing namespaces and class structure (`namespace aether { ... }`) intact.
- Follow current style and avoid unrelated refactors.
- Treat embedded constraints as first-class:
  - avoid unnecessary dynamic allocation
  - keep code lightweight and deterministic
- Temperature handling rule:
  - store/cache temperatures internally in Celsius
  - convert to Fahrenheit only at presentation/render time when enabled

## Python Codegen Conventions (`aether_ui/__init__.py`)

- Keep `CONFIG_SCHEMA` and `to_code` strictly aligned with C++ API.
- For each added config field, implement all three:
  - schema validation entry
  - variable fetch in `to_code`
  - matching C++ setter call
- Keep required `AUTO_LOAD` modules intact (`web_server_base`, `update`).

## Web UI Conventions

- Keep UI lightweight and responsive on mobile and desktop.
- Prefer targeted CSS/JS updates over full rewrites.
- Preserve tab behavior, state polling flow, and OTA trigger flow unless requested.
- Keep unit-toggle behavior consistent with display/backend temperature-unit state.

## Validation Checklist (Before Finalizing)

- Verify no ID mismatches across YAML, Python, and C++.
- Verify added/renamed fields are wired end-to-end.
- If available, run config validation:
  - `esphome config firmware/aether.yaml`
- If rendering, units, or OTA behavior changes, document exactly what changed and why.

## Non-Goals

- No broad architecture rewrites unless explicitly requested.
- Do not rename stable entities/IDs without clear migration reason.
- Do not alter OTA source URLs or update strategy unless explicitly requested.

## Maintenance Rule

- Keep this document accurate as the project evolves.
- When foundational architecture, dependencies, or wiring patterns change, update this file.
