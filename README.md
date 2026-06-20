# Aether

<img width="4285" height="2857" alt="A6700222" src="https://github.com/user-attachments/assets/8be34c35-cbe4-4628-9b6f-72ee45ff123e" />

### Aether is a comprehensive air quality monitor by [**Syntropy Labs**](https://syntropylabs.io/products/aether-air-quality-monitor) built around an ESP32-C3, a Sensirion SEN66 sensor, and a 3.7" e-paper display. It measures **CO2, Temperature, Humidity, PM1.0, PM2.5, PM4.0, PM10, VOC index, and NOx index**, and exposes those readings on-device, on the local network, and through Home Assistant.

## Repo Guide

| Path                      | Contents                                                                   |
| ------------------------- | -------------------------------------------------------------------------- |
| `firmware/`               | ESPHome configuration, custom components, web UI sources, and display code |
| `hardware/pcb/`           | KiCad PCB project                                                          |
| `hardware/manufacturing/` | PCB manufacturing outputs, BOM, and centroid files                         |
| `hardware/datasheets/`    | Reference datasheets used during hardware design                           |
| `enclosure/`              | SolidWorks enclosure and assembly files                                    |
| `flash/`                  | WebUSB flashing page and firmware manifests                                |

## Contents

- [Features](#features)
- [Hardware](#hardware)
- [Assembly](#assembly)
- [Setup](#setup)
  - [Flashing firmware onto a device](#flashing-firmware-onto-a-device)
  - [First-time network setup](#first-time-network-setup)
  - [Firmware development](#firmware-development)
- [Using Aether](#using-aether)
  - [Normal operation](#normal-operation)
  - [Wi-Fi behavior](#wi-fi-behavior)
  - [Side button behavior](#side-button-behavior)
  - [Local web UI](#local-web-ui)
  - [Home Assistant & ESPHome API](#home-assistant--esphome-api)
  - [Firmware updates](#firmware-updates)
- [License](#license)

## Features

- Measures CO2, Temperature, Humidity, PM1.0, PM2.5, PM4.0, PM10, VOC index, and NOx index using the **Sensirion SEN66**
- Displays live readings on a **416x240 3.7" e-paper panel**
- Serves a **local web dashboard** from the device itself
- Supports **ESPHome / Home Assistant** integration
- Supports **USB factory flashing**, **firmware recovery**, and **OTA updates**
- Fully local, and can run **with or without Wi-Fi**

## Hardware

To build one Aether unit, the following core parts are needed:

| Part                                       | Link                                                                                                                                                                                                                                          |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Aether PCB**                             | [Syntropy Labs Store](https://syntropylabs.io/products/aether-air-quality-monitor-custom-pcb) (PCB files available in [`hardware/`](https://github.com/EnriqueNeyra/aether/tree/main/hardware/manufacturing) folder)                          |
| **3.7" e-paper display module**            | [Good Display](https://buy-lcd.com/products/37-inch-416x240-e-paper-black-and-white-spi-fast-refresh-electronic-eink-display-screen-esl-gdey037t03?VariantsId=10346)                                                                          |
| **Sensirion SEN66** air quality sensor     | [Sensirion](https://sensirion.com/products/catalog/SEN66)                                                                                                                                                                                     |
| **Reverse 6-pin JST-GH cable**             | [Adafruit](https://www.adafruit.com/product/5754?srsltid=AfmBOopcC6siE6KqjNqU-nj7AwIrnvlDSqnL19iwFrZ47O6lRIhqRYC8) / [AliExpress](https://www.aliexpress.us/w/wholesale-jst-gh-1.25mm-connector-6-pin-reverse.html?spm=a2g0o.detail.search.0) |
| **3D-printed enclosure parts**             | [`enclosure/`](https://github.com/EnriqueNeyra/aether/tree/main/enclosure)                                                                                                                                                                    |
| **2x M3 x 6 mm countersunk screws + nuts** | [Screws](https://amzn.to/3SKvOgg) / [Nuts](https://amzn.to/44jUIG1)                                                                                                                                                                           |

<p align="center"><img alt="A6700230" src="https://github.com/user-attachments/assets/9861c7f3-c20a-4694-845d-c945b032f5fe" width="700"></p>

## Assembly

High-level assembly flow:

### 1. Press-fit the SEN66 into the enclosure body.

<p align="center"><img alt="Insert_SEN66_into_Enclosure" src="https://github.com/user-attachments/assets/6f8cd045-af82-40ae-ae59-631ba9e61631" width="700"></p>

### 2. Connect the e-paper display to the PCB with the FPC connector.

<p align="center"><img alt="Connect_ePaper_to_PCB" src="https://github.com/user-attachments/assets/74e306dd-42e3-4feb-860c-e935da8fb220" width="700"></p>

### 3. Press-fit the PCB into the enclosure body.

<p align="center"><img alt="Insert_PCB_into_Enclosure" src="https://github.com/user-attachments/assets/752068ef-0f64-40fd-9949-34c260d7f2eb" width="700"></p>

### 4. Connect 6-pin JST GH Cable.

<p align="center"><img alt="Connect_JST_Cable" src="https://github.com/user-attachments/assets/cb314557-cf85-4ab1-a9ef-7a8a2115400f" width="700"></p>

### 5. Bring display over in position above the PCB.

<p align="center"><img alt="Display_Placement_1" src="https://github.com/user-attachments/assets/120c1d63-db2c-4569-8030-ebec3ec0e074" width="700"></p>
<p align="center"><img alt="Display_Placement_2" src="https://github.com/user-attachments/assets/8cbffd19-aee5-40a8-b3b6-ce1f91275711" width="700"></p>

### 6. Attach the display cover.

<p align="center"><img alt="Attach_Display_Cover" src="https://github.com/user-attachments/assets/800e34a6-3082-4a30-908d-15d8ea018a68" width="700"></p>

### 7. Secure the assembly from the bottom with the two M3 fasteners.

<p align="center"><img alt="Secure_With_M3_Screws" src="https://github.com/user-attachments/assets/4d94f443-7d26-4a1e-abb5-98dc9f584dc8" width="700"></p>

## Setup

### Flashing firmware onto a device

#### For a new board or a device that needs recovery:

1. Connect Aether to your computer over USB.
2. Open the [Aether WebUSB flashing tool](https://aether.syntropylabs.io/flash/).
3. Choose **Factory flash (recommended)** for first-time setup or full recovery.
4. Keep the page open until flashing is complete.
5. Let the device boot.

#### For a device that is already set up and just needs newer firmware:

1. Connect the device over USB and open the same flashing page.
2. Choose **Update firmware (OTA)** if you want to preserve the current device settings.

_Note: The flashing page in `flash/` is built around **ESP Web Tools / WebUSB** and is intended to work best in **Chrome or Edge**._

### First-time network setup

On boot, Aether can expose a temporary setup access point so you can connect it to Wi-Fi through the captive portal flow.

- If you want **local web UI access**, **network-based updates**, or **Home Assistant integration**, connect the device to Wi-Fi during setup.
- If you do **not** configure Wi-Fi, the device is still intended to work as a standalone monitor in **offline mode**.

Once connected to Wi-Fi, the device also hosts its own local web UI on port 80.

> **TODO:** Add the exact captive portal steps and screenshots.
> **TODO:** Add the default setup AP naming example shown to end users during onboarding.

### Firmware development

If you are working on the ESPHome firmware:

```sh
git clone https://github.com/EnriqueNeyra/aether.git
cd "aether/firmware"
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

## License

This project (hardware, design files, and firmware) is licensed under the
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-nc-sa/4.0/).

[![License: CC BY-NC-SA 4.0](https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-nc-sa/4.0/)
