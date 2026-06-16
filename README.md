# Aether

Aether is an air quality monitor by **Syntropy Labs** built around an ESP32-C3, a Sensirion SEN66 sensor, and a 3.7" e-paper display. It measures **CO2, temperature, humidity, PM1.0, PM2.5, PM4.0, PM10, VOC index, and NOx index**, and exposes those readings on-device, on the local network, and through Home Assistant.

This repository contains the full project source:

| Path                      | Contents                                                                   |
| ------------------------- | -------------------------------------------------------------------------- |
| `firmware/`               | ESPHome configuration, custom components, web UI sources, and display code |
| `hardware/pcb/`           | KiCad PCB project                                                          |
| `hardware/manufacturing/` | PCB manufacturing outputs, BOM, and centroid files                         |
| `hardware/datasheets/`    | Reference datasheets used during hardware design                           |
| `enclosure/`              | SolidWorks enclosure and assembly files                                    |
| `flash/`                  | WebUSB flashing page and firmware manifests                                |

## Features

- Measures CO2, Temperature, Humidity, PM1.0, PM2.5, PM4.0, PM10, VOC index, and NOx index using the **Sensirion SEN66**
- Displays live readings on a **416x240 3.7" e-paper panel**
- Serves a **local web dashboard** from the device itself
- Supports **ESPHome / Home Assistant** integration
- Supports **USB factory flashing**, **firmware recovery**, and **OTA updates**
- Can run **with or without Wi-Fi**

## Hardware

To build one Aether unit, this repository currently points to the following core parts:

| Part                                      | Link                                                      |
| ----------------------------------------- | --------------------------------------------------------- |
| **Aether PCB**                            | `hardware/pcb/`, `hardware/manufacturing/`                |
| **3.7" e-paper display module**           | TBD                                                       |
| **Sensirion SEN66** air quality sensor    | [Sensirion](https://sensirion.com/products/catalog/SEN66) |
| **3D-printed enclosure parts**            | TBD (also in `enclosure/`)                                |
| **Reverse 6-pin JST-GH cable**            | TBD                                                       |
| **2x M3 x 6 mm screws and matching nuts** | TBD                                                       |

> **TODO:** Add exact purchase links, approved part numbers, and a cleaned-up builder BOM for anyone sourcing parts manually.

## Assembly

High-level assembly flow:

1. Press-fit the SEN66 into the enclosure body.
2. Connect the e-paper display to the PCB with the FPC connector.
3. Press-fit the PCB into the enclosure body.
4. Connect 6-pin JST GH Cable
5. Bring display over in position above the PCB.
6. Attach the display cover.
7. Secure the assembly from the bottom with the two M3 fasteners.

> **TODO:** Add assembly photos for each step.

## Setup

### Flashing firmware onto a device

For a new board or a device that needs recovery:

1. Connect Aether to your computer over USB.
2. Open the [Aether WebUSB flashing tool](aether.syntropylabs.io/flash)
3. Choose **Factory flash (recommended)** for first-time setup or full recovery.
4. Keep the page open until flashing is complete.
5. Let the device boot.

For a device that is already set up and just needs newer firmware:

1. Connect the device over USB and open the same flashing page.
2. Choose **Update firmware (OTA)** if you want to preserve the current device settings.

The flashing page in `flash/` is built around **ESP Web Tools / WebUSB** and is intended to work best in **Chrome or Edge**.

### First-time network setup

On boot, Aether can expose a temporary setup access point so you can connect it to Wi-Fi through the captive portal flow.

- If you want **local web UI access**, **network-based updates**, or **Home Assistant integration**, connect the device to Wi-Fi during setup.
- If you do **not** configure Wi-Fi, the device is still intended to work as a standalone monitor in **offline mode**.

Once connected to Wi-Fi, the device also hosts its own local web UI on port 80.

> **TODO:** Add the exact captive portal steps and screenshots.
>
> **TODO:** Add the default setup AP naming example shown to end users during onboarding.

### Firmware development

If you are working on the ESPHome firmware:

```sh
git clone https://github.com/EnriqueNeyra/Aeroq.git
cd "Aeroq/Aeroq Firmware"
```

```sh
cd firmware
esphome config aether.yaml
esphome compile aether.yaml
```

## Using Aether

### Normal operation

Once powered, Aether reads the SEN66 and refreshes both the on-device display and the local web dashboard. Temperature can be shown in either **Fahrenheit** or **Celsius**.

### Wi-Fi behavior

- Aether can be set up on Wi-Fi through captive portal onboarding.
- If no Wi-Fi is configured, it can still operate as an offline monitor.
- If Wi-Fi is configured, the device can expose its local web dashboard and participate in network-based update and automation flows.

### Side button behavior

The side / boot button supports multiple actions:

- **Short press:** toggles between the normal dashboard and the info screen
- **Long press from the normal screen:** enters factory reset confirmation
- **Long press again on the reset screen:** confirms the factory reset flow

> **TODO:** Add a short visual or photo showing the physical button location.

### Local web UI

When Aether is on your network, it serves a local browser-based dashboard directly from the device. That UI includes:

- A live environment view for sensor readings
- Temperature unit switching
- Firmware version and update status
- A way to trigger firmware updates from the device UI

> **TODO:** Add screenshots of the web UI.
>
> **TODO:** Add the exact local hostname / URL pattern users should expect after setup.

### Home Assistant & ESPHome API

Because Aether runs ESPHome, it features a native API over Wi-Fi. This allows for:

- **Zero-configuration Home Assistant Integration:** Aether will automatically appear in Home Assistant via mDNS discovery once connected to your network. It securely exposes all 9 sensor metrics (CO2, Temp, PM2.5, etc.) without requiring an MQTT broker or cloud account.
- **Custom Integrations:** You can integrate Aether into your own software or scripts using its local HTTP API. Exposed endpoints include:
  - `GET /api/state` - Returns a JSON object with all current sensor metrics and device status.
  - `POST /api/temp_unit?unit=C|F` - Changes the preferred temperature unit.
  - `POST /api/perform_update` - Triggers the device to immediately begin downloading and installing the latest firmware update over HTTP (if one is available).

> **TODO:** Add the exact Home Assistant onboarding flow, discovery behavior, and the list of exposed entities once that user-facing flow is finalized.

### Firmware updates

Aether supports more than one update path:

- **Local USB flashing** through the WebUSB tool in `flash/`
- **In-device update flow** through the local web UI
- **HTTP-based update checks** when the device is online

Use the USB flashing page for first-time setup and recovery. Use the local web UI for routine updates on devices that are already deployed.

## Documentation TODOs

- Add final public flashing URL
- Add a curated BOM with sourcing links
- Add assembly photos
- Add enclosure print settings and build notes
- Add captive portal screenshots and onboarding copy
- Add web UI screenshots
- Add Home Assistant setup details and exposed entities

## License

This project (hardware, design files, and firmware) is licensed under the
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-nc-sa/4.0/).

[![License: CC BY-NC-SA 4.0](https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-nc-sa/4.0/)
